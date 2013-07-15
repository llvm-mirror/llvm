//===-- PPCInstrInfo.h - PowerPC Instruction Information --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef POWERPC_INSTRUCTIONINFO_H
#define POWERPC_INSTRUCTIONINFO_H

#include "PPC.h"
#include "PPCRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "PPCGenInstrInfo.inc"

namespace llvm {

/// PPCII - This namespace holds all of the PowerPC target-specific
/// per-instruction flags.  These must match the corresponding definitions in
/// PPC.td and PPCInstrFormats.td.
namespace PPCII {
enum {
  // PPC970 Instruction Flags.  These flags describe the characteristics of the
  // PowerPC 970 (aka G5) dispatch groups and how they are formed out of
  // raw machine instructions.

  /// PPC970_First - This instruction starts a new dispatch group, so it will
  /// always be the first one in the group.
  PPC970_First = 0x1,

  /// PPC970_Single - This instruction starts a new dispatch group and
  /// terminates it, so it will be the sole instruction in the group.
  PPC970_Single = 0x2,

  /// PPC970_Cracked - This instruction is cracked into two pieces, requiring
  /// two dispatch pipes to be available to issue.
  PPC970_Cracked = 0x4,

  /// PPC970_Mask/Shift - This is a bitmask that selects the pipeline type that
  /// an instruction is issued to.
  PPC970_Shift = 3,
  PPC970_Mask = 0x07 << PPC970_Shift
};
enum PPC970_Unit {
  /// These are the various PPC970 execution unit pipelines.  Each instruction
  /// is one of these.
  PPC970_Pseudo = 0 << PPC970_Shift,   // Pseudo instruction
  PPC970_FXU    = 1 << PPC970_Shift,   // Fixed Point (aka Integer/ALU) Unit
  PPC970_LSU    = 2 << PPC970_Shift,   // Load Store Unit
  PPC970_FPU    = 3 << PPC970_Shift,   // Floating Point Unit
  PPC970_CRU    = 4 << PPC970_Shift,   // Control Register Unit
  PPC970_VALU   = 5 << PPC970_Shift,   // Vector ALU
  PPC970_VPERM  = 6 << PPC970_Shift,   // Vector Permute Unit
  PPC970_BRU    = 7 << PPC970_Shift    // Branch Unit
};
} // end namespace PPCII


class PPCInstrInfo : public PPCGenInstrInfo {
  PPCTargetMachine &TM;
  const PPCRegisterInfo RI;

  bool StoreRegToStackSlot(MachineFunction &MF,
                           unsigned SrcReg, bool isKill, int FrameIdx,
                           const TargetRegisterClass *RC,
                           SmallVectorImpl<MachineInstr*> &NewMIs,
                           bool &NonRI, bool &SpillsVRS) const;
  bool LoadRegFromStackSlot(MachineFunction &MF, DebugLoc DL,
                            unsigned DestReg, int FrameIdx,
                            const TargetRegisterClass *RC,
                            SmallVectorImpl<MachineInstr*> &NewMIs,
                            bool &NonRI, bool &SpillsVRS) const;
public:
  explicit PPCInstrInfo(PPCTargetMachine &TM);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  virtual const PPCRegisterInfo &getRegisterInfo() const { return RI; }

  ScheduleHazardRecognizer *
  CreateTargetHazardRecognizer(const TargetMachine *TM,
                               const ScheduleDAG *DAG) const;
  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const;

  bool isCoalescableExtInstr(const MachineInstr &MI,
                             unsigned &SrcReg, unsigned &DstReg,
                             unsigned &SubIdx) const;
  unsigned isLoadFromStackSlot(const MachineInstr *MI,
                               int &FrameIndex) const;
  unsigned isStoreToStackSlot(const MachineInstr *MI,
                              int &FrameIndex) const;

  // commuteInstruction - We can commute rlwimi instructions, but only if the
  // rotate amt is zero.  We also have to munge the immediates a bit.
  virtual MachineInstr *commuteInstruction(MachineInstr *MI, bool NewMI) const;

  virtual void insertNoop(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MI) const;


  // Branch analysis.
  virtual bool AnalyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                             MachineBasicBlock *&FBB,
                             SmallVectorImpl<MachineOperand> &Cond,
                             bool AllowModify) const;
  virtual unsigned RemoveBranch(MachineBasicBlock &MBB) const;
  virtual unsigned InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                                MachineBasicBlock *FBB,
                                const SmallVectorImpl<MachineOperand> &Cond,
                                DebugLoc DL) const;

  // Select analysis.
  virtual bool canInsertSelect(const MachineBasicBlock&,
                               const SmallVectorImpl<MachineOperand> &Cond,
                               unsigned, unsigned, int&, int&, int&) const;
  virtual void insertSelect(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, DebugLoc DL,
                            unsigned DstReg,
                            const SmallVectorImpl<MachineOperand> &Cond,
                            unsigned TrueReg, unsigned FalseReg) const;

  virtual void copyPhysReg(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I, DebugLoc DL,
                           unsigned DestReg, unsigned SrcReg,
                           bool KillSrc) const;

  virtual void storeRegToStackSlot(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   unsigned SrcReg, bool isKill, int FrameIndex,
                                   const TargetRegisterClass *RC,
                                   const TargetRegisterInfo *TRI) const;

  virtual void loadRegFromStackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    unsigned DestReg, int FrameIndex,
                                    const TargetRegisterClass *RC,
                                    const TargetRegisterInfo *TRI) const;

  virtual
  bool ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const;

  virtual bool FoldImmediate(MachineInstr *UseMI, MachineInstr *DefMI,
                             unsigned Reg, MachineRegisterInfo *MRI) const;

  // If conversion by predication (only supported by some branch instructions).
  // All of the profitability checks always return true; it is always
  // profitable to use the predicated branches.
  virtual bool isProfitableToIfCvt(MachineBasicBlock &MBB,
                                   unsigned NumCycles, unsigned ExtraPredCycles,
                                   const BranchProbability &Probability) const {
    return true;
  }

  virtual bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                                   unsigned NumT, unsigned ExtraT,
                                   MachineBasicBlock &FMBB,
                                   unsigned NumF, unsigned ExtraF,
                                   const BranchProbability &Probability) const;

  virtual bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB,
                                         unsigned NumCycles,
                                         const BranchProbability
                                         &Probability) const {
    return true;
  }

  virtual bool isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                         MachineBasicBlock &FMBB) const {
    return false;
  }

  // Predication support.
  bool isPredicated(const MachineInstr *MI) const;

  virtual bool isUnpredicatedTerminator(const MachineInstr *MI) const;

  virtual
  bool PredicateInstruction(MachineInstr *MI,
                            const SmallVectorImpl<MachineOperand> &Pred) const;

  virtual
  bool SubsumesPredicate(const SmallVectorImpl<MachineOperand> &Pred1,
                         const SmallVectorImpl<MachineOperand> &Pred2) const;

  virtual bool DefinesPredicate(MachineInstr *MI,
                                std::vector<MachineOperand> &Pred) const;

  virtual bool isPredicable(MachineInstr *MI) const;

  // Comparison optimization.


  virtual bool analyzeCompare(const MachineInstr *MI,
                              unsigned &SrcReg, unsigned &SrcReg2,
                              int &Mask, int &Value) const;

  virtual bool optimizeCompareInstr(MachineInstr *CmpInstr,
                                    unsigned SrcReg, unsigned SrcReg2,
                                    int Mask, int Value,
                                    const MachineRegisterInfo *MRI) const;

  /// GetInstSize - Return the number of bytes of code the specified
  /// instruction may be.  This returns the maximum number of bytes.
  ///
  virtual unsigned GetInstSizeInBytes(const MachineInstr *MI) const;
};

}

#endif
