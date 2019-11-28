//===-- EVMInstrInfo.h - EVM Instruction Information --------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_EVM_EVMINSTRINFO_H
#define LLVM_LIB_TARGET_EVM_EVMINSTRINFO_H

#include "llvm/Support/Debug.h"
#include "EVMRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "EVMGenInstrInfo.inc"

namespace llvm {

namespace EVM {
enum AsmComments {
  PUTLOCAL = 0,
  GETLOCAL,              
  SUBROUTINE_BEGIN,          
  SUBROUTINE_END,
  RETURN_FROM_SUBROUTINE,
  LAST_TYPE_OF_COMMENT = 1 << 15 
};
}

class EVMInstrInfo : public EVMGenInstrInfo {

public:
  explicit EVMInstrInfo();

  const EVMRegisterInfo &getRegisterInfo() const { return RI; }

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, unsigned SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, unsigned DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  bool analyzeBranch(MachineBasicBlock &MBB,
                     MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

private:
  void expandJUMPSUB(MachineInstr &MI) const;
  void expandRETURNSUB(MachineInstr &MI) const;
  void expandJUMPIF(MachineInstr &MI) const;
  void expandJUMPTO(MachineInstr &MI) const;

  MachineBasicBlock *findJumpTarget(MachineInstr& MI) const;

  const EVMRegisterInfo RI;
};

namespace EVM {
  LLVM_READONLY
  int getStackOpcode(uint16_t Opcode);
};

}
#endif
