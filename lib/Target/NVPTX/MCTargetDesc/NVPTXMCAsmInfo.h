//===-- NVPTXMCAsmInfo.h - NVPTX asm properties ----------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVPTXMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Target;
class StringRef;

class NVPTXMCAsmInfo : public MCAsmInfo {
  virtual void anchor();
public:
  explicit NVPTXMCAsmInfo(StringRef TT);
};
} // namespace llvm

#endif
