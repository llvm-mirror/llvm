//===-- MipsInstrInfo.h - Mips Instruction Information ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of the TargetInstrInfo class.
//
// FIXME: We need to override TargetInstrInfo::getInlineAsmLength method in
// order for MipsLongBranch pass to work correctly when the code has inline
// assembly.  The returned value doesn't have to be the asm instruction's exact
// size in bytes; MipsLongBranch only expects it to be the correct upper bound.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSINSTRINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPSINSTRINFO_H

#include "Mips.h"
#include "MipsAnalyzeImmediate.h"
#include "MipsRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "MipsGenInstrInfo.inc"

namespace llvm {

class MipsInstrInfo : public MipsGenInstrInfo {
  virtual void anchor();
protected:
  const MipsSubtarget &Subtarget;
  unsigned UncondBrOpc;

public:
  enum BranchType {
    BT_None,       // Couldn't analyze branch.
    BT_NoBranch,   // No branches found.
    BT_Uncond,     // One unconditional branch.
    BT_Cond,       // One conditional branch.
    BT_CondUncond, // A conditional branch followed by an unconditional branch.
    BT_Indirect    // One indirct branch.
  };

  explicit MipsInstrInfo(const MipsSubtarget &STI, unsigned UncondBrOpc);

  static const MipsInstrInfo *create(MipsSubtarget &STI);

  /// Branch Analysis
  bool AnalyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  unsigned RemoveBranch(MachineBasicBlock &MBB) const override;

  unsigned InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB,
                        const SmallVectorImpl<MachineOperand> &Cond,
                        DebugLoc DL) const override;

  bool
  ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  BranchType AnalyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                           MachineBasicBlock *&FBB,
                           SmallVectorImpl<MachineOperand> &Cond,
                           bool AllowModify,
                           SmallVectorImpl<MachineInstr*> &BranchInstrs) const;

  /// Insert nop instruction when hazard condition is found
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  virtual const MipsRegisterInfo &getRegisterInfo() const = 0;

  virtual unsigned getOppositeBranchOpc(unsigned Opc) const = 0;

  /// Return the number of bytes of code the specified instruction may be.
  unsigned GetInstSizeInBytes(const MachineInstr *MI) const;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override {
    storeRegToStack(MBB, MBBI, SrcReg, isKill, FrameIndex, RC, TRI, 0);
  }

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override {
    loadRegFromStack(MBB, MBBI, DestReg, FrameIndex, RC, TRI, 0);
  }

  virtual void storeRegToStack(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               unsigned SrcReg, bool isKill, int FrameIndex,
                               const TargetRegisterClass *RC,
                               const TargetRegisterInfo *TRI,
                               int64_t Offset) const = 0;

  virtual void loadRegFromStack(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                unsigned DestReg, int FrameIndex,
                                const TargetRegisterClass *RC,
                                const TargetRegisterInfo *TRI,
                                int64_t Offset) const = 0;

  /// Create an instruction which has the same operands and memory operands
  /// as MI but has a new opcode.
  MachineInstrBuilder genInstrWithNewOpc(unsigned NewOpc,
                                         MachineBasicBlock::iterator I) const;

protected:
  bool isZeroImm(const MachineOperand &op) const;

  MachineMemOperand *GetMemOperand(MachineBasicBlock &MBB, int FI,
                                   unsigned Flag) const;

private:
  virtual unsigned getAnalyzableBrOpc(unsigned Opc) const = 0;

  void AnalyzeCondBr(const MachineInstr *Inst, unsigned Opc,
                     MachineBasicBlock *&BB,
                     SmallVectorImpl<MachineOperand> &Cond) const;

  void BuildCondBr(MachineBasicBlock &MBB, MachineBasicBlock *TBB, DebugLoc DL,
                   const SmallVectorImpl<MachineOperand>& Cond) const;
};

/// Create MipsInstrInfo objects.
const MipsInstrInfo *createMips16InstrInfo(const MipsSubtarget &STI);
const MipsInstrInfo *createMipsSEInstrInfo(const MipsSubtarget &STI);

}

#endif
