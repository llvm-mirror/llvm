//===-- SystemZInstrInfo.h - SystemZ instruction information ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the SystemZ implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H

#include "SystemZ.h"
#include "SystemZRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "SystemZGenInstrInfo.inc"

namespace llvm {

class SystemZTargetMachine;

namespace SystemZII {
enum {
  // See comments in SystemZInstrFormats.td.
  SimpleBDXLoad          = (1 << 0),
  SimpleBDXStore         = (1 << 1),
  Has20BitOffset         = (1 << 2),
  HasIndex               = (1 << 3),
  Is128Bit               = (1 << 4),
  AccessSizeMask         = (31 << 5),
  AccessSizeShift        = 5,
  CCValuesMask           = (15 << 10),
  CCValuesShift          = 10,
  CompareZeroCCMaskMask  = (15 << 14),
  CompareZeroCCMaskShift = 14,
  CCMaskFirst            = (1 << 18),
  CCMaskLast             = (1 << 19),
  IsLogical              = (1 << 20)
};
static inline unsigned getAccessSize(unsigned int Flags) {
  return (Flags & AccessSizeMask) >> AccessSizeShift;
}
static inline unsigned getCCValues(unsigned int Flags) {
  return (Flags & CCValuesMask) >> CCValuesShift;
}
static inline unsigned getCompareZeroCCMask(unsigned int Flags) {
  return (Flags & CompareZeroCCMaskMask) >> CompareZeroCCMaskShift;
}

// SystemZ MachineOperand target flags.
enum {
  // Masks out the bits for the access model.
  MO_SYMBOL_MODIFIER = (1 << 0),

  // @GOT (aka @GOTENT)
  MO_GOT = (1 << 0)
};
// Classifies a branch.
enum BranchType {
  // An instruction that branches on the current value of CC.
  BranchNormal,

  // An instruction that peforms a 32-bit signed comparison and branches
  // on the result.
  BranchC,

  // An instruction that peforms a 32-bit unsigned comparison and branches
  // on the result.
  BranchCL,

  // An instruction that peforms a 64-bit signed comparison and branches
  // on the result.
  BranchCG,

  // An instruction that peforms a 64-bit unsigned comparison and branches
  // on the result.
  BranchCLG,

  // An instruction that decrements a 32-bit register and branches if
  // the result is nonzero.
  BranchCT,

  // An instruction that decrements a 64-bit register and branches if
  // the result is nonzero.
  BranchCTG
};
// Information about a branch instruction.
struct Branch {
  // The type of the branch.
  BranchType Type;

  // CCMASK_<N> is set if CC might be equal to N.
  unsigned CCValid;

  // CCMASK_<N> is set if the branch should be taken when CC == N.
  unsigned CCMask;

  // The target of the branch.
  const MachineOperand *Target;

  Branch(BranchType type, unsigned ccValid, unsigned ccMask,
         const MachineOperand *target)
    : Type(type), CCValid(ccValid), CCMask(ccMask), Target(target) {}
};
} // end namespace SystemZII

class SystemZSubtarget;
class SystemZInstrInfo : public SystemZGenInstrInfo {
  const SystemZRegisterInfo RI;
  SystemZSubtarget &STI;

  void splitMove(MachineBasicBlock::iterator MI, unsigned NewOpcode) const;
  void splitAdjDynAlloc(MachineBasicBlock::iterator MI) const;
  void expandRIPseudo(MachineInstr *MI, unsigned LowOpcode,
                      unsigned HighOpcode, bool ConvertHigh) const;
  void expandRIEPseudo(MachineInstr *MI, unsigned LowOpcode,
                       unsigned LowOpcodeK, unsigned HighOpcode) const;
  void expandRXYPseudo(MachineInstr *MI, unsigned LowOpcode,
                       unsigned HighOpcode) const;
  void expandZExtPseudo(MachineInstr *MI, unsigned LowOpcode,
                        unsigned Size) const;
  void emitGRX32Move(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     DebugLoc DL, unsigned DestReg, unsigned SrcReg,
                     unsigned LowLowOpcode, unsigned Size, bool KillSrc) const;
  virtual void anchor();
  
public:
  explicit SystemZInstrInfo(SystemZSubtarget &STI);

  // Override TargetInstrInfo.
  unsigned isLoadFromStackSlot(const MachineInstr *MI,
                               int &FrameIndex) const override;
  unsigned isStoreToStackSlot(const MachineInstr *MI,
                              int &FrameIndex) const override;
  bool isStackSlotCopy(const MachineInstr *MI, int &DestFrameIndex,
                       int &SrcFrameIndex) const override;
  bool AnalyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned RemoveBranch(MachineBasicBlock &MBB) const override;
  unsigned InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB,
                        const SmallVectorImpl<MachineOperand> &Cond,
                        DebugLoc DL) const override;
  bool analyzeCompare(const MachineInstr *MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &Mask, int &Value) const override;
  bool optimizeCompareInstr(MachineInstr *CmpInstr, unsigned SrcReg,
                            unsigned SrcReg2, int Mask, int Value,
                            const MachineRegisterInfo *MRI) const override;
  bool isPredicable(MachineInstr *MI) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           const BranchProbability &Probability) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumCyclesT, unsigned ExtraPredCyclesT,
                           MachineBasicBlock &FMBB,
                           unsigned NumCyclesF, unsigned ExtraPredCyclesF,
                           const BranchProbability &Probability) const override;
  bool PredicateInstruction(MachineInstr *MI,
                            const SmallVectorImpl<MachineOperand> &Pred) const
    override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   DebugLoc DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIdx,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;
  MachineInstr *convertToThreeAddress(MachineFunction::iterator &MFI,
                                      MachineBasicBlock::iterator &MBBI,
                                      LiveVariables *LV) const override;
  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr *MI,
                                      const SmallVectorImpl<unsigned> &Ops,
                                      int FrameIndex) const override;
  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr* MI,
                                      const SmallVectorImpl<unsigned> &Ops,
                                      MachineInstr* LoadMI) const override;
  bool expandPostRAPseudo(MachineBasicBlock::iterator MBBI) const override;
  bool ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const
    override;

  // Return the SystemZRegisterInfo, which this class owns.
  const SystemZRegisterInfo &getRegisterInfo() const { return RI; }

  // Return the size in bytes of MI.
  uint64_t getInstSizeInBytes(const MachineInstr *MI) const;

  // Return true if MI is a conditional or unconditional branch.
  // When returning true, set Cond to the mask of condition-code
  // values on which the instruction will branch, and set Target
  // to the operand that contains the branch target.  This target
  // can be a register or a basic block.
  SystemZII::Branch getBranchInfo(const MachineInstr *MI) const;

  // Get the load and store opcodes for a given register class.
  void getLoadStoreOpcodes(const TargetRegisterClass *RC,
                           unsigned &LoadOpcode, unsigned &StoreOpcode) const;

  // Opcode is the opcode of an instruction that has an address operand,
  // and the caller wants to perform that instruction's operation on an
  // address that has displacement Offset.  Return the opcode of a suitable
  // instruction (which might be Opcode itself) or 0 if no such instruction
  // exists.
  unsigned getOpcodeForOffset(unsigned Opcode, int64_t Offset) const;

  // If Opcode is a load instruction that has a LOAD AND TEST form,
  // return the opcode for the testing form, otherwise return 0.
  unsigned getLoadAndTest(unsigned Opcode) const;

  // Return true if ROTATE AND ... SELECTED BITS can be used to select bits
  // Mask of the R2 operand, given that only the low BitSize bits of Mask are
  // significant.  Set Start and End to the I3 and I4 operands if so.
  bool isRxSBGMask(uint64_t Mask, unsigned BitSize,
                   unsigned &Start, unsigned &End) const;

  // If Opcode is a COMPARE opcode for which an associated COMPARE AND
  // BRANCH exists, return the opcode for the latter, otherwise return 0.
  // MI, if nonnull, is the compare instruction.
  unsigned getCompareAndBranch(unsigned Opcode,
                               const MachineInstr *MI = nullptr) const;

  // Emit code before MBBI in MI to move immediate value Value into
  // physical register Reg.
  void loadImmediate(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator MBBI,
                     unsigned Reg, uint64_t Value) const;
};
} // end namespace llvm

#endif
