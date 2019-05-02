//===-- EVMShrinkpush.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

#define DEBUG_TYPE "evm-shrink-push"

namespace {
class EVMShrinkpush final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMShrinkpush() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM shrink PUSH";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMShrinkpush::ID = 0;
INITIALIZE_PASS(EVMShrinkpush, DEBUG_TYPE,
                "Replace push32 with shorter variations",
                false, false)

FunctionPass *llvm::createEVMShrinkpush() {
  return new EVMShrinkpush();
}

unsigned get_imm_size(int64_t imm) {
  // TODO: check correct-ness.
  unsigned type_size = sizeof(uint64_t);

  if (imm >= 0) {
    unsigned zeros = countLeadingZeros<uint64_t>(imm);
    unsigned bytes = type_size - (zeros / 8);

    // if it is 0, then at least we need to use PUSH1
    return bytes == 0 ? 1 : bytes;

  } else {
    unsigned ones = countLeadingOnes<uint64_t>(imm);
    assert(ones > 0);
    return type_size - (ones / 8);
  }

}

static int get_push_opcode(unsigned s) {
  switch (s) {
    default:
      llvm_unreachable("incorrect size or unimplemented");
    case 1: return EVM::PUSH1;
    case 2: return EVM::PUSH2;
    case 3: return EVM::PUSH3;
    case 4: return EVM::PUSH4;
    case 5: return EVM::PUSH5;
    case 6: return EVM::PUSH6;
    case 7: return EVM::PUSH7;
    case 8: return EVM::PUSH8;
  }
}

bool EVMShrinkpush::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Shrink PUSH **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  bool Changed = false;

  for (MachineBasicBlock & MBB : MF) {
    for (MachineInstr & MI : MBB) {
      unsigned opcode = MI.getOpcode();
      if (opcode == EVM::PUSH32) {
        LLVM_DEBUG({
            dbgs() << "Converting: "; MI.dump(); dbgs();
        });

        auto MO = MI.getOperand(0);
        assert((MO.isImm() || MO.isCImm()) && "Illegal PUSH32 instruction.");

        if (MO.isImm()) {
          int64_t imm = MO.getImm();
          unsigned s = get_imm_size(imm);
          int new_opcode = get_push_opcode(s);
          MI.setDesc(TII.get(new_opcode));
          if (imm < 0) {
            // append SIGNEXTEND after MI.
            auto InsertPt = std::next(MI.getIterator());
            BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::PUSH1)).addImm(s);
            BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::SIGNEXTEND));
          }
        } else {
          llvm_unreachable("CImm case is unimplemented.");
        }

        LLVM_DEBUG({
            dbgs() << " to: "; MI.dump(); dbgs() << "\n";
        });

        Changed = true;
      }
    }
  }

  LLVM_DEBUG({
    dbgs() << "********** End of Shrink PUSH on Function:"
           << MF.getName() << " *********" << '\n';
  });
  return Changed;
}
