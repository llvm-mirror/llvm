//===-- BPFFrameLowering.cpp - BPF Frame Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the BPF implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "BPFFrameLowering.h"
#include "BPFInstrInfo.h"
#include "BPFSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

bool BPFFrameLowering::hasFP(const MachineFunction &MF) const { return true; }

void BPFFrameLowering::emitPrologue(MachineFunction &MF) const {}

void BPFFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {}

void BPFFrameLowering::processFunctionBeforeCalleeSavedScan(
    MachineFunction &MF, RegScavenger *RS) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  MRI.setPhysRegUnused(BPF::R6);
  MRI.setPhysRegUnused(BPF::R7);
  MRI.setPhysRegUnused(BPF::R8);
  MRI.setPhysRegUnused(BPF::R9);
}
