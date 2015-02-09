//===- PassManagerBuilder.cpp - Build Standard Pass -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the PassManagerBuilder class, which is used to set up a
// "standard" optimization sequence suitable for languages like C and C++.
//
//===----------------------------------------------------------------------===//


#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm-c/Transforms/PassManagerBuilder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Vectorize.h"

using namespace llvm;

static cl::opt<bool>
RunLoopVectorization("vectorize-loops", cl::Hidden,
                     cl::desc("Run the Loop vectorization passes"));

static cl::opt<bool>
RunSLPVectorization("vectorize-slp", cl::Hidden,
                    cl::desc("Run the SLP vectorization passes"));

static cl::opt<bool>
RunBBVectorization("vectorize-slp-aggressive", cl::Hidden,
                    cl::desc("Run the BB vectorization passes"));

static cl::opt<bool>
UseGVNAfterVectorization("use-gvn-after-vectorization",
  cl::init(false), cl::Hidden,
  cl::desc("Run GVN instead of Early CSE after vectorization passes"));

static cl::opt<bool> ExtraVectorizerPasses(
    "extra-vectorizer-passes", cl::init(false), cl::Hidden,
    cl::desc("Run cleanup optimization passes after vectorization."));

static cl::opt<bool> UseNewSROA("use-new-sroa",
  cl::init(true), cl::Hidden,
  cl::desc("Enable the new, experimental SROA pass"));

static cl::opt<bool>
RunLoopRerolling("reroll-loops", cl::Hidden,
                 cl::desc("Run the loop rerolling pass"));

static cl::opt<bool> RunLoadCombine("combine-loads", cl::init(false),
                                    cl::Hidden,
                                    cl::desc("Run the load combining pass"));

static cl::opt<bool>
RunSLPAfterLoopVectorization("run-slp-after-loop-vectorization",
  cl::init(true), cl::Hidden,
  cl::desc("Run the SLP vectorizer (and BB vectorizer) after the Loop "
           "vectorizer instead of before"));

static cl::opt<bool> UseCFLAA("use-cfl-aa",
  cl::init(false), cl::Hidden,
  cl::desc("Enable the new, experimental CFL alias analysis"));

static cl::opt<bool>
EnableMLSM("mlsm", cl::init(true), cl::Hidden,
           cl::desc("Enable motion of merged load and store"));

PassManagerBuilder::PassManagerBuilder() {
    OptLevel = 2;
    SizeLevel = 0;
    LibraryInfo = nullptr;
    Inliner = nullptr;
    DisableTailCalls = false;
    DisableUnitAtATime = false;
    DisableUnrollLoops = false;
    BBVectorize = RunBBVectorization;
    SLPVectorize = RunSLPVectorization;
    LoopVectorize = RunLoopVectorization;
    RerollLoops = RunLoopRerolling;
    LoadCombine = RunLoadCombine;
    DisableGVNLoadPRE = false;
    VerifyInput = false;
    VerifyOutput = false;
    StripDebug = false;
    MergeFunctions = false;
}

PassManagerBuilder::~PassManagerBuilder() {
  delete LibraryInfo;
  delete Inliner;
}

/// Set of global extensions, automatically added as part of the standard set.
static ManagedStatic<SmallVector<std::pair<PassManagerBuilder::ExtensionPointTy,
   PassManagerBuilder::ExtensionFn>, 8> > GlobalExtensions;

void PassManagerBuilder::addGlobalExtension(
    PassManagerBuilder::ExtensionPointTy Ty,
    PassManagerBuilder::ExtensionFn Fn) {
  GlobalExtensions->push_back(std::make_pair(Ty, Fn));
}

void PassManagerBuilder::addExtension(ExtensionPointTy Ty, ExtensionFn Fn) {
  Extensions.push_back(std::make_pair(Ty, Fn));
}

void PassManagerBuilder::addExtensionsToPM(ExtensionPointTy ETy,
                                           PassManagerBase &PM) const {
  for (unsigned i = 0, e = GlobalExtensions->size(); i != e; ++i)
    if ((*GlobalExtensions)[i].first == ETy)
      (*GlobalExtensions)[i].second(*this, PM);
  for (unsigned i = 0, e = Extensions.size(); i != e; ++i)
    if (Extensions[i].first == ETy)
      Extensions[i].second(*this, PM);
}

void
PassManagerBuilder::addInitialAliasAnalysisPasses(PassManagerBase &PM) const {
  // Add TypeBasedAliasAnalysis before BasicAliasAnalysis so that
  // BasicAliasAnalysis wins if they disagree. This is intended to help
  // support "obvious" type-punning idioms.
  if (UseCFLAA)
    PM.add(createCFLAliasAnalysisPass());
  PM.add(createTypeBasedAliasAnalysisPass());
  PM.add(createScopedNoAliasAAPass());
  PM.add(createBasicAliasAnalysisPass());
}

void PassManagerBuilder::populateFunctionPassManager(FunctionPassManager &FPM) {
  addExtensionsToPM(EP_EarlyAsPossible, FPM);

  // Add LibraryInfo if we have some.
  if (LibraryInfo)
    FPM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  if (OptLevel == 0) return;

  addInitialAliasAnalysisPasses(FPM);

  FPM.add(createCFGSimplificationPass());
  if (UseNewSROA)
    FPM.add(createSROAPass());
  else
    FPM.add(createScalarReplAggregatesPass());
  FPM.add(createEarlyCSEPass());
  FPM.add(createLowerExpectIntrinsicPass());
}

void PassManagerBuilder::populateModulePassManager(PassManagerBase &MPM) {
  // If all optimizations are disabled, just run the always-inline pass and,
  // if enabled, the function merging pass.
  if (OptLevel == 0) {
    if (Inliner) {
      MPM.add(Inliner);
      Inliner = nullptr;
    }

    // FIXME: The BarrierNoopPass is a HACK! The inliner pass above implicitly
    // creates a CGSCC pass manager, but we don't want to add extensions into
    // that pass manager. To prevent this we insert a no-op module pass to reset
    // the pass manager to get the same behavior as EP_OptimizerLast in non-O0
    // builds. The function merging pass is 
    if (MergeFunctions)
      MPM.add(createMergeFunctionsPass());
    else if (!GlobalExtensions->empty() || !Extensions.empty())
      MPM.add(createBarrierNoopPass());

    addExtensionsToPM(EP_EnabledOnOptLevel0, MPM);
    return;
  }

  // Add LibraryInfo if we have some.
  if (LibraryInfo)
    MPM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  addInitialAliasAnalysisPasses(MPM);

  if (!DisableUnitAtATime) {
    addExtensionsToPM(EP_ModuleOptimizerEarly, MPM);

    MPM.add(createIPSCCPPass());              // IP SCCP
    MPM.add(createGlobalOptimizerPass());     // Optimize out global vars

    MPM.add(createDeadArgEliminationPass());  // Dead argument elimination

    MPM.add(createInstructionCombiningPass());// Clean up after IPCP & DAE
    addExtensionsToPM(EP_Peephole, MPM);
    MPM.add(createCFGSimplificationPass());   // Clean up after IPCP & DAE
  }

  // Start of CallGraph SCC passes.
  if (!DisableUnitAtATime)
    MPM.add(createPruneEHPass());             // Remove dead EH info
  if (Inliner) {
    MPM.add(Inliner);
    Inliner = nullptr;
  }
  if (!DisableUnitAtATime)
    MPM.add(createFunctionAttrsPass());       // Set readonly/readnone attrs
  if (OptLevel > 2)
    MPM.add(createArgumentPromotionPass());   // Scalarize uninlined fn args

  // Start of function pass.
  // Break up aggregate allocas, using SSAUpdater.
  if (UseNewSROA)
    MPM.add(createSROAPass(/*RequiresDomTree*/ false));
  else
    MPM.add(createScalarReplAggregatesPass(-1, false));
  MPM.add(createEarlyCSEPass());              // Catch trivial redundancies
  MPM.add(createJumpThreadingPass());         // Thread jumps.
  MPM.add(createCorrelatedValuePropagationPass()); // Propagate conditionals
  MPM.add(createCFGSimplificationPass());     // Merge & remove BBs
  MPM.add(createInstructionCombiningPass());  // Combine silly seq's
  addExtensionsToPM(EP_Peephole, MPM);

  if (!DisableTailCalls)
    MPM.add(createTailCallEliminationPass()); // Eliminate tail calls
  MPM.add(createCFGSimplificationPass());     // Merge & remove BBs
  MPM.add(createReassociatePass());           // Reassociate expressions
  // Rotate Loop - disable header duplication at -Oz
  MPM.add(createLoopRotatePass(SizeLevel == 2 ? 0 : -1));
  MPM.add(createLICMPass());                  // Hoist loop invariants
  MPM.add(createLoopUnswitchPass(SizeLevel || OptLevel < 3));
  MPM.add(createInstructionCombiningPass());
  MPM.add(createIndVarSimplifyPass());        // Canonicalize indvars
  MPM.add(createLoopIdiomPass());             // Recognize idioms like memset.
  MPM.add(createLoopDeletionPass());          // Delete dead loops

  if (!DisableUnrollLoops)
    MPM.add(createSimpleLoopUnrollPass());    // Unroll small loops
  addExtensionsToPM(EP_LoopOptimizerEnd, MPM);

  if (OptLevel > 1) {
    if (EnableMLSM)
      MPM.add(createMergedLoadStoreMotionPass()); // Merge ld/st in diamonds
    MPM.add(createGVNPass(DisableGVNLoadPRE));  // Remove redundancies
  }
  MPM.add(createMemCpyOptPass());             // Remove memcpy / form memset
  MPM.add(createSCCPPass());                  // Constant prop with SCCP

  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  MPM.add(createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, MPM);
  MPM.add(createJumpThreadingPass());         // Thread jumps
  MPM.add(createCorrelatedValuePropagationPass());
  MPM.add(createDeadStoreEliminationPass());  // Delete dead stores

  addExtensionsToPM(EP_ScalarOptimizerLate, MPM);

  if (RerollLoops)
    MPM.add(createLoopRerollPass());
  if (!RunSLPAfterLoopVectorization) {
    if (SLPVectorize)
      MPM.add(createSLPVectorizerPass());   // Vectorize parallel scalar chains.

    if (BBVectorize) {
      MPM.add(createBBVectorizePass());
      MPM.add(createInstructionCombiningPass());
      addExtensionsToPM(EP_Peephole, MPM);
      if (OptLevel > 1 && UseGVNAfterVectorization)
        MPM.add(createGVNPass(DisableGVNLoadPRE)); // Remove redundancies
      else
        MPM.add(createEarlyCSEPass());      // Catch trivial redundancies

      // BBVectorize may have significantly shortened a loop body; unroll again.
      if (!DisableUnrollLoops)
        MPM.add(createLoopUnrollPass());
    }
  }

  if (LoadCombine)
    MPM.add(createLoadCombinePass());

  MPM.add(createAggressiveDCEPass());         // Delete dead instructions
  MPM.add(createCFGSimplificationPass()); // Merge & remove BBs
  MPM.add(createInstructionCombiningPass());  // Clean up after everything.
  addExtensionsToPM(EP_Peephole, MPM);

  // FIXME: This is a HACK! The inliner pass above implicitly creates a CGSCC
  // pass manager that we are specifically trying to avoid. To prevent this
  // we must insert a no-op module pass to reset the pass manager.
  MPM.add(createBarrierNoopPass());

  // Re-rotate loops in all our loop nests. These may have fallout out of
  // rotated form due to GVN or other transformations, and the vectorizer relies
  // on the rotated form.
  if (ExtraVectorizerPasses)
    MPM.add(createLoopRotatePass());

  MPM.add(createLoopVectorizePass(DisableUnrollLoops, LoopVectorize));
  // FIXME: Because of #pragma vectorize enable, the passes below are always
  // inserted in the pipeline, even when the vectorizer doesn't run (ex. when
  // on -O1 and no #pragma is found). Would be good to have these two passes
  // as function calls, so that we can only pass them when the vectorizer
  // changed the code.
  MPM.add(createInstructionCombiningPass());
  if (OptLevel > 1 && ExtraVectorizerPasses) {
    // At higher optimization levels, try to clean up any runtime overlap and
    // alignment checks inserted by the vectorizer. We want to track correllated
    // runtime checks for two inner loops in the same outer loop, fold any
    // common computations, hoist loop-invariant aspects out of any outer loop,
    // and unswitch the runtime checks if possible. Once hoisted, we may have
    // dead (or speculatable) control flows or more combining opportunities.
    MPM.add(createEarlyCSEPass());
    MPM.add(createCorrelatedValuePropagationPass());
    MPM.add(createInstructionCombiningPass());
    MPM.add(createLICMPass());
    MPM.add(createLoopUnswitchPass(SizeLevel || OptLevel < 3));
    MPM.add(createCFGSimplificationPass());
    MPM.add(createInstructionCombiningPass());
  }

  if (RunSLPAfterLoopVectorization) {
    if (SLPVectorize) {
      MPM.add(createSLPVectorizerPass());   // Vectorize parallel scalar chains.
      if (OptLevel > 1 && ExtraVectorizerPasses) {
        MPM.add(createEarlyCSEPass());
      }
    }

    if (BBVectorize) {
      MPM.add(createBBVectorizePass());
      MPM.add(createInstructionCombiningPass());
      addExtensionsToPM(EP_Peephole, MPM);
      if (OptLevel > 1 && UseGVNAfterVectorization)
        MPM.add(createGVNPass(DisableGVNLoadPRE)); // Remove redundancies
      else
        MPM.add(createEarlyCSEPass());      // Catch trivial redundancies

      // BBVectorize may have significantly shortened a loop body; unroll again.
      if (!DisableUnrollLoops)
        MPM.add(createLoopUnrollPass());
    }
  }

  addExtensionsToPM(EP_Peephole, MPM);
  MPM.add(createCFGSimplificationPass());
  MPM.add(createInstructionCombiningPass());

  if (!DisableUnrollLoops)
    MPM.add(createLoopUnrollPass());    // Unroll small loops

  // After vectorization and unrolling, assume intrinsics may tell us more
  // about pointer alignments.
  MPM.add(createAlignmentFromAssumptionsPass());

  if (!DisableUnitAtATime) {
    // FIXME: We shouldn't bother with this anymore.
    MPM.add(createStripDeadPrototypesPass()); // Get rid of dead prototypes

    // GlobalOpt already deletes dead functions and globals, at -O2 try a
    // late pass of GlobalDCE.  It is capable of deleting dead cycles.
    if (OptLevel > 1) {
      MPM.add(createGlobalDCEPass());         // Remove dead fns and globals.
      MPM.add(createConstantMergePass());     // Merge dup global constants
    }
  }

  if (MergeFunctions)
    MPM.add(createMergeFunctionsPass());

  addExtensionsToPM(EP_OptimizerLast, MPM);
}

void PassManagerBuilder::addLTOOptimizationPasses(PassManagerBase &PM) {
  // Provide AliasAnalysis services for optimizations.
  addInitialAliasAnalysisPasses(PM);

  // Propagate constants at call sites into the functions they call.  This
  // opens opportunities for globalopt (and inlining) by substituting function
  // pointers passed as arguments to direct uses of functions.
  PM.add(createIPSCCPPass());

  // Now that we internalized some globals, see if we can hack on them!
  PM.add(createGlobalOptimizerPass());

  // Linking modules together can lead to duplicated global constants, only
  // keep one copy of each constant.
  PM.add(createConstantMergePass());

  // Remove unused arguments from functions.
  PM.add(createDeadArgEliminationPass());

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  PM.add(createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, PM);

  // Inline small functions
  bool RunInliner = Inliner;
  if (RunInliner) {
    PM.add(Inliner);
    Inliner = nullptr;
  }

  PM.add(createPruneEHPass());   // Remove dead EH info.

  // Optimize globals again if we ran the inliner.
  if (RunInliner)
    PM.add(createGlobalOptimizerPass());
  PM.add(createGlobalDCEPass()); // Remove dead functions.

  // If we didn't decide to inline a function, check to see if we can
  // transform it to pass arguments by value instead of by reference.
  PM.add(createArgumentPromotionPass());

  // The IPO passes may leave cruft around.  Clean up after them.
  PM.add(createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, PM);
  PM.add(createJumpThreadingPass());

  // Break up allocas
  if (UseNewSROA)
    PM.add(createSROAPass());
  else
    PM.add(createScalarReplAggregatesPass());

  // Run a few AA driven optimizations here and now, to cleanup the code.
  PM.add(createFunctionAttrsPass()); // Add nocapture.
  PM.add(createGlobalsModRefPass()); // IP alias analysis.

  PM.add(createLICMPass());                 // Hoist loop invariants.
  if (EnableMLSM)
    PM.add(createMergedLoadStoreMotionPass()); // Merge ld/st in diamonds.
  PM.add(createGVNPass(DisableGVNLoadPRE)); // Remove redundancies.
  PM.add(createMemCpyOptPass());            // Remove dead memcpys.

  // Nuke dead stores.
  PM.add(createDeadStoreEliminationPass());

  // More loops are countable; try to optimize them.
  PM.add(createIndVarSimplifyPass());
  PM.add(createLoopDeletionPass());
  PM.add(createLoopVectorizePass(true, LoopVectorize));

  // More scalar chains could be vectorized due to more alias information
  if (RunSLPAfterLoopVectorization)
    if (SLPVectorize)
      PM.add(createSLPVectorizerPass()); // Vectorize parallel scalar chains.

  // After vectorization, assume intrinsics may tell us more about pointer
  // alignments.
  PM.add(createAlignmentFromAssumptionsPass());

  if (LoadCombine)
    PM.add(createLoadCombinePass());

  // Cleanup and simplify the code after the scalar optimizations.
  PM.add(createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, PM);

  PM.add(createJumpThreadingPass());

  // Delete basic blocks, which optimization passes may have killed.
  PM.add(createCFGSimplificationPass());

  // Now that we have optimized the program, discard unreachable functions.
  PM.add(createGlobalDCEPass());

  // FIXME: this is profitable (for compiler time) to do at -O0 too, but
  // currently it damages debug info.
  if (MergeFunctions)
    PM.add(createMergeFunctionsPass());
}

void PassManagerBuilder::populateLTOPassManager(PassManagerBase &PM) {
  if (LibraryInfo)
    PM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  if (VerifyInput)
    PM.add(createVerifierPass());

  if (StripDebug)
    PM.add(createStripSymbolsPass(true));

  if (VerifyInput)
    PM.add(createDebugInfoVerifierPass());

  if (OptLevel != 0)
    addLTOOptimizationPasses(PM);

  if (VerifyOutput) {
    PM.add(createVerifierPass());
    PM.add(createDebugInfoVerifierPass());
  }
}

inline PassManagerBuilder *unwrap(LLVMPassManagerBuilderRef P) {
    return reinterpret_cast<PassManagerBuilder*>(P);
}

inline LLVMPassManagerBuilderRef wrap(PassManagerBuilder *P) {
  return reinterpret_cast<LLVMPassManagerBuilderRef>(P);
}

LLVMPassManagerBuilderRef LLVMPassManagerBuilderCreate() {
  PassManagerBuilder *PMB = new PassManagerBuilder();
  return wrap(PMB);
}

void LLVMPassManagerBuilderDispose(LLVMPassManagerBuilderRef PMB) {
  PassManagerBuilder *Builder = unwrap(PMB);
  delete Builder;
}

void
LLVMPassManagerBuilderSetOptLevel(LLVMPassManagerBuilderRef PMB,
                                  unsigned OptLevel) {
  PassManagerBuilder *Builder = unwrap(PMB);
  Builder->OptLevel = OptLevel;
}

void
LLVMPassManagerBuilderSetSizeLevel(LLVMPassManagerBuilderRef PMB,
                                   unsigned SizeLevel) {
  PassManagerBuilder *Builder = unwrap(PMB);
  Builder->SizeLevel = SizeLevel;
}

void
LLVMPassManagerBuilderSetDisableUnitAtATime(LLVMPassManagerBuilderRef PMB,
                                            LLVMBool Value) {
  PassManagerBuilder *Builder = unwrap(PMB);
  Builder->DisableUnitAtATime = Value;
}

void
LLVMPassManagerBuilderSetDisableUnrollLoops(LLVMPassManagerBuilderRef PMB,
                                            LLVMBool Value) {
  PassManagerBuilder *Builder = unwrap(PMB);
  Builder->DisableUnrollLoops = Value;
}

void
LLVMPassManagerBuilderSetDisableSimplifyLibCalls(LLVMPassManagerBuilderRef PMB,
                                                 LLVMBool Value) {
  // NOTE: The simplify-libcalls pass has been removed.
}

void
LLVMPassManagerBuilderUseInlinerWithThreshold(LLVMPassManagerBuilderRef PMB,
                                              unsigned Threshold) {
  PassManagerBuilder *Builder = unwrap(PMB);
  Builder->Inliner = createFunctionInliningPass(Threshold);
}

void
LLVMPassManagerBuilderPopulateFunctionPassManager(LLVMPassManagerBuilderRef PMB,
                                                  LLVMPassManagerRef PM) {
  PassManagerBuilder *Builder = unwrap(PMB);
  FunctionPassManager *FPM = unwrap<FunctionPassManager>(PM);
  Builder->populateFunctionPassManager(*FPM);
}

void
LLVMPassManagerBuilderPopulateModulePassManager(LLVMPassManagerBuilderRef PMB,
                                                LLVMPassManagerRef PM) {
  PassManagerBuilder *Builder = unwrap(PMB);
  PassManagerBase *MPM = unwrap(PM);
  Builder->populateModulePassManager(*MPM);
}

void LLVMPassManagerBuilderPopulateLTOPassManager(LLVMPassManagerBuilderRef PMB,
                                                  LLVMPassManagerRef PM,
                                                  LLVMBool Internalize,
                                                  LLVMBool RunInliner) {
  PassManagerBuilder *Builder = unwrap(PMB);
  PassManagerBase *LPM = unwrap(PM);

  // A small backwards compatibility hack. populateLTOPassManager used to take
  // an RunInliner option.
  if (RunInliner && !Builder->Inliner)
    Builder->Inliner = createFunctionInliningPass();

  Builder->populateLTOPassManager(*LPM);
}
