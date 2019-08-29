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
  void expandRETURN(MachineInstr* MI) const;
  void expandJUMPSUB(MachineInstr* MI) const;

  bool handleFramePointer(MachineInstr *MI);

};
} // end anonymous namespace

char EVMExpandPseudos::ID = 0;
INITIALIZE_PASS(EVMExpandPseudos, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMExpandPseudos() {
  return new EVMExpandPseudos();
}

/// eliminate frame pointer.
bool EVMExpandPseudos::handleFramePointer(MachineInstr *MI) {
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
               << Register::virtReg2Index(fpReg) << " in instruction: ";
        MI->dump();
      });
      MI->getOperand(i).setReg(fpReg);
      Changed = true; 
    }
  }
  return Changed;
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


  BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), reg)
      .addImm(ST->getFreeMemoryPointer());
  BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), fpReg)
    .addReg(reg);
  BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), immReg)
      .addImm(MI->getOperand(1).getImm());
  BuildMI(*MBB, MI, DL, TII->get(EVM::ADD_r), addrReg)
    .addReg(fpReg).addReg(immReg);

  unsigned localReg = MI->getOperand(0).getReg();

  if (opc == EVM::pGETLOCAL_r) {
    BuildMI(*MBB, MI, DL, TII->get(EVM::MLOAD_r), localReg).addReg(addrReg);
  } else if (opc == EVM::pPUTLOCAL_r) {
    // MSTORE_r addrReg value
    BuildMI(*MBB, MI, DL, TII->get(EVM::MSTORE_r)).addReg(addrReg).addReg(localReg);
  } else {
    llvm_unreachable("invalid parameter");
  }
  MI->eraseFromParent();
}

void EVMExpandPseudos::expandJUMPSUB(MachineInstr* MI) const {
  MachineBasicBlock* MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned opc = MI->getOpcode();

  assert(opc == EVM::pJUMPSUBVOID_r || opc == EVM::pJUMPSUB_r);

  bool isVoid = opc == EVM::pJUMPSUBVOID_r;

  // TODO: if it is an external function, emit call.
  // otherwise, emit jump. At this moment, simply use jump.
  unsigned newOpcode = EVM::pJUMPSUB_r;

  // grab the jump target
  MachineOperand *tgt;

  if (isVoid) {
    tgt = &MI->getOperand(0);
  } else {
    tgt = &MI->getOperand(1);
  }
  assert(tgt->isGlobal());

  // instantiate jump target
  unsigned reg = this->getNewRegister(MI);
  BuildMI(*MBB, MI, DL, TII->get(EVM::PUSH32_r), reg)
      .addGlobalAddress(tgt->getGlobal());

  if (isVoid) {
    MachineInstrBuilder mib = BuildMI(*MBB, MI, DL, TII->get(newOpcode));
    for (unsigned i = 1; i < MI->getNumOperands(); ++i) {
      mib.add(MI->getOperand(i));
    }
    
  } else {
    MachineOperand &rv = MI->getOperand(0);
    assert(rv.isReg());
    unsigned rvreg = rv.getReg();
    MachineInstrBuilder mib = BuildMI(*MBB, MI, DL, TII->get(newOpcode), rvreg);
    for (unsigned i = 2; i < MI->getNumOperands(); ++i) {
      mib.add(MI->getOperand(i));
    }
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
        case EVM::pMOVE_r:
          llvm_unreachable(
              "MOVE instructions should have been eliminated already.");
          break;
        case EVM::pJUMPSUB_r:
        case EVM::pJUMPSUBVOID_r:
          expandJUMPSUB(MI);
          llvm_unreachable(
              "JUMP(SUB) instructions should have been eliminated already.");
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
