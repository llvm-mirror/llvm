//===-- llvm/Analysis/Passes.h - Constructors for analyses ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the analysis libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PASSES_H
#define LLVM_ANALYSIS_PASSES_H

namespace llvm {
  class FunctionPass;
  class ImmutablePass;
  class LoopPass;
  class ModulePass;
  class Pass;
  class PassInfo;

  //===--------------------------------------------------------------------===//
  //
  // createAAEvalPass - This pass implements a simple N^2 alias analysis
  // accuracy evaluator.
  //
  FunctionPass *createAAEvalPass();

  //===--------------------------------------------------------------------===//
  //
  // createObjCARCAAWrapperPass - This pass implements ObjC-ARC-based
  // alias analysis.
  //
  ImmutablePass *createObjCARCAAWrapperPass();

  FunctionPass *createPAEvalPass();

  //===--------------------------------------------------------------------===//
  //
  /// createLazyValueInfoPass - This creates an instance of the LazyValueInfo
  /// pass.
  FunctionPass *createLazyValueInfoPass();

  //===--------------------------------------------------------------------===//
  //
  // createDependenceAnalysisPass - This creates an instance of the
  // DependenceAnalysis pass.
  //
  FunctionPass *createDependenceAnalysisPass();

  //===--------------------------------------------------------------------===//
  //
  // createCostModelAnalysisPass - This creates an instance of the
  // CostModelAnalysis pass.
  //
  FunctionPass *createCostModelAnalysisPass();

  //===--------------------------------------------------------------------===//
  //
  // createDelinearizationPass - This pass implements attempts to restore
  // multidimensional array indices from linearized expressions.
  //
  FunctionPass *createDelinearizationPass();

  //===--------------------------------------------------------------------===//
  //
  // createDivergenceAnalysisPass - This pass determines which branches in a GPU
  // program are divergent.
  //
  FunctionPass *createDivergenceAnalysisPass();

  //===--------------------------------------------------------------------===//
  //
  // Minor pass prototypes, allowing us to expose them through bugpoint and
  // analyze.
  FunctionPass *createInstCountPass();

  //===--------------------------------------------------------------------===//
  //
  // createRegionInfoPass - This pass finds all single entry single exit regions
  // in a function and builds the region hierarchy.
  //
  FunctionPass *createRegionInfoPass();

  // Print module-level debug info metadata in human-readable form.
  ModulePass *createModuleDebugInfoPrinterPass();

  //===--------------------------------------------------------------------===//
  //
  // createMemDepPrinter - This pass exhaustively collects all memdep
  // information and prints it with -analyze.
  //
  FunctionPass *createMemDepPrinter();

  //===--------------------------------------------------------------------===//
  //
  // createMemDerefPrinter - This pass collects memory dereferenceability
  // information and prints it with -analyze.
  //
  FunctionPass *createMemDerefPrinter();

}

#endif
