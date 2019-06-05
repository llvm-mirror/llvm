//===-- EVMFrameLowering.cpp - EVM Frame Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the EVM implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVMFrameLowering.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"

using namespace llvm;

bool EVMFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         RegInfo->needsStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}

void EVMFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
  unsigned fiSize = MFI.getFrameIndexSize();

  const EVMInstrInfo *TII =
    static_cast<const EVMInstrInfo *>(MF.getSubtarget().getInstrInfo());
  // we insert ADJFPUP_r, and later we will alter the paramter
  MachineBasicBlock::iterator MBBI = MBB.getFirstNonDebugInstr();

  // skip pSTACKARGs.
  while (MBBI->getOpcode() == EVM::pSTACKARG_r) {
    ++MBBI;
  }
  DebugLoc DL = MBBI->getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(EVM::pADJFPUP_r)).addImm(fiSize);
}

void EVMFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
  unsigned fiSize = MFI.getFrameIndexSize();
  const EVMInstrInfo *TII =
    static_cast<const EVMInstrInfo *>(MF.getSubtarget().getInstrInfo());

  // we insert ADJFPDOWN_r, and later we will alter the paramter
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  DebugLoc DL = MBBI->getDebugLoc();

  BuildMI(MBB, MBBI, DL, TII->get(EVM::pADJFPDOWN_r)).addImm(fiSize);
}

void EVMFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
}

// Not preserve stack space within prologue for outgoing variables when the
// function contains variable size objects and let eliminateCallFramePseudoInstr
// preserve stack space for it.
bool EVMFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions.
MachineBasicBlock::iterator EVMFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  return MBB.erase(MI);
}

const char*
EVMFrameLowering::getTargetNodeName(unsigned Opcode) const {
  switch (static_cast<EVMISD::NodeType>(Opcode)) {
  case EVMISD::FIRST_NUMBER:
    break;
#define NODE(N)                   \
  case EVMISD::N:                 \
    return "EVMISD::" #N;
#include "EVMISD.def"
#undef NODE
  }
  return nullptr;
}
