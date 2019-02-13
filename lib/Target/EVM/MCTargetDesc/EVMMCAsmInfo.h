//===-- EVMMCAsmInfo.h - EVM Asm Info ----------------------*- C++ -*--===//
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

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class EVMMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit EVMMCAsmInfo(const Triple &TargetTriple);
};

} // namespace llvm

#endif
