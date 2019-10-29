//===-- MCAsmInfoEVM.cpp - EVM asm properties ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoEVM.h"
using namespace llvm;

void MCAsmInfoEVM::anchor() {}

MCAsmInfoEVM::MCAsmInfoEVM() {
  HasIdentDirective = false;
  HasNoDeadStrip = false;
}
