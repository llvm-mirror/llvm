#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"

using namespace llvm;

#define DEBUG_TYPE "offload"

namespace {

static int getDedicatedUnroll(MDNode *Loop, StringRef Str) {
  for (size_t i = 0; i < Loop->getNumOperands(); ++i) {
    auto Node = dyn_cast<MDNode>(Loop->getOperand(i));
    assert(Node);
    auto Str = dyn_cast<MDString>(Node->getOperand(0));
    if (Str && Str->getString() == "llvm.loop.ss.dedicated") {
      llvm::errs() << Node->getNumOperands() << "\n";
      assert(Node->getNumOperands() == 2);
      Node->getOperand(1)->dump();
      auto Factor = dyn_cast<ConstantAsMetadata>(Node->getOperand(1));
      assert(Factor);
      return (int) Factor->getValue()->getUniqueInteger().getSExtValue();
    }
  }
  return -1;
}

static int isMemoryOp(unsigned int Op) {
  return Op >= Instruction::MemoryOpsBegin && Op < Instruction::MemoryOpsEnd;
}

}

namespace {
  struct OffloadDFG : public FunctionPass {
    static char ID;
    OffloadDFG() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      LI->begin();
      llvm::errs() << "# " << F.getName() << "\n";

      for (auto Loop : *LI) {
        llvm::errs() << "# " << Loop->getName() << "\n";
        auto ID = Loop->getLoopID();
        auto Factor = getDedicatedUnroll(ID, "llvm.loop.ss.dedicated");
        if (Factor != -1) {
          assert(Loop->getNumBlocks() == 1 && "So far, only perfect loop supported");
          auto Block = Loop->getBlocks()[0];

          for (auto &Inst : *Block) {
            if (Inst.getOpcode() == Instruction::Store) {
            }
            if (!isMemoryOp(Inst.getOpcode())) {
              Inst.dump();
              for (auto User : Inst.users()) {
                User->dump();
              }
              llvm::errs() << "\n";
            }
          }
        }
      }
      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<BlockFrequencyInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<TargetTransformInfoWrapperPass>();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<LoopAccessLegacyAnalysis>();
      AU.addRequired<DemandedBitsWrapperPass>();
      AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addPreserved<BasicAAWrapperPass>();
      AU.addPreserved<GlobalsAAWrapperPass>();
    }

  };
}

char OffloadDFG::ID = 0;
static RegisterPass<OffloadDFG> X("offload-dfg", "Offload Dataflow Graph Pass", false, true);

