//===-- CFGMST.h - Minimum Spanning Tree for CFG ----------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a Union-find algorithm to compute Minimum Spanning Tree
// for a given CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <string>
#include <utility>
#include <vector>

namespace llvm {

#define DEBUG_TYPE "cfgmst"

/// \brief An union-find based Minimum Spanning Tree for CFG
///
/// Implements a Union-find algorithm to compute Minimum Spanning Tree
/// for a given CFG.
template <class Edge, class BBInfo> class CFGMST {
public:
  Function &F;

  // Store all the edges in CFG. It may contain some stale edges
  // when Removed is set.
  std::vector<std::unique_ptr<Edge>> AllEdges;

  // This map records the auxiliary information for each BB.
  DenseMap<const BasicBlock *, std::unique_ptr<BBInfo>> BBInfos;

  // Find the root group of the G and compress the path from G to the root.
  BBInfo *findAndCompressGroup(BBInfo *G) {
    if (G->Group != G)
      G->Group = findAndCompressGroup(static_cast<BBInfo *>(G->Group));
    return static_cast<BBInfo *>(G->Group);
  }

  // Union BB1 and BB2 into the same group and return true.
  // Returns false if BB1 and BB2 are already in the same group.
  bool unionGroups(const BasicBlock *BB1, const BasicBlock *BB2) {
    BBInfo *BB1G = findAndCompressGroup(&getBBInfo(BB1));
    BBInfo *BB2G = findAndCompressGroup(&getBBInfo(BB2));

    if (BB1G == BB2G)
      return false;

    // Make the smaller rank tree a direct child or the root of high rank tree.
    if (BB1G->Rank < BB2G->Rank)
      BB1G->Group = BB2G;
    else {
      BB2G->Group = BB1G;
      // If the ranks are the same, increment root of one tree by one.
      if (BB1G->Rank == BB2G->Rank)
        BB1G->Rank++;
    }
    return true;
  }

  // Give BB, return the auxiliary information.
  BBInfo &getBBInfo(const BasicBlock *BB) const {
    auto It = BBInfos.find(BB);
    assert(It->second.get() != nullptr);
    return *It->second.get();
  }

  // Traverse the CFG using a stack. Find all the edges and assign the weight.
  // Edges with large weight will be put into MST first so they are less likely
  // to be instrumented.
  void buildEdges() {
    DEBUG(dbgs() << "Build Edge on " << F.getName() << "\n");

    const BasicBlock *BB = &(F.getEntryBlock());
    uint64_t EntryWeight = (BFI != nullptr ? BFI->getEntryFreq() : 2);
    // Add a fake edge to the entry.
    addEdge(nullptr, BB, EntryWeight);

    // Special handling for single BB functions.
    if (succ_empty(BB)) {
      addEdge(BB, nullptr, EntryWeight);
      return;
    }

    static const uint32_t CriticalEdgeMultiplier = 1000;

    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
      TerminatorInst *TI = BB->getTerminator();
      uint64_t BBWeight =
          (BFI != nullptr ? BFI->getBlockFreq(&*BB).getFrequency() : 2);
      uint64_t Weight = 2;
      if (int successors = TI->getNumSuccessors()) {
        for (int i = 0; i != successors; ++i) {
          BasicBlock *TargetBB = TI->getSuccessor(i);
          bool Critical = isCriticalEdge(TI, i);
          uint64_t scaleFactor = BBWeight;
          if (Critical) {
            if (scaleFactor < UINT64_MAX / CriticalEdgeMultiplier)
              scaleFactor *= CriticalEdgeMultiplier;
            else
              scaleFactor = UINT64_MAX;
          }
          if (BPI != nullptr)
            Weight = BPI->getEdgeProbability(&*BB, TargetBB).scale(scaleFactor);
          addEdge(&*BB, TargetBB, Weight).IsCritical = Critical;
          DEBUG(dbgs() << "  Edge: from " << BB->getName() << " to "
                       << TargetBB->getName() << "  w=" << Weight << "\n");
        }
      } else {
        addEdge(&*BB, nullptr, BBWeight);
        DEBUG(dbgs() << "  Edge: from " << BB->getName() << " to exit"
                     << " w = " << BBWeight << "\n");
      }
    }
  }

  // Sort CFG edges based on its weight.
  void sortEdgesByWeight() {
    std::stable_sort(AllEdges.begin(), AllEdges.end(),
                     [](const std::unique_ptr<Edge> &Edge1,
                        const std::unique_ptr<Edge> &Edge2) {
                       return Edge1->Weight > Edge2->Weight;
                     });
  }

  // Traverse all the edges and compute the Minimum Weight Spanning Tree
  // using union-find algorithm.
  void computeMinimumSpanningTree() {
    // First, put all the critical edge with landing-pad as the Dest to MST.
    // This works around the insufficient support of critical edges split
    // when destination BB is a landing pad.
    for (auto &Ei : AllEdges) {
      if (Ei->Removed)
        continue;
      if (Ei->IsCritical) {
        if (Ei->DestBB && Ei->DestBB->isLandingPad()) {
          if (unionGroups(Ei->SrcBB, Ei->DestBB))
            Ei->InMST = true;
        }
      }
    }

    for (auto &Ei : AllEdges) {
      if (Ei->Removed)
        continue;
      if (unionGroups(Ei->SrcBB, Ei->DestBB))
        Ei->InMST = true;
    }
  }

  // Dump the Debug information about the instrumentation.
  void dumpEdges(raw_ostream &OS, const Twine &Message) const {
    if (!Message.str().empty())
      OS << Message << "\n";
    OS << "  Number of Basic Blocks: " << BBInfos.size() << "\n";
    for (auto &BI : BBInfos) {
      const BasicBlock *BB = BI.first;
      OS << "  BB: " << (BB == nullptr ? "FakeNode" : BB->getName()) << "  "
         << BI.second->infoString() << "\n";
    }

    OS << "  Number of Edges: " << AllEdges.size()
       << " (*: Instrument, C: CriticalEdge, -: Removed)\n";
    uint32_t Count = 0;
    for (auto &EI : AllEdges)
      OS << "  Edge " << Count++ << ": " << getBBInfo(EI->SrcBB).Index << "-->"
         << getBBInfo(EI->DestBB).Index << EI->infoString() << "\n";
  }

  // Add an edge to AllEdges with weight W.
  Edge &addEdge(const BasicBlock *Src, const BasicBlock *Dest, uint64_t W) {
    uint32_t Index = BBInfos.size();
    auto Iter = BBInfos.end();
    bool Inserted;
    std::tie(Iter, Inserted) = BBInfos.insert(std::make_pair(Src, nullptr));
    if (Inserted) {
      // Newly inserted, update the real info.
      Iter->second = std::move(llvm::make_unique<BBInfo>(Index));
      Index++;
    }
    std::tie(Iter, Inserted) = BBInfos.insert(std::make_pair(Dest, nullptr));
    if (Inserted)
      // Newly inserted, update the real info.
      Iter->second = std::move(llvm::make_unique<BBInfo>(Index));
    AllEdges.emplace_back(new Edge(Src, Dest, W));
    return *AllEdges.back();
  }

  BranchProbabilityInfo *BPI;
  BlockFrequencyInfo *BFI;

public:
  CFGMST(Function &Func, BranchProbabilityInfo *BPI_ = nullptr,
         BlockFrequencyInfo *BFI_ = nullptr)
      : F(Func), BPI(BPI_), BFI(BFI_) {
    buildEdges();
    sortEdgesByWeight();
    computeMinimumSpanningTree();
  }
};

#undef DEBUG_TYPE // "cfgmst"
} // end namespace llvm
