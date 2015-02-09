//===-- MCTargetDesc/AMDGPUMCAsmInfo.cpp - Assembly Info ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//===----------------------------------------------------------------------===//

#include "AMDGPUMCAsmInfo.h"

using namespace llvm;
AMDGPUMCAsmInfo::AMDGPUMCAsmInfo(StringRef &TT) : MCAsmInfoELF() {
  HasSingleParameterDotFile = false;
  //===------------------------------------------------------------------===//
  MaxInstLength = 16;
  SeparatorString = "\n";
  CommentString = ";";
  PrivateLabelPrefix = "";
  InlineAsmStart = ";#ASMSTART";
  InlineAsmEnd = ";#ASMEND";

  //===--- Data Emission Directives -------------------------------------===//
  ZeroDirective = ".zero";
  AsciiDirective = ".ascii\t";
  AscizDirective = ".asciz\t";
  Data8bitsDirective = ".byte\t";
  Data16bitsDirective = ".short\t";
  Data32bitsDirective = ".long\t";
  Data64bitsDirective = ".quad\t";
  SunStyleELFSectionSwitchSyntax = true;
  UsesELFSectionDirectiveForBSS = true;

  //===--- Global Variable Emission Directives --------------------------===//
  HasAggressiveSymbolFolding = true;
  COMMDirectiveAlignmentIsInBytes = false;
  HasDotTypeDotSizeDirective = false;
  HasNoDeadStrip = true;
  WeakRefDirective = ".weakref\t";
  //===--- Dwarf Emission Directives -----------------------------------===//
  SupportsDebugInformation = true;
}
