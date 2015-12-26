//===- ADCE.h - Aggressive dead code elimination --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Aggressive Dead Code Elimination
// pass. This pass optimistically assumes that all instructions are dead until
// proven otherwise, allowing it to eliminate dead computations that other DCE
// passes do not catch, particularly involving loop computations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_ADCE_H
#define LLVM_TRANSFORMS_SCALAR_ADCE_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// A DCE pass that assumes instructions are dead until proven otherwise.
///
/// This pass eliminates dead code by optimistically assuming that all
/// instructions are dead until proven otherwise. This allows it to eliminate
/// dead computations that other DCE passes do not catch, particularly involving
/// loop computations.
class ADCEPass {
public:
  static StringRef name() { return "ADCEPass"; }
  PreservedAnalyses run(Function &F);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_ADCE_H
