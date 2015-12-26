//===-- Analysis.cpp ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Analysis.h"
#include "llvm-c/Initialization.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

using namespace llvm;

/// initializeAnalysis - Initialize all passes linked into the Analysis library.
void llvm::initializeAnalysis(PassRegistry &Registry) {
  initializeAAEvalPass(Registry);
  initializeAliasSetPrinterPass(Registry);
  initializeBasicAAWrapperPassPass(Registry);
  initializeBlockFrequencyInfoWrapperPassPass(Registry);
  initializeBranchProbabilityInfoWrapperPassPass(Registry);
  initializeCallGraphWrapperPassPass(Registry);
  initializeCallGraphPrinterPass(Registry);
  initializeCallGraphViewerPass(Registry);
  initializeCostModelAnalysisPass(Registry);
  initializeCFGViewerPass(Registry);
  initializeCFGPrinterPass(Registry);
  initializeCFGOnlyViewerPass(Registry);
  initializeCFGOnlyPrinterPass(Registry);
  initializeCFLAAWrapperPassPass(Registry);
  initializeDependenceAnalysisPass(Registry);
  initializeDelinearizationPass(Registry);
  initializeDemandedBitsPass(Registry);
  initializeDivergenceAnalysisPass(Registry);
  initializeDominanceFrontierPass(Registry);
  initializeDomViewerPass(Registry);
  initializeDomPrinterPass(Registry);
  initializeDomOnlyViewerPass(Registry);
  initializePostDomViewerPass(Registry);
  initializeDomOnlyPrinterPass(Registry);
  initializePostDomPrinterPass(Registry);
  initializePostDomOnlyViewerPass(Registry);
  initializePostDomOnlyPrinterPass(Registry);
  initializeAAResultsWrapperPassPass(Registry);
  initializeGlobalsAAWrapperPassPass(Registry);
  initializeIVUsersPass(Registry);
  initializeInstCountPass(Registry);
  initializeIntervalPartitionPass(Registry);
  initializeLazyValueInfoPass(Registry);
  initializeLintPass(Registry);
  initializeLoopInfoWrapperPassPass(Registry);
  initializeMemDepPrinterPass(Registry);
  initializeMemDerefPrinterPass(Registry);
  initializeMemoryDependenceAnalysisPass(Registry);
  initializeModuleDebugInfoPrinterPass(Registry);
  initializeObjCARCAAWrapperPassPass(Registry);
  initializePostDominatorTreePass(Registry);
  initializeRegionInfoPassPass(Registry);
  initializeRegionViewerPass(Registry);
  initializeRegionPrinterPass(Registry);
  initializeRegionOnlyViewerPass(Registry);
  initializeRegionOnlyPrinterPass(Registry);
  initializeSCEVAAWrapperPassPass(Registry);
  initializeScalarEvolutionWrapperPassPass(Registry);
  initializeTargetTransformInfoWrapperPassPass(Registry);
  initializeTypeBasedAAWrapperPassPass(Registry);
  initializeScopedNoAliasAAWrapperPassPass(Registry);
}

void LLVMInitializeAnalysis(LLVMPassRegistryRef R) {
  initializeAnalysis(*unwrap(R));
}

void LLVMInitializeIPA(LLVMPassRegistryRef R) {
  initializeAnalysis(*unwrap(R));
}

LLVMBool LLVMVerifyModule(LLVMModuleRef M, LLVMVerifierFailureAction Action,
                          char **OutMessages) {
  raw_ostream *DebugOS = Action != LLVMReturnStatusAction ? &errs() : nullptr;
  std::string Messages;
  raw_string_ostream MsgsOS(Messages);

  LLVMBool Result = verifyModule(*unwrap(M), OutMessages ? &MsgsOS : DebugOS);

  // Duplicate the output to stderr.
  if (DebugOS && OutMessages)
    *DebugOS << MsgsOS.str();

  if (Action == LLVMAbortProcessAction && Result)
    report_fatal_error("Broken module found, compilation aborted!");

  if (OutMessages)
    *OutMessages = strdup(MsgsOS.str().c_str());

  return Result;
}

LLVMBool LLVMVerifyFunction(LLVMValueRef Fn, LLVMVerifierFailureAction Action) {
  LLVMBool Result = verifyFunction(
      *unwrap<Function>(Fn), Action != LLVMReturnStatusAction ? &errs()
                                                              : nullptr);

  if (Action == LLVMAbortProcessAction && Result)
    report_fatal_error("Broken function found, compilation aborted!");

  return Result;
}

void LLVMViewFunctionCFG(LLVMValueRef Fn) {
  Function *F = unwrap<Function>(Fn);
  F->viewCFG();
}

void LLVMViewFunctionCFGOnly(LLVMValueRef Fn) {
  Function *F = unwrap<Function>(Fn);
  F->viewCFGOnly();
}
