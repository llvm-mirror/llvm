//===-- EVMExpandFramePointer.cpp -----------------------------------------===//
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

#define DEBUG_TYPE "evm-expand-fp"

namespace {
class EVMExpandFramePointer final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMExpandFramePointer() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM expand framepointer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const TargetInstrInfo* TII;
  const EVMSubtarget* ST;

  unsigned getNewRegister(MachineInstr* MI) const;

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool handleFramePointer(MachineInstr *MI);

};
} // end anonymous namespace

char EVMExpandFramePointer::ID = 0;
INITIALIZE_PASS(EVMExpandFramePointer, DEBUG_TYPE,
                "Replace $fp with virtual registers", false, false)

FunctionPass *llvm::createEVMExpandFramePointer() {
  return new EVMExpandFramePointer();
}

/// eliminate frame pointer.
bool EVMExpandFramePointer::handleFramePointer(MachineInstr *MI) {
  bool Changed = false;
  for (unsigned i = 0; i < MI->getNumExplicitOperands(); ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg()) continue;

    unsigned Reg = MO.getReg();
    if (Register::isPhysicalRegister(Reg)) {
      assert(Reg == EVM::FP);

      MachineBasicBlock *MBB = MI->getParent();
      DebugLoc DL = MI->getDebugLoc();

      // $reg = PUSH32_r 64
      // $fp = MLOAD $reg
      unsigned fpReg = this->getNewRegister(MI);
      unsigned reg = this->getNewRegister(MI);
      BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), reg)
          .addImm(ST->getFreeMemoryPointer());
      BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), fpReg)
          .addReg(reg);
      LLVM_DEBUG({
        dbgs() << "Expanding $fp to %"
               <<Register::virtReg2Index(fpReg) << " in instruction: ";
        MI->dump();
      });
      MI->getOperand(i).setReg(fpReg);
      Changed = true; 
    }
  }
  return Changed;
}

unsigned EVMExpandFramePointer::getNewRegister(MachineInstr* MI) const {
    MachineFunction *F = MI->getParent()->getParent();
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    return RegInfo.createVirtualRegister(&EVM::GPRRegClass);
}
bool EVMExpandFramePointer::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Expand pseudo instructions **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  this->ST = &MF.getSubtarget<EVMSubtarget>();
  this->TII = ST->getInstrInfo();

  bool Changed = false;

  for (MachineBasicBlock & MBB : MF) {
    // TODO: use iterator since we need to remove
    // pseudo instructions
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
         I != E;) {

      MachineInstr *MI = &*I++; 
      unsigned opcode = MI->getOpcode();

      Changed |= handleFramePointer(MI);
    }
  }

  return Changed;
}
