//===-- Mips.h - Top-level interface for Mips representation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM Mips back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS_H
#define LLVM_LIB_TARGET_MIPS_MIPS_H

#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class MipsTargetMachine;
  class FunctionPass;

  FunctionPass *createMipsOptimizePICCallPass(MipsTargetMachine &TM);
  FunctionPass *createMipsDelaySlotFillerPass(MipsTargetMachine &TM);
  FunctionPass *createMipsLongBranchPass(MipsTargetMachine &TM);
  FunctionPass *createMipsConstantIslandPass(MipsTargetMachine &tm);
} // end namespace llvm;

#endif
