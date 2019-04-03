//===-- EVMInstrInfo.h - EVM Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the EVM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_EVMINSTRINFO_H
#define LLVM_LIB_TARGET_EVM_EVMINSTRINFO_H

#include "EVMRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "EVMGenInstrInfo.inc"

namespace llvm {

class EVMInstrInfo : public EVMGenInstrInfo {

public:
  explicit EVMInstrInfo();

  const EVMRegisterInfo &getRegisterInfo() const { return RI; }

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, unsigned SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, unsigned DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;


private:
  const EVMRegisterInfo RI;
};

namespace EVM {
  LLVM_READONLY
  int getStackOpcode(uint16_t Opcode);
};

}
#endif
