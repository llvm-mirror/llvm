//===-- EVMInstrInfo.cpp - EVM Instruction Information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the EVM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "EVMInstrInfo.h"
#include "EVM.h"
#include "EVMSubtarget.h"
#include "EVMTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "EVMGenInstrInfo.inc"

using namespace llvm;

EVMInstrInfo::EVMInstrInfo()
    : EVMGenInstrInfo() {}

unsigned EVMInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {

  return 0;
}

unsigned EVMInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {

  return 0;
}

void EVMInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const DebugLoc &DL, unsigned DstReg,
                                 unsigned SrcReg, bool KillSrc) const {
}

void EVMInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         unsigned SrcReg, bool IsKill, int FI,
                                         const TargetRegisterClass *RC,
                                         const TargetRegisterInfo *TRI) const {
}

void EVMInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          unsigned DstReg, int FI,
                                          const TargetRegisterClass *RC,
                                          const TargetRegisterInfo *TRI) const {
}

void EVMInstrInfo::movImm32(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              const DebugLoc &DL, unsigned DstReg, uint64_t Val,
                              MachineInstr::MIFlag Flag) const {
}

// The contents of values added to Cond are not examined outside of
// EVMInstrInfo, giving us flexibility in what to push to it. For EVM, we
// push BranchOpcode, Reg1, Reg2.
static void parseCondBranch(MachineInstr &LastInst, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
}

static unsigned getOppositeBranchOpcode(int Opc) {
  return 0;
}

bool EVMInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  return true;
}

unsigned EVMInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  return 0;
}

// Inserts a branch into the end of the specific MachineBasicBlock, returning
// the number of instructions inserted.
unsigned EVMInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  return 0;
}

unsigned EVMInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                              MachineBasicBlock &DestBB,
                                              const DebugLoc &DL,
                                              int64_t BrOffset,
                                              RegScavenger *RS) const {
  return 0;
}

bool EVMInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  return false;
}

MachineBasicBlock *
EVMInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  assert(MI.getDesc().isBranch() && "Unexpected opcode!");
  // The branch target is always the last operand.
  int NumOp = MI.getNumExplicitOperands();
  return MI.getOperand(NumOp - 1).getMBB();
}

bool EVMInstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                           int64_t BrOffset) const {
  return false;
}

unsigned EVMInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  return 0;
}

bool EVMInstrInfo::isAsCheapAsAMove(const MachineInstr &MI) const {
  return false;
}
