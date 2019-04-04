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

bool EVMShrinkpush::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Shrink PUSH **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  bool Changed = false;

  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      // TODO
    }
  }

  return Changed;
}
