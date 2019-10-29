//===-- llvm/MC/MCAsmInfoEVM.h - EVM Asm info ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOEVM_H
#define LLVM_MC_MCASMINFOEVM_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class MCAsmInfoEVM : public MCAsmInfo {
  virtual void anchor();

protected:
  MCAsmInfoEVM();
};
} // namespace llvm

#endif
