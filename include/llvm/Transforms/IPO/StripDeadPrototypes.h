//===-- StripDeadPrototypes.h - Remove unused function declarations -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass loops over all of the functions in the input module, looking for
// dead declarations and removes them. Dead declarations are declarations of
// functions for which no implementation is available (i.e., declarations for
// unused library functions).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H
#define LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass to remove unused function declarations.
class StripDeadPrototypesPass {
public:
  static StringRef name() { return "StripDeadPrototypesPass"; }
  PreservedAnalyses run(Module &M);
};

}

#endif // LLVM_TRANSFORMS_IPO_STRIPDEADPROTOTYPES_H
