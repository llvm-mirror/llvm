//===-- EVMMCAsmInfo.cpp - EVM Asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the EVMMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "EVMMCAsmInfo.h"
#include "llvm/ADT/Triple.h"
using namespace llvm;

void EVMMCAsmInfo::anchor() {}

EVMMCAsmInfo::EVMMCAsmInfo(const Triple &TT) {
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
}
