//===-- EVMConvertRegToStack.cpp - Move virtual regisers to memory location -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the actual transformation from register based
/// instructions to stack based instruction. It considers the order of the
/// instructions in a basicblock is correct, and it does not change the order.
/// Some stack manipulation instructions are inserted into the code.
/// It has a prerequiste pass: EVMVRegToMem.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "EVMTreeWalker.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-reg-to-stacks"

/// Copied from WebAssembly's backend.
namespace {
class EVMConvertRegToStack final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMConvertRegToStack() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM Convert register to stacks";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMConvertRegToStack::ID = 0;
INITIALIZE_PASS(EVMConvertRegToStack, DEBUG_TYPE,
                "Convert register-based instructions into stack-based",
                false, false)

FunctionPass *llvm::createEVMConvertRegToStack() {
  return new EVMConvertRegToStack();
}

bool EVMConvertRegToStack::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Convert register to stack **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  const auto &TRI = *MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock & MBB : MF) {
    for (MachineBasicBlock::instr_iterator I = MBB.instr_begin(), E = MBB.instr_end(); I != E;) {
      MachineInstr &MI = *I++;

      unsigned opc = MI.getOpcode();

      // Convert irregular reg->stack mapping
      if (opc == EVM::PUSH32_r) {
        assert(MI.getNumOperands() == 2 && "PUSH32_r's number of operands must be 2.");
        auto &MO = MI.getOperand(1);
        assert(MO.isImm() && "PUSH32_r's use operand must be immediate.");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::PUSH32)).addImm(MO.getImm());
        MI.eraseFromParent();
        continue;
      } else if (opc == EVM::pSTACKARG_r) {
        MI.RemoveOperand(0);
        MI.setDesc(TII.get(EVM::pSTACKARG));
        continue;
      }

      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        // Immediate value: insert `PUSH32 Imm` before instruction.
        if (MO.isImm()) {
          auto &MIB = BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::PUSH32)).addImm(MO.getImm());
          LLVM_DEBUG({ dbgs() << "********** Inserting new instruction:"; MIB.getInstr()->dump(); });
          continue;
        }

        // register value:
        if (MO.isReg()) {
          // TODO: at this moment we only have FP as physical register. Need special handling.
          if (TargetRegisterInfo::isPhysicalRegister(MO.getReg())) {
            continue;
          }

          assert(MRI.hasOneUse(MO.getReg()) && "registers should have only one use.");

          // TODO: the register operand should already be on the stack.
        }
      }

      // now def and uses are handled. should convert the instruction.
      {
        auto RegOpcode = MI.getOpcode();
        auto StackOpcode = llvm::EVM::getStackOpcode(RegOpcode);

        if (StackOpcode == -1) {
          // special handling for return pseudo, as we will expand
          // it at the finalization pass.
          if (RegOpcode == EVM::pRETURNSUB_r ||
              RegOpcode == EVM::pRETURNSUBVOID_r) {
            StackOpcode = EVM::pRETURNSUB;
          }
        }
        assert(StackOpcode != -1 && "Failed to convert instruction to stack mode.");

        MI.setDesc(TII.get(StackOpcode));

        // Remove register operands.
        for (unsigned i = 0; i < MI.getNumOperands();) {
          auto &MO = MI.getOperand(i);
          if ( MO.isReg() || MO.isImm() || MO.isCImm() ) {
            MI.RemoveOperand(i);
          } else if (MO.isMBB() || MO.isGlobal() ||
                     MO.isSymbol()) {
            // this is special case for jumps.
            ++i;
          } else {
            llvm_unreachable("unimplemented");
          }
        }

        LLVM_DEBUG({
            dbgs() << "********** Generating new instruction:"; MI.dump();
        });
      }
    }
  }

#ifndef NDEBUG
  // Assert that no instructions are register-based.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isLabel())
        continue;

      // TODO

    }
  }
#endif

  return Changed;
}
