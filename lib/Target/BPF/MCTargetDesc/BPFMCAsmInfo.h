//===-- BPFMCAsmInfo.h - BPF asm properties -------------------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the BPFMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_MCTARGETDESC_BPFMCASMINFO_H
#define LLVM_LIB_TARGET_BPF_MCTARGETDESC_BPFMCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Target;

class BPFMCAsmInfo : public MCAsmInfo {
public:
  explicit BPFMCAsmInfo(StringRef TT) {
    PrivateGlobalPrefix = ".L";
    WeakRefDirective = "\t.weak\t";

    UsesELFSectionDirectiveForBSS = true;
    HasSingleParameterDotFile = false;
    HasDotTypeDotSizeDirective = false;
  }
};
}

#endif
