//===- TypeBasedAliasAnalysis.h - Type-Based Alias Analysis -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for a metadata-based TBAA. See the source file for
/// details on the algorithm.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
#define LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Pass.h"

namespace llvm {

/// A simple AA result that uses TBAA metadata to answer queries.
class TypeBasedAAResult : public AAResultBase<TypeBasedAAResult> {
  friend AAResultBase<TypeBasedAAResult>;

public:
  explicit TypeBasedAAResult(const TargetLibraryInfo &TLI)
      : AAResultBase(TLI) {}
  TypeBasedAAResult(TypeBasedAAResult &&Arg) : AAResultBase(std::move(Arg)) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &) { return false; }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal);
  FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS);
  FunctionModRefBehavior getModRefBehavior(const Function *F);
  ModRefInfo getModRefInfo(ImmutableCallSite CS, const MemoryLocation &Loc);
  ModRefInfo getModRefInfo(ImmutableCallSite CS1, ImmutableCallSite CS2);

private:
  bool Aliases(const MDNode *A, const MDNode *B) const;
  bool PathAliases(const MDNode *A, const MDNode *B) const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class TypeBasedAA {
public:
  typedef TypeBasedAAResult Result;

  /// \brief Opaque, unique identifier for this analysis pass.
  static void *ID() { return (void *)&PassID; }

  TypeBasedAAResult run(Function &F, AnalysisManager<Function> *AM);

  /// \brief Provide access to a name for this pass for debugging purposes.
  static StringRef name() { return "TypeBasedAA"; }

private:
  static char PassID;
};

/// Legacy wrapper pass to provide the TypeBasedAAResult object.
class TypeBasedAAWrapperPass : public ImmutablePass {
  std::unique_ptr<TypeBasedAAResult> Result;

public:
  static char ID;

  TypeBasedAAWrapperPass();

  TypeBasedAAResult &getResult() { return *Result; }
  const TypeBasedAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createTypeBasedAAWrapperPass - This pass implements metadata-based
// type-based alias analysis.
//
ImmutablePass *createTypeBasedAAWrapperPass();
}

#endif
