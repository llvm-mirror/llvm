//===-- EquivClassGraphs.h - Merge equiv-class graphs -----------*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass is the same as the complete bottom-up graphs, but with functions
// partitioned into equivalence classes and a single merged DS graph for all
// functions in an equivalence class.  After this merging, graphs are inlined
// bottom-up on the SCCs of the final (CBU) call graph.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DataStructure/DataStructure.h"
#include "llvm/Analysis/DataStructure/DSGraph.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/STLExtras.h"
#include <vector>
#include <map>
#include <ext/hash_map>

namespace llvm {

class Module;
class Function;

namespace PA {

  /// EquivClassGraphArgsInfo - Information about the set of argument nodes 
  /// in a DS graph (the number of argument nodes is the max of argument nodes
  /// for all functions folded into the graph).
  /// FIXME: This class is only used temporarily and could be eliminated.
  ///  
  struct EquivClassGraphArgsInfo {
    const DSGraph* ECGraph;
    std::vector<DSNodeHandle> argNodes;
    EquivClassGraphArgsInfo() : ECGraph(NULL) {}
  };

  /// EquivClassGraphs - This is the same as the complete bottom-up graphs, but
  /// with functions partitioned into equivalence classes and a single merged
  /// DS graph for all functions in an equivalence class.  After this merging,
  /// graphs are inlined bottom-up on the SCCs of the final (CBU) call graph.
  ///
  struct EquivClassGraphs : public ModulePass {
    CompleteBUDataStructures *CBU;

    DSGraph *GlobalsGraph;

    // FoldedGraphsMap, one graph for each function
    hash_map<const Function*, DSGraph*> FoldedGraphsMap;

    /// ActualCallees - The actual functions callable from indirect call sites.
    ///
    hash_multimap<Instruction*, Function*> ActualCallees;
  
    // Equivalence class where functions that can potentially be called via the
    // same function pointer are in the same class.
    EquivalenceClasses<Function*> FuncECs;

    // Each equivalence class graph contains several functions.
    // Remember their argument nodes (and return nodes?)
    std::map<const DSGraph*, EquivClassGraphArgsInfo> ECGraphInfo;
    
    /// OneCalledFunction - For each indirect call, we keep track of one
    /// target of the call.  This is used to find equivalence class called by
    /// a call site.
    std::map<DSNode*, Function *> OneCalledFunction;

  public:
    /// EquivClassGraphs - Computes the equivalence classes and then the
    /// folded DS graphs for each class.
    /// 
    virtual bool runOnModule(Module &M);

    /// getCBUDataStructures - Get the CompleteBUDataStructures object
    /// 
    CompleteBUDataStructures *getCBUDataStructures() { return CBU; }

    /// getDSGraph - Return the data structure graph for the specified function.
    /// This returns the folded graph.  The folded graph is the same as the CBU
    /// graph iff the function is in a singleton equivalence class AND all its 
    /// callees also have the same folded graph as the CBU graph.
    /// 
    DSGraph &getDSGraph(const Function &F) const {
      hash_map<const Function*, DSGraph*>::const_iterator I =
        FoldedGraphsMap.find(const_cast<Function*>(&F));
      assert(I != FoldedGraphsMap.end() && "No folded graph for function!");
      return *I->second;
    }

    /// getSomeCalleeForCallSite - Return any one callee function at
    /// a call site.
    /// 
    Function *getSomeCalleeForCallSite(const CallSite &CS) const;

    /// getDSGraphForCallSite - Return the common data structure graph for
    /// callees at the specified call site.
    /// 
    DSGraph &getDSGraphForCallSite(const CallSite &CS) const {
      return this->getDSGraph(*getSomeCalleeForCallSite(CS));
    }

    /// getEquivClassForCallSite - Get the set of functions in the equivalence
    /// class for a given call site.
    /// 
    const std::set<Function*>& getEquivClassForCallSite(const CallSite& CS) {
      Function* leaderF = FuncECs.findClass(getSomeCalleeForCallSite(CS));
      return FuncECs.getEqClass(leaderF);
    }

    /// getECGraphInfo - Get the graph info object with arg nodes info
    /// 
    EquivClassGraphArgsInfo &getECGraphInfo(const DSGraph* G) {
      assert(G != NULL && "getECGraphInfo: Null graph!");
      EquivClassGraphArgsInfo& GraphInfo = ECGraphInfo[G];
      if (GraphInfo.ECGraph == NULL)
        GraphInfo.ECGraph = G;
      return GraphInfo;
    }

    DSGraph &getGlobalsGraph() const {
      return *GlobalsGraph;
    }
    
    typedef hash_multimap<Instruction*, Function*> ActualCalleesTy;
    const ActualCalleesTy &getActualCallees() const {
      return ActualCallees;
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      AU.addRequired<CompleteBUDataStructures>();
    }

    /// print - Print out the analysis results...
    ///
    void print(std::ostream &O, const Module *M) const {}

  private:
    void buildIndirectFunctionSets(Module &M);

    unsigned processSCC(DSGraph &FG, Function &F, std::vector<Function*> &Stack,
                        unsigned &NextID, 
                        std::map<Function*, unsigned> &ValMap);

    void processGraph(DSGraph &FG, Function &F);

    DSGraph &getOrCreateGraph(Function &F);

    DSGraph* cloneGraph(Function &F);

    bool hasFoldedGraph(const Function& F) const {
      hash_map<const Function*, DSGraph*>::const_iterator I =
        FoldedGraphsMap.find(const_cast<Function*>(&F));
      return (I != FoldedGraphsMap.end());
    }

    DSGraph* getOrCreateLeaderGraph(const Function& leader) {
      DSGraph*& leaderGraph = FoldedGraphsMap[&leader];
      if (leaderGraph == NULL)
        leaderGraph = new DSGraph(CBU->getGlobalsGraph().getTargetData());
      return leaderGraph;
    }
  };

}; // end PA namespace

}; // end llvm namespace
