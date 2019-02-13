//===-- EVMRegisterInfo.cpp - EVM Register Information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the EVM implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "EVMRegisterInfo.h"
#include "EVM.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "evm-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "EVMGenRegisterInfo.inc"

using namespace llvm;

EVMRegisterInfo::EVMRegisterInfo(const Triple &TT)
    : EVMGenRegisterInfo(0), TT(TT) {}

const MCPhysReg *
EVMRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector EVMRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  for (auto Reg : {EVM::SP, EVM::FP})
    Reserved.set(Reg);
  return Reserved;
}

void EVMRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  // TODO
}

unsigned EVMRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return EVM::FP;
}

