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

#define GET_REGINFO_TARGET_DESC
#include "EVMGenRegisterInfo.inc"

using namespace llvm;

EVMRegisterInfo::EVMRegisterInfo(unsigned HwMode)
    : EVMGenRegisterInfo(EVM::X1, /*DwarfFlavour*/0, /*EHFlavor*/0,
                           /*PC*/0, HwMode) {}

const MCPhysReg *
EVMRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (MF->getFunction().hasFnAttribute("interrupt")) {
    if (MF->getSubtarget<EVMSubtarget>().hasStdExtD())
      return CSR_XLEN_F64_Interrupt_SaveList;
    if (MF->getSubtarget<EVMSubtarget>().hasStdExtF())
      return CSR_XLEN_F32_Interrupt_SaveList;
    return CSR_Interrupt_SaveList;
  }
  return CSR_SaveList;
}

BitVector EVMRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // Use markSuperRegs to ensure any register aliases are also reserved
  markSuperRegs(Reserved, EVM::X0); // zero
  markSuperRegs(Reserved, EVM::X1); // ra
  markSuperRegs(Reserved, EVM::X2); // sp
  markSuperRegs(Reserved, EVM::X3); // gp
  markSuperRegs(Reserved, EVM::X4); // tp
  markSuperRegs(Reserved, EVM::X8); // fp
  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

bool EVMRegisterInfo::isConstantPhysReg(unsigned PhysReg) const {
  return PhysReg == EVM::X0;
}

const uint32_t *EVMRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

void EVMRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const EVMInstrInfo *TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  unsigned FrameReg;
  int Offset =
      getFrameLowering(MF)->getFrameIndexReference(MF, FrameIndex, FrameReg) +
      MI.getOperand(FIOperandNum + 1).getImm();

  if (!isInt<32>(Offset)) {
    report_fatal_error(
        "Frame offsets outside of the signed 32-bit range not supported");
  }

  MachineBasicBlock &MBB = *MI.getParent();
  bool FrameRegIsKill = false;

  if (!isInt<12>(Offset)) {
    assert(isInt<32>(Offset) && "Int32 expected");
    // The offset won't fit in an immediate, so use a scratch register instead
    // Modify Offset and FrameReg appropriately
    unsigned ScratchReg = MRI.createVirtualRegister(&EVM::GPRRegClass);
    TII->movImm32(MBB, II, DL, ScratchReg, Offset);
    BuildMI(MBB, II, DL, TII->get(EVM::ADD), ScratchReg)
        .addReg(FrameReg)
        .addReg(ScratchReg, RegState::Kill);
    Offset = 0;
    FrameReg = ScratchReg;
    FrameRegIsKill = true;
  }

  MI.getOperand(FIOperandNum)
      .ChangeToRegister(FrameReg, false, false, FrameRegIsKill);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
}

unsigned EVMRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? EVM::X8 : EVM::X2;
}

const uint32_t *
EVMRegisterInfo::getCallPreservedMask(const MachineFunction & MF,
                                        CallingConv::ID /*CC*/) const {
  if (MF.getFunction().hasFnAttribute("interrupt")) {
    if (MF.getSubtarget<EVMSubtarget>().hasStdExtD())
      return CSR_XLEN_F64_Interrupt_RegMask;
    if (MF.getSubtarget<EVMSubtarget>().hasStdExtF())
      return CSR_XLEN_F32_Interrupt_RegMask;
    return CSR_Interrupt_RegMask;
  }
  return CSR_RegMask;
}
