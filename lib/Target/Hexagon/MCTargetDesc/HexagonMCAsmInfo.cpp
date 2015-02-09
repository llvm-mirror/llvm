//===-- HexagonMCAsmInfo.cpp - Hexagon asm properties ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the HexagonMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "HexagonMCAsmInfo.h"

using namespace llvm;

// Pin the vtable to this file.
void HexagonMCAsmInfo::anchor() {}

HexagonMCAsmInfo::HexagonMCAsmInfo(StringRef TT) {
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = nullptr;  // .xword is only supported by V9.
  ZeroDirective = "\t.skip\t";
  CommentString = "//";

  LCOMMDirectiveAlignmentType = LCOMM::ByteAlignment;
  InlineAsmStart = "# InlineAsm Start";
  InlineAsmEnd = "# InlineAsm End";
  ZeroDirective = "\t.space\t";
  AscizDirective = "\t.string\t";

  SupportsDebugInformation = true;
  UsesELFSectionDirectiveForBSS  = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
}
