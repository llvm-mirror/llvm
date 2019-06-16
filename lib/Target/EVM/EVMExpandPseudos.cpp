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

  const TargetInstrInfo* TII;
  const EVMSubtarget* ST;

  unsigned getNewRegister(MachineInstr* MI) const;

  bool runOnMachineFunction(MachineFunction &MF) override;
  void expandLOCAL(MachineInstr* MI) const;
  void expandADJFP(MachineInstr* MI) const;
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

unsigned EVMExpandPseudos::getNewRegister(MachineInstr* MI) const {
    MachineFunction *F = MI->getParent()->getParent();
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    return RegInfo.createVirtualRegister(&EVM::GPRRegClass);
}

void EVMExpandPseudos::expandLOCAL(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  // GETLOCAL:
  // fp    = MLOAD FP
  // addr  = ADD fp index
  // local = MLOAD addr

  // PUTLOCAL:
  // fp    = MLOAD FP
  // addr  = ADD fp index
  // MSTORE addr
  unsigned opc = MI->getOpcode();

  unsigned fpReg    = this->getNewRegister(MI);
  unsigned addrReg  = this->getNewRegister(MI);
  unsigned localReg = MI->getOperand(0).getReg();

  BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), fpReg)
    .addImm(ST->getFreeMemoryPointer());
  BuildMI(*MBB, MI, DL, TII->get(EVM::ADD_r), addrReg)
    .addReg(fpReg).addImm(MI->getOperand(1).getImm());

  if (opc == EVM::pGETLOCAL_r) {
    BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), localReg).addReg(addrReg);
  } else if (opc == EVM::pPUTLOCAL_r) {
    // MSTORE_r offset value
    BuildMI(*MBB, MI, DL, TII->get(EVM::MSTORE_r)).addReg(addrReg).addReg(localReg);
  } else {
    llvm_unreachable("invalid parameter");
  }
  MI->eraseFromParent();
}

void EVMExpandPseudos::expandADJFP(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned opc = MI->getOpcode();

  // Small optimization: if there is no frame adjustment needed,
  // remove the instruction.
  unsigned index = MI->getOperand(0).getImm();
  if (index == 0) {
    MI->eraseFromParent();
    return;
  }

  unsigned oldFP = this->getNewRegister(MI);
  unsigned newFP = this->getNewRegister(MI);

  BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), oldFP)
    .addImm(ST->getFreeMemoryPointer());

  if (opc == EVM::pADJFPUP_r) {
    BuildMI(*MBB, MI, DL, TII->get(EVM::ADD_r), newFP)
      .addReg(oldFP).addImm(MI->getOperand(0).getImm() * 32);
  } else if (opc == EVM::pADJFPDOWN_r) {
    BuildMI(*MBB, MI, DL, TII->get(EVM::SUB_r), newFP)
      .addReg(oldFP).addImm(MI->getOperand(0).getImm() * 32);
  } else {
    llvm_unreachable("invalid parameter");
  }

  BuildMI(*MBB, MI, DL, TII->get(EVM::MSTORE_r))
  .addImm(ST->getFreeMemoryPointer()).addReg(newFP);

  MI->eraseFromParent();
}

void EVMExpandPseudos::expandRETURN(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned opc = MI->getOpcode();

  unsigned numOpnds = MI->getNumOperands();
  assert(numOpnds <= 2 &&
         "Does not support more than 1 return values");

  if (opc == EVM::pRETURNSUB_r) {
    llvm_unreachable("unimplemented");
  } else {
    BuildMI(*MBB, MI, DL, TII->get(EVM::JUMP_r))
      .add(MI->getOperand(0));
  }

  MI->eraseFromParent();
}

bool EVMExpandPseudos::runOnMachineFunction(MachineFunction &MF) {
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

      switch (opcode) {
        case EVM::pPUTLOCAL_r:
        case EVM::pGETLOCAL_r:
          expandLOCAL(MI);
          Changed = true;
          break;
        case EVM::pADJFPUP_r:
        case EVM::pADJFPDOWN_r:
          expandADJFP(MI);
          Changed = true;
          break;
        case EVM::pMOVE_r:
          llvm_unreachable("MOVE instructions should be eliminated");
          break;
        // suspend it and expand at finalization time
        //case EVM::pRETURNSUB_r:
        //case EVM::pRETURNSUBVOID_r:
        //  expandRETURN(MI);
        //  Changed = true;
        //  break;
      }

    }
  }

  return Changed;
}
