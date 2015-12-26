//===-- X86TargetFrameLowering.h - Define frame lowering for X86 -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements X86-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86FRAMELOWERING_H
#define LLVM_LIB_TARGET_X86_X86FRAMELOWERING_H

#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {

class MachineInstrBuilder;
class MCCFIInstruction;
class X86Subtarget;
class X86RegisterInfo;

class X86FrameLowering : public TargetFrameLowering {
public:
  X86FrameLowering(const X86Subtarget &STI, unsigned StackAlignOverride);

  // Cached subtarget predicates.

  const X86Subtarget &STI;
  const TargetInstrInfo &TII;
  const X86RegisterInfo *TRI;

  unsigned SlotSize;

  /// Is64Bit implies that x86_64 instructions are available.
  bool Is64Bit;

  bool IsLP64;

  /// True if the 64-bit frame or stack pointer should be used. True for most
  /// 64-bit targets with the exception of x32. If this is false, 32-bit
  /// instruction operands should be used to manipulate StackPtr and FramePtr.
  bool Uses64BitFramePtr;

  unsigned StackPtr;

  /// Emit target stack probe code. This is required for all
  /// large stack allocations on Windows. The caller is required to materialize
  /// the number of bytes to probe in RAX/EAX. Returns instruction just
  /// after the expansion.
  MachineInstr *emitStackProbe(MachineFunction &MF, MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI, DebugLoc DL,
                               bool InProlog) const;

  /// Replace a StackProbe inline-stub with the actual probe code inline.
  void inlineStackProbe(MachineFunction &MF,
                        MachineBasicBlock &PrologMBB) const override;

  void emitCalleeSavedFrameMoves(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 DebugLoc DL) const;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  void adjustForSegmentedStacks(MachineFunction &MF,
                                MachineBasicBlock &PrologueMBB) const override;

  void adjustForHiPEPrologue(MachineFunction &MF,
                             MachineBasicBlock &PrologueMBB) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  const std::vector<CalleeSavedInfo> &CSI,
                                  const TargetRegisterInfo *TRI) const override;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  bool canSimplifyCallFramePseudos(const MachineFunction &MF) const override;
  bool needsFrameIndexResolution(const MachineFunction &MF) const override;

  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;

  int getFrameIndexReferenceFromSP(const MachineFunction &MF, int FI,
                                   unsigned &FrameReg) const override;

  void eliminateCallFramePseudoInstr(MachineFunction &MF,
                                 MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI) const override;

  unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  /// Check the instruction before/after the passed instruction. If
  /// it is an ADD/SUB/LEA instruction it is deleted argument and the
  /// stack adjustment is returned as a positive value for ADD/LEA and
  /// a negative for SUB.
  int mergeSPUpdates(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                     bool doMergeWithPrevious) const;

  /// Emit a series of instructions to increment / decrement the stack
  /// pointer by a constant value.
  void emitSPUpdate(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                    int64_t NumBytes, bool InEpilogue) const;

  /// Check that LEA can be used on SP in an epilogue sequence for \p MF.
  bool canUseLEAForSPInEpilogue(const MachineFunction &MF) const;

  /// Check whether or not the given \p MBB can be used as a epilogue
  /// for the target.
  /// The epilogue will be inserted before the first terminator of that block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  /// convertArgMovsToPushes - This method tries to convert a call sequence
  /// that uses sub and mov instructions to put the argument onto the stack
  /// into a series of pushes.
  /// Returns true if the transformation succeeded, false if not.
  bool convertArgMovsToPushes(MachineFunction &MF, 
                              MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I, 
                              uint64_t Amount) const;

  /// Wraps up getting a CFI index and building a MachineInstr for it.
  void BuildCFI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                DebugLoc DL, MCCFIInstruction CFIInst) const;

  /// Sets up EBP and optionally ESI based on the incoming EBP value.  Only
  /// needed for 32-bit. Used in funclet prologues and at catchret destinations.
  MachineBasicBlock::iterator
  restoreWin32EHStackPointers(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI, DebugLoc DL,
                              bool RestoreSP = false) const;

private:
  uint64_t calculateMaxStackAlign(const MachineFunction &MF) const;

  /// Emit target stack probe as a call to a helper function
  MachineInstr *emitStackProbeCall(MachineFunction &MF, MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   DebugLoc DL, bool InProlog) const;

  /// Emit target stack probe as an inline sequence.
  MachineInstr *emitStackProbeInline(MachineFunction &MF,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     DebugLoc DL, bool InProlog) const;

  /// Emit a stub to later inline the target stack probe.
  MachineInstr *emitStackProbeInlineStub(MachineFunction &MF,
                                         MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI,
                                         DebugLoc DL, bool InProlog) const;

  /// Aligns the stack pointer by ANDing it with -MaxAlign.
  void BuildStackAlignAND(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, DebugLoc DL,
                          unsigned Reg, uint64_t MaxAlign) const;

  /// Make small positive stack adjustments using POPs.
  bool adjustStackWithPops(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, DebugLoc DL,
                           int Offset) const;

  /// Adjusts the stack pointer using LEA, SUB, or ADD.
  MachineInstrBuilder BuildStackAdjustment(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           DebugLoc DL, int64_t Offset,
                                           bool InEpilogue) const;

  unsigned getPSPSlotOffsetFromSP(const MachineFunction &MF) const;

  unsigned getWinEHFuncletFrameSize(const MachineFunction &MF) const;
};

} // End llvm namespace

#endif
