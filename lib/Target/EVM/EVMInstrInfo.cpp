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
    : EVMGenInstrInfo(EVM::ADJCALLSTACKDOWN, EVM::ADJCALLSTACKUP), RI() {
}

void EVMInstrInfo::copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const {
  // replace the copy with pMOVE_r
  BuildMI(MBB, I, DL, get(EVM::pMOVE_r), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
}

void EVMInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       unsigned SrcReg, bool IsKill, int FI,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  // Load the frame index and then store it using the FI offset.
  // fr = MLOAD FR
  // fo = ADD fr FI
  // ch = MSTORE fo SrcReg

  llvm_unreachable("unimplemented");
}
void EVMInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MI, unsigned DestReg,
                        int FrameIndex, const TargetRegisterClass *RC,
                        const TargetRegisterInfo *TRI) const {
  // fr = MLOAD FR
  // pos = ADD fr FrameIndex
  // MLOAD pos
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  assert(RC == &EVM::GPRRegClass && "Invalid register class");

  llvm_unreachable("unimplemented");
}

bool EVMInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  // We do not use this phase. Instead we use ExpandPseudos pass.
  return false;
}

// We copied it from BPF backend. It seems to be quite incompleted,
// but let's bear with it for now.
bool EVMInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  
  bool HaveCond = false;
  for (MachineInstr &MI : MBB.terminators()) {
    switch (MI.getOpcode()) {
    default:
      // Unhandled instruction; bail out.
      return true;
    case EVM::JUMP_r: {
      MachineBasicBlock *TMBB = MI.getOperand(0).getMBB();
      if (!HaveCond) TBB = TMBB;
      else           FBB = TMBB;
      break;
      }
    case EVM::JUMPI_r: {
      if (HaveCond) return true;
      Cond.push_back(MachineOperand::CreateImm(true));
      Cond.push_back(MI.getOperand(0));
      TBB = MI.getOperand(1).getMBB();
      HaveCond = true;
      break;
      }
    }
  }

  return false;
}

unsigned EVMInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");

  if (Cond.empty()) {
    if (!TBB)
      return 0;

    BuildMI(&MBB, DL, get(EVM::JUMP_r)).addMBB(TBB);
    return 1;
  }

  assert(Cond.size() == 2 && "Expected a flag and a successor block");

  MachineFunction &MF = *MBB.getParent();
  auto &MRI = MF.getRegInfo();

  assert(Cond[0].getImm() && "insertBranch does not accept false parameter");

  BuildMI(&MBB, DL, get(EVM::JUMPI_r)).add(Cond[1]).addMBB(TBB);

  if (!FBB)
    return 1;

  BuildMI(&MBB, DL, get(EVM::JUMP_r)).addMBB(FBB);
  return 2;
}

unsigned EVMInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != EVM::JUMP_r)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}
