//===-- EVMInstrInfo.cpp - EVM Instruction Information ------*- C++ -*-===//
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

#include "EVMInstrInfo.h"
#include "EVM.h"
#include "EVMSubtarget.h"
#include "EVMTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "EVMGenInstrInfo.inc"

using namespace llvm;

EVMInstrInfo::EVMInstrInfo()
    : EVMGenInstrInfo(EVM::ADJCALLSTACKDOWN, EVM::ADJCALLSTACKUP), RI() {
}

void EVMInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       unsigned SrcReg, bool IsKill, int FI,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  // Load the frame index and then store it using the FI offset.
  // MSTORE (ADD (MLOAD 0) FI) REG
  // fr = MLOAD 0
  // fo = ADD fr FI
  // ch = MSTORE fo SrcReg


}
void EVMInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MI, unsigned DestReg,
                        int FrameIndex, const TargetRegisterClass *RC,
                        const TargetRegisterInfo *TRI) const {
  // fr = MLOAD 0
  // fo = ADD fr FI
  // rg = MLOAD fo 


}
