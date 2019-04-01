//===-- EVMAddJumpdest.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "EVMRegisterInfo.h"
#include "EVMTargetMachine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "evm-add-jumpdest"

namespace {
class EVMAddJumpdest final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMAddJumpdest() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Add Jumpdest"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMAddJumpdest::ID = 0;
//INITIALIZE_PASS(EVMAddJumpdest, DEBUG_TYPE,
//                "Adding Jumpdest instructions for EVM", false, false)

FunctionPass *llvm::createEVMAddJumpdest() {
  //return new EVMAddJumpdest();
  return NULL;
}

bool EVMAddJumpdest::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Add Jumpdest instruction **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  return false;
  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  bool Changed = false;

  // FIXME: JUMPDEST takes one byte of space
  for (MachineBasicBlock & MBB : MF) {
    bool isJmpTarget = false;
    if (MBB.hasAddressTaken()) {
      isJmpTarget = true;
    }

    for (MachineBasicBlock *Pred : MBB.predecessors()) {
      MachineBasicBlock * fallthrough = Pred->getFallThrough();
      if (fallthrough != &MBB) {
        isJmpTarget = true;
      }
    }

    if (isJmpTarget) {
      // construct a JUMPDEST instructionj
      // TODO: fix the location so it does not move at all.
      BuildMI(MBB, MBB.begin(), MBB.findDebugLoc(MBB.begin()),
              TII.get(EVM::JUMPDEST_r));
      LLVM_DEBUG({
          dbgs() << "Inserting JUMPDEST at the beginning of BB: "
          << MBB.getNumber() << '\n';
          });
      Changed = true;
    }
  }

  return Changed;
}
