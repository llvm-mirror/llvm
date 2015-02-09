//===-- R600MachineFunctionInfo.cpp - R600 Machine Function Info-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//===----------------------------------------------------------------------===//

#include "R600MachineFunctionInfo.h"

using namespace llvm;


// Pin the vtable to this file.
void R600MachineFunctionInfo::anchor() {}

R600MachineFunctionInfo::R600MachineFunctionInfo(const MachineFunction &MF)
  : AMDGPUMachineFunction(MF) { }
