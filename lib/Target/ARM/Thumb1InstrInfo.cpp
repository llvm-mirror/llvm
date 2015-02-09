//===-- Thumb1InstrInfo.cpp - Thumb-1 Instruction Information -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-1 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMSubtarget.h"
#include "Thumb1InstrInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

Thumb1InstrInfo::Thumb1InstrInfo(const ARMSubtarget &STI)
  : ARMBaseInstrInfo(STI), RI(STI) {
}

/// getNoopForMachoTarget - Return the noop instruction to use for a noop.
void Thumb1InstrInfo::getNoopForMachoTarget(MCInst &NopInst) const {
  NopInst.setOpcode(ARM::tMOVr);
  NopInst.addOperand(MCOperand::CreateReg(ARM::R8));
  NopInst.addOperand(MCOperand::CreateReg(ARM::R8));
  NopInst.addOperand(MCOperand::CreateImm(ARMCC::AL));
  NopInst.addOperand(MCOperand::CreateReg(0));
}

unsigned Thumb1InstrInfo::getUnindexedOpcode(unsigned Opc) const {
  return 0;
}

void Thumb1InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I, DebugLoc DL,
                                  unsigned DestReg, unsigned SrcReg,
                                  bool KillSrc) const {
  // Need to check the arch.
  MachineFunction &MF = *MBB.getParent();
  const ARMSubtarget &st = MF.getTarget().getSubtarget<ARMSubtarget>();

  assert(ARM::GPRRegClass.contains(DestReg, SrcReg) &&
         "Thumb1 can only copy GPR registers");

  if (st.hasV6Ops() || ARM::hGPRRegClass.contains(SrcReg)
      || !ARM::tGPRRegClass.contains(DestReg))
    AddDefaultPred(BuildMI(MBB, I, DL, get(ARM::tMOVr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc)));
  else {
    // FIXME: The performance consequences of this are going to be atrocious.
    // Some things to try that should be better:
    //   * 'mov hi, $src; mov $dst, hi', with hi as either r10 or r11
    //   * 'movs $dst, $src' if cpsr isn't live
    // See: http://lists.cs.uiuc.edu/pipermail/llvmdev/2014-August/075998.html

    // 'MOV lo, lo' is unpredictable on < v6, so use the stack to do it
    AddDefaultPred(BuildMI(MBB, I, DL, get(ARM::tPUSH)))
      .addReg(SrcReg, getKillRegState(KillSrc));
    AddDefaultPred(BuildMI(MBB, I, DL, get(ARM::tPOP)))
      .addReg(DestReg, getDefRegState(true));
  }
}

void Thumb1InstrInfo::
storeRegToStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned SrcReg, bool isKill, int FI,
                    const TargetRegisterClass *RC,
                    const TargetRegisterInfo *TRI) const {
  assert((RC == &ARM::tGPRRegClass ||
          (TargetRegisterInfo::isPhysicalRegister(SrcReg) &&
           isARMLowRegister(SrcReg))) && "Unknown regclass!");

  if (RC == &ARM::tGPRRegClass ||
      (TargetRegisterInfo::isPhysicalRegister(SrcReg) &&
       isARMLowRegister(SrcReg))) {
    DebugLoc DL;
    if (I != MBB.end()) DL = I->getDebugLoc();

    MachineFunction &MF = *MBB.getParent();
    MachineFrameInfo &MFI = *MF.getFrameInfo();
    MachineMemOperand *MMO =
      MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(FI),
                              MachineMemOperand::MOStore,
                              MFI.getObjectSize(FI),
                              MFI.getObjectAlignment(FI));
    AddDefaultPred(BuildMI(MBB, I, DL, get(ARM::tSTRspi))
                   .addReg(SrcReg, getKillRegState(isKill))
                   .addFrameIndex(FI).addImm(0).addMemOperand(MMO));
  }
}

void Thumb1InstrInfo::
loadRegFromStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DestReg, int FI,
                     const TargetRegisterClass *RC,
                     const TargetRegisterInfo *TRI) const {
  assert((RC == &ARM::tGPRRegClass ||
          (TargetRegisterInfo::isPhysicalRegister(DestReg) &&
           isARMLowRegister(DestReg))) && "Unknown regclass!");

  if (RC == &ARM::tGPRRegClass ||
      (TargetRegisterInfo::isPhysicalRegister(DestReg) &&
       isARMLowRegister(DestReg))) {
    DebugLoc DL;
    if (I != MBB.end()) DL = I->getDebugLoc();

    MachineFunction &MF = *MBB.getParent();
    MachineFrameInfo &MFI = *MF.getFrameInfo();
    MachineMemOperand *MMO =
      MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(FI),
                              MachineMemOperand::MOLoad,
                              MFI.getObjectSize(FI),
                              MFI.getObjectAlignment(FI));
    AddDefaultPred(BuildMI(MBB, I, DL, get(ARM::tLDRspi), DestReg)
                   .addFrameIndex(FI).addImm(0).addMemOperand(MMO));
  }
}

void
Thumb1InstrInfo::expandLoadStackGuard(MachineBasicBlock::iterator MI,
                                      Reloc::Model RM) const {
  if (RM == Reloc::PIC_)
    expandLoadStackGuardBase(MI, ARM::tLDRLIT_ga_pcrel, ARM::tLDRi, RM);
  else
    expandLoadStackGuardBase(MI, ARM::tLDRLIT_ga_abs, ARM::tLDRi, RM);
}
