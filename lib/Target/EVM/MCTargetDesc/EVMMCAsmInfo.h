//===-- EVMMCAsmInfo.h - EVM asm properties -------------------*- C++ -*--====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the EVMMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMMCASMINFO_H
#define LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMMCASMINFO_H

#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class Target;

class EVMMCAsmInfo : public MCAsmInfo {
public:
  explicit EVMMCAsmInfo(const Triple &TT) {
  }
};
}

#endif
