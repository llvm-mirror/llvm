//===-- MSP430MCAsmInfo.cpp - MSP430 asm properties -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the MSP430MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "MSP430MCAsmInfo.h"
#include "llvm/ADT/StringRef.h"
using namespace llvm;

void MSP430MCAsmInfo::anchor() { }

MSP430MCAsmInfo::MSP430MCAsmInfo(StringRef TT) {
  PointerSize = CalleeSaveStackSlotSize = 2;

  CommentString = ";";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;
}
