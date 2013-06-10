//===-- rvexTargetInfo.cpp - rvex Target Implementation -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "rvex.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::ThervexTarget;

extern "C" void LLVMInitializervexTargetInfo() {
  RegisterTarget<Triple::rvex,
        /*HasJIT=*/true> X(ThervexTarget, "rvex", "rvex");

}
