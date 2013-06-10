//===-- rvexFrameLowering.h - Define frame lowering for rvex ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//
#ifndef rvex_FRAMEINFO_H
#define rvex_FRAMEINFO_H

#include "rvex.h"
#include "rvexSubtarget.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
  class rvexSubtarget;

class rvexFrameLowering : public TargetFrameLowering {
protected:
  const rvexSubtarget &STI;

public:
  explicit rvexFrameLowering(const rvexSubtarget &sti)
    : TargetFrameLowering(StackGrowsDown, 8, 0),
      STI(sti) {
  }

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF) const;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const;
  bool hasFP(const MachineFunction &MF) const;
  void processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                            RegScavenger *RS) const;
};

} // End llvm namespace

#endif

