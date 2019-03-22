//===-- EVMRegisterInfo.cpp - EVM Register Information ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the EVM implementation of the
/// TargetRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#include "EVMRegisterInfo.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVMFrameLowering.h"
#include "EVMInstrInfo.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

#define DEBUG_TYPE "evm-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "EVMGenRegisterInfo.inc"

EVMRegisterInfo::EVMRegisterInfo()
    : EVMGenRegisterInfo(0) {}

const MCPhysReg *
EVMRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector
EVMRegisterInfo::getReservedRegs(const MachineFunction & /*MF*/) const {
  BitVector Reserved(getNumRegs());
  for (auto Reg : {EVM::SP, EVM::FP})
    Reserved.set(Reg);
  return Reserved;
}

void EVMRegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger * /*RS*/) const {
  assert(SPAdj == 0 && "Unexpected");

  unsigned i = 0;
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI.getDebugLoc();

  const auto *TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  if (!DL)
    /* try harder to get some debug loc */
    for (auto &I : MBB)
      if (I.getDebugLoc()) {
        DL = I.getDebugLoc();
        break;
      }

  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned FrameReg = getFrameRegister(MF);

  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) * 32;

  // MLOAD 
  if (MI.getOpcode() == EVM::MLOAD_r) {
    unsigned reg = MI.getOperand(i - 1).getReg();


    unsigned opc;
    if (Offset > 0) {
      opc = EVM::ADD_r;
    } else {
      opc = EVM::SUB_r;
    }

    unsigned newReg = MRI.createVirtualRegister(&EVM::GPRRegClass);
    BuildMI(MBB, ++II, DL, TII->get(opc), newReg)
      .addReg(FrameReg)
      .addImm(Offset);
    MI.getOperand(i).ChangeToRegister(newReg, false);
    return;
  }

  // MSTORE
  if (MI.getOpcode() == EVM::MSTORE_r) {
    unsigned newReg = MRI.createVirtualRegister(&EVM::GPRRegClass);

    unsigned opc;
    if (Offset > 0) {
      opc = EVM::ADD_r;
    } else {
      opc = EVM::SUB_r;
    }


    BuildMI(MBB, ++II, DL, TII->get(opc), newReg)
      .addReg(FrameReg).addImm(Offset);
    MI.getOperand(i).ChangeToRegister(newReg, false);
    return;
  }
}

unsigned
EVMRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return EVM::FP;
}

