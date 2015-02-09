//===- CallGraph.cpp - Build a Module's call graph ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
// Implementations of the CallGraph class methods.
//

CallGraph::CallGraph(Module &M)
    : M(M), Root(nullptr), ExternalCallingNode(getOrInsertFunction(nullptr)),
      CallsExternalNode(new CallGraphNode(nullptr)) {
  // Add every function to the call graph.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    addToCallGraph(I);

  // If we didn't find a main function, use the external call graph node
  if (!Root)
    Root = ExternalCallingNode;
}

CallGraph::~CallGraph() {
  // CallsExternalNode is not in the function map, delete it explicitly.
  CallsExternalNode->allReferencesDropped();
  delete CallsExternalNode;

// Reset all node's use counts to zero before deleting them to prevent an
// assertion from firing.
#ifndef NDEBUG
  for (FunctionMapTy::iterator I = FunctionMap.begin(), E = FunctionMap.end();
       I != E; ++I)
    I->second->allReferencesDropped();
#endif
  for (FunctionMapTy::iterator I = FunctionMap.begin(), E = FunctionMap.end();
       I != E; ++I)
    delete I->second;
}

void CallGraph::addToCallGraph(Function *F) {
  CallGraphNode *Node = getOrInsertFunction(F);

  // If this function has external linkage, anything could call it.
  if (!F->hasLocalLinkage()) {
    ExternalCallingNode->addCalledFunction(CallSite(), Node);

    // Found the entry point?
    if (F->getName() == "main") {
      if (Root) // Found multiple external mains?  Don't pick one.
        Root = ExternalCallingNode;
      else
        Root = Node; // Found a main, keep track of it!
    }
  }

  // If this function has its address taken, anything could call it.
  if (F->hasAddressTaken())
    ExternalCallingNode->addCalledFunction(CallSite(), Node);

  // If this function is not defined in this translation unit, it could call
  // anything.
  if (F->isDeclaration() && !F->isIntrinsic())
    Node->addCalledFunction(CallSite(), CallsExternalNode);

  // Look for calls by this function.
  for (Function::iterator BB = F->begin(), BBE = F->end(); BB != BBE; ++BB)
    for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;
         ++II) {
      CallSite CS(cast<Value>(II));
      if (CS) {
        const Function *Callee = CS.getCalledFunction();
        if (!Callee)
          // Indirect calls of intrinsics are not allowed so no need to check.
          Node->addCalledFunction(CS, CallsExternalNode);
        else if (!Callee->isIntrinsic())
          Node->addCalledFunction(CS, getOrInsertFunction(Callee));
      }
    }
}

void CallGraph::print(raw_ostream &OS) const {
  OS << "CallGraph Root is: ";
  if (Function *F = Root->getFunction())
    OS << F->getName() << "\n";
  else {
    OS << "<<null function: 0x" << Root << ">>\n";
  }

  for (CallGraph::const_iterator I = begin(), E = end(); I != E; ++I)
    I->second->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void CallGraph::dump() const { print(dbgs()); }
#endif

// removeFunctionFromModule - Unlink the function from this module, returning
// it.  Because this removes the function from the module, the call graph node
// is destroyed.  This is only valid if the function does not call any other
// functions (ie, there are no edges in it's CGN).  The easiest way to do this
// is to dropAllReferences before calling this.
//
Function *CallGraph::removeFunctionFromModule(CallGraphNode *CGN) {
  assert(CGN->empty() && "Cannot remove function from call "
         "graph if it references other functions!");
  Function *F = CGN->getFunction(); // Get the function for the call graph node
  delete CGN;                       // Delete the call graph node for this func
  FunctionMap.erase(F);             // Remove the call graph node from the map

  M.getFunctionList().remove(F);
  return F;
}

/// spliceFunction - Replace the function represented by this node by another.
/// This does not rescan the body of the function, so it is suitable when
/// splicing the body of the old function to the new while also updating all
/// callers from old to new.
///
void CallGraph::spliceFunction(const Function *From, const Function *To) {
  assert(FunctionMap.count(From) && "No CallGraphNode for function!");
  assert(!FunctionMap.count(To) &&
         "Pointing CallGraphNode at a function that already exists");
  FunctionMapTy::iterator I = FunctionMap.find(From);
  I->second->F = const_cast<Function*>(To);
  FunctionMap[To] = I->second;
  FunctionMap.erase(I);
}

// getOrInsertFunction - This method is identical to calling operator[], but
// it will insert a new CallGraphNode for the specified function if one does
// not already exist.
CallGraphNode *CallGraph::getOrInsertFunction(const Function *F) {
  CallGraphNode *&CGN = FunctionMap[F];
  if (CGN)
    return CGN;

  assert((!F || F->getParent() == &M) && "Function not in current module!");
  return CGN = new CallGraphNode(const_cast<Function*>(F));
}

//===----------------------------------------------------------------------===//
// Implementations of the CallGraphNode class methods.
//

void CallGraphNode::print(raw_ostream &OS) const {
  if (Function *F = getFunction())
    OS << "Call graph node for function: '" << F->getName() << "'";
  else
    OS << "Call graph node <<null function>>";
  
  OS << "<<" << this << ">>  #uses=" << getNumReferences() << '\n';

  for (const_iterator I = begin(), E = end(); I != E; ++I) {
    OS << "  CS<" << I->first << "> calls ";
    if (Function *FI = I->second->getFunction())
      OS << "function '" << FI->getName() <<"'\n";
    else
      OS << "external node\n";
  }
  OS << '\n';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void CallGraphNode::dump() const { print(dbgs()); }
#endif

/// removeCallEdgeFor - This method removes the edge in the node for the
/// specified call site.  Note that this method takes linear time, so it
/// should be used sparingly.
void CallGraphNode::removeCallEdgeFor(CallSite CS) {
  for (CalledFunctionsVector::iterator I = CalledFunctions.begin(); ; ++I) {
    assert(I != CalledFunctions.end() && "Cannot find callsite to remove!");
    if (I->first == CS.getInstruction()) {
      I->second->DropRef();
      *I = CalledFunctions.back();
      CalledFunctions.pop_back();
      return;
    }
  }
}

// removeAnyCallEdgeTo - This method removes any call edges from this node to
// the specified callee function.  This takes more time to execute than
// removeCallEdgeTo, so it should not be used unless necessary.
void CallGraphNode::removeAnyCallEdgeTo(CallGraphNode *Callee) {
  for (unsigned i = 0, e = CalledFunctions.size(); i != e; ++i)
    if (CalledFunctions[i].second == Callee) {
      Callee->DropRef();
      CalledFunctions[i] = CalledFunctions.back();
      CalledFunctions.pop_back();
      --i; --e;
    }
}

/// removeOneAbstractEdgeTo - Remove one edge associated with a null callsite
/// from this node to the specified callee function.
void CallGraphNode::removeOneAbstractEdgeTo(CallGraphNode *Callee) {
  for (CalledFunctionsVector::iterator I = CalledFunctions.begin(); ; ++I) {
    assert(I != CalledFunctions.end() && "Cannot find callee to remove!");
    CallRecord &CR = *I;
    if (CR.second == Callee && CR.first == nullptr) {
      Callee->DropRef();
      *I = CalledFunctions.back();
      CalledFunctions.pop_back();
      return;
    }
  }
}

/// replaceCallEdge - This method replaces the edge in the node for the
/// specified call site with a new one.  Note that this method takes linear
/// time, so it should be used sparingly.
void CallGraphNode::replaceCallEdge(CallSite CS,
                                    CallSite NewCS, CallGraphNode *NewNode){
  for (CalledFunctionsVector::iterator I = CalledFunctions.begin(); ; ++I) {
    assert(I != CalledFunctions.end() && "Cannot find callsite to remove!");
    if (I->first == CS.getInstruction()) {
      I->second->DropRef();
      I->first = NewCS.getInstruction();
      I->second = NewNode;
      NewNode->AddRef();
      return;
    }
  }
}

//===----------------------------------------------------------------------===//
// Out-of-line definitions of CallGraphAnalysis class members.
//

char CallGraphAnalysis::PassID;

//===----------------------------------------------------------------------===//
// Implementations of the CallGraphWrapperPass class methods.
//

CallGraphWrapperPass::CallGraphWrapperPass() : ModulePass(ID) {
  initializeCallGraphWrapperPassPass(*PassRegistry::getPassRegistry());
}

CallGraphWrapperPass::~CallGraphWrapperPass() {}

void CallGraphWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

bool CallGraphWrapperPass::runOnModule(Module &M) {
  // All the real work is done in the constructor for the CallGraph.
  G.reset(new CallGraph(M));
  return false;
}

INITIALIZE_PASS(CallGraphWrapperPass, "basiccg", "CallGraph Construction",
                false, true)

char CallGraphWrapperPass::ID = 0;

void CallGraphWrapperPass::releaseMemory() { G.reset(); }

void CallGraphWrapperPass::print(raw_ostream &OS, const Module *) const {
  if (!G) {
    OS << "No call graph has been built!\n";
    return;
  }

  // Just delegate.
  G->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void CallGraphWrapperPass::dump() const { print(dbgs(), nullptr); }
#endif
