//===- ScalarEvolutionExpander.cpp - Scalar Evolution Analysis --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the scalar evolution expander,
// which is used to generate the code corresponding to a given scalar evolution
// expression.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

/// ReuseOrCreateCast - Arrange for there to be a cast of V to Ty at IP,
/// reusing an existing cast if a suitable one exists, moving an existing
/// cast if a suitable one exists but isn't in the right place, or
/// creating a new one.
Value *SCEVExpander::ReuseOrCreateCast(Value *V, Type *Ty,
                                       Instruction::CastOps Op,
                                       BasicBlock::iterator IP) {
  // This function must be called with the builder having a valid insertion
  // point. It doesn't need to be the actual IP where the uses of the returned
  // cast will be added, but it must dominate such IP.
  // We use this precondition to produce a cast that will dominate all its
  // uses. In particular, this is crucial for the case where the builder's
  // insertion point *is* the point where we were asked to put the cast.
  // Since we don't know the builder's insertion point is actually
  // where the uses will be added (only that it dominates it), we are
  // not allowed to move it.
  BasicBlock::iterator BIP = Builder.GetInsertPoint();

  Instruction *Ret = NULL;

  // Check to see if there is already a cast!
  for (Value::use_iterator UI = V->use_begin(), E = V->use_end();
       UI != E; ++UI) {
    User *U = *UI;
    if (U->getType() == Ty)
      if (CastInst *CI = dyn_cast<CastInst>(U))
        if (CI->getOpcode() == Op) {
          // If the cast isn't where we want it, create a new cast at IP.
          // Likewise, do not reuse a cast at BIP because it must dominate
          // instructions that might be inserted before BIP.
          if (BasicBlock::iterator(CI) != IP || BIP == IP) {
            // Create a new cast, and leave the old cast in place in case
            // it is being used as an insert point. Clear its operand
            // so that it doesn't hold anything live.
            Ret = CastInst::Create(Op, V, Ty, "", IP);
            Ret->takeName(CI);
            CI->replaceAllUsesWith(Ret);
            CI->setOperand(0, UndefValue::get(V->getType()));
            break;
          }
          Ret = CI;
          break;
        }
  }

  // Create a new cast.
  if (!Ret)
    Ret = CastInst::Create(Op, V, Ty, V->getName(), IP);

  // We assert at the end of the function since IP might point to an
  // instruction with different dominance properties than a cast
  // (an invoke for example) and not dominate BIP (but the cast does).
  assert(SE.DT->dominates(Ret, BIP));

  rememberInstruction(Ret);
  return Ret;
}

/// InsertNoopCastOfTo - Insert a cast of V to the specified type,
/// which must be possible with a noop cast, doing what we can to share
/// the casts.
Value *SCEVExpander::InsertNoopCastOfTo(Value *V, Type *Ty) {
  Instruction::CastOps Op = CastInst::getCastOpcode(V, false, Ty, false);
  assert((Op == Instruction::BitCast ||
          Op == Instruction::PtrToInt ||
          Op == Instruction::IntToPtr) &&
         "InsertNoopCastOfTo cannot perform non-noop casts!");
  assert(SE.getTypeSizeInBits(V->getType()) == SE.getTypeSizeInBits(Ty) &&
         "InsertNoopCastOfTo cannot change sizes!");

  // Short-circuit unnecessary bitcasts.
  if (Op == Instruction::BitCast) {
    if (V->getType() == Ty)
      return V;
    if (CastInst *CI = dyn_cast<CastInst>(V)) {
      if (CI->getOperand(0)->getType() == Ty)
        return CI->getOperand(0);
    }
  }
  // Short-circuit unnecessary inttoptr<->ptrtoint casts.
  if ((Op == Instruction::PtrToInt || Op == Instruction::IntToPtr) &&
      SE.getTypeSizeInBits(Ty) == SE.getTypeSizeInBits(V->getType())) {
    if (CastInst *CI = dyn_cast<CastInst>(V))
      if ((CI->getOpcode() == Instruction::PtrToInt ||
           CI->getOpcode() == Instruction::IntToPtr) &&
          SE.getTypeSizeInBits(CI->getType()) ==
          SE.getTypeSizeInBits(CI->getOperand(0)->getType()))
        return CI->getOperand(0);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
      if ((CE->getOpcode() == Instruction::PtrToInt ||
           CE->getOpcode() == Instruction::IntToPtr) &&
          SE.getTypeSizeInBits(CE->getType()) ==
          SE.getTypeSizeInBits(CE->getOperand(0)->getType()))
        return CE->getOperand(0);
  }

  // Fold a cast of a constant.
  if (Constant *C = dyn_cast<Constant>(V))
    return ConstantExpr::getCast(Op, C, Ty);

  // Cast the argument at the beginning of the entry block, after
  // any bitcasts of other arguments.
  if (Argument *A = dyn_cast<Argument>(V)) {
    BasicBlock::iterator IP = A->getParent()->getEntryBlock().begin();
    while ((isa<BitCastInst>(IP) &&
            isa<Argument>(cast<BitCastInst>(IP)->getOperand(0)) &&
            cast<BitCastInst>(IP)->getOperand(0) != A) ||
           isa<DbgInfoIntrinsic>(IP) ||
           isa<LandingPadInst>(IP))
      ++IP;
    return ReuseOrCreateCast(A, Ty, Op, IP);
  }

  // Cast the instruction immediately after the instruction.
  Instruction *I = cast<Instruction>(V);
  BasicBlock::iterator IP = I; ++IP;
  if (InvokeInst *II = dyn_cast<InvokeInst>(I))
    IP = II->getNormalDest()->begin();
  while (isa<PHINode>(IP) || isa<LandingPadInst>(IP))
    ++IP;
  return ReuseOrCreateCast(I, Ty, Op, IP);
}

/// InsertBinop - Insert the specified binary operator, doing a small amount
/// of work to avoid inserting an obviously redundant operation.
Value *SCEVExpander::InsertBinop(Instruction::BinaryOps Opcode,
                                 Value *LHS, Value *RHS) {
  // Fold a binop with constant operands.
  if (Constant *CLHS = dyn_cast<Constant>(LHS))
    if (Constant *CRHS = dyn_cast<Constant>(RHS))
      return ConstantExpr::get(Opcode, CLHS, CRHS);

  // Do a quick scan to see if we have this binop nearby.  If so, reuse it.
  unsigned ScanLimit = 6;
  BasicBlock::iterator BlockBegin = Builder.GetInsertBlock()->begin();
  // Scanning starts from the last instruction before the insertion point.
  BasicBlock::iterator IP = Builder.GetInsertPoint();
  if (IP != BlockBegin) {
    --IP;
    for (; ScanLimit; --IP, --ScanLimit) {
      // Don't count dbg.value against the ScanLimit, to avoid perturbing the
      // generated code.
      if (isa<DbgInfoIntrinsic>(IP))
        ScanLimit++;
      if (IP->getOpcode() == (unsigned)Opcode && IP->getOperand(0) == LHS &&
          IP->getOperand(1) == RHS)
        return IP;
      if (IP == BlockBegin) break;
    }
  }

  // Save the original insertion point so we can restore it when we're done.
  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();

  // Move the insertion point out of as many loops as we can.
  while (const Loop *L = SE.LI->getLoopFor(Builder.GetInsertBlock())) {
    if (!L->isLoopInvariant(LHS) || !L->isLoopInvariant(RHS)) break;
    BasicBlock *Preheader = L->getLoopPreheader();
    if (!Preheader) break;

    // Ok, move up a level.
    Builder.SetInsertPoint(Preheader, Preheader->getTerminator());
  }

  // If we haven't found this binop, insert it.
  Instruction *BO = cast<Instruction>(Builder.CreateBinOp(Opcode, LHS, RHS));
  BO->setDebugLoc(SaveInsertPt->getDebugLoc());
  rememberInstruction(BO);

  // Restore the original insert point.
  if (SaveInsertBB)
    restoreInsertPoint(SaveInsertBB, SaveInsertPt);

  return BO;
}

/// FactorOutConstant - Test if S is divisible by Factor, using signed
/// division. If so, update S with Factor divided out and return true.
/// S need not be evenly divisible if a reasonable remainder can be
/// computed.
/// TODO: When ScalarEvolution gets a SCEVSDivExpr, this can be made
/// unnecessary; in its place, just signed-divide Ops[i] by the scale and
/// check to see if the divide was folded.
static bool FactorOutConstant(const SCEV *&S,
                              const SCEV *&Remainder,
                              const SCEV *Factor,
                              ScalarEvolution &SE,
                              const DataLayout *TD) {
  // Everything is divisible by one.
  if (Factor->isOne())
    return true;

  // x/x == 1.
  if (S == Factor) {
    S = SE.getConstant(S->getType(), 1);
    return true;
  }

  // For a Constant, check for a multiple of the given factor.
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S)) {
    // 0/x == 0.
    if (C->isZero())
      return true;
    // Check for divisibility.
    if (const SCEVConstant *FC = dyn_cast<SCEVConstant>(Factor)) {
      ConstantInt *CI =
        ConstantInt::get(SE.getContext(),
                         C->getValue()->getValue().sdiv(
                                                   FC->getValue()->getValue()));
      // If the quotient is zero and the remainder is non-zero, reject
      // the value at this scale. It will be considered for subsequent
      // smaller scales.
      if (!CI->isZero()) {
        const SCEV *Div = SE.getConstant(CI);
        S = Div;
        Remainder =
          SE.getAddExpr(Remainder,
                        SE.getConstant(C->getValue()->getValue().srem(
                                                  FC->getValue()->getValue())));
        return true;
      }
    }
  }

  // In a Mul, check if there is a constant operand which is a multiple
  // of the given factor.
  if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(S)) {
    if (TD) {
      // With DataLayout, the size is known. Check if there is a constant
      // operand which is a multiple of the given factor. If so, we can
      // factor it.
      const SCEVConstant *FC = cast<SCEVConstant>(Factor);
      if (const SCEVConstant *C = dyn_cast<SCEVConstant>(M->getOperand(0)))
        if (!C->getValue()->getValue().srem(FC->getValue()->getValue())) {
          SmallVector<const SCEV *, 4> NewMulOps(M->op_begin(), M->op_end());
          NewMulOps[0] =
            SE.getConstant(C->getValue()->getValue().sdiv(
                                                   FC->getValue()->getValue()));
          S = SE.getMulExpr(NewMulOps);
          return true;
        }
    } else {
      // Without DataLayout, check if Factor can be factored out of any of the
      // Mul's operands. If so, we can just remove it.
      for (unsigned i = 0, e = M->getNumOperands(); i != e; ++i) {
        const SCEV *SOp = M->getOperand(i);
        const SCEV *Remainder = SE.getConstant(SOp->getType(), 0);
        if (FactorOutConstant(SOp, Remainder, Factor, SE, TD) &&
            Remainder->isZero()) {
          SmallVector<const SCEV *, 4> NewMulOps(M->op_begin(), M->op_end());
          NewMulOps[i] = SOp;
          S = SE.getMulExpr(NewMulOps);
          return true;
        }
      }
    }
  }

  // In an AddRec, check if both start and step are divisible.
  if (const SCEVAddRecExpr *A = dyn_cast<SCEVAddRecExpr>(S)) {
    const SCEV *Step = A->getStepRecurrence(SE);
    const SCEV *StepRem = SE.getConstant(Step->getType(), 0);
    if (!FactorOutConstant(Step, StepRem, Factor, SE, TD))
      return false;
    if (!StepRem->isZero())
      return false;
    const SCEV *Start = A->getStart();
    if (!FactorOutConstant(Start, Remainder, Factor, SE, TD))
      return false;
    S = SE.getAddRecExpr(Start, Step, A->getLoop(),
                         A->getNoWrapFlags(SCEV::FlagNW));
    return true;
  }

  return false;
}

/// SimplifyAddOperands - Sort and simplify a list of add operands. NumAddRecs
/// is the number of SCEVAddRecExprs present, which are kept at the end of
/// the list.
///
static void SimplifyAddOperands(SmallVectorImpl<const SCEV *> &Ops,
                                Type *Ty,
                                ScalarEvolution &SE) {
  unsigned NumAddRecs = 0;
  for (unsigned i = Ops.size(); i > 0 && isa<SCEVAddRecExpr>(Ops[i-1]); --i)
    ++NumAddRecs;
  // Group Ops into non-addrecs and addrecs.
  SmallVector<const SCEV *, 8> NoAddRecs(Ops.begin(), Ops.end() - NumAddRecs);
  SmallVector<const SCEV *, 8> AddRecs(Ops.end() - NumAddRecs, Ops.end());
  // Let ScalarEvolution sort and simplify the non-addrecs list.
  const SCEV *Sum = NoAddRecs.empty() ?
                    SE.getConstant(Ty, 0) :
                    SE.getAddExpr(NoAddRecs);
  // If it returned an add, use the operands. Otherwise it simplified
  // the sum into a single value, so just use that.
  Ops.clear();
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Sum))
    Ops.append(Add->op_begin(), Add->op_end());
  else if (!Sum->isZero())
    Ops.push_back(Sum);
  // Then append the addrecs.
  Ops.append(AddRecs.begin(), AddRecs.end());
}

/// SplitAddRecs - Flatten a list of add operands, moving addrec start values
/// out to the top level. For example, convert {a + b,+,c} to a, b, {0,+,d}.
/// This helps expose more opportunities for folding parts of the expressions
/// into GEP indices.
///
static void SplitAddRecs(SmallVectorImpl<const SCEV *> &Ops,
                         Type *Ty,
                         ScalarEvolution &SE) {
  // Find the addrecs.
  SmallVector<const SCEV *, 8> AddRecs;
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    while (const SCEVAddRecExpr *A = dyn_cast<SCEVAddRecExpr>(Ops[i])) {
      const SCEV *Start = A->getStart();
      if (Start->isZero()) break;
      const SCEV *Zero = SE.getConstant(Ty, 0);
      AddRecs.push_back(SE.getAddRecExpr(Zero,
                                         A->getStepRecurrence(SE),
                                         A->getLoop(),
                                         A->getNoWrapFlags(SCEV::FlagNW)));
      if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Start)) {
        Ops[i] = Zero;
        Ops.append(Add->op_begin(), Add->op_end());
        e += Add->getNumOperands();
      } else {
        Ops[i] = Start;
      }
    }
  if (!AddRecs.empty()) {
    // Add the addrecs onto the end of the list.
    Ops.append(AddRecs.begin(), AddRecs.end());
    // Resort the operand list, moving any constants to the front.
    SimplifyAddOperands(Ops, Ty, SE);
  }
}

/// expandAddToGEP - Expand an addition expression with a pointer type into
/// a GEP instead of using ptrtoint+arithmetic+inttoptr. This helps
/// BasicAliasAnalysis and other passes analyze the result. See the rules
/// for getelementptr vs. inttoptr in
/// http://llvm.org/docs/LangRef.html#pointeraliasing
/// for details.
///
/// Design note: The correctness of using getelementptr here depends on
/// ScalarEvolution not recognizing inttoptr and ptrtoint operators, as
/// they may introduce pointer arithmetic which may not be safely converted
/// into getelementptr.
///
/// Design note: It might seem desirable for this function to be more
/// loop-aware. If some of the indices are loop-invariant while others
/// aren't, it might seem desirable to emit multiple GEPs, keeping the
/// loop-invariant portions of the overall computation outside the loop.
/// However, there are a few reasons this is not done here. Hoisting simple
/// arithmetic is a low-level optimization that often isn't very
/// important until late in the optimization process. In fact, passes
/// like InstructionCombining will combine GEPs, even if it means
/// pushing loop-invariant computation down into loops, so even if the
/// GEPs were split here, the work would quickly be undone. The
/// LoopStrengthReduction pass, which is usually run quite late (and
/// after the last InstructionCombining pass), takes care of hoisting
/// loop-invariant portions of expressions, after considering what
/// can be folded using target addressing modes.
///
Value *SCEVExpander::expandAddToGEP(const SCEV *const *op_begin,
                                    const SCEV *const *op_end,
                                    PointerType *PTy,
                                    Type *Ty,
                                    Value *V) {
  Type *ElTy = PTy->getElementType();
  SmallVector<Value *, 4> GepIndices;
  SmallVector<const SCEV *, 8> Ops(op_begin, op_end);
  bool AnyNonZeroIndices = false;

  // Split AddRecs up into parts as either of the parts may be usable
  // without the other.
  SplitAddRecs(Ops, Ty, SE);

  // Descend down the pointer's type and attempt to convert the other
  // operands into GEP indices, at each level. The first index in a GEP
  // indexes into the array implied by the pointer operand; the rest of
  // the indices index into the element or field type selected by the
  // preceding index.
  for (;;) {
    // If the scale size is not 0, attempt to factor out a scale for
    // array indexing.
    SmallVector<const SCEV *, 8> ScaledOps;
    if (ElTy->isSized()) {
      const SCEV *ElSize = SE.getSizeOfExpr(ElTy);
      if (!ElSize->isZero()) {
        SmallVector<const SCEV *, 8> NewOps;
        for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
          const SCEV *Op = Ops[i];
          const SCEV *Remainder = SE.getConstant(Ty, 0);
          if (FactorOutConstant(Op, Remainder, ElSize, SE, SE.TD)) {
            // Op now has ElSize factored out.
            ScaledOps.push_back(Op);
            if (!Remainder->isZero())
              NewOps.push_back(Remainder);
            AnyNonZeroIndices = true;
          } else {
            // The operand was not divisible, so add it to the list of operands
            // we'll scan next iteration.
            NewOps.push_back(Ops[i]);
          }
        }
        // If we made any changes, update Ops.
        if (!ScaledOps.empty()) {
          Ops = NewOps;
          SimplifyAddOperands(Ops, Ty, SE);
        }
      }
    }

    // Record the scaled array index for this level of the type. If
    // we didn't find any operands that could be factored, tentatively
    // assume that element zero was selected (since the zero offset
    // would obviously be folded away).
    Value *Scaled = ScaledOps.empty() ?
                    Constant::getNullValue(Ty) :
                    expandCodeFor(SE.getAddExpr(ScaledOps), Ty);
    GepIndices.push_back(Scaled);

    // Collect struct field index operands.
    while (StructType *STy = dyn_cast<StructType>(ElTy)) {
      bool FoundFieldNo = false;
      // An empty struct has no fields.
      if (STy->getNumElements() == 0) break;
      if (SE.TD) {
        // With DataLayout, field offsets are known. See if a constant offset
        // falls within any of the struct fields.
        if (Ops.empty()) break;
        if (const SCEVConstant *C = dyn_cast<SCEVConstant>(Ops[0]))
          if (SE.getTypeSizeInBits(C->getType()) <= 64) {
            const StructLayout &SL = *SE.TD->getStructLayout(STy);
            uint64_t FullOffset = C->getValue()->getZExtValue();
            if (FullOffset < SL.getSizeInBytes()) {
              unsigned ElIdx = SL.getElementContainingOffset(FullOffset);
              GepIndices.push_back(
                  ConstantInt::get(Type::getInt32Ty(Ty->getContext()), ElIdx));
              ElTy = STy->getTypeAtIndex(ElIdx);
              Ops[0] =
                SE.getConstant(Ty, FullOffset - SL.getElementOffset(ElIdx));
              AnyNonZeroIndices = true;
              FoundFieldNo = true;
            }
          }
      } else {
        // Without DataLayout, just check for an offsetof expression of the
        // appropriate struct type.
        for (unsigned i = 0, e = Ops.size(); i != e; ++i)
          if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(Ops[i])) {
            Type *CTy;
            Constant *FieldNo;
            if (U->isOffsetOf(CTy, FieldNo) && CTy == STy) {
              GepIndices.push_back(FieldNo);
              ElTy =
                STy->getTypeAtIndex(cast<ConstantInt>(FieldNo)->getZExtValue());
              Ops[i] = SE.getConstant(Ty, 0);
              AnyNonZeroIndices = true;
              FoundFieldNo = true;
              break;
            }
          }
      }
      // If no struct field offsets were found, tentatively assume that
      // field zero was selected (since the zero offset would obviously
      // be folded away).
      if (!FoundFieldNo) {
        ElTy = STy->getTypeAtIndex(0u);
        GepIndices.push_back(
          Constant::getNullValue(Type::getInt32Ty(Ty->getContext())));
      }
    }

    if (ArrayType *ATy = dyn_cast<ArrayType>(ElTy))
      ElTy = ATy->getElementType();
    else
      break;
  }

  // If none of the operands were convertible to proper GEP indices, cast
  // the base to i8* and do an ugly getelementptr with that. It's still
  // better than ptrtoint+arithmetic+inttoptr at least.
  if (!AnyNonZeroIndices) {
    // Cast the base to i8*.
    V = InsertNoopCastOfTo(V,
       Type::getInt8PtrTy(Ty->getContext(), PTy->getAddressSpace()));

    assert(!isa<Instruction>(V) ||
           SE.DT->dominates(cast<Instruction>(V), Builder.GetInsertPoint()));

    // Expand the operands for a plain byte offset.
    Value *Idx = expandCodeFor(SE.getAddExpr(Ops), Ty);

    // Fold a GEP with constant operands.
    if (Constant *CLHS = dyn_cast<Constant>(V))
      if (Constant *CRHS = dyn_cast<Constant>(Idx))
        return ConstantExpr::getGetElementPtr(CLHS, CRHS);

    // Do a quick scan to see if we have this GEP nearby.  If so, reuse it.
    unsigned ScanLimit = 6;
    BasicBlock::iterator BlockBegin = Builder.GetInsertBlock()->begin();
    // Scanning starts from the last instruction before the insertion point.
    BasicBlock::iterator IP = Builder.GetInsertPoint();
    if (IP != BlockBegin) {
      --IP;
      for (; ScanLimit; --IP, --ScanLimit) {
        // Don't count dbg.value against the ScanLimit, to avoid perturbing the
        // generated code.
        if (isa<DbgInfoIntrinsic>(IP))
          ScanLimit++;
        if (IP->getOpcode() == Instruction::GetElementPtr &&
            IP->getOperand(0) == V && IP->getOperand(1) == Idx)
          return IP;
        if (IP == BlockBegin) break;
      }
    }

    // Save the original insertion point so we can restore it when we're done.
    BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
    BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();

    // Move the insertion point out of as many loops as we can.
    while (const Loop *L = SE.LI->getLoopFor(Builder.GetInsertBlock())) {
      if (!L->isLoopInvariant(V) || !L->isLoopInvariant(Idx)) break;
      BasicBlock *Preheader = L->getLoopPreheader();
      if (!Preheader) break;

      // Ok, move up a level.
      Builder.SetInsertPoint(Preheader, Preheader->getTerminator());
    }

    // Emit a GEP.
    Value *GEP = Builder.CreateGEP(V, Idx, "uglygep");
    rememberInstruction(GEP);

    // Restore the original insert point.
    if (SaveInsertBB)
      restoreInsertPoint(SaveInsertBB, SaveInsertPt);

    return GEP;
  }

  // Save the original insertion point so we can restore it when we're done.
  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();

  // Move the insertion point out of as many loops as we can.
  while (const Loop *L = SE.LI->getLoopFor(Builder.GetInsertBlock())) {
    if (!L->isLoopInvariant(V)) break;

    bool AnyIndexNotLoopInvariant = false;
    for (SmallVectorImpl<Value *>::const_iterator I = GepIndices.begin(),
         E = GepIndices.end(); I != E; ++I)
      if (!L->isLoopInvariant(*I)) {
        AnyIndexNotLoopInvariant = true;
        break;
      }
    if (AnyIndexNotLoopInvariant)
      break;

    BasicBlock *Preheader = L->getLoopPreheader();
    if (!Preheader) break;

    // Ok, move up a level.
    Builder.SetInsertPoint(Preheader, Preheader->getTerminator());
  }

  // Insert a pretty getelementptr. Note that this GEP is not marked inbounds,
  // because ScalarEvolution may have changed the address arithmetic to
  // compute a value which is beyond the end of the allocated object.
  Value *Casted = V;
  if (V->getType() != PTy)
    Casted = InsertNoopCastOfTo(Casted, PTy);
  Value *GEP = Builder.CreateGEP(Casted,
                                 GepIndices,
                                 "scevgep");
  Ops.push_back(SE.getUnknown(GEP));
  rememberInstruction(GEP);

  // Restore the original insert point.
  if (SaveInsertBB)
    restoreInsertPoint(SaveInsertBB, SaveInsertPt);

  return expand(SE.getAddExpr(Ops));
}

/// PickMostRelevantLoop - Given two loops pick the one that's most relevant for
/// SCEV expansion. If they are nested, this is the most nested. If they are
/// neighboring, pick the later.
static const Loop *PickMostRelevantLoop(const Loop *A, const Loop *B,
                                        DominatorTree &DT) {
  if (!A) return B;
  if (!B) return A;
  if (A->contains(B)) return B;
  if (B->contains(A)) return A;
  if (DT.dominates(A->getHeader(), B->getHeader())) return B;
  if (DT.dominates(B->getHeader(), A->getHeader())) return A;
  return A; // Arbitrarily break the tie.
}

/// getRelevantLoop - Get the most relevant loop associated with the given
/// expression, according to PickMostRelevantLoop.
const Loop *SCEVExpander::getRelevantLoop(const SCEV *S) {
  // Test whether we've already computed the most relevant loop for this SCEV.
  std::pair<DenseMap<const SCEV *, const Loop *>::iterator, bool> Pair =
    RelevantLoops.insert(std::make_pair(S, static_cast<const Loop *>(0)));
  if (!Pair.second)
    return Pair.first->second;

  if (isa<SCEVConstant>(S))
    // A constant has no relevant loops.
    return 0;
  if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    if (const Instruction *I = dyn_cast<Instruction>(U->getValue()))
      return Pair.first->second = SE.LI->getLoopFor(I->getParent());
    // A non-instruction has no relevant loops.
    return 0;
  }
  if (const SCEVNAryExpr *N = dyn_cast<SCEVNAryExpr>(S)) {
    const Loop *L = 0;
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S))
      L = AR->getLoop();
    for (SCEVNAryExpr::op_iterator I = N->op_begin(), E = N->op_end();
         I != E; ++I)
      L = PickMostRelevantLoop(L, getRelevantLoop(*I), *SE.DT);
    return RelevantLoops[N] = L;
  }
  if (const SCEVCastExpr *C = dyn_cast<SCEVCastExpr>(S)) {
    const Loop *Result = getRelevantLoop(C->getOperand());
    return RelevantLoops[C] = Result;
  }
  if (const SCEVUDivExpr *D = dyn_cast<SCEVUDivExpr>(S)) {
    const Loop *Result =
      PickMostRelevantLoop(getRelevantLoop(D->getLHS()),
                           getRelevantLoop(D->getRHS()),
                           *SE.DT);
    return RelevantLoops[D] = Result;
  }
  llvm_unreachable("Unexpected SCEV type!");
}

namespace {

/// LoopCompare - Compare loops by PickMostRelevantLoop.
class LoopCompare {
  DominatorTree &DT;
public:
  explicit LoopCompare(DominatorTree &dt) : DT(dt) {}

  bool operator()(std::pair<const Loop *, const SCEV *> LHS,
                  std::pair<const Loop *, const SCEV *> RHS) const {
    // Keep pointer operands sorted at the end.
    if (LHS.second->getType()->isPointerTy() !=
        RHS.second->getType()->isPointerTy())
      return LHS.second->getType()->isPointerTy();

    // Compare loops with PickMostRelevantLoop.
    if (LHS.first != RHS.first)
      return PickMostRelevantLoop(LHS.first, RHS.first, DT) != LHS.first;

    // If one operand is a non-constant negative and the other is not,
    // put the non-constant negative on the right so that a sub can
    // be used instead of a negate and add.
    if (LHS.second->isNonConstantNegative()) {
      if (!RHS.second->isNonConstantNegative())
        return false;
    } else if (RHS.second->isNonConstantNegative())
      return true;

    // Otherwise they are equivalent according to this comparison.
    return false;
  }
};

}

Value *SCEVExpander::visitAddExpr(const SCEVAddExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());

  // Collect all the add operands in a loop, along with their associated loops.
  // Iterate in reverse so that constants are emitted last, all else equal, and
  // so that pointer operands are inserted first, which the code below relies on
  // to form more involved GEPs.
  SmallVector<std::pair<const Loop *, const SCEV *>, 8> OpsAndLoops;
  for (std::reverse_iterator<SCEVAddExpr::op_iterator> I(S->op_end()),
       E(S->op_begin()); I != E; ++I)
    OpsAndLoops.push_back(std::make_pair(getRelevantLoop(*I), *I));

  // Sort by loop. Use a stable sort so that constants follow non-constants and
  // pointer operands precede non-pointer operands.
  std::stable_sort(OpsAndLoops.begin(), OpsAndLoops.end(), LoopCompare(*SE.DT));

  // Emit instructions to add all the operands. Hoist as much as possible
  // out of loops, and form meaningful getelementptrs where possible.
  Value *Sum = 0;
  for (SmallVectorImpl<std::pair<const Loop *, const SCEV *> >::iterator
       I = OpsAndLoops.begin(), E = OpsAndLoops.end(); I != E; ) {
    const Loop *CurLoop = I->first;
    const SCEV *Op = I->second;
    if (!Sum) {
      // This is the first operand. Just expand it.
      Sum = expand(Op);
      ++I;
    } else if (PointerType *PTy = dyn_cast<PointerType>(Sum->getType())) {
      // The running sum expression is a pointer. Try to form a getelementptr
      // at this level with that as the base.
      SmallVector<const SCEV *, 4> NewOps;
      for (; I != E && I->first == CurLoop; ++I) {
        // If the operand is SCEVUnknown and not instructions, peek through
        // it, to enable more of it to be folded into the GEP.
        const SCEV *X = I->second;
        if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(X))
          if (!isa<Instruction>(U->getValue()))
            X = SE.getSCEV(U->getValue());
        NewOps.push_back(X);
      }
      Sum = expandAddToGEP(NewOps.begin(), NewOps.end(), PTy, Ty, Sum);
    } else if (PointerType *PTy = dyn_cast<PointerType>(Op->getType())) {
      // The running sum is an integer, and there's a pointer at this level.
      // Try to form a getelementptr. If the running sum is instructions,
      // use a SCEVUnknown to avoid re-analyzing them.
      SmallVector<const SCEV *, 4> NewOps;
      NewOps.push_back(isa<Instruction>(Sum) ? SE.getUnknown(Sum) :
                                               SE.getSCEV(Sum));
      for (++I; I != E && I->first == CurLoop; ++I)
        NewOps.push_back(I->second);
      Sum = expandAddToGEP(NewOps.begin(), NewOps.end(), PTy, Ty, expand(Op));
    } else if (Op->isNonConstantNegative()) {
      // Instead of doing a negate and add, just do a subtract.
      Value *W = expandCodeFor(SE.getNegativeSCEV(Op), Ty);
      Sum = InsertNoopCastOfTo(Sum, Ty);
      Sum = InsertBinop(Instruction::Sub, Sum, W);
      ++I;
    } else {
      // A simple add.
      Value *W = expandCodeFor(Op, Ty);
      Sum = InsertNoopCastOfTo(Sum, Ty);
      // Canonicalize a constant to the RHS.
      if (isa<Constant>(Sum)) std::swap(Sum, W);
      Sum = InsertBinop(Instruction::Add, Sum, W);
      ++I;
    }
  }

  return Sum;
}

Value *SCEVExpander::visitMulExpr(const SCEVMulExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());

  // Collect all the mul operands in a loop, along with their associated loops.
  // Iterate in reverse so that constants are emitted last, all else equal.
  SmallVector<std::pair<const Loop *, const SCEV *>, 8> OpsAndLoops;
  for (std::reverse_iterator<SCEVMulExpr::op_iterator> I(S->op_end()),
       E(S->op_begin()); I != E; ++I)
    OpsAndLoops.push_back(std::make_pair(getRelevantLoop(*I), *I));

  // Sort by loop. Use a stable sort so that constants follow non-constants.
  std::stable_sort(OpsAndLoops.begin(), OpsAndLoops.end(), LoopCompare(*SE.DT));

  // Emit instructions to mul all the operands. Hoist as much as possible
  // out of loops.
  Value *Prod = 0;
  for (SmallVectorImpl<std::pair<const Loop *, const SCEV *> >::iterator
       I = OpsAndLoops.begin(), E = OpsAndLoops.end(); I != E; ) {
    const SCEV *Op = I->second;
    if (!Prod) {
      // This is the first operand. Just expand it.
      Prod = expand(Op);
      ++I;
    } else if (Op->isAllOnesValue()) {
      // Instead of doing a multiply by negative one, just do a negate.
      Prod = InsertNoopCastOfTo(Prod, Ty);
      Prod = InsertBinop(Instruction::Sub, Constant::getNullValue(Ty), Prod);
      ++I;
    } else {
      // A simple mul.
      Value *W = expandCodeFor(Op, Ty);
      Prod = InsertNoopCastOfTo(Prod, Ty);
      // Canonicalize a constant to the RHS.
      if (isa<Constant>(Prod)) std::swap(Prod, W);
      Prod = InsertBinop(Instruction::Mul, Prod, W);
      ++I;
    }
  }

  return Prod;
}

Value *SCEVExpander::visitUDivExpr(const SCEVUDivExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());

  Value *LHS = expandCodeFor(S->getLHS(), Ty);
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(S->getRHS())) {
    const APInt &RHS = SC->getValue()->getValue();
    if (RHS.isPowerOf2())
      return InsertBinop(Instruction::LShr, LHS,
                         ConstantInt::get(Ty, RHS.logBase2()));
  }

  Value *RHS = expandCodeFor(S->getRHS(), Ty);
  return InsertBinop(Instruction::UDiv, LHS, RHS);
}

/// Move parts of Base into Rest to leave Base with the minimal
/// expression that provides a pointer operand suitable for a
/// GEP expansion.
static void ExposePointerBase(const SCEV *&Base, const SCEV *&Rest,
                              ScalarEvolution &SE) {
  while (const SCEVAddRecExpr *A = dyn_cast<SCEVAddRecExpr>(Base)) {
    Base = A->getStart();
    Rest = SE.getAddExpr(Rest,
                         SE.getAddRecExpr(SE.getConstant(A->getType(), 0),
                                          A->getStepRecurrence(SE),
                                          A->getLoop(),
                                          A->getNoWrapFlags(SCEV::FlagNW)));
  }
  if (const SCEVAddExpr *A = dyn_cast<SCEVAddExpr>(Base)) {
    Base = A->getOperand(A->getNumOperands()-1);
    SmallVector<const SCEV *, 8> NewAddOps(A->op_begin(), A->op_end());
    NewAddOps.back() = Rest;
    Rest = SE.getAddExpr(NewAddOps);
    ExposePointerBase(Base, Rest, SE);
  }
}

/// Determine if this is a well-behaved chain of instructions leading back to
/// the PHI. If so, it may be reused by expanded expressions.
bool SCEVExpander::isNormalAddRecExprPHI(PHINode *PN, Instruction *IncV,
                                         const Loop *L) {
  if (IncV->getNumOperands() == 0 || isa<PHINode>(IncV) ||
      (isa<CastInst>(IncV) && !isa<BitCastInst>(IncV)))
    return false;
  // If any of the operands don't dominate the insert position, bail.
  // Addrec operands are always loop-invariant, so this can only happen
  // if there are instructions which haven't been hoisted.
  if (L == IVIncInsertLoop) {
    for (User::op_iterator OI = IncV->op_begin()+1,
           OE = IncV->op_end(); OI != OE; ++OI)
      if (Instruction *OInst = dyn_cast<Instruction>(OI))
        if (!SE.DT->dominates(OInst, IVIncInsertPos))
          return false;
  }
  // Advance to the next instruction.
  IncV = dyn_cast<Instruction>(IncV->getOperand(0));
  if (!IncV)
    return false;

  if (IncV->mayHaveSideEffects())
    return false;

  if (IncV != PN)
    return true;

  return isNormalAddRecExprPHI(PN, IncV, L);
}

/// getIVIncOperand returns an induction variable increment's induction
/// variable operand.
///
/// If allowScale is set, any type of GEP is allowed as long as the nonIV
/// operands dominate InsertPos.
///
/// If allowScale is not set, ensure that a GEP increment conforms to one of the
/// simple patterns generated by getAddRecExprPHILiterally and
/// expandAddtoGEP. If the pattern isn't recognized, return NULL.
Instruction *SCEVExpander::getIVIncOperand(Instruction *IncV,
                                           Instruction *InsertPos,
                                           bool allowScale) {
  if (IncV == InsertPos)
    return NULL;

  switch (IncV->getOpcode()) {
  default:
    return NULL;
  // Check for a simple Add/Sub or GEP of a loop invariant step.
  case Instruction::Add:
  case Instruction::Sub: {
    Instruction *OInst = dyn_cast<Instruction>(IncV->getOperand(1));
    if (!OInst || SE.DT->dominates(OInst, InsertPos))
      return dyn_cast<Instruction>(IncV->getOperand(0));
    return NULL;
  }
  case Instruction::BitCast:
    return dyn_cast<Instruction>(IncV->getOperand(0));
  case Instruction::GetElementPtr:
    for (Instruction::op_iterator I = IncV->op_begin()+1, E = IncV->op_end();
         I != E; ++I) {
      if (isa<Constant>(*I))
        continue;
      if (Instruction *OInst = dyn_cast<Instruction>(*I)) {
        if (!SE.DT->dominates(OInst, InsertPos))
          return NULL;
      }
      if (allowScale) {
        // allow any kind of GEP as long as it can be hoisted.
        continue;
      }
      // This must be a pointer addition of constants (pretty), which is already
      // handled, or some number of address-size elements (ugly). Ugly geps
      // have 2 operands. i1* is used by the expander to represent an
      // address-size element.
      if (IncV->getNumOperands() != 2)
        return NULL;
      unsigned AS = cast<PointerType>(IncV->getType())->getAddressSpace();
      if (IncV->getType() != Type::getInt1PtrTy(SE.getContext(), AS)
          && IncV->getType() != Type::getInt8PtrTy(SE.getContext(), AS))
        return NULL;
      break;
    }
    return dyn_cast<Instruction>(IncV->getOperand(0));
  }
}

/// hoistStep - Attempt to hoist a simple IV increment above InsertPos to make
/// it available to other uses in this loop. Recursively hoist any operands,
/// until we reach a value that dominates InsertPos.
bool SCEVExpander::hoistIVInc(Instruction *IncV, Instruction *InsertPos) {
  if (SE.DT->dominates(IncV, InsertPos))
      return true;

  // InsertPos must itself dominate IncV so that IncV's new position satisfies
  // its existing users.
  if (isa<PHINode>(InsertPos)
      || !SE.DT->dominates(InsertPos->getParent(), IncV->getParent()))
    return false;

  // Check that the chain of IV operands leading back to Phi can be hoisted.
  SmallVector<Instruction*, 4> IVIncs;
  for(;;) {
    Instruction *Oper = getIVIncOperand(IncV, InsertPos, /*allowScale*/true);
    if (!Oper)
      return false;
    // IncV is safe to hoist.
    IVIncs.push_back(IncV);
    IncV = Oper;
    if (SE.DT->dominates(IncV, InsertPos))
      break;
  }
  for (SmallVectorImpl<Instruction*>::reverse_iterator I = IVIncs.rbegin(),
         E = IVIncs.rend(); I != E; ++I) {
    (*I)->moveBefore(InsertPos);
  }
  return true;
}

/// Determine if this cyclic phi is in a form that would have been generated by
/// LSR. We don't care if the phi was actually expanded in this pass, as long
/// as it is in a low-cost form, for example, no implied multiplication. This
/// should match any patterns generated by getAddRecExprPHILiterally and
/// expandAddtoGEP.
bool SCEVExpander::isExpandedAddRecExprPHI(PHINode *PN, Instruction *IncV,
                                           const Loop *L) {
  for(Instruction *IVOper = IncV;
      (IVOper = getIVIncOperand(IVOper, L->getLoopPreheader()->getTerminator(),
                                /*allowScale=*/false));) {
    if (IVOper == PN)
      return true;
  }
  return false;
}

/// expandIVInc - Expand an IV increment at Builder's current InsertPos.
/// Typically this is the LatchBlock terminator or IVIncInsertPos, but we may
/// need to materialize IV increments elsewhere to handle difficult situations.
Value *SCEVExpander::expandIVInc(PHINode *PN, Value *StepV, const Loop *L,
                                 Type *ExpandTy, Type *IntTy,
                                 bool useSubtract) {
  Value *IncV;
  // If the PHI is a pointer, use a GEP, otherwise use an add or sub.
  if (ExpandTy->isPointerTy()) {
    PointerType *GEPPtrTy = cast<PointerType>(ExpandTy);
    // If the step isn't constant, don't use an implicitly scaled GEP, because
    // that would require a multiply inside the loop.
    if (!isa<ConstantInt>(StepV))
      GEPPtrTy = PointerType::get(Type::getInt1Ty(SE.getContext()),
                                  GEPPtrTy->getAddressSpace());
    const SCEV *const StepArray[1] = { SE.getSCEV(StepV) };
    IncV = expandAddToGEP(StepArray, StepArray+1, GEPPtrTy, IntTy, PN);
    if (IncV->getType() != PN->getType()) {
      IncV = Builder.CreateBitCast(IncV, PN->getType());
      rememberInstruction(IncV);
    }
  } else {
    IncV = useSubtract ?
      Builder.CreateSub(PN, StepV, Twine(IVName) + ".iv.next") :
      Builder.CreateAdd(PN, StepV, Twine(IVName) + ".iv.next");
    rememberInstruction(IncV);
  }
  return IncV;
}

/// getAddRecExprPHILiterally - Helper for expandAddRecExprLiterally. Expand
/// the base addrec, which is the addrec without any non-loop-dominating
/// values, and return the PHI.
PHINode *
SCEVExpander::getAddRecExprPHILiterally(const SCEVAddRecExpr *Normalized,
                                        const Loop *L,
                                        Type *ExpandTy,
                                        Type *IntTy) {
  assert((!IVIncInsertLoop||IVIncInsertPos) && "Uninitialized insert position");

  // Reuse a previously-inserted PHI, if present.
  BasicBlock *LatchBlock = L->getLoopLatch();
  if (LatchBlock) {
    for (BasicBlock::iterator I = L->getHeader()->begin();
         PHINode *PN = dyn_cast<PHINode>(I); ++I) {
      if (!SE.isSCEVable(PN->getType()) ||
          (SE.getEffectiveSCEVType(PN->getType()) !=
           SE.getEffectiveSCEVType(Normalized->getType())) ||
          SE.getSCEV(PN) != Normalized)
        continue;

      Instruction *IncV =
        cast<Instruction>(PN->getIncomingValueForBlock(LatchBlock));

      if (LSRMode) {
        if (!isExpandedAddRecExprPHI(PN, IncV, L))
          continue;
        if (L == IVIncInsertLoop && !hoistIVInc(IncV, IVIncInsertPos))
          continue;
      }
      else {
        if (!isNormalAddRecExprPHI(PN, IncV, L))
          continue;
        if (L == IVIncInsertLoop)
          do {
            if (SE.DT->dominates(IncV, IVIncInsertPos))
              break;
            // Make sure the increment is where we want it. But don't move it
            // down past a potential existing post-inc user.
            IncV->moveBefore(IVIncInsertPos);
            IVIncInsertPos = IncV;
            IncV = cast<Instruction>(IncV->getOperand(0));
          } while (IncV != PN);
      }
      // Ok, the add recurrence looks usable.
      // Remember this PHI, even in post-inc mode.
      InsertedValues.insert(PN);
      // Remember the increment.
      rememberInstruction(IncV);
      return PN;
    }
  }

  // Save the original insertion point so we can restore it when we're done.
  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();

  // Another AddRec may need to be recursively expanded below. For example, if
  // this AddRec is quadratic, the StepV may itself be an AddRec in this
  // loop. Remove this loop from the PostIncLoops set before expanding such
  // AddRecs. Otherwise, we cannot find a valid position for the step
  // (i.e. StepV can never dominate its loop header).  Ideally, we could do
  // SavedIncLoops.swap(PostIncLoops), but we generally have a single element,
  // so it's not worth implementing SmallPtrSet::swap.
  PostIncLoopSet SavedPostIncLoops = PostIncLoops;
  PostIncLoops.clear();

  // Expand code for the start value.
  Value *StartV = expandCodeFor(Normalized->getStart(), ExpandTy,
                                L->getHeader()->begin());

  // StartV must be hoisted into L's preheader to dominate the new phi.
  assert(!isa<Instruction>(StartV) ||
         SE.DT->properlyDominates(cast<Instruction>(StartV)->getParent(),
                                  L->getHeader()));

  // Expand code for the step value. Do this before creating the PHI so that PHI
  // reuse code doesn't see an incomplete PHI.
  const SCEV *Step = Normalized->getStepRecurrence(SE);
  // If the stride is negative, insert a sub instead of an add for the increment
  // (unless it's a constant, because subtracts of constants are canonicalized
  // to adds).
  bool useSubtract = !ExpandTy->isPointerTy() && Step->isNonConstantNegative();
  if (useSubtract)
    Step = SE.getNegativeSCEV(Step);
  // Expand the step somewhere that dominates the loop header.
  Value *StepV = expandCodeFor(Step, IntTy, L->getHeader()->begin());

  // Create the PHI.
  BasicBlock *Header = L->getHeader();
  Builder.SetInsertPoint(Header, Header->begin());
  pred_iterator HPB = pred_begin(Header), HPE = pred_end(Header);
  PHINode *PN = Builder.CreatePHI(ExpandTy, std::distance(HPB, HPE),
                                  Twine(IVName) + ".iv");
  rememberInstruction(PN);

  // Create the step instructions and populate the PHI.
  for (pred_iterator HPI = HPB; HPI != HPE; ++HPI) {
    BasicBlock *Pred = *HPI;

    // Add a start value.
    if (!L->contains(Pred)) {
      PN->addIncoming(StartV, Pred);
      continue;
    }

    // Create a step value and add it to the PHI.
    // If IVIncInsertLoop is non-null and equal to the addrec's loop, insert the
    // instructions at IVIncInsertPos.
    Instruction *InsertPos = L == IVIncInsertLoop ?
      IVIncInsertPos : Pred->getTerminator();
    Builder.SetInsertPoint(InsertPos);
    Value *IncV = expandIVInc(PN, StepV, L, ExpandTy, IntTy, useSubtract);
    if (isa<OverflowingBinaryOperator>(IncV)) {
      if (Normalized->getNoWrapFlags(SCEV::FlagNUW))
        cast<BinaryOperator>(IncV)->setHasNoUnsignedWrap();
      if (Normalized->getNoWrapFlags(SCEV::FlagNSW))
        cast<BinaryOperator>(IncV)->setHasNoSignedWrap();
    }
    PN->addIncoming(IncV, Pred);
  }

  // Restore the original insert point.
  if (SaveInsertBB)
    restoreInsertPoint(SaveInsertBB, SaveInsertPt);

  // After expanding subexpressions, restore the PostIncLoops set so the caller
  // can ensure that IVIncrement dominates the current uses.
  PostIncLoops = SavedPostIncLoops;

  // Remember this PHI, even in post-inc mode.
  InsertedValues.insert(PN);

  return PN;
}

Value *SCEVExpander::expandAddRecExprLiterally(const SCEVAddRecExpr *S) {
  Type *STy = S->getType();
  Type *IntTy = SE.getEffectiveSCEVType(STy);
  const Loop *L = S->getLoop();

  // Determine a normalized form of this expression, which is the expression
  // before any post-inc adjustment is made.
  const SCEVAddRecExpr *Normalized = S;
  if (PostIncLoops.count(L)) {
    PostIncLoopSet Loops;
    Loops.insert(L);
    Normalized =
      cast<SCEVAddRecExpr>(TransformForPostIncUse(Normalize, S, 0, 0,
                                                  Loops, SE, *SE.DT));
  }

  // Strip off any non-loop-dominating component from the addrec start.
  const SCEV *Start = Normalized->getStart();
  const SCEV *PostLoopOffset = 0;
  if (!SE.properlyDominates(Start, L->getHeader())) {
    PostLoopOffset = Start;
    Start = SE.getConstant(Normalized->getType(), 0);
    Normalized = cast<SCEVAddRecExpr>(
      SE.getAddRecExpr(Start, Normalized->getStepRecurrence(SE),
                       Normalized->getLoop(),
                       Normalized->getNoWrapFlags(SCEV::FlagNW)));
  }

  // Strip off any non-loop-dominating component from the addrec step.
  const SCEV *Step = Normalized->getStepRecurrence(SE);
  const SCEV *PostLoopScale = 0;
  if (!SE.dominates(Step, L->getHeader())) {
    PostLoopScale = Step;
    Step = SE.getConstant(Normalized->getType(), 1);
    Normalized =
      cast<SCEVAddRecExpr>(SE.getAddRecExpr(
                             Start, Step, Normalized->getLoop(),
                             Normalized->getNoWrapFlags(SCEV::FlagNW)));
  }

  // Expand the core addrec. If we need post-loop scaling, force it to
  // expand to an integer type to avoid the need for additional casting.
  Type *ExpandTy = PostLoopScale ? IntTy : STy;
  PHINode *PN = getAddRecExprPHILiterally(Normalized, L, ExpandTy, IntTy);

  // Accommodate post-inc mode, if necessary.
  Value *Result;
  if (!PostIncLoops.count(L))
    Result = PN;
  else {
    // In PostInc mode, use the post-incremented value.
    BasicBlock *LatchBlock = L->getLoopLatch();
    assert(LatchBlock && "PostInc mode requires a unique loop latch!");
    Result = PN->getIncomingValueForBlock(LatchBlock);

    // For an expansion to use the postinc form, the client must call
    // expandCodeFor with an InsertPoint that is either outside the PostIncLoop
    // or dominated by IVIncInsertPos.
    if (isa<Instruction>(Result)
        && !SE.DT->dominates(cast<Instruction>(Result),
                             Builder.GetInsertPoint())) {
      // The induction variable's postinc expansion does not dominate this use.
      // IVUsers tries to prevent this case, so it is rare. However, it can
      // happen when an IVUser outside the loop is not dominated by the latch
      // block. Adjusting IVIncInsertPos before expansion begins cannot handle
      // all cases. Consider a phi outide whose operand is replaced during
      // expansion with the value of the postinc user. Without fundamentally
      // changing the way postinc users are tracked, the only remedy is
      // inserting an extra IV increment. StepV might fold into PostLoopOffset,
      // but hopefully expandCodeFor handles that.
      bool useSubtract =
        !ExpandTy->isPointerTy() && Step->isNonConstantNegative();
      if (useSubtract)
        Step = SE.getNegativeSCEV(Step);
      // Expand the step somewhere that dominates the loop header.
      BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
      BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();
      Value *StepV = expandCodeFor(Step, IntTy, L->getHeader()->begin());
      // Restore the insertion point to the place where the caller has
      // determined dominates all uses.
      restoreInsertPoint(SaveInsertBB, SaveInsertPt);
      Result = expandIVInc(PN, StepV, L, ExpandTy, IntTy, useSubtract);
    }
  }

  // Re-apply any non-loop-dominating scale.
  if (PostLoopScale) {
    Result = InsertNoopCastOfTo(Result, IntTy);
    Result = Builder.CreateMul(Result,
                               expandCodeFor(PostLoopScale, IntTy));
    rememberInstruction(Result);
  }

  // Re-apply any non-loop-dominating offset.
  if (PostLoopOffset) {
    if (PointerType *PTy = dyn_cast<PointerType>(ExpandTy)) {
      const SCEV *const OffsetArray[1] = { PostLoopOffset };
      Result = expandAddToGEP(OffsetArray, OffsetArray+1, PTy, IntTy, Result);
    } else {
      Result = InsertNoopCastOfTo(Result, IntTy);
      Result = Builder.CreateAdd(Result,
                                 expandCodeFor(PostLoopOffset, IntTy));
      rememberInstruction(Result);
    }
  }

  return Result;
}

Value *SCEVExpander::visitAddRecExpr(const SCEVAddRecExpr *S) {
  if (!CanonicalMode) return expandAddRecExprLiterally(S);

  Type *Ty = SE.getEffectiveSCEVType(S->getType());
  const Loop *L = S->getLoop();

  // First check for an existing canonical IV in a suitable type.
  PHINode *CanonicalIV = 0;
  if (PHINode *PN = L->getCanonicalInductionVariable())
    if (SE.getTypeSizeInBits(PN->getType()) >= SE.getTypeSizeInBits(Ty))
      CanonicalIV = PN;

  // Rewrite an AddRec in terms of the canonical induction variable, if
  // its type is more narrow.
  if (CanonicalIV &&
      SE.getTypeSizeInBits(CanonicalIV->getType()) >
      SE.getTypeSizeInBits(Ty)) {
    SmallVector<const SCEV *, 4> NewOps(S->getNumOperands());
    for (unsigned i = 0, e = S->getNumOperands(); i != e; ++i)
      NewOps[i] = SE.getAnyExtendExpr(S->op_begin()[i], CanonicalIV->getType());
    Value *V = expand(SE.getAddRecExpr(NewOps, S->getLoop(),
                                       S->getNoWrapFlags(SCEV::FlagNW)));
    BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
    BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();
    BasicBlock::iterator NewInsertPt =
      llvm::next(BasicBlock::iterator(cast<Instruction>(V)));
    while (isa<PHINode>(NewInsertPt) || isa<DbgInfoIntrinsic>(NewInsertPt) ||
           isa<LandingPadInst>(NewInsertPt))
      ++NewInsertPt;
    V = expandCodeFor(SE.getTruncateExpr(SE.getUnknown(V), Ty), 0,
                      NewInsertPt);
    restoreInsertPoint(SaveInsertBB, SaveInsertPt);
    return V;
  }

  // {X,+,F} --> X + {0,+,F}
  if (!S->getStart()->isZero()) {
    SmallVector<const SCEV *, 4> NewOps(S->op_begin(), S->op_end());
    NewOps[0] = SE.getConstant(Ty, 0);
    const SCEV *Rest = SE.getAddRecExpr(NewOps, L,
                                        S->getNoWrapFlags(SCEV::FlagNW));

    // Turn things like ptrtoint+arithmetic+inttoptr into GEP. See the
    // comments on expandAddToGEP for details.
    const SCEV *Base = S->getStart();
    const SCEV *RestArray[1] = { Rest };
    // Dig into the expression to find the pointer base for a GEP.
    ExposePointerBase(Base, RestArray[0], SE);
    // If we found a pointer, expand the AddRec with a GEP.
    if (PointerType *PTy = dyn_cast<PointerType>(Base->getType())) {
      // Make sure the Base isn't something exotic, such as a multiplied
      // or divided pointer value. In those cases, the result type isn't
      // actually a pointer type.
      if (!isa<SCEVMulExpr>(Base) && !isa<SCEVUDivExpr>(Base)) {
        Value *StartV = expand(Base);
        assert(StartV->getType() == PTy && "Pointer type mismatch for GEP!");
        return expandAddToGEP(RestArray, RestArray+1, PTy, Ty, StartV);
      }
    }

    // Just do a normal add. Pre-expand the operands to suppress folding.
    return expand(SE.getAddExpr(SE.getUnknown(expand(S->getStart())),
                                SE.getUnknown(expand(Rest))));
  }

  // If we don't yet have a canonical IV, create one.
  if (!CanonicalIV) {
    // Create and insert the PHI node for the induction variable in the
    // specified loop.
    BasicBlock *Header = L->getHeader();
    pred_iterator HPB = pred_begin(Header), HPE = pred_end(Header);
    CanonicalIV = PHINode::Create(Ty, std::distance(HPB, HPE), "indvar",
                                  Header->begin());
    rememberInstruction(CanonicalIV);

    Constant *One = ConstantInt::get(Ty, 1);
    for (pred_iterator HPI = HPB; HPI != HPE; ++HPI) {
      BasicBlock *HP = *HPI;
      if (L->contains(HP)) {
        // Insert a unit add instruction right before the terminator
        // corresponding to the back-edge.
        Instruction *Add = BinaryOperator::CreateAdd(CanonicalIV, One,
                                                     "indvar.next",
                                                     HP->getTerminator());
        Add->setDebugLoc(HP->getTerminator()->getDebugLoc());
        rememberInstruction(Add);
        CanonicalIV->addIncoming(Add, HP);
      } else {
        CanonicalIV->addIncoming(Constant::getNullValue(Ty), HP);
      }
    }
  }

  // {0,+,1} --> Insert a canonical induction variable into the loop!
  if (S->isAffine() && S->getOperand(1)->isOne()) {
    assert(Ty == SE.getEffectiveSCEVType(CanonicalIV->getType()) &&
           "IVs with types different from the canonical IV should "
           "already have been handled!");
    return CanonicalIV;
  }

  // {0,+,F} --> {0,+,1} * F

  // If this is a simple linear addrec, emit it now as a special case.
  if (S->isAffine())    // {0,+,F} --> i*F
    return
      expand(SE.getTruncateOrNoop(
        SE.getMulExpr(SE.getUnknown(CanonicalIV),
                      SE.getNoopOrAnyExtend(S->getOperand(1),
                                            CanonicalIV->getType())),
        Ty));

  // If this is a chain of recurrences, turn it into a closed form, using the
  // folders, then expandCodeFor the closed form.  This allows the folders to
  // simplify the expression without having to build a bunch of special code
  // into this folder.
  const SCEV *IH = SE.getUnknown(CanonicalIV);   // Get I as a "symbolic" SCEV.

  // Promote S up to the canonical IV type, if the cast is foldable.
  const SCEV *NewS = S;
  const SCEV *Ext = SE.getNoopOrAnyExtend(S, CanonicalIV->getType());
  if (isa<SCEVAddRecExpr>(Ext))
    NewS = Ext;

  const SCEV *V = cast<SCEVAddRecExpr>(NewS)->evaluateAtIteration(IH, SE);
  //cerr << "Evaluated: " << *this << "\n     to: " << *V << "\n";

  // Truncate the result down to the original type, if needed.
  const SCEV *T = SE.getTruncateOrNoop(V, Ty);
  return expand(T);
}

Value *SCEVExpander::visitTruncateExpr(const SCEVTruncateExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());
  Value *V = expandCodeFor(S->getOperand(),
                           SE.getEffectiveSCEVType(S->getOperand()->getType()));
  Value *I = Builder.CreateTrunc(V, Ty);
  rememberInstruction(I);
  return I;
}

Value *SCEVExpander::visitZeroExtendExpr(const SCEVZeroExtendExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());
  Value *V = expandCodeFor(S->getOperand(),
                           SE.getEffectiveSCEVType(S->getOperand()->getType()));
  Value *I = Builder.CreateZExt(V, Ty);
  rememberInstruction(I);
  return I;
}

Value *SCEVExpander::visitSignExtendExpr(const SCEVSignExtendExpr *S) {
  Type *Ty = SE.getEffectiveSCEVType(S->getType());
  Value *V = expandCodeFor(S->getOperand(),
                           SE.getEffectiveSCEVType(S->getOperand()->getType()));
  Value *I = Builder.CreateSExt(V, Ty);
  rememberInstruction(I);
  return I;
}

Value *SCEVExpander::visitSMaxExpr(const SCEVSMaxExpr *S) {
  Value *LHS = expand(S->getOperand(S->getNumOperands()-1));
  Type *Ty = LHS->getType();
  for (int i = S->getNumOperands()-2; i >= 0; --i) {
    // In the case of mixed integer and pointer types, do the
    // rest of the comparisons as integer.
    if (S->getOperand(i)->getType() != Ty) {
      Ty = SE.getEffectiveSCEVType(Ty);
      LHS = InsertNoopCastOfTo(LHS, Ty);
    }
    Value *RHS = expandCodeFor(S->getOperand(i), Ty);
    Value *ICmp = Builder.CreateICmpSGT(LHS, RHS);
    rememberInstruction(ICmp);
    Value *Sel = Builder.CreateSelect(ICmp, LHS, RHS, "smax");
    rememberInstruction(Sel);
    LHS = Sel;
  }
  // In the case of mixed integer and pointer types, cast the
  // final result back to the pointer type.
  if (LHS->getType() != S->getType())
    LHS = InsertNoopCastOfTo(LHS, S->getType());
  return LHS;
}

Value *SCEVExpander::visitUMaxExpr(const SCEVUMaxExpr *S) {
  Value *LHS = expand(S->getOperand(S->getNumOperands()-1));
  Type *Ty = LHS->getType();
  for (int i = S->getNumOperands()-2; i >= 0; --i) {
    // In the case of mixed integer and pointer types, do the
    // rest of the comparisons as integer.
    if (S->getOperand(i)->getType() != Ty) {
      Ty = SE.getEffectiveSCEVType(Ty);
      LHS = InsertNoopCastOfTo(LHS, Ty);
    }
    Value *RHS = expandCodeFor(S->getOperand(i), Ty);
    Value *ICmp = Builder.CreateICmpUGT(LHS, RHS);
    rememberInstruction(ICmp);
    Value *Sel = Builder.CreateSelect(ICmp, LHS, RHS, "umax");
    rememberInstruction(Sel);
    LHS = Sel;
  }
  // In the case of mixed integer and pointer types, cast the
  // final result back to the pointer type.
  if (LHS->getType() != S->getType())
    LHS = InsertNoopCastOfTo(LHS, S->getType());
  return LHS;
}

Value *SCEVExpander::expandCodeFor(const SCEV *SH, Type *Ty,
                                   Instruction *IP) {
  Builder.SetInsertPoint(IP->getParent(), IP);
  return expandCodeFor(SH, Ty);
}

Value *SCEVExpander::expandCodeFor(const SCEV *SH, Type *Ty) {
  // Expand the code for this SCEV.
  Value *V = expand(SH);
  if (Ty) {
    assert(SE.getTypeSizeInBits(Ty) == SE.getTypeSizeInBits(SH->getType()) &&
           "non-trivial casts should be done with the SCEVs directly!");
    V = InsertNoopCastOfTo(V, Ty);
  }
  return V;
}

Value *SCEVExpander::expand(const SCEV *S) {
  // Compute an insertion point for this SCEV object. Hoist the instructions
  // as far out in the loop nest as possible.
  Instruction *InsertPt = Builder.GetInsertPoint();
  for (Loop *L = SE.LI->getLoopFor(Builder.GetInsertBlock()); ;
       L = L->getParentLoop())
    if (SE.isLoopInvariant(S, L)) {
      if (!L) break;
      if (BasicBlock *Preheader = L->getLoopPreheader())
        InsertPt = Preheader->getTerminator();
      else {
        // LSR sets the insertion point for AddRec start/step values to the
        // block start to simplify value reuse, even though it's an invalid
        // position. SCEVExpander must correct for this in all cases.
        InsertPt = L->getHeader()->getFirstInsertionPt();
      }
    } else {
      // If the SCEV is computable at this level, insert it into the header
      // after the PHIs (and after any other instructions that we've inserted
      // there) so that it is guaranteed to dominate any user inside the loop.
      if (L && SE.hasComputableLoopEvolution(S, L) && !PostIncLoops.count(L))
        InsertPt = L->getHeader()->getFirstInsertionPt();
      while (InsertPt != Builder.GetInsertPoint()
             && (isInsertedInstruction(InsertPt)
                 || isa<DbgInfoIntrinsic>(InsertPt))) {
        InsertPt = llvm::next(BasicBlock::iterator(InsertPt));
      }
      break;
    }

  // Check to see if we already expanded this here.
  std::map<std::pair<const SCEV *, Instruction *>, TrackingVH<Value> >::iterator
    I = InsertedExpressions.find(std::make_pair(S, InsertPt));
  if (I != InsertedExpressions.end())
    return I->second;

  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();
  Builder.SetInsertPoint(InsertPt->getParent(), InsertPt);

  // Expand the expression into instructions.
  Value *V = visit(S);

  // Remember the expanded value for this SCEV at this location.
  //
  // This is independent of PostIncLoops. The mapped value simply materializes
  // the expression at this insertion point. If the mapped value happened to be
  // a postinc expansion, it could be reused by a non postinc user, but only if
  // its insertion point was already at the head of the loop.
  InsertedExpressions[std::make_pair(S, InsertPt)] = V;

  restoreInsertPoint(SaveInsertBB, SaveInsertPt);
  return V;
}

void SCEVExpander::rememberInstruction(Value *I) {
  if (!PostIncLoops.empty())
    InsertedPostIncValues.insert(I);
  else
    InsertedValues.insert(I);
}

void SCEVExpander::restoreInsertPoint(BasicBlock *BB, BasicBlock::iterator I) {
  Builder.SetInsertPoint(BB, I);
}

/// getOrInsertCanonicalInductionVariable - This method returns the
/// canonical induction variable of the specified type for the specified
/// loop (inserting one if there is none).  A canonical induction variable
/// starts at zero and steps by one on each iteration.
PHINode *
SCEVExpander::getOrInsertCanonicalInductionVariable(const Loop *L,
                                                    Type *Ty) {
  assert(Ty->isIntegerTy() && "Can only insert integer induction variables!");

  // Build a SCEV for {0,+,1}<L>.
  // Conservatively use FlagAnyWrap for now.
  const SCEV *H = SE.getAddRecExpr(SE.getConstant(Ty, 0),
                                   SE.getConstant(Ty, 1), L, SCEV::FlagAnyWrap);

  // Emit code for it.
  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();
  PHINode *V = cast<PHINode>(expandCodeFor(H, 0, L->getHeader()->begin()));
  if (SaveInsertBB)
    restoreInsertPoint(SaveInsertBB, SaveInsertPt);

  return V;
}

/// Sort values by integer width for replaceCongruentIVs.
static bool width_descending(Value *lhs, Value *rhs) {
  // Put pointers at the back and make sure pointer < pointer = false.
  if (!lhs->getType()->isIntegerTy() || !rhs->getType()->isIntegerTy())
    return rhs->getType()->isIntegerTy() && !lhs->getType()->isIntegerTy();
  return rhs->getType()->getPrimitiveSizeInBits()
    < lhs->getType()->getPrimitiveSizeInBits();
}

/// replaceCongruentIVs - Check for congruent phis in this loop header and
/// replace them with their most canonical representative. Return the number of
/// phis eliminated.
///
/// This does not depend on any SCEVExpander state but should be used in
/// the same context that SCEVExpander is used.
unsigned SCEVExpander::replaceCongruentIVs(Loop *L, const DominatorTree *DT,
                                           SmallVectorImpl<WeakVH> &DeadInsts,
                                           const TargetTransformInfo *TTI) {
  // Find integer phis in order of increasing width.
  SmallVector<PHINode*, 8> Phis;
  for (BasicBlock::iterator I = L->getHeader()->begin();
       PHINode *Phi = dyn_cast<PHINode>(I); ++I) {
    Phis.push_back(Phi);
  }
  if (TTI)
    std::sort(Phis.begin(), Phis.end(), width_descending);

  unsigned NumElim = 0;
  DenseMap<const SCEV *, PHINode *> ExprToIVMap;
  // Process phis from wide to narrow. Mapping wide phis to the their truncation
  // so narrow phis can reuse them.
  for (SmallVectorImpl<PHINode*>::const_iterator PIter = Phis.begin(),
         PEnd = Phis.end(); PIter != PEnd; ++PIter) {
    PHINode *Phi = *PIter;

    // Fold constant phis. They may be congruent to other constant phis and
    // would confuse the logic below that expects proper IVs.
    if (Value *V = Phi->hasConstantValue()) {
      Phi->replaceAllUsesWith(V);
      DeadInsts.push_back(Phi);
      ++NumElim;
      DEBUG_WITH_TYPE(DebugType, dbgs()
                      << "INDVARS: Eliminated constant iv: " << *Phi << '\n');
      continue;
    }

    if (!SE.isSCEVable(Phi->getType()))
      continue;

    PHINode *&OrigPhiRef = ExprToIVMap[SE.getSCEV(Phi)];
    if (!OrigPhiRef) {
      OrigPhiRef = Phi;
      if (Phi->getType()->isIntegerTy() && TTI
          && TTI->isTruncateFree(Phi->getType(), Phis.back()->getType())) {
        // This phi can be freely truncated to the narrowest phi type. Map the
        // truncated expression to it so it will be reused for narrow types.
        const SCEV *TruncExpr =
          SE.getTruncateExpr(SE.getSCEV(Phi), Phis.back()->getType());
        ExprToIVMap[TruncExpr] = Phi;
      }
      continue;
    }

    // Replacing a pointer phi with an integer phi or vice-versa doesn't make
    // sense.
    if (OrigPhiRef->getType()->isPointerTy() != Phi->getType()->isPointerTy())
      continue;

    if (BasicBlock *LatchBlock = L->getLoopLatch()) {
      Instruction *OrigInc =
        cast<Instruction>(OrigPhiRef->getIncomingValueForBlock(LatchBlock));
      Instruction *IsomorphicInc =
        cast<Instruction>(Phi->getIncomingValueForBlock(LatchBlock));

      // If this phi has the same width but is more canonical, replace the
      // original with it. As part of the "more canonical" determination,
      // respect a prior decision to use an IV chain.
      if (OrigPhiRef->getType() == Phi->getType()
          && !(ChainedPhis.count(Phi)
               || isExpandedAddRecExprPHI(OrigPhiRef, OrigInc, L))
          && (ChainedPhis.count(Phi)
              || isExpandedAddRecExprPHI(Phi, IsomorphicInc, L))) {
        std::swap(OrigPhiRef, Phi);
        std::swap(OrigInc, IsomorphicInc);
      }
      // Replacing the congruent phi is sufficient because acyclic redundancy
      // elimination, CSE/GVN, should handle the rest. However, once SCEV proves
      // that a phi is congruent, it's often the head of an IV user cycle that
      // is isomorphic with the original phi. It's worth eagerly cleaning up the
      // common case of a single IV increment so that DeleteDeadPHIs can remove
      // cycles that had postinc uses.
      const SCEV *TruncExpr = SE.getTruncateOrNoop(SE.getSCEV(OrigInc),
                                                   IsomorphicInc->getType());
      if (OrigInc != IsomorphicInc
          && TruncExpr == SE.getSCEV(IsomorphicInc)
          && ((isa<PHINode>(OrigInc) && isa<PHINode>(IsomorphicInc))
              || hoistIVInc(OrigInc, IsomorphicInc))) {
        DEBUG_WITH_TYPE(DebugType, dbgs()
                        << "INDVARS: Eliminated congruent iv.inc: "
                        << *IsomorphicInc << '\n');
        Value *NewInc = OrigInc;
        if (OrigInc->getType() != IsomorphicInc->getType()) {
          Instruction *IP = isa<PHINode>(OrigInc)
            ? (Instruction*)L->getHeader()->getFirstInsertionPt()
            : OrigInc->getNextNode();
          IRBuilder<> Builder(IP);
          Builder.SetCurrentDebugLocation(IsomorphicInc->getDebugLoc());
          NewInc = Builder.
            CreateTruncOrBitCast(OrigInc, IsomorphicInc->getType(), IVName);
        }
        IsomorphicInc->replaceAllUsesWith(NewInc);
        DeadInsts.push_back(IsomorphicInc);
      }
    }
    DEBUG_WITH_TYPE(DebugType, dbgs()
                    << "INDVARS: Eliminated congruent iv: " << *Phi << '\n');
    ++NumElim;
    Value *NewIV = OrigPhiRef;
    if (OrigPhiRef->getType() != Phi->getType()) {
      IRBuilder<> Builder(L->getHeader()->getFirstInsertionPt());
      Builder.SetCurrentDebugLocation(Phi->getDebugLoc());
      NewIV = Builder.CreateTruncOrBitCast(OrigPhiRef, Phi->getType(), IVName);
    }
    Phi->replaceAllUsesWith(NewIV);
    DeadInsts.push_back(Phi);
  }
  return NumElim;
}

namespace {
// Search for a SCEV subexpression that is not safe to expand.  Any expression
// that may expand to a !isSafeToSpeculativelyExecute value is unsafe, namely
// UDiv expressions. We don't know if the UDiv is derived from an IR divide
// instruction, but the important thing is that we prove the denominator is
// nonzero before expansion.
//
// IVUsers already checks that IV-derived expressions are safe. So this check is
// only needed when the expression includes some subexpression that is not IV
// derived.
//
// Currently, we only allow division by a nonzero constant here. If this is
// inadequate, we could easily allow division by SCEVUnknown by using
// ValueTracking to check isKnownNonZero().
struct SCEVFindUnsafe {
  bool IsUnsafe;

  SCEVFindUnsafe(): IsUnsafe(false) {}

  bool follow(const SCEV *S) {
    const SCEVUDivExpr *D = dyn_cast<SCEVUDivExpr>(S);
    if (!D)
      return true;
    const SCEVConstant *SC = dyn_cast<SCEVConstant>(D->getRHS());
    if (SC && !SC->getValue()->isZero())
      return true;
    IsUnsafe = true;
    return false;
  }
  bool isDone() const { return IsUnsafe; }
};
}

namespace llvm {
bool isSafeToExpand(const SCEV *S) {
  SCEVFindUnsafe Search;
  visitAll(S, Search);
  return !Search.IsUnsafe;
}
}
