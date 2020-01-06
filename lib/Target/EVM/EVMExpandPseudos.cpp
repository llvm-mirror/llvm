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
#include "EVMInstrInfo.h"
#include "EVMUtils.h"
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
    return "EVM expand pseudos";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const TargetInstrInfo* TII;
  const EVMSubtarget* ST;
  const MachineFunction* MF;

  unsigned getNewRegister(MachineInstr* MI) const;

  bool runOnMachineFunction(MachineFunction &MF) override;
  void expandLOCAL(MachineInstr* MI) const;
  void expandRETURN(MachineInstr* MI) const;
  void expandJUMP(MachineInstr* MI) const;
  void expandMOVE(MachineInstr* MI) const;
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

void EVMExpandPseudos::expandJUMP(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  unsigned opc = MI->getOpcode();

  if (opc == EVM::pJUMPTO_r) {
    MachineBasicBlock * dest = MI->getOperand(0).getMBB();
    unsigned destReg = this->getNewRegister(MI);

    BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), destReg).addMBB(dest);
    BuildMI(*MBB, MI, DL, TII->get(EVM::JUMP_r)).addReg(destReg);
  }

  if (opc == EVM::pJUMPIF_r) {
    MachineBasicBlock * dest = MI->getOperand(1).getMBB();
    unsigned destReg = this->getNewRegister(MI);

    BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), destReg).addMBB(dest);
    BuildMI(*MBB, MI, DL, TII->get(EVM::JUMPI_r))
        .add(MI->getOperand(0))
        .addReg(destReg);
  }
  MI->eraseFromParent();
}

void EVMExpandPseudos::expandMOVE(MachineInstr* MI) const {

}

void EVMExpandPseudos::expandLOCAL(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  // GETLOCAL:
  // reg   = PUSH32 FP
  // fp    = MLOAD reg
  // index = PUSH32 index
  // addr  = ADD fp index
  // local = MLOAD addr

  // PUTLOCAL:
  // fp    = MLOAD FP
  // addr  = ADD fp index
  // MSTORE addr
  unsigned opc = MI->getOpcode();

  unsigned reg    = this->getNewRegister(MI);
  unsigned fpReg    = this->getNewRegister(MI);
  unsigned immReg   = this->getNewRegister(MI);
  unsigned addrReg  = this->getNewRegister(MI);

  //# TODO: improve this
  unsigned fiSize = this->MF->getFrameInfo().getStackSize();

  BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), reg)
      .addImm(ST->getFreeMemoryPointer());
  BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), fpReg)
    .addReg(reg);
  unsigned slot_index = MI->getOperand(1).getImm() + (fiSize/32);
  BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), immReg)
      .addImm(slot_index * 32);
  BuildMI(*MBB, MI, DL, TII->get(EVM::ADD_r), addrReg)
    .addReg(fpReg).addReg(immReg);

  unsigned localReg = MI->getOperand(0).getReg();
  if (opc == EVM::pGETLOCAL_r) {
    auto mi = BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), localReg).addReg(addrReg);

    uint32_t flags = EVM::BuildCommentFlags(EVM::GETLOCAL, slot_index);
    mi->setAsmPrinterFlag(flags);
  } else if (opc == EVM::pPUTLOCAL_r) {
    // MSTORE_r addrReg value
    auto mi = BuildMI(*MBB, MI, DL, TII->get(EVM::MSTORE_r))
                  .addReg(addrReg)
                  .addReg(localReg);

    uint32_t flags = EVM::BuildCommentFlags(EVM::PUTLOCAL, slot_index);
    mi->setAsmPrinterFlag(flags);
  } else {
    llvm_unreachable("invalid parameter");
  }
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

    //uint32_t flags = EVM::BuildCommentFlags(EVM::RETURN_FROM_SUBROUTINE, 0);
    //mi->setAsmPrinterFlag(flags);
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
  this->MF = &MF;

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
        case EVM::pMOVE_r:
          expandMOVE(MI);
          break;
        case EVM::pJUMPTO_r:
        case EVM::pJUMPIF_r:
          expandJUMP(MI);
          Changed = true;
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
