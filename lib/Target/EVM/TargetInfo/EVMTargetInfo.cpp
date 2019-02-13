//===-- EVMTargetInfo.cpp - EVM Target Implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

namespace llvm {
Target &getTheEVMTarget() {
  static Target TheEVMTarget;
  return TheEVMTarget;
}
}

extern "C" void LLVMInitializeEVMTargetInfo() {
  RegisterTarget<Triple::evm64> Y(getTheEVMTarget(), "evm64",
                                    "64-bit EVM", "EVM");
}
