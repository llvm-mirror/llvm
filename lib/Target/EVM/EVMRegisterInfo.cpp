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
#include "llvm/CodeGen/TargetRegisterInfo.h"
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

  /*
  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  */

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg = getFrameRegister(MF);

  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);

  unsigned fiReg = FrameReg;
  if (Offset != 0) {
    unsigned opc = 0;
    fiReg = MRI.createVirtualRegister(&EVM::GPRRegClass);

    if (Offset > 0) {
      opc = EVM::ADD_r;
    } else if (Offset < 0) {
      opc = EVM::SUB_r;
    }
    unsigned offsetReg = MRI.createVirtualRegister(&EVM::GPRRegClass);
    BuildMI(MBB, MI, DL, TII->get(EVM::PUSH32_r), offsetReg).addImm(Offset);
    BuildMI(MBB, MI, DL, TII->get(opc), fiReg)
        .addReg(FrameReg)
        .addReg(offsetReg);
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(fiReg, false);
  return;

  /*
    // replace the MLOAD/MSTORE with pPUTOLCAL/pGETLOCAL
    unsigned opc = MI.getOpcode();
    assert(opc == EVM::MLOAD_r || opc == EVM::MSTORE_r);
    if (opc == EVM::MLOAD_r) {
      opc = EVM::pGETLOCAL_r;

      MachineOperand &MO = MI.getOperand(1);
      assert(MO.isFI());
      unsigned frIdx = MO.getIndex();

      MI.setDesc(TII->get(opc));
      MI.getOperand(1).ChangeToImmediate(frIdx);
    }
    if (opc == EVM::MSTORE_r) {
      opc = EVM::pPUTLOCAL_r;

      MachineOperand &MO = MI.getOperand(0);
      assert(MO.isFI());
      unsigned frIdx = MO.getIndex();

      MI.setDesc(TII->get(opc));
      // MSTORE offset value
      unsigned reg = MI.getOperand(1).getReg();
      MI.getOperand(0).ChangeToRegister(reg, false);
      MI.getOperand(1).ChangeToImmediate(frIdx);
    }
  */
}

Register
EVMRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // This will eventually be expaned
  return EVM::FP;
}

