//===-- BPFFrameLowering.h - Define frame lowering for BPF -----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements BPF-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPFFRAMELOWERING_H
#define LLVM_LIB_TARGET_BPF_BPFFRAMELOWERING_H

#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class BPFSubtarget;

class BPFFrameLowering : public TargetFrameLowering {
public:
  explicit BPFFrameLowering(const BPFSubtarget &sti)
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 8, 0) {}

  void emitPrologue(MachineFunction &MF) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool hasFP(const MachineFunction &MF) const override;
  void processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                            RegScavenger *RS) const override;

  void
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override {
    MBB.erase(MI);
  }
};
}
#endif
