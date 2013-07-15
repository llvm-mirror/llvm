//===-- MipsSERegisterInfo.h - Mips32/64 Register Information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips32/64 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef MIPSSEREGISTERINFO_H
#define MIPSSEREGISTERINFO_H

#include "MipsRegisterInfo.h"

namespace llvm {
class MipsSEInstrInfo;

class MipsSERegisterInfo : public MipsRegisterInfo {
public:
  MipsSERegisterInfo(const MipsSubtarget &Subtarget);

  bool requiresRegisterScavenging(const MachineFunction &MF) const;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const;

  virtual const TargetRegisterClass *intRegClass(unsigned Size) const;

private:
  virtual void eliminateFI(MachineBasicBlock::iterator II, unsigned OpNo,
                           int FrameIndex, uint64_t StackSize,
                           int64_t SPOffset) const;
};

} // end namespace llvm

#endif
