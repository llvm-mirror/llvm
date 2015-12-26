//===-- R600RegisterInfo.h - R600 Register Info Interface ------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Interface definition for R600RegisterInfo
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_R600_R600REGISTERINFO_H
#define LLVM_LIB_TARGET_R600_R600REGISTERINFO_H

#include "AMDGPURegisterInfo.h"

namespace llvm {

class AMDGPUSubtarget;

struct R600RegisterInfo : public AMDGPURegisterInfo {
  RegClassWeight RCW;

  R600RegisterInfo();

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  /// \brief get the HW encoding for a register's channel.
  unsigned getHWRegChan(unsigned reg) const;

  unsigned getHWRegIndex(unsigned Reg) const override;

  /// \brief get the register class of the specified type to use in the
  /// CFGStructurizer
  const TargetRegisterClass *getCFGStructurizerRegClass(MVT VT) const;

  const RegClassWeight &
    getRegClassWeight(const TargetRegisterClass *RC) const override;

  // \returns true if \p Reg can be defined in one ALU caluse and used in another.
  bool isPhysRegLiveAcrossClauses(unsigned Reg) const;
};

} // End namespace llvm

#endif
