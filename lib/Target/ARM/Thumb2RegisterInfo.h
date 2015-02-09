//===- Thumb2RegisterInfo.h - Thumb-2 Register Information Impl -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-2 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_THUMB2REGISTERINFO_H
#define LLVM_LIB_TARGET_ARM_THUMB2REGISTERINFO_H

#include "ARMBaseRegisterInfo.h"

namespace llvm {

class ARMSubtarget;

struct Thumb2RegisterInfo : public ARMBaseRegisterInfo {
public:
  Thumb2RegisterInfo(const ARMSubtarget &STI);

  /// emitLoadConstPool - Emits a load from constpool to materialize the
  /// specified immediate.
  void
  emitLoadConstPool(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                    DebugLoc dl, unsigned DestReg, unsigned SubIdx, int Val,
                    ARMCC::CondCodes Pred = ARMCC::AL, unsigned PredReg = 0,
                    unsigned MIFlags = MachineInstr::NoFlags) const override;
};
}

#endif
