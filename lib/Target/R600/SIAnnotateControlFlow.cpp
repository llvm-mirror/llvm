//===-- SIAnnotateControlFlow.cpp -  ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Annotates the control flow with hardware specific intrinsics.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

using namespace llvm;

namespace {

// Complex types used in this pass
typedef std::pair<BasicBlock *, Value *> StackEntry;
typedef SmallVector<StackEntry, 16> StackVector;

// Intrinsic names the control flow is annotated with
static const char *IfIntrinsic = "llvm.SI.if";
static const char *ElseIntrinsic = "llvm.SI.else";
static const char *BreakIntrinsic = "llvm.SI.break";
static const char *IfBreakIntrinsic = "llvm.SI.if.break";
static const char *ElseBreakIntrinsic = "llvm.SI.else.break";
static const char *LoopIntrinsic = "llvm.SI.loop";
static const char *EndCfIntrinsic = "llvm.SI.end.cf";

class SIAnnotateControlFlow : public FunctionPass {

  static char ID;

  Type *Boolean;
  Type *Void;
  Type *Int64;
  Type *ReturnStruct;

  ConstantInt *BoolTrue;
  ConstantInt *BoolFalse;
  UndefValue *BoolUndef;
  Constant *Int64Zero;

  Constant *If;
  Constant *Else;
  Constant *Break;
  Constant *IfBreak;
  Constant *ElseBreak;
  Constant *Loop;
  Constant *EndCf;

  DominatorTree *DT;
  StackVector Stack;
  SSAUpdater PhiInserter;

  bool isTopOfStack(BasicBlock *BB);

  Value *popSaved();

  void push(BasicBlock *BB, Value *Saved);

  bool isElse(PHINode *Phi);

  void eraseIfUnused(PHINode *Phi);

  void openIf(BranchInst *Term);

  void insertElse(BranchInst *Term);

  void handleLoopCondition(Value *Cond);

  void handleLoop(BranchInst *Term);

  void closeControlFlow(BasicBlock *BB);

public:
  SIAnnotateControlFlow():
    FunctionPass(ID) { }

  virtual bool doInitialization(Module &M);

  virtual bool runOnFunction(Function &F);

  virtual const char *getPassName() const {
    return "SI annotate control flow";
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<DominatorTree>();
    AU.addPreserved<DominatorTree>();
    FunctionPass::getAnalysisUsage(AU);
  }

};

} // end anonymous namespace

char SIAnnotateControlFlow::ID = 0;

/// \brief Initialize all the types and constants used in the pass
bool SIAnnotateControlFlow::doInitialization(Module &M) {
  LLVMContext &Context = M.getContext();

  Void = Type::getVoidTy(Context);
  Boolean = Type::getInt1Ty(Context);
  Int64 = Type::getInt64Ty(Context);
  ReturnStruct = StructType::get(Boolean, Int64, (Type *)0);

  BoolTrue = ConstantInt::getTrue(Context);
  BoolFalse = ConstantInt::getFalse(Context);
  BoolUndef = UndefValue::get(Boolean);
  Int64Zero = ConstantInt::get(Int64, 0);

  If = M.getOrInsertFunction(
    IfIntrinsic, ReturnStruct, Boolean, (Type *)0);

  Else = M.getOrInsertFunction(
    ElseIntrinsic, ReturnStruct, Int64, (Type *)0);

  Break = M.getOrInsertFunction(
    BreakIntrinsic, Int64, Int64, (Type *)0);

  IfBreak = M.getOrInsertFunction(
    IfBreakIntrinsic, Int64, Boolean, Int64, (Type *)0);

  ElseBreak = M.getOrInsertFunction(
    ElseBreakIntrinsic, Int64, Int64, Int64, (Type *)0);

  Loop = M.getOrInsertFunction(
    LoopIntrinsic, Boolean, Int64, (Type *)0);

  EndCf = M.getOrInsertFunction(
    EndCfIntrinsic, Void, Int64, (Type *)0);

  return false;
}

/// \brief Is BB the last block saved on the stack ?
bool SIAnnotateControlFlow::isTopOfStack(BasicBlock *BB) {
  return !Stack.empty() && Stack.back().first == BB;
}

/// \brief Pop the last saved value from the control flow stack
Value *SIAnnotateControlFlow::popSaved() {
  return Stack.pop_back_val().second;
}

/// \brief Push a BB and saved value to the control flow stack
void SIAnnotateControlFlow::push(BasicBlock *BB, Value *Saved) {
  Stack.push_back(std::make_pair(BB, Saved));
}

/// \brief Can the condition represented by this PHI node treated like
/// an "Else" block?
bool SIAnnotateControlFlow::isElse(PHINode *Phi) {
  BasicBlock *IDom = DT->getNode(Phi->getParent())->getIDom()->getBlock();
  for (unsigned i = 0, e = Phi->getNumIncomingValues(); i != e; ++i) {
    if (Phi->getIncomingBlock(i) == IDom) {

      if (Phi->getIncomingValue(i) != BoolTrue)
        return false;

    } else {
      if (Phi->getIncomingValue(i) != BoolFalse)
        return false;
 
    }
  }
  return true;
}

// \brief Erase "Phi" if it is not used any more
void SIAnnotateControlFlow::eraseIfUnused(PHINode *Phi) {
  if (!Phi->hasNUsesOrMore(1))
    Phi->eraseFromParent();
}

/// \brief Open a new "If" block
void SIAnnotateControlFlow::openIf(BranchInst *Term) {
  Value *Ret = CallInst::Create(If, Term->getCondition(), "", Term);
  Term->setCondition(ExtractValueInst::Create(Ret, 0, "", Term));
  push(Term->getSuccessor(1), ExtractValueInst::Create(Ret, 1, "", Term));
}

/// \brief Close the last "If" block and open a new "Else" block
void SIAnnotateControlFlow::insertElse(BranchInst *Term) {
  Value *Ret = CallInst::Create(Else, popSaved(), "", Term);
  Term->setCondition(ExtractValueInst::Create(Ret, 0, "", Term));
  push(Term->getSuccessor(1), ExtractValueInst::Create(Ret, 1, "", Term));
}

/// \brief Recursively handle the condition leading to a loop
void SIAnnotateControlFlow::handleLoopCondition(Value *Cond) {
  if (PHINode *Phi = dyn_cast<PHINode>(Cond)) {

    // Handle all non constant incoming values first
    for (unsigned i = 0, e = Phi->getNumIncomingValues(); i != e; ++i) {
      Value *Incoming = Phi->getIncomingValue(i);
      if (isa<ConstantInt>(Incoming))
        continue;

      Phi->setIncomingValue(i, BoolFalse);
      handleLoopCondition(Incoming);
    }

    BasicBlock *Parent = Phi->getParent();
    BasicBlock *IDom = DT->getNode(Parent)->getIDom()->getBlock();

    for (unsigned i = 0, e = Phi->getNumIncomingValues(); i != e; ++i) {

      Value *Incoming = Phi->getIncomingValue(i);
      if (Incoming != BoolTrue)
        continue;

      BasicBlock *From = Phi->getIncomingBlock(i);
      if (From == IDom) {
        CallInst *OldEnd = dyn_cast<CallInst>(Parent->getFirstInsertionPt());
        if (OldEnd && OldEnd->getCalledFunction() == EndCf) {
          Value *Args[] = {
            OldEnd->getArgOperand(0),
            PhiInserter.GetValueAtEndOfBlock(Parent)
          };
          Value *Ret = CallInst::Create(ElseBreak, Args, "", OldEnd);
          PhiInserter.AddAvailableValue(Parent, Ret);
          continue;
        }
      }

      TerminatorInst *Insert = From->getTerminator();
      Value *Arg = PhiInserter.GetValueAtEndOfBlock(From);
      Value *Ret = CallInst::Create(Break, Arg, "", Insert);
      PhiInserter.AddAvailableValue(From, Ret);
    }
    eraseIfUnused(Phi);

  } else if (Instruction *Inst = dyn_cast<Instruction>(Cond)) {
    BasicBlock *Parent = Inst->getParent();
    TerminatorInst *Insert = Parent->getTerminator();
    Value *Args[] = { Cond, PhiInserter.GetValueAtEndOfBlock(Parent) };
    Value *Ret = CallInst::Create(IfBreak, Args, "", Insert);
    PhiInserter.AddAvailableValue(Parent, Ret);

  } else {
    assert(0 && "Unhandled loop condition!");
  }
}

/// \brief Handle a back edge (loop)
void SIAnnotateControlFlow::handleLoop(BranchInst *Term) {
  BasicBlock *Target = Term->getSuccessor(1);
  PHINode *Broken = PHINode::Create(Int64, 0, "", &Target->front());

  PhiInserter.Initialize(Int64, "");
  PhiInserter.AddAvailableValue(Target, Broken);

  Value *Cond = Term->getCondition();
  Term->setCondition(BoolTrue);
  handleLoopCondition(Cond);

  BasicBlock *BB = Term->getParent();
  Value *Arg = PhiInserter.GetValueAtEndOfBlock(BB);
  for (pred_iterator PI = pred_begin(Target), PE = pred_end(Target);
       PI != PE; ++PI) {

    Broken->addIncoming(*PI == BB ? Arg : Int64Zero, *PI);
  }

  Term->setCondition(CallInst::Create(Loop, Arg, "", Term));
  push(Term->getSuccessor(0), Arg);
}

/// \brief Close the last opened control flow
void SIAnnotateControlFlow::closeControlFlow(BasicBlock *BB) {
  CallInst::Create(EndCf, popSaved(), "", BB->getFirstInsertionPt());
}

/// \brief Annotate the control flow with intrinsics so the backend can
/// recognize if/then/else and loops.
bool SIAnnotateControlFlow::runOnFunction(Function &F) {
  DT = &getAnalysis<DominatorTree>();

  for (df_iterator<BasicBlock *> I = df_begin(&F.getEntryBlock()),
       E = df_end(&F.getEntryBlock()); I != E; ++I) {

    BranchInst *Term = dyn_cast<BranchInst>((*I)->getTerminator());

    if (!Term || Term->isUnconditional()) {
      if (isTopOfStack(*I))
        closeControlFlow(*I);
      continue;
    }

    if (I.nodeVisited(Term->getSuccessor(1))) {
      if (isTopOfStack(*I))
        closeControlFlow(*I);
      handleLoop(Term);
      continue;
    }

    if (isTopOfStack(*I)) {
      PHINode *Phi = dyn_cast<PHINode>(Term->getCondition());
      if (Phi && Phi->getParent() == *I && isElse(Phi)) {
        insertElse(Term);
        eraseIfUnused(Phi);
        continue;
      }
      closeControlFlow(*I);
    }
    openIf(Term);
  }

  assert(Stack.empty());
  return true;
}

/// \brief Create the annotation pass
FunctionPass *llvm::createSIAnnotateControlFlowPass() {
  return new SIAnnotateControlFlow();
}
