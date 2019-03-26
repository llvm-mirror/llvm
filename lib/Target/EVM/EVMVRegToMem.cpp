//===-- EVMVRegToMem.cpp - Move virtual regisers to memory location -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that moves virtual register access to memory
/// locations.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-move-vregs"

/// Copied from WebAssembly's backend.
namespace {
class EVMVRegToMem final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMVRegToMem() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM Move VRegisters to Memory";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMVRegToMem::ID = 0;
INITIALIZE_PASS(EVMVRegToMem, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMVRegToMem() {
  return new EVMVRegToMem();
}

bool EVMVRegToMem::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Virtual Registers to Memory Locations **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &TRI = *MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  bool Changed = false;

  DenseMap<unsigned, unsigned> Reg2Mem;
  unsigned index = 0;

  // first, find all the virtual registers.
  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      for (const MachineOperand &MO : MI.explicit_operands()) {
        // only cares about registers.
        if ( !MO.isReg() || !MO.isDef() ) continue;
        unsigned reg = MO.getReg();

        LLVM_DEBUG({dbgs() << "Find vreg: " << reg <<
                           ", location: " << index << "\n"; });

        Reg2Mem.insert(std::make_pair(reg, index));
        index ++;
      }
    }
  }

  LLVM_DEBUG({
      dbgs() << "Total Number of virtual register definintion: " << Reg2Mem.size();
  });

  // at the beginning of the function, increment memory allocator pointer.
  // TODO

  // at the end of the function, decrement memory allocator pointer.
  // TODO

  // second: replace those instructions with acess to memory.
  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      for (const MachineOperand &MO : MI.explicit_operands()) {
        if ( !MO.isReg() ) continue;

        unsigned reg = MO.getReg();
        assert(Reg2Mem.find(reg) != Reg2Mem.end());
        unsigned index = Reg2Mem[reg];

        Changed = true;

        if (MO.isDef()) {
          // insert after instruction:
          // PUSH (index * 32)
          // MSTORE

        } else {
          assert(MO.isUse());
          // PUSH (index *32) # offset
          // MLOAD

        }
      }
    }
  }

  return Changed;
}
