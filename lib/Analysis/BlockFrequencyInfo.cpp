//===- BlockFrequencyInfo.cpp - Block Frequency Analysis ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/CFG.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

#define DEBUG_TYPE "block-freq"

#ifndef NDEBUG
enum GVDAGType {
  GVDT_None,
  GVDT_Fraction,
  GVDT_Integer
};

static cl::opt<GVDAGType>
ViewBlockFreqPropagationDAG("view-block-freq-propagation-dags", cl::Hidden,
          cl::desc("Pop up a window to show a dag displaying how block "
                   "frequencies propagation through the CFG."),
          cl::values(
            clEnumValN(GVDT_None, "none",
                       "do not display graphs."),
            clEnumValN(GVDT_Fraction, "fraction", "display a graph using the "
                       "fractional block frequency representation."),
            clEnumValN(GVDT_Integer, "integer", "display a graph using the raw "
                       "integer fractional block frequency representation."),
            clEnumValEnd));

namespace llvm {

template <>
struct GraphTraits<BlockFrequencyInfo *> {
  typedef const BasicBlock NodeType;
  typedef succ_const_iterator ChildIteratorType;
  typedef Function::const_iterator nodes_iterator;

  static inline const NodeType *getEntryNode(const BlockFrequencyInfo *G) {
    return &G->getFunction()->front();
  }
  static ChildIteratorType child_begin(const NodeType *N) {
    return succ_begin(N);
  }
  static ChildIteratorType child_end(const NodeType *N) {
    return succ_end(N);
  }
  static nodes_iterator nodes_begin(const BlockFrequencyInfo *G) {
    return G->getFunction()->begin();
  }
  static nodes_iterator nodes_end(const BlockFrequencyInfo *G) {
    return G->getFunction()->end();
  }
};

template<>
struct DOTGraphTraits<BlockFrequencyInfo*> : public DefaultDOTGraphTraits {
  explicit DOTGraphTraits(bool isSimple=false) :
    DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const BlockFrequencyInfo *G) {
    return G->getFunction()->getName();
  }

  std::string getNodeLabel(const BasicBlock *Node,
                           const BlockFrequencyInfo *Graph) {
    std::string Result;
    raw_string_ostream OS(Result);

    OS << Node->getName() << ":";
    switch (ViewBlockFreqPropagationDAG) {
    case GVDT_Fraction:
      Graph->printBlockFreq(OS, Node);
      break;
    case GVDT_Integer:
      OS << Graph->getBlockFreq(Node).getFrequency();
      break;
    case GVDT_None:
      llvm_unreachable("If we are not supposed to render a graph we should "
                       "never reach this point.");
    }

    return Result;
  }
};

} // end namespace llvm
#endif

BlockFrequencyInfo::BlockFrequencyInfo() {}

BlockFrequencyInfo::BlockFrequencyInfo(const Function &F,
                                       const BranchProbabilityInfo &BPI,
                                       const LoopInfo &LI) {
  calculate(F, BPI, LI);
}

void BlockFrequencyInfo::calculate(const Function &F,
                                   const BranchProbabilityInfo &BPI,
                                   const LoopInfo &LI) {
  if (!BFI)
    BFI.reset(new ImplType);
  BFI->calculate(F, BPI, LI);
#ifndef NDEBUG
  if (ViewBlockFreqPropagationDAG != GVDT_None)
    view();
#endif
}

BlockFrequency BlockFrequencyInfo::getBlockFreq(const BasicBlock *BB) const {
  return BFI ? BFI->getBlockFreq(BB) : 0;
}

void BlockFrequencyInfo::setBlockFreq(const BasicBlock *BB,
                                      uint64_t Freq) {
  assert(BFI && "Expected analysis to be available");
  BFI->setBlockFreq(BB, Freq);
}

/// Pop up a ghostview window with the current block frequency propagation
/// rendered using dot.
void BlockFrequencyInfo::view() const {
// This code is only for debugging.
#ifndef NDEBUG
  ViewGraph(const_cast<BlockFrequencyInfo *>(this), "BlockFrequencyDAGs");
#else
  errs() << "BlockFrequencyInfo::view is only available in debug builds on "
            "systems with Graphviz or gv!\n";
#endif // NDEBUG
}

const Function *BlockFrequencyInfo::getFunction() const {
  return BFI ? BFI->getFunction() : nullptr;
}

raw_ostream &BlockFrequencyInfo::
printBlockFreq(raw_ostream &OS, const BlockFrequency Freq) const {
  return BFI ? BFI->printBlockFreq(OS, Freq) : OS;
}

raw_ostream &
BlockFrequencyInfo::printBlockFreq(raw_ostream &OS,
                                   const BasicBlock *BB) const {
  return BFI ? BFI->printBlockFreq(OS, BB) : OS;
}

uint64_t BlockFrequencyInfo::getEntryFreq() const {
  return BFI ? BFI->getEntryFreq() : 0;
}

void BlockFrequencyInfo::releaseMemory() { BFI.reset(); }

void BlockFrequencyInfo::print(raw_ostream &OS) const {
  if (BFI)
    BFI->print(OS);
}


INITIALIZE_PASS_BEGIN(BlockFrequencyInfoWrapperPass, "block-freq",
                      "Block Frequency Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(BlockFrequencyInfoWrapperPass, "block-freq",
                    "Block Frequency Analysis", true, true)

char BlockFrequencyInfoWrapperPass::ID = 0;


BlockFrequencyInfoWrapperPass::BlockFrequencyInfoWrapperPass()
    : FunctionPass(ID) {
  initializeBlockFrequencyInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

BlockFrequencyInfoWrapperPass::~BlockFrequencyInfoWrapperPass() {}

void BlockFrequencyInfoWrapperPass::print(raw_ostream &OS,
                                          const Module *) const {
  BFI.print(OS);
}

void BlockFrequencyInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BranchProbabilityInfoWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

void BlockFrequencyInfoWrapperPass::releaseMemory() { BFI.releaseMemory(); }

bool BlockFrequencyInfoWrapperPass::runOnFunction(Function &F) {
  BranchProbabilityInfo &BPI =
      getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  BFI.calculate(F, BPI, LI);
  return false;
}
