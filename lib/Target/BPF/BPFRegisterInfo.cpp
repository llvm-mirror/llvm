//===-- BPFRegisterInfo.cpp - BPF Register Information ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the BPF implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFRegisterInfo.h"
#include "BPFSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"

#define GET_REGINFO_TARGET_DESC
#include "BPFGenRegisterInfo.inc"
using namespace llvm;

BPFRegisterInfo::BPFRegisterInfo()
    : BPFGenRegisterInfo(BPF::R0) {}

const MCPhysReg *
BPFRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_SaveList;
}

BitVector BPFRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(BPF::R10); // R10 is read only frame pointer
  Reserved.set(BPF::R11); // R11 is pseudo stack pointer
  return Reserved;
}

void BPFRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  unsigned i = 0;
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  DebugLoc DL = MI.getDebugLoc();

  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }

  unsigned FrameReg = getFrameRegister(MF);
  int FrameIndex = MI.getOperand(i).getIndex();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock &MBB = *MI.getParent();

  if (MI.getOpcode() == BPF::MOV_rr) {
    int Offset = MF.getFrameInfo()->getObjectOffset(FrameIndex);

    MI.getOperand(i).ChangeToRegister(FrameReg, false);
    unsigned reg = MI.getOperand(i - 1).getReg();
    BuildMI(MBB, ++II, DL, TII.get(BPF::ADD_ri), reg)
        .addReg(reg)
        .addImm(Offset);
    return;
  }

  int Offset = MF.getFrameInfo()->getObjectOffset(FrameIndex) +
               MI.getOperand(i + 1).getImm();

  if (!isInt<32>(Offset))
    llvm_unreachable("bug in frame offset");

  if (MI.getOpcode() == BPF::FI_ri) {
    // architecture does not really support FI_ri, replace it with
    //    MOV_rr <target_reg>, frame_reg
    //    ADD_ri <target_reg>, imm
    unsigned reg = MI.getOperand(i - 1).getReg();

    BuildMI(MBB, ++II, DL, TII.get(BPF::MOV_rr), reg)
        .addReg(FrameReg);
    BuildMI(MBB, II, DL, TII.get(BPF::ADD_ri), reg)
        .addReg(reg)
        .addImm(Offset);

    // Remove FI_ri instruction
    MI.eraseFromParent();
  } else {
    MI.getOperand(i).ChangeToRegister(FrameReg, false);
    MI.getOperand(i + 1).ChangeToImmediate(Offset);
  }
}

unsigned BPFRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return BPF::R10;
}
