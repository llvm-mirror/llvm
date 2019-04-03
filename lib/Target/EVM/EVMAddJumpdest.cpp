//===-- EVMAddJumpdest.cpp - Argument instruction moving ---------===//
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

#define DEBUG_TYPE "evm-add-jumpdest"

namespace {
class EVMAddJumpdest final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMAddJumpdest() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM add Jumpdest";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMAddJumpdest::ID = 0;
INITIALIZE_PASS(EVMAddJumpdest, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMAddJumpdest() {
  return new EVMAddJumpdest();
}

bool EVMAddJumpdest::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Add Jumpdest instruction **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  bool Changed = false;

  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      if (!MI.isBranch()) continue;

      // the operand is the target MBB.
      const MachineOperand & MO = MI.getOperand(0);
      assert(MO.isMBB() && "Branch instruction should have only MBB operand.");

      MachineBasicBlock * tMBB = MO.getMBB();

      // if the first instruction in MBB is already JUMPDEST, skip.
      const MachineInstr &tMI = *(tMBB->begin());
      if (tMBB->begin() != tMBB->end() &&
          tMBB->begin()->getOpcode() == EVM::JUMPDEST) {
        continue;
      }

      BuildMI(*tMBB, tMBB->begin(), tMBB->findDebugLoc(tMBB->begin()),
              TII.get(EVM::JUMPDEST));
      LLVM_DEBUG({
          dbgs() << "Inserting JUMPDEST at the beginning of BB: "
          << tMBB->getNumber() << '\n';
      });
      Changed = true;
    }
  }

  return Changed;
}
