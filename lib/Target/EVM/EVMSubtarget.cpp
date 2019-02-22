//===-- EVMSubtarget.cpp - EVM Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the EVM specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "EVMSubtarget.h"
#include "EVM.h"
#include "EVMFrameLowering.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "evm-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "EVMGenSubtargetInfo.inc"

void EVMSubtarget::anchor() {}

EVMSubtarget &EVMSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef FS,
                                                            bool Is64Bit) {
  // Determine default and user-specified characteristics
  return *this;
}

EVMSubtarget::EVMSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : EVMGenSubtargetInfo(TT, CPU, FS),
      FrameLowering(initializeSubtargetDependencies(CPU, FS, TT.isArch64Bit())),
      InstrInfo(), TLInfo(TM, *this) {}
