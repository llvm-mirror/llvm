//===-- MipsFrameLowering.h - Define frame lowering for Mips ----*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_MIPS_MIPSFRAMELOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSFRAMELOWERING_H

#include "Mips.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
  class MipsSubtarget;

class MipsFrameLowering : public TargetFrameLowering {
protected:
  const MipsSubtarget &STI;

public:
  explicit MipsFrameLowering(const MipsSubtarget &sti, unsigned Alignment)
    : TargetFrameLowering(StackGrowsDown, Alignment, 0, Alignment), STI(sti) {}

  static const MipsFrameLowering *create(const MipsSubtarget &ST);

  bool hasFP(const MachineFunction &MF) const override;

protected:
  uint64_t estimateStackSize(const MachineFunction &MF) const;
};

/// Create MipsFrameLowering objects.
const MipsFrameLowering *createMips16FrameLowering(const MipsSubtarget &ST);
const MipsFrameLowering *createMipsSEFrameLowering(const MipsSubtarget &ST);

} // End llvm namespace

#endif
