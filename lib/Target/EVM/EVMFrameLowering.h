//===-- EVMFrameLowering.h - Define frame lowering for EVM -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements EVM-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_EVMFRAMELOWERING_H
#define LLVM_LIB_TARGET_EVM_EVMFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class EVMSubtarget;

class EVMFrameLowering : public TargetFrameLowering {
public:
  explicit EVMFrameLowering(const EVMSubtarget &STI)
      : TargetFrameLowering(StackGrowsUp,
                            /*StackAlignment=*/16,
                            /*LocalAreaOffset=*/0),
        STI(STI) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  bool hasFP(const MachineFunction &MF) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  const char* getTargetNodeName(unsigned Opcode) const;

protected:
  const EVMSubtarget &STI;

private:
  unsigned calculateFrameSize(MachineFunction &MF);
};
}
#endif
