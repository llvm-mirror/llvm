//===-- EVMExpandPseudos.cpp - Argument instruction moving ---------===//
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

#define DEBUG_TYPE "evm-expand-pseudos"

namespace {
class EVMExpandPseudos final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMExpandPseudos() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM add Jumpdest";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void expandLOCAL(MachineInstr* MI) const;
  void expandADJFP(MachineInstr* MI) const;
  void expandJUMPSUB(MachineInstr* MI) const;
  void expandRETURN(MachineInstr* MI) const;

};
} // end anonymous namespace

char EVMExpandPseudos::ID = 0;
INITIALIZE_PASS(EVMExpandPseudos, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMExpandPseudos() {
  return new EVMExpandPseudos();
}

void EVMExpandPseudos::expandLOCAL(MachineInstr* MI) const {
}

bool EVMExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Expand pseudo instructions **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  bool Changed = false;

  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      unsigned opcode = MI.getOpcode();

      switch (opcode) {
        case EVM::pPUTLOCAL:
        case EVM::pGETLOCAL:
          expandLOCAL(MI);
          break;
        case EVM::pADJFPUP:
        case EVM::pADJFPDOWN:
          expandADJFP(MI);
          break;
      }

    }
  }

  return Changed;
}
