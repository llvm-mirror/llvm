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

void EVMInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       unsigned SrcReg, bool IsKill, int FI,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  // Load the frame index and then store it using the FI offset.
  // fr = MLOAD FR
  // fo = ADD fr FI
  // ch = MSTORE fo SrcReg




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



}

void EVMInstrInfo::expandJUMPSUB(MachineInstr &MI) const {

  MachineFunction *MF = MI.getMF();
  const auto &STI = MF->getSubtarget<EVMSubtarget>();
  const EVMInstrInfo &TII = *STI.getInstrInfo();

  unsigned opc = MI.getOpcode();

  if (opc == EVM::pJUMPSUB_VOID) {
    // we set up the framepointer in ADJCALLSTACKUP/DOWN
    // so do nothing here.
    MI.setDesc(TII.get(EVM::CALLVOID_r));
    while (MI.getNumOperands() > 1) {
      MI.RemoveOperand(MI.getNumOperands() - 1);
    }
  } else {
    assert(opc == EVM::pJUMPSUB);
    // rv = pJUMPSUB tgt, variables
    MI.setDesc(TII.get(EVM::CALLRET_r));
    while (MI.getNumOperands() > 2) {
      MI.RemoveOperand(MI.getNumOperands() - 1);
    }
  }
}

void EVMInstrInfo::expandRETURNSUB(MachineInstr &MI) const {
  MachineFunction *MF = MI.getMF();
  const auto &STI = MF->getSubtarget<EVMSubtarget>();
  const EVMInstrInfo &TII = *STI.getInstrInfo();

  unsigned opc = MI.getOpcode();
  if (opc == EVM::pRETURNSUB_r) {
    // For an instruction:
    //   pRETURNSUB_r %retval
    // We should generate:
    //   PUSH_r %retval
    //   swap1
    //   JUMP_r
    // However it will break the convention we
    // built (PUSH does not have standalone form).
    // So here we will convert it into:
    //   RETSUB_r %retval
    // Which gets stackified into:
    //   PUSH32 %retval
    //   RETSUB
    // And we expand RETSUB in the MCEmitter to:
    //   SWAP1
    //   JUMP
    MI.setDesc(TII.get(EVM::RETSUB_r));
  } else if (opc == EVM::pRETURNSUBVOID_r) {
    // This version does not have the SWAP1,
    // it is simply translated into a JUMP.
    MI.setDesc(TII.get(EVM::RETSUBVOID_r));
  } else {
    llvm_unreachable("wrong opcode");
  }

}

void EVMInstrInfo::expandJUMPTO(MachineInstr &MI) const {

  MachineFunction *MF = MI.getMF();
  const auto &STI = MF->getSubtarget<EVMSubtarget>();
  const EVMInstrInfo &TII = *STI.getInstrInfo();

  // convert it to:
  // JUMP_r addr
  MI.setDesc(TII.get(EVM::JUMP_r));
}

void EVMInstrInfo::expandJUMPIF(MachineInstr &MI) const {
  llvm_unreachable("not implemented");

  MachineFunction *MF = MI.getMF();
  const auto &STI = MF->getSubtarget<EVMSubtarget>();
  const EVMInstrInfo &TII = *STI.getInstrInfo();

  // convert it to:
  // JUMPI_r addr
  MI.setDesc(TII.get(EVM::JUMPI_r));
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
  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator
    // instruction, we're done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled
    // by this analysis.
    if (!I->isBranch())
      return true;

    // Handle unconditional branches.
    if (I->getOpcode() == EVM::JUMP_r) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a J, delete them.
      while (std::next(I) != MBB.end())
        std::next(I)->eraseFromParent();
      Cond.clear();
      FBB = nullptr;

      // Delete the J if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }
    // Cannot handle conditional branches
    return true;
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
    // Unconditional branch
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(EVM::JUMP_r)).addMBB(TBB);
    return 1;
  }

  llvm_unreachable("Unexpected conditional branch");
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
