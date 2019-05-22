//===-- EVMArgumentMove.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file moves pSTACKARG instructions after ScheduleDAG scheduling.
///
/// This is copied from WebAssembly backend.
/// Another way to do it might be that  we create glues to bind stack arguments
/// to the beginning (EntryToken). But it is just a thought.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-argument-move"

namespace {
class EVMArgumentMove final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMArgumentMove() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Argument Move"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  static bool isStackArg(const MachineInstr &MI);
};
} // end anonymous namespace

char EVMArgumentMove::ID = 0;
INITIALIZE_PASS(EVMArgumentMove, DEBUG_TYPE,
                "Move function arguments for EVM", false, false)

FunctionPass *llvm::createEVMArgumentMove() {
  return new EVMArgumentMove();
}

bool EVMArgumentMove::isStackArg(const MachineInstr &MI) {
  unsigned opc = MI.getOpcode();

  return (opc == EVM::pSTACKARG_r);
}

bool EVMArgumentMove::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Argument Move **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;
  MachineBasicBlock &EntryMBB = MF.front();
  MachineBasicBlock::iterator InsertPt = EntryMBB.end();

  // Look for the first NonArg instruction.
  for (MachineInstr &MI : EntryMBB) {
    if (!EVMArgumentMove::isStackArg(MI)) {
      InsertPt = MI;
      break;
    }
  }

  // Now move any argument instructions later in the block
  // to before our first NonArg instruction.
  for (MachineInstr &MI : llvm::make_range(InsertPt, EntryMBB.end())) {
    if (EVMArgumentMove::isStackArg(MI)) {
      EntryMBB.insert(InsertPt, MI.removeFromParent());
      Changed = true;
    }
  }

  return Changed;
}
