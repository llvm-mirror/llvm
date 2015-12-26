//===--- HexagonGenExtract.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::opt<unsigned> ExtractCutoff("extract-cutoff", cl::init(~0U),
  cl::Hidden, cl::desc("Cutoff for generating \"extract\""
  " instructions"));

// This prevents generating extract instructions that have the offset of 0.
// One of the reasons for "extract" is to put a sequence of bits in a regis-
// ter, starting at offset 0 (so that these bits can then be used by an
// "insert"). If the bits are already at offset 0, it is better not to gene-
// rate "extract", since logical bit operations can be merged into compound
// instructions (as opposed to "extract").
static cl::opt<bool> NoSR0("extract-nosr0", cl::init(true), cl::Hidden,
  cl::desc("No extract instruction with offset 0"));

static cl::opt<bool> NeedAnd("extract-needand", cl::init(true), cl::Hidden,
  cl::desc("Require & in extract patterns"));

namespace llvm {
  void initializeHexagonGenExtractPass(PassRegistry&);
  FunctionPass *createHexagonGenExtract();
}


namespace {
  class HexagonGenExtract : public FunctionPass {
  public:
    static char ID;
    HexagonGenExtract() : FunctionPass(ID), ExtractCount(0) {
      initializeHexagonGenExtractPass(*PassRegistry::getPassRegistry());
    }
    virtual const char *getPassName() const override {
      return "Hexagon generate \"extract\" instructions";
    }
    virtual bool runOnFunction(Function &F) override;
    virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addPreserved<MachineFunctionAnalysis>();
      FunctionPass::getAnalysisUsage(AU);
    }
  private:
    bool visitBlock(BasicBlock *B);
    bool convert(Instruction *In);

    unsigned ExtractCount;
    DominatorTree *DT;
  };

  char HexagonGenExtract::ID = 0;
}

INITIALIZE_PASS_BEGIN(HexagonGenExtract, "hextract", "Hexagon generate "
  "\"extract\" instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(HexagonGenExtract, "hextract", "Hexagon generate "
  "\"extract\" instructions", false, false)


bool HexagonGenExtract::convert(Instruction *In) {
  using namespace PatternMatch;
  Value *BF = 0;
  ConstantInt *CSL = 0, *CSR = 0, *CM = 0;
  BasicBlock *BB = In->getParent();
  LLVMContext &Ctx = BB->getContext();
  bool LogicalSR;

  // (and (shl (lshr x, #sr), #sl), #m)
  LogicalSR = true;
  bool Match = match(In, m_And(m_Shl(m_LShr(m_Value(BF), m_ConstantInt(CSR)),
                               m_ConstantInt(CSL)),
                         m_ConstantInt(CM)));

  if (!Match) {
    // (and (shl (ashr x, #sr), #sl), #m)
    LogicalSR = false;
    Match = match(In, m_And(m_Shl(m_AShr(m_Value(BF), m_ConstantInt(CSR)),
                            m_ConstantInt(CSL)),
                      m_ConstantInt(CM)));
  }
  if (!Match) {
    // (and (shl x, #sl), #m)
    LogicalSR = true;
    CSR = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
    Match = match(In, m_And(m_Shl(m_Value(BF), m_ConstantInt(CSL)),
                      m_ConstantInt(CM)));
    if (Match && NoSR0)
      return false;
  }
  if (!Match) {
    // (and (lshr x, #sr), #m)
    LogicalSR = true;
    CSL = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
    Match = match(In, m_And(m_LShr(m_Value(BF), m_ConstantInt(CSR)),
                            m_ConstantInt(CM)));
  }
  if (!Match) {
    // (and (ashr x, #sr), #m)
    LogicalSR = false;
    CSL = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
    Match = match(In, m_And(m_AShr(m_Value(BF), m_ConstantInt(CSR)),
                            m_ConstantInt(CM)));
  }
  if (!Match) {
    CM = 0;
    // (shl (lshr x, #sr), #sl)
    LogicalSR = true;
    Match = match(In, m_Shl(m_LShr(m_Value(BF), m_ConstantInt(CSR)),
                            m_ConstantInt(CSL)));
  }
  if (!Match) {
    CM = 0;
    // (shl (ashr x, #sr), #sl)
    LogicalSR = false;
    Match = match(In, m_Shl(m_AShr(m_Value(BF), m_ConstantInt(CSR)),
                            m_ConstantInt(CSL)));
  }
  if (!Match)
    return false;

  Type *Ty = BF->getType();
  if (!Ty->isIntegerTy())
    return false;
  unsigned BW = Ty->getPrimitiveSizeInBits();
  if (BW != 32 && BW != 64)
    return false;

  uint32_t SR = CSR->getZExtValue();
  uint32_t SL = CSL->getZExtValue();

  if (!CM) {
    // If there was no and, and the shift left did not remove all potential
    // sign bits created by the shift right, then extractu cannot reproduce
    // this value.
    if (!LogicalSR && (SR > SL))
      return false;
    APInt A = APInt(BW, ~0ULL).lshr(SR).shl(SL);
    CM = ConstantInt::get(Ctx, A);
  }

  // CM is the shifted-left mask. Shift it back right to remove the zero
  // bits on least-significant positions.
  APInt M = CM->getValue().lshr(SL);
  uint32_t T = M.countTrailingOnes();

  // During the shifts some of the bits will be lost. Calculate how many
  // of the original value will remain after shift right and then left.
  uint32_t U = BW - std::max(SL, SR);
  // The width of the extracted field is the minimum of the original bits
  // that remain after the shifts and the number of contiguous 1s in the mask.
  uint32_t W = std::min(U, T);
  if (W == 0)
    return false;

  // Check if the extracted bits are contained within the mask that it is
  // and-ed with. The extract operation will copy these bits, and so the
  // mask cannot any holes in it that would clear any of the bits of the
  // extracted field.
  if (!LogicalSR) {
    // If the shift right was arithmetic, it could have included some 1 bits.
    // It is still ok to generate extract, but only if the mask eliminates
    // those bits (i.e. M does not have any bits set beyond U).
    APInt C = APInt::getHighBitsSet(BW, BW-U);
    if (M.intersects(C) || !APIntOps::isMask(W, M))
      return false;
  } else {
    // Check if M starts with a contiguous sequence of W times 1 bits. Get
    // the low U bits of M (which eliminates the 0 bits shifted in on the
    // left), and check if the result is APInt's "mask":
    if (!APIntOps::isMask(W, M.getLoBits(U)))
      return false;
  }

  IRBuilder<> IRB(In);
  Intrinsic::ID IntId = (BW == 32) ? Intrinsic::hexagon_S2_extractu
                                   : Intrinsic::hexagon_S2_extractup;
  Module *Mod = BB->getParent()->getParent();
  Value *ExtF = Intrinsic::getDeclaration(Mod, IntId);
  Value *NewIn = IRB.CreateCall(ExtF, {BF, IRB.getInt32(W), IRB.getInt32(SR)});
  if (SL != 0)
    NewIn = IRB.CreateShl(NewIn, SL, CSL->getName());
  In->replaceAllUsesWith(NewIn);
  return true;
}


bool HexagonGenExtract::visitBlock(BasicBlock *B) {
  // Depth-first, bottom-up traversal.
  DomTreeNode *DTN = DT->getNode(B);
  typedef GraphTraits<DomTreeNode*> GTN;
  typedef GTN::ChildIteratorType Iter;
  for (Iter I = GTN::child_begin(DTN), E = GTN::child_end(DTN); I != E; ++I)
    visitBlock((*I)->getBlock());

  // Allow limiting the number of generated extracts for debugging purposes.
  bool HasCutoff = ExtractCutoff.getPosition();
  unsigned Cutoff = ExtractCutoff;

  bool Changed = false;
  BasicBlock::iterator I = std::prev(B->end()), NextI, Begin = B->begin();
  while (true) {
    if (HasCutoff && (ExtractCount >= Cutoff))
      return Changed;
    bool Last = (I == Begin);
    if (!Last)
      NextI = std::prev(I);
    Instruction *In = &*I;
    bool Done = convert(In);
    if (HasCutoff && Done)
      ExtractCount++;
    Changed |= Done;
    if (Last)
      break;
    I = NextI;
  }
  return Changed;
}


bool HexagonGenExtract::runOnFunction(Function &F) {
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  bool Changed;

  // Traverse the function bottom-up, to see super-expressions before their
  // sub-expressions.
  BasicBlock *Entry = GraphTraits<Function*>::getEntryNode(&F);
  Changed = visitBlock(Entry);

  return Changed;
}


FunctionPass *llvm::createHexagonGenExtract() {
  return new HexagonGenExtract();
}
