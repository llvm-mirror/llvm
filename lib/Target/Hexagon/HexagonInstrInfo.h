//===- HexagonInstrInfo.h - Hexagon Instruction Information -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONINSTRINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONINSTRINFO_H

#include "HexagonRegisterInfo.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "HexagonGenInstrInfo.inc"

namespace llvm {

struct EVT;
class HexagonSubtarget;

class HexagonInstrInfo : public HexagonGenInstrInfo {
  virtual void anchor();
  const HexagonRegisterInfo RI;

public:
  explicit HexagonInstrInfo(HexagonSubtarget &ST);

  /// TargetInstrInfo overrides.
  ///

  /// If the specified machine instruction is a direct
  /// load from a stack slot, return the virtual or physical register number of
  /// the destination along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than loading from the stack slot.
  unsigned isLoadFromStackSlot(const MachineInstr *MI,
                               int &FrameIndex) const override;

  /// If the specified machine instruction is a direct
  /// store to a stack slot, return the virtual or physical register number of
  /// the source reg along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than storing to the stack slot.
  unsigned isStoreToStackSlot(const MachineInstr *MI,
                              int &FrameIndex) const override;

  /// Analyze the branching code at the end of MBB, returning
  /// true if it cannot be understood (e.g. it's a switch dispatch or isn't
  /// implemented for a target).  Upon success, this returns false and returns
  /// with the following information in various cases:
  ///
  /// 1. If this block ends with no branches (it just falls through to its succ)
  ///    just return false, leaving TBB/FBB null.
  /// 2. If this block ends with only an unconditional branch, it sets TBB to be
  ///    the destination block.
  /// 3. If this block ends with a conditional branch and it falls through to a
  ///    successor block, it sets TBB to be the branch destination block and a
  ///    list of operands that evaluate the condition. These operands can be
  ///    passed to other TargetInstrInfo methods to create new branches.
  /// 4. If this block ends with a conditional branch followed by an
  ///    unconditional branch, it returns the 'true' destination in TBB, the
  ///    'false' destination in FBB, and a list of operands that evaluate the
  ///    condition.  These operands can be passed to other TargetInstrInfo
  ///    methods to create new branches.
  ///
  /// Note that RemoveBranch and InsertBranch must be implemented to support
  /// cases where this method returns success.
  ///
  /// If AllowModify is true, then this routine is allowed to modify the basic
  /// block (e.g. delete instructions after the unconditional branch).
  ///
  bool AnalyzeBranch(MachineBasicBlock &MBB,MachineBasicBlock *&TBB,
                         MachineBasicBlock *&FBB,
                         SmallVectorImpl<MachineOperand> &Cond,
                         bool AllowModify) const override;

  /// Remove the branching code at the end of the specific MBB.
  /// This is only invoked in cases where AnalyzeBranch returns success. It
  /// returns the number of instructions that were removed.
  unsigned RemoveBranch(MachineBasicBlock &MBB) const override;

  /// Insert branch code into the end of the specified MachineBasicBlock.
  /// The operands to this method are the same as those
  /// returned by AnalyzeBranch.  This is only invoked in cases where
  /// AnalyzeBranch returns success. It returns the number of instructions
  /// inserted.
  ///
  /// It is also invoked by tail merging to add unconditional branches in
  /// cases where AnalyzeBranch doesn't apply because there was no original
  /// branch to analyze.  At least this much must be implemented, else tail
  /// merging needs to be disabled.
  unsigned InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        DebugLoc DL) const override;

  /// Return true if it's profitable to predicate
  /// instructions with accumulated instruction latency of "NumCycles"
  /// of the specified basic block, where the probability of the instructions
  /// being executed is given by Probability, and Confidence is a measure
  /// of our confidence that it will be properly predicted.
  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;

  /// Second variant of isProfitableToIfCvt. This one
  /// checks for the case where two basic blocks from true and false path
  /// of a if-then-else (diamond) are predicated on mutally exclusive
  /// predicates, where the probability of the true path being taken is given
  /// by Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumTCycles, unsigned ExtraTCycles,
                           MachineBasicBlock &FMBB,
                           unsigned NumFCycles, unsigned ExtraFCycles,
                           BranchProbability Probability) const override;

  /// Return true if it's profitable for if-converter to duplicate instructions
  /// of specified accumulated instruction latencies in the specified MBB to
  /// enable if-conversion.
  /// The probability of the instructions being executed is given by
  /// Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                 BranchProbability Probability) const override;

  /// Emit instructions to copy a pair of physical registers.
  ///
  /// This function should support copies within any legal register class as
  /// well as any cross-class copies created during instruction selection.
  ///
  /// The source and destination registers may overlap, which may require a
  /// careful implementation when multiple copy instructions are required for
  /// large registers. See for example the ARM target.
  void copyPhysReg(MachineBasicBlock &MBB,
                   MachineBasicBlock::iterator I, DebugLoc DL,
                   unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

  /// Store the specified register of the given register class to the specified
  /// stack frame index. The store instruction is to be added to the given
  /// machine basic block before the specified machine instruction. If isKill
  /// is true, the register operand is the last use and must be marked kill.
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  /// Load the specified register of the given register class from the specified
  /// stack frame index. The load instruction is to be added to the given
  /// machine basic block before the specified machine instruction.
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  /// This function is called for all pseudo instructions
  /// that remain after register allocation. Many pseudo instructions are
  /// created to help register allocation. This is the place to convert them
  /// into real instructions. The target can edit MI in place, or it can insert
  /// new instructions and erase MI. The function should return true if
  /// anything was changed.
  bool expandPostRAPseudo(MachineBasicBlock::iterator MI) const override;

  /// Reverses the branch condition of the specified condition list,
  /// returning false on success and true if it cannot be reversed.
  bool ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond)
        const override;

  /// Insert a noop into the instruction stream at the specified point.
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  /// Returns true if the instruction is already predicated.
  bool isPredicated(const MachineInstr *MI) const override;

  /// Convert the instruction into a predicated instruction.
  /// It returns true if the operation was successful.
  bool PredicateInstruction(MachineInstr *MI,
                            ArrayRef<MachineOperand> Cond) const override;

  /// Returns true if the first specified predicate
  /// subsumes the second, e.g. GE subsumes GT.
  bool SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                         ArrayRef<MachineOperand> Pred2) const override;

  /// If the specified instruction defines any predicate
  /// or condition code register(s) used for predication, returns true as well
  /// as the definition predicate(s) by reference.
  bool DefinesPredicate(MachineInstr *MI,
                        std::vector<MachineOperand> &Pred) const override;

  /// Return true if the specified instruction can be predicated.
  /// By default, this returns true for every instruction with a
  /// PredicateOperand.
  bool isPredicable(MachineInstr *MI) const override;

  /// Test if the given instruction should be considered a scheduling boundary.
  /// This primarily includes labels and terminators.
  bool isSchedulingBoundary(const MachineInstr *MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  /// Measure the specified inline asm to determine an approximation of its
  /// length.
  unsigned getInlineAsmLength(const char *Str,
                              const MCAsmInfo &MAI) const override;

  /// Allocate and return a hazard recognizer to use for this target when
  /// scheduling the machine instructions after register allocation.
  ScheduleHazardRecognizer*
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData*,
                                     const ScheduleDAG *DAG) const override;

  /// For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  bool analyzeCompare(const MachineInstr *MI,
                      unsigned &SrcReg, unsigned &SrcReg2,
                      int &Mask, int &Value) const override;

  /// Compute the instruction latency of a given instruction.
  /// If the instruction has higher cost when predicated, it's returned via
  /// PredCost.
  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           const MachineInstr *MI,
                           unsigned *PredCost = 0) const override;

  /// Create machine specific model for scheduling.
  DFAPacketizer *
  CreateTargetScheduleState(const TargetSubtargetInfo &STI) const override;

  // Sometimes, it is possible for the target
  // to tell, even without aliasing information, that two MIs access different
  // memory addresses. This function returns true if two MIs access different
  // memory addresses and false otherwise.
  bool areMemAccessesTriviallyDisjoint(MachineInstr *MIa, MachineInstr *MIb,
                                       AliasAnalysis *AA = nullptr)
                                       const override;


  /// HexagonInstrInfo specifics.
  ///

  const HexagonRegisterInfo &getRegisterInfo() const { return RI; }

  unsigned createVR(MachineFunction* MF, MVT VT) const;

  bool isAbsoluteSet(const MachineInstr* MI) const;
  bool isAccumulator(const MachineInstr *MI) const;
  bool isComplex(const MachineInstr *MI) const;
  bool isCompoundBranchInstr(const MachineInstr *MI) const;
  bool isCondInst(const MachineInstr *MI) const;
  bool isConditionalALU32 (const MachineInstr* MI) const;
  bool isConditionalLoad(const MachineInstr* MI) const;
  bool isConditionalStore(const MachineInstr* MI) const;
  bool isConditionalTransfer(const MachineInstr* MI) const;
  bool isConstExtended(const MachineInstr *MI) const;
  bool isDeallocRet(const MachineInstr *MI) const;
  bool isDependent(const MachineInstr *ProdMI,
                   const MachineInstr *ConsMI) const;
  bool isDotCurInst(const MachineInstr* MI) const;
  bool isDotNewInst(const MachineInstr* MI) const;
  bool isDuplexPair(const MachineInstr *MIa, const MachineInstr *MIb) const;
  bool isEarlySourceInstr(const MachineInstr *MI) const;
  bool isEndLoopN(unsigned Opcode) const;
  bool isExpr(unsigned OpType) const;
  bool isExtendable(const MachineInstr* MI) const;
  bool isExtended(const MachineInstr* MI) const;
  bool isFloat(const MachineInstr *MI) const;
  bool isHVXMemWithAIndirect(const MachineInstr *I,
                             const MachineInstr *J) const;
  bool isIndirectCall(const MachineInstr *MI) const;
  bool isIndirectL4Return(const MachineInstr *MI) const;
  bool isJumpR(const MachineInstr *MI) const;
  bool isJumpWithinBranchRange(const MachineInstr *MI, unsigned offset) const;
  bool isLateInstrFeedsEarlyInstr(const MachineInstr *LRMI,
                                  const MachineInstr *ESMI) const;
  bool isLateResultInstr(const MachineInstr *MI) const;
  bool isLateSourceInstr(const MachineInstr *MI) const;
  bool isLoopN(const MachineInstr *MI) const;
  bool isMemOp(const MachineInstr *MI) const;
  bool isNewValue(const MachineInstr* MI) const;
  bool isNewValue(unsigned Opcode) const;
  bool isNewValueInst(const MachineInstr* MI) const;
  bool isNewValueJump(const MachineInstr* MI) const;
  bool isNewValueJump(unsigned Opcode) const;
  bool isNewValueStore(const MachineInstr* MI) const;
  bool isNewValueStore(unsigned Opcode) const;
  bool isOperandExtended(const MachineInstr *MI, unsigned OperandNum) const;
  bool isPostIncrement(const MachineInstr* MI) const;
  bool isPredicatedNew(const MachineInstr *MI) const;
  bool isPredicatedNew(unsigned Opcode) const;
  bool isPredicatedTrue(const MachineInstr *MI) const;
  bool isPredicatedTrue(unsigned Opcode) const;
  bool isPredicated(unsigned Opcode) const;
  bool isPredicateLate(unsigned Opcode) const;
  bool isPredictedTaken(unsigned Opcode) const;
  bool isSaveCalleeSavedRegsCall(const MachineInstr *MI) const;
  bool isSolo(const MachineInstr* MI) const;
  bool isSpillPredRegOp(const MachineInstr *MI) const;
  bool isTC1(const MachineInstr *MI) const;
  bool isTC2(const MachineInstr *MI) const;
  bool isTC2Early(const MachineInstr *MI) const;
  bool isTC4x(const MachineInstr *MI) const;
  bool isV60VectorInstruction(const MachineInstr *MI) const;
  bool isValidAutoIncImm(const EVT VT, const int Offset) const;
  bool isValidOffset(unsigned Opcode, int Offset, bool Extend = true) const;
  bool isVecAcc(const MachineInstr *MI) const;
  bool isVecALU(const MachineInstr *MI) const;
  bool isVecUsableNextPacket(const MachineInstr *ProdMI,
                             const MachineInstr *ConsMI) const;


  bool canExecuteInBundle(const MachineInstr *First,
                          const MachineInstr *Second) const;
  bool hasEHLabel(const MachineBasicBlock *B) const;
  bool hasNonExtEquivalent(const MachineInstr *MI) const;
  bool hasPseudoInstrPair(const MachineInstr *MI) const;
  bool hasUncondBranch(const MachineBasicBlock *B) const;
  bool mayBeCurLoad(const MachineInstr* MI) const;
  bool mayBeNewStore(const MachineInstr* MI) const;
  bool producesStall(const MachineInstr *ProdMI,
                     const MachineInstr *ConsMI) const;
  bool producesStall(const MachineInstr *MI,
                     MachineBasicBlock::const_instr_iterator MII) const;
  bool predCanBeUsedAsDotNew(const MachineInstr *MI, unsigned PredReg) const;
  bool PredOpcodeHasJMP_c(unsigned Opcode) const;
  bool predOpcodeHasNot(ArrayRef<MachineOperand> Cond) const;


  unsigned getAddrMode(const MachineInstr* MI) const;
  unsigned getBaseAndOffset(const MachineInstr *MI, int &Offset,
                            unsigned &AccessSize) const;
  bool getBaseAndOffsetPosition(const MachineInstr *MI, unsigned &BasePos,
                                unsigned &OffsetPos) const;
  SmallVector<MachineInstr*,2> getBranchingInstrs(MachineBasicBlock& MBB) const;
  unsigned getCExtOpNum(const MachineInstr *MI) const;
  HexagonII::CompoundGroup
  getCompoundCandidateGroup(const MachineInstr *MI) const;
  unsigned getCompoundOpcode(const MachineInstr *GA,
                             const MachineInstr *GB) const;
  int getCondOpcode(int Opc, bool sense) const;
  int getDotCurOp(const MachineInstr* MI) const;
  int getDotNewOp(const MachineInstr* MI) const;
  int getDotNewPredJumpOp(const MachineInstr *MI,
                          const MachineBranchProbabilityInfo *MBPI) const;
  int getDotNewPredOp(const MachineInstr *MI,
                      const MachineBranchProbabilityInfo *MBPI) const;
  int getDotOldOp(const int opc) const;
  HexagonII::SubInstructionGroup getDuplexCandidateGroup(const MachineInstr *MI)
                                                         const;
  short getEquivalentHWInstr(const MachineInstr *MI) const;
  MachineInstr *getFirstNonDbgInst(MachineBasicBlock *BB) const;
  unsigned getInstrTimingClassLatency(const InstrItineraryData *ItinData,
                                      const MachineInstr *MI) const;
  bool getInvertedPredSense(SmallVectorImpl<MachineOperand> &Cond) const;
  unsigned getInvertedPredicatedOpcode(const int Opc) const;
  int getMaxValue(const MachineInstr *MI) const;
  unsigned getMemAccessSize(const MachineInstr* MI) const;
  int getMinValue(const MachineInstr *MI) const;
  short getNonExtOpcode(const MachineInstr *MI) const;
  bool getPredReg(ArrayRef<MachineOperand> Cond, unsigned &PredReg,
                  unsigned &PredRegPos, unsigned &PredRegFlags) const;
  short getPseudoInstrPair(const MachineInstr *MI) const;
  short getRegForm(const MachineInstr *MI) const;
  unsigned getSize(const MachineInstr *MI) const;
  uint64_t getType(const MachineInstr* MI) const;
  unsigned getUnits(const MachineInstr* MI) const;
  unsigned getValidSubTargets(const unsigned Opcode) const;


  /// getInstrTimingClassLatency - Compute the instruction latency of a given
  /// instruction using Timing Class information, if available.
  unsigned nonDbgBBSize(const MachineBasicBlock *BB) const;
  unsigned nonDbgBundleSize(MachineBasicBlock::const_iterator BundleHead) const;


  void immediateExtend(MachineInstr *MI) const;
  bool invertAndChangeJumpTarget(MachineInstr* MI,
                                 MachineBasicBlock* NewTarget) const;
  void genAllInsnTimingClasses(MachineFunction &MF) const;
  bool reversePredSense(MachineInstr* MI) const;
  unsigned reversePrediction(unsigned Opcode) const;
  bool validateBranchCond(const ArrayRef<MachineOperand> &Cond) const;
};

}

#endif
