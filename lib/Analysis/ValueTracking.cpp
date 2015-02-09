//===- ValueTracking.cpp - Walk computations to compute properties --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help analyze properties that chains of
// computations have.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include <cstring>
using namespace llvm;
using namespace llvm::PatternMatch;

const unsigned MaxDepth = 6;

/// Returns the bitwidth of the given scalar or pointer type (if unknown returns
/// 0). For vector types, returns the element type's bitwidth.
static unsigned getBitWidth(Type *Ty, const DataLayout *TD) {
  if (unsigned BitWidth = Ty->getScalarSizeInBits())
    return BitWidth;

  return TD ? TD->getPointerTypeSizeInBits(Ty) : 0;
}

// Many of these functions have internal versions that take an assumption
// exclusion set. This is because of the potential for mutual recursion to
// cause computeKnownBits to repeatedly visit the same assume intrinsic. The
// classic case of this is assume(x = y), which will attempt to determine
// bits in x from bits in y, which will attempt to determine bits in y from
// bits in x, etc. Regarding the mutual recursion, computeKnownBits can call
// isKnownNonZero, which calls computeKnownBits and ComputeSignBit and
// isKnownToBeAPowerOfTwo (all of which can call computeKnownBits), and so on.
typedef SmallPtrSet<const Value *, 8> ExclInvsSet;

namespace {
// Simplifying using an assume can only be done in a particular control-flow
// context (the context instruction provides that context). If an assume and
// the context instruction are not in the same block then the DT helps in
// figuring out if we can use it.
struct Query {
  ExclInvsSet ExclInvs;
  AssumptionCache *AC;
  const Instruction *CxtI;
  const DominatorTree *DT;

  Query(AssumptionCache *AC = nullptr, const Instruction *CxtI = nullptr,
        const DominatorTree *DT = nullptr)
      : AC(AC), CxtI(CxtI), DT(DT) {}

  Query(const Query &Q, const Value *NewExcl)
      : ExclInvs(Q.ExclInvs), AC(Q.AC), CxtI(Q.CxtI), DT(Q.DT) {
    ExclInvs.insert(NewExcl);
  }
};
} // end anonymous namespace

// Given the provided Value and, potentially, a context instruction, return
// the preferred context instruction (if any).
static const Instruction *safeCxtI(const Value *V, const Instruction *CxtI) {
  // If we've been provided with a context instruction, then use that (provided
  // it has been inserted).
  if (CxtI && CxtI->getParent())
    return CxtI;

  // If the value is really an already-inserted instruction, then use that.
  CxtI = dyn_cast<Instruction>(V);
  if (CxtI && CxtI->getParent())
    return CxtI;

  return nullptr;
}

static void computeKnownBits(Value *V, APInt &KnownZero, APInt &KnownOne,
                            const DataLayout *TD, unsigned Depth,
                            const Query &Q);

void llvm::computeKnownBits(Value *V, APInt &KnownZero, APInt &KnownOne,
                            const DataLayout *TD, unsigned Depth,
                            AssumptionCache *AC, const Instruction *CxtI,
                            const DominatorTree *DT) {
  ::computeKnownBits(V, KnownZero, KnownOne, TD, Depth,
                     Query(AC, safeCxtI(V, CxtI), DT));
}

static void ComputeSignBit(Value *V, bool &KnownZero, bool &KnownOne,
                          const DataLayout *TD, unsigned Depth,
                          const Query &Q);

void llvm::ComputeSignBit(Value *V, bool &KnownZero, bool &KnownOne,
                          const DataLayout *TD, unsigned Depth,
                          AssumptionCache *AC, const Instruction *CxtI,
                          const DominatorTree *DT) {
  ::ComputeSignBit(V, KnownZero, KnownOne, TD, Depth,
                   Query(AC, safeCxtI(V, CxtI), DT));
}

static bool isKnownToBeAPowerOfTwo(Value *V, bool OrZero, unsigned Depth,
                                   const Query &Q);

bool llvm::isKnownToBeAPowerOfTwo(Value *V, bool OrZero, unsigned Depth,
                                  AssumptionCache *AC, const Instruction *CxtI,
                                  const DominatorTree *DT) {
  return ::isKnownToBeAPowerOfTwo(V, OrZero, Depth,
                                  Query(AC, safeCxtI(V, CxtI), DT));
}

static bool isKnownNonZero(Value *V, const DataLayout *TD, unsigned Depth,
                           const Query &Q);

bool llvm::isKnownNonZero(Value *V, const DataLayout *TD, unsigned Depth,
                          AssumptionCache *AC, const Instruction *CxtI,
                          const DominatorTree *DT) {
  return ::isKnownNonZero(V, TD, Depth, Query(AC, safeCxtI(V, CxtI), DT));
}

static bool MaskedValueIsZero(Value *V, const APInt &Mask,
                              const DataLayout *TD, unsigned Depth,
                              const Query &Q);

bool llvm::MaskedValueIsZero(Value *V, const APInt &Mask, const DataLayout *TD,
                             unsigned Depth, AssumptionCache *AC,
                             const Instruction *CxtI, const DominatorTree *DT) {
  return ::MaskedValueIsZero(V, Mask, TD, Depth,
                             Query(AC, safeCxtI(V, CxtI), DT));
}

static unsigned ComputeNumSignBits(Value *V, const DataLayout *TD,
                                   unsigned Depth, const Query &Q);

unsigned llvm::ComputeNumSignBits(Value *V, const DataLayout *TD,
                                  unsigned Depth, AssumptionCache *AC,
                                  const Instruction *CxtI,
                                  const DominatorTree *DT) {
  return ::ComputeNumSignBits(V, TD, Depth, Query(AC, safeCxtI(V, CxtI), DT));
}

static void computeKnownBitsAddSub(bool Add, Value *Op0, Value *Op1, bool NSW,
                                   APInt &KnownZero, APInt &KnownOne,
                                   APInt &KnownZero2, APInt &KnownOne2,
                                   const DataLayout *TD, unsigned Depth,
                                   const Query &Q) {
  if (!Add) {
    if (ConstantInt *CLHS = dyn_cast<ConstantInt>(Op0)) {
      // We know that the top bits of C-X are clear if X contains less bits
      // than C (i.e. no wrap-around can happen).  For example, 20-X is
      // positive if we can prove that X is >= 0 and < 16.
      if (!CLHS->getValue().isNegative()) {
        unsigned BitWidth = KnownZero.getBitWidth();
        unsigned NLZ = (CLHS->getValue()+1).countLeadingZeros();
        // NLZ can't be BitWidth with no sign bit
        APInt MaskV = APInt::getHighBitsSet(BitWidth, NLZ+1);
        computeKnownBits(Op1, KnownZero2, KnownOne2, TD, Depth+1, Q);

        // If all of the MaskV bits are known to be zero, then we know the
        // output top bits are zero, because we now know that the output is
        // from [0-C].
        if ((KnownZero2 & MaskV) == MaskV) {
          unsigned NLZ2 = CLHS->getValue().countLeadingZeros();
          // Top bits known zero.
          KnownZero = APInt::getHighBitsSet(BitWidth, NLZ2);
        }
      }
    }
  }

  unsigned BitWidth = KnownZero.getBitWidth();

  // If an initial sequence of bits in the result is not needed, the
  // corresponding bits in the operands are not needed.
  APInt LHSKnownZero(BitWidth, 0), LHSKnownOne(BitWidth, 0);
  computeKnownBits(Op0, LHSKnownZero, LHSKnownOne, TD, Depth+1, Q);
  computeKnownBits(Op1, KnownZero2, KnownOne2, TD, Depth+1, Q);

  // Carry in a 1 for a subtract, rather than a 0.
  APInt CarryIn(BitWidth, 0);
  if (!Add) {
    // Sum = LHS + ~RHS + 1
    std::swap(KnownZero2, KnownOne2);
    CarryIn.setBit(0);
  }

  APInt PossibleSumZero = ~LHSKnownZero + ~KnownZero2 + CarryIn;
  APInt PossibleSumOne = LHSKnownOne + KnownOne2 + CarryIn;

  // Compute known bits of the carry.
  APInt CarryKnownZero = ~(PossibleSumZero ^ LHSKnownZero ^ KnownZero2);
  APInt CarryKnownOne = PossibleSumOne ^ LHSKnownOne ^ KnownOne2;

  // Compute set of known bits (where all three relevant bits are known).
  APInt LHSKnown = LHSKnownZero | LHSKnownOne;
  APInt RHSKnown = KnownZero2 | KnownOne2;
  APInt CarryKnown = CarryKnownZero | CarryKnownOne;
  APInt Known = LHSKnown & RHSKnown & CarryKnown;

  assert((PossibleSumZero & Known) == (PossibleSumOne & Known) &&
         "known bits of sum differ");

  // Compute known bits of the result.
  KnownZero = ~PossibleSumOne & Known;
  KnownOne = PossibleSumOne & Known;

  // Are we still trying to solve for the sign bit?
  if (!Known.isNegative()) {
    if (NSW) {
      // Adding two non-negative numbers, or subtracting a negative number from
      // a non-negative one, can't wrap into negative.
      if (LHSKnownZero.isNegative() && KnownZero2.isNegative())
        KnownZero |= APInt::getSignBit(BitWidth);
      // Adding two negative numbers, or subtracting a non-negative number from
      // a negative one, can't wrap into non-negative.
      else if (LHSKnownOne.isNegative() && KnownOne2.isNegative())
        KnownOne |= APInt::getSignBit(BitWidth);
    }
  }
}

static void computeKnownBitsMul(Value *Op0, Value *Op1, bool NSW,
                                APInt &KnownZero, APInt &KnownOne,
                                APInt &KnownZero2, APInt &KnownOne2,
                                const DataLayout *TD, unsigned Depth,
                                const Query &Q) {
  unsigned BitWidth = KnownZero.getBitWidth();
  computeKnownBits(Op1, KnownZero, KnownOne, TD, Depth+1, Q);
  computeKnownBits(Op0, KnownZero2, KnownOne2, TD, Depth+1, Q);

  bool isKnownNegative = false;
  bool isKnownNonNegative = false;
  // If the multiplication is known not to overflow, compute the sign bit.
  if (NSW) {
    if (Op0 == Op1) {
      // The product of a number with itself is non-negative.
      isKnownNonNegative = true;
    } else {
      bool isKnownNonNegativeOp1 = KnownZero.isNegative();
      bool isKnownNonNegativeOp0 = KnownZero2.isNegative();
      bool isKnownNegativeOp1 = KnownOne.isNegative();
      bool isKnownNegativeOp0 = KnownOne2.isNegative();
      // The product of two numbers with the same sign is non-negative.
      isKnownNonNegative = (isKnownNegativeOp1 && isKnownNegativeOp0) ||
        (isKnownNonNegativeOp1 && isKnownNonNegativeOp0);
      // The product of a negative number and a non-negative number is either
      // negative or zero.
      if (!isKnownNonNegative)
        isKnownNegative = (isKnownNegativeOp1 && isKnownNonNegativeOp0 &&
                           isKnownNonZero(Op0, TD, Depth, Q)) ||
                          (isKnownNegativeOp0 && isKnownNonNegativeOp1 &&
                           isKnownNonZero(Op1, TD, Depth, Q));
    }
  }

  // If low bits are zero in either operand, output low known-0 bits.
  // Also compute a conserative estimate for high known-0 bits.
  // More trickiness is possible, but this is sufficient for the
  // interesting case of alignment computation.
  KnownOne.clearAllBits();
  unsigned TrailZ = KnownZero.countTrailingOnes() +
                    KnownZero2.countTrailingOnes();
  unsigned LeadZ =  std::max(KnownZero.countLeadingOnes() +
                             KnownZero2.countLeadingOnes(),
                             BitWidth) - BitWidth;

  TrailZ = std::min(TrailZ, BitWidth);
  LeadZ = std::min(LeadZ, BitWidth);
  KnownZero = APInt::getLowBitsSet(BitWidth, TrailZ) |
              APInt::getHighBitsSet(BitWidth, LeadZ);

  // Only make use of no-wrap flags if we failed to compute the sign bit
  // directly.  This matters if the multiplication always overflows, in
  // which case we prefer to follow the result of the direct computation,
  // though as the program is invoking undefined behaviour we can choose
  // whatever we like here.
  if (isKnownNonNegative && !KnownOne.isNegative())
    KnownZero.setBit(BitWidth - 1);
  else if (isKnownNegative && !KnownZero.isNegative())
    KnownOne.setBit(BitWidth - 1);
}

void llvm::computeKnownBitsFromRangeMetadata(const MDNode &Ranges,
                                             APInt &KnownZero) {
  unsigned BitWidth = KnownZero.getBitWidth();
  unsigned NumRanges = Ranges.getNumOperands() / 2;
  assert(NumRanges >= 1);

  // Use the high end of the ranges to find leading zeros.
  unsigned MinLeadingZeros = BitWidth;
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges.getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());
    if (Range.isWrappedSet())
      MinLeadingZeros = 0; // -1 has no zeros
    unsigned LeadingZeros = (Upper->getValue() - 1).countLeadingZeros();
    MinLeadingZeros = std::min(LeadingZeros, MinLeadingZeros);
  }

  KnownZero = APInt::getHighBitsSet(BitWidth, MinLeadingZeros);
}

static bool isEphemeralValueOf(Instruction *I, const Value *E) {
  SmallVector<const Value *, 16> WorkSet(1, I);
  SmallPtrSet<const Value *, 32> Visited;
  SmallPtrSet<const Value *, 16> EphValues;

  while (!WorkSet.empty()) {
    const Value *V = WorkSet.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    // If all uses of this value are ephemeral, then so is this value.
    bool FoundNEUse = false;
    for (const User *I : V->users())
      if (!EphValues.count(I)) {
        FoundNEUse = true;
        break;
      }

    if (!FoundNEUse) {
      if (V == E)
        return true;

      EphValues.insert(V);
      if (const User *U = dyn_cast<User>(V))
        for (User::const_op_iterator J = U->op_begin(), JE = U->op_end();
             J != JE; ++J) {
          if (isSafeToSpeculativelyExecute(*J))
            WorkSet.push_back(*J);
        }
    }
  }

  return false;
}

// Is this an intrinsic that cannot be speculated but also cannot trap?
static bool isAssumeLikeIntrinsic(const Instruction *I) {
  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (Function *F = CI->getCalledFunction())
      switch (F->getIntrinsicID()) {
      default: break;
      // FIXME: This list is repeated from NoTTI::getIntrinsicCost.
      case Intrinsic::assume:
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
      case Intrinsic::invariant_start:
      case Intrinsic::invariant_end:
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
      case Intrinsic::objectsize:
      case Intrinsic::ptr_annotation:
      case Intrinsic::var_annotation:
        return true;
      }

  return false;
}

static bool isValidAssumeForContext(Value *V, const Query &Q,
                                    const DataLayout *DL) {
  Instruction *Inv = cast<Instruction>(V);

  // There are two restrictions on the use of an assume:
  //  1. The assume must dominate the context (or the control flow must
  //     reach the assume whenever it reaches the context).
  //  2. The context must not be in the assume's set of ephemeral values
  //     (otherwise we will use the assume to prove that the condition
  //     feeding the assume is trivially true, thus causing the removal of
  //     the assume).

  if (Q.DT) {
    if (Q.DT->dominates(Inv, Q.CxtI)) {
      return true;
    } else if (Inv->getParent() == Q.CxtI->getParent()) {
      // The context comes first, but they're both in the same block. Make sure
      // there is nothing in between that might interrupt the control flow.
      for (BasicBlock::const_iterator I =
             std::next(BasicBlock::const_iterator(Q.CxtI)),
                                      IE(Inv); I != IE; ++I)
        if (!isSafeToSpeculativelyExecute(I, DL) &&
            !isAssumeLikeIntrinsic(I))
          return false;

      return !isEphemeralValueOf(Inv, Q.CxtI);
    }

    return false;
  }

  // When we don't have a DT, we do a limited search...
  if (Inv->getParent() == Q.CxtI->getParent()->getSinglePredecessor()) {
    return true;
  } else if (Inv->getParent() == Q.CxtI->getParent()) {
    // Search forward from the assume until we reach the context (or the end
    // of the block); the common case is that the assume will come first.
    for (BasicBlock::iterator I = std::next(BasicBlock::iterator(Inv)),
         IE = Inv->getParent()->end(); I != IE; ++I)
      if (I == Q.CxtI)
        return true;

    // The context must come first...
    for (BasicBlock::const_iterator I =
           std::next(BasicBlock::const_iterator(Q.CxtI)),
                                    IE(Inv); I != IE; ++I)
      if (!isSafeToSpeculativelyExecute(I, DL) &&
          !isAssumeLikeIntrinsic(I))
        return false;

    return !isEphemeralValueOf(Inv, Q.CxtI);
  }

  return false;
}

bool llvm::isValidAssumeForContext(const Instruction *I,
                                   const Instruction *CxtI,
                                   const DataLayout *DL,
                                   const DominatorTree *DT) {
  return ::isValidAssumeForContext(const_cast<Instruction*>(I),
                                   Query(nullptr, CxtI, DT), DL);
}

template<typename LHS, typename RHS>
inline match_combine_or<CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>,
                        CmpClass_match<RHS, LHS, ICmpInst, ICmpInst::Predicate>>
m_c_ICmp(ICmpInst::Predicate &Pred, const LHS &L, const RHS &R) {
  return m_CombineOr(m_ICmp(Pred, L, R), m_ICmp(Pred, R, L));
}

template<typename LHS, typename RHS>
inline match_combine_or<BinaryOp_match<LHS, RHS, Instruction::And>,
                        BinaryOp_match<RHS, LHS, Instruction::And>>
m_c_And(const LHS &L, const RHS &R) {
  return m_CombineOr(m_And(L, R), m_And(R, L));
}

template<typename LHS, typename RHS>
inline match_combine_or<BinaryOp_match<LHS, RHS, Instruction::Or>,
                        BinaryOp_match<RHS, LHS, Instruction::Or>>
m_c_Or(const LHS &L, const RHS &R) {
  return m_CombineOr(m_Or(L, R), m_Or(R, L));
}

template<typename LHS, typename RHS>
inline match_combine_or<BinaryOp_match<LHS, RHS, Instruction::Xor>,
                        BinaryOp_match<RHS, LHS, Instruction::Xor>>
m_c_Xor(const LHS &L, const RHS &R) {
  return m_CombineOr(m_Xor(L, R), m_Xor(R, L));
}

static void computeKnownBitsFromAssume(Value *V, APInt &KnownZero,
                                       APInt &KnownOne,
                                       const DataLayout *DL,
                                       unsigned Depth, const Query &Q) {
  // Use of assumptions is context-sensitive. If we don't have a context, we
  // cannot use them!
  if (!Q.AC || !Q.CxtI)
    return;

  unsigned BitWidth = KnownZero.getBitWidth();

  for (auto &AssumeVH : Q.AC->assumptions()) {
    if (!AssumeVH)
      continue;
    CallInst *I = cast<CallInst>(AssumeVH);
    assert(I->getParent()->getParent() == Q.CxtI->getParent()->getParent() &&
           "Got assumption for the wrong function!");
    if (Q.ExclInvs.count(I))
      continue;

    // Warning: This loop can end up being somewhat performance sensetive.
    // We're running this loop for once for each value queried resulting in a
    // runtime of ~O(#assumes * #values).

    assert(isa<IntrinsicInst>(I) &&
           dyn_cast<IntrinsicInst>(I)->getIntrinsicID() == Intrinsic::assume &&
           "must be an assume intrinsic");
    
    Value *Arg = I->getArgOperand(0);

    if (Arg == V &&
        isValidAssumeForContext(I, Q, DL)) {
      assert(BitWidth == 1 && "assume operand is not i1?");
      KnownZero.clearAllBits();
      KnownOne.setAllBits();
      return;
    }

    // The remaining tests are all recursive, so bail out if we hit the limit.
    if (Depth == MaxDepth)
      continue;

    Value *A, *B;
    auto m_V = m_CombineOr(m_Specific(V),
                           m_CombineOr(m_PtrToInt(m_Specific(V)),
                           m_BitCast(m_Specific(V))));

    CmpInst::Predicate Pred;
    ConstantInt *C;
    // assume(v = a)
    if (match(Arg, m_c_ICmp(Pred, m_V, m_Value(A))) &&
        Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      KnownZero |= RHSKnownZero;
      KnownOne  |= RHSKnownOne;
    // assume(v & b = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_c_And(m_V, m_Value(B)),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt MaskKnownZero(BitWidth, 0), MaskKnownOne(BitWidth, 0);
      computeKnownBits(B, MaskKnownZero, MaskKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in the mask that are known to be one, we can propagate
      // known bits from the RHS to V.
      KnownZero |= RHSKnownZero & MaskKnownOne;
      KnownOne  |= RHSKnownOne  & MaskKnownOne;
    // assume(~(v & b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_And(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt MaskKnownZero(BitWidth, 0), MaskKnownOne(BitWidth, 0);
      computeKnownBits(B, MaskKnownZero, MaskKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in the mask that are known to be one, we can propagate
      // inverted known bits from the RHS to V.
      KnownZero |= RHSKnownOne  & MaskKnownOne;
      KnownOne  |= RHSKnownZero & MaskKnownOne;
    // assume(v | b = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_c_Or(m_V, m_Value(B)),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt BKnownZero(BitWidth, 0), BKnownOne(BitWidth, 0);
      computeKnownBits(B, BKnownZero, BKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate known
      // bits from the RHS to V.
      KnownZero |= RHSKnownZero & BKnownZero;
      KnownOne  |= RHSKnownOne  & BKnownZero;
    // assume(~(v | b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_Or(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt BKnownZero(BitWidth, 0), BKnownOne(BitWidth, 0);
      computeKnownBits(B, BKnownZero, BKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate
      // inverted known bits from the RHS to V.
      KnownZero |= RHSKnownOne  & BKnownZero;
      KnownOne  |= RHSKnownZero & BKnownZero;
    // assume(v ^ b = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_c_Xor(m_V, m_Value(B)),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt BKnownZero(BitWidth, 0), BKnownOne(BitWidth, 0);
      computeKnownBits(B, BKnownZero, BKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate known
      // bits from the RHS to V. For those bits in B that are known to be one,
      // we can propagate inverted known bits from the RHS to V.
      KnownZero |= RHSKnownZero & BKnownZero;
      KnownOne  |= RHSKnownOne  & BKnownZero;
      KnownZero |= RHSKnownOne  & BKnownOne;
      KnownOne  |= RHSKnownZero & BKnownOne;
    // assume(~(v ^ b) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_c_Xor(m_V, m_Value(B))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      APInt BKnownZero(BitWidth, 0), BKnownOne(BitWidth, 0);
      computeKnownBits(B, BKnownZero, BKnownOne, DL, Depth+1, Query(Q, I));

      // For those bits in B that are known to be zero, we can propagate
      // inverted known bits from the RHS to V. For those bits in B that are
      // known to be one, we can propagate known bits from the RHS to V.
      KnownZero |= RHSKnownOne  & BKnownZero;
      KnownOne  |= RHSKnownZero & BKnownZero;
      KnownZero |= RHSKnownZero & BKnownOne;
      KnownOne  |= RHSKnownOne  & BKnownOne;
    // assume(v << c = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Shl(m_V, m_ConstantInt(C)),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them to known
      // bits in V shifted to the right by C.
      KnownZero |= RHSKnownZero.lshr(C->getZExtValue());
      KnownOne  |= RHSKnownOne.lshr(C->getZExtValue());
    // assume(~(v << c) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_Shl(m_V, m_ConstantInt(C))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them inverted
      // to known bits in V shifted to the right by C.
      KnownZero |= RHSKnownOne.lshr(C->getZExtValue());
      KnownOne  |= RHSKnownZero.lshr(C->getZExtValue());
    // assume(v >> c = a)
    } else if (match(Arg,
                     m_c_ICmp(Pred, m_CombineOr(m_LShr(m_V, m_ConstantInt(C)),
                                                  m_AShr(m_V,
                                                         m_ConstantInt(C))),
                                     m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them to known
      // bits in V shifted to the right by C.
      KnownZero |= RHSKnownZero << C->getZExtValue();
      KnownOne  |= RHSKnownOne  << C->getZExtValue();
    // assume(~(v >> c) = a)
    } else if (match(Arg, m_c_ICmp(Pred, m_Not(m_CombineOr(
                                              m_LShr(m_V, m_ConstantInt(C)),
                                              m_AShr(m_V, m_ConstantInt(C)))),
                                   m_Value(A))) &&
               Pred == ICmpInst::ICMP_EQ && isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));
      // For those bits in RHS that are known, we can propagate them inverted
      // to known bits in V shifted to the right by C.
      KnownZero |= RHSKnownOne  << C->getZExtValue();
      KnownOne  |= RHSKnownZero << C->getZExtValue();
    // assume(v >=_s c) where c is non-negative
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SGE &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      if (RHSKnownZero.isNegative()) {
        // We know that the sign bit is zero.
        KnownZero |= APInt::getSignBit(BitWidth);
      }
    // assume(v >_s c) where c is at least -1.
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SGT &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      if (RHSKnownOne.isAllOnesValue() || RHSKnownZero.isNegative()) {
        // We know that the sign bit is zero.
        KnownZero |= APInt::getSignBit(BitWidth);
      }
    // assume(v <=_s c) where c is negative
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SLE &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      if (RHSKnownOne.isNegative()) {
        // We know that the sign bit is one.
        KnownOne |= APInt::getSignBit(BitWidth);
      }
    // assume(v <_s c) where c is non-positive
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_SLT &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      if (RHSKnownZero.isAllOnesValue() || RHSKnownOne.isNegative()) {
        // We know that the sign bit is one.
        KnownOne |= APInt::getSignBit(BitWidth);
      }
    // assume(v <=_u c)
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_ULE &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      // Whatever high bits in c are zero are known to be zero.
      KnownZero |=
        APInt::getHighBitsSet(BitWidth, RHSKnownZero.countLeadingOnes());
    // assume(v <_u c)
    } else if (match(Arg, m_ICmp(Pred, m_V, m_Value(A))) &&
               Pred == ICmpInst::ICMP_ULT &&
               isValidAssumeForContext(I, Q, DL)) {
      APInt RHSKnownZero(BitWidth, 0), RHSKnownOne(BitWidth, 0);
      computeKnownBits(A, RHSKnownZero, RHSKnownOne, DL, Depth+1, Query(Q, I));

      // Whatever high bits in c are zero are known to be zero (if c is a power
      // of 2, then one more).
      if (isKnownToBeAPowerOfTwo(A, false, Depth+1, Query(Q, I)))
        KnownZero |=
          APInt::getHighBitsSet(BitWidth, RHSKnownZero.countLeadingOnes()+1);
      else
        KnownZero |=
          APInt::getHighBitsSet(BitWidth, RHSKnownZero.countLeadingOnes());
    }
  }
}

/// Determine which bits of V are known to be either zero or one and return
/// them in the KnownZero/KnownOne bit sets.
///
/// NOTE: we cannot consider 'undef' to be "IsZero" here.  The problem is that
/// we cannot optimize based on the assumption that it is zero without changing
/// it to be an explicit zero.  If we don't change it to zero, other code could
/// optimized based on the contradictory assumption that it is non-zero.
/// Because instcombine aggressively folds operations with undef args anyway,
/// this won't lose us code quality.
///
/// This function is defined on values with integer type, values with pointer
/// type (but only if TD is non-null), and vectors of integers.  In the case
/// where V is a vector, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
void computeKnownBits(Value *V, APInt &KnownZero, APInt &KnownOne,
                      const DataLayout *TD, unsigned Depth,
                      const Query &Q) {
  assert(V && "No Value?");
  assert(Depth <= MaxDepth && "Limit Search Depth");
  unsigned BitWidth = KnownZero.getBitWidth();

  assert((V->getType()->isIntOrIntVectorTy() ||
          V->getType()->getScalarType()->isPointerTy()) &&
         "Not integer or pointer type!");
  assert((!TD ||
          TD->getTypeSizeInBits(V->getType()->getScalarType()) == BitWidth) &&
         (!V->getType()->isIntOrIntVectorTy() ||
          V->getType()->getScalarSizeInBits() == BitWidth) &&
         KnownZero.getBitWidth() == BitWidth &&
         KnownOne.getBitWidth() == BitWidth &&
         "V, KnownOne and KnownZero should have same BitWidth");

  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    // We know all of the bits for a constant!
    KnownOne = CI->getValue();
    KnownZero = ~KnownOne;
    return;
  }
  // Null and aggregate-zero are all-zeros.
  if (isa<ConstantPointerNull>(V) ||
      isa<ConstantAggregateZero>(V)) {
    KnownOne.clearAllBits();
    KnownZero = APInt::getAllOnesValue(BitWidth);
    return;
  }
  // Handle a constant vector by taking the intersection of the known bits of
  // each element.  There is no real need to handle ConstantVector here, because
  // we don't handle undef in any particularly useful way.
  if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(V)) {
    // We know that CDS must be a vector of integers. Take the intersection of
    // each element.
    KnownZero.setAllBits(); KnownOne.setAllBits();
    APInt Elt(KnownZero.getBitWidth(), 0);
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      Elt = CDS->getElementAsInteger(i);
      KnownZero &= ~Elt;
      KnownOne &= Elt;
    }
    return;
  }

  // The address of an aligned GlobalValue has trailing zeros.
  if (auto *GO = dyn_cast<GlobalObject>(V)) {
    unsigned Align = GO->getAlignment();
    if (Align == 0 && TD) {
      if (auto *GVar = dyn_cast<GlobalVariable>(GO)) {
        Type *ObjectType = GVar->getType()->getElementType();
        if (ObjectType->isSized()) {
          // If the object is defined in the current Module, we'll be giving
          // it the preferred alignment. Otherwise, we have to assume that it
          // may only have the minimum ABI alignment.
          if (!GVar->isDeclaration() && !GVar->isWeakForLinker())
            Align = TD->getPreferredAlignment(GVar);
          else
            Align = TD->getABITypeAlignment(ObjectType);
        }
      }
    }
    if (Align > 0)
      KnownZero = APInt::getLowBitsSet(BitWidth,
                                       countTrailingZeros(Align));
    else
      KnownZero.clearAllBits();
    KnownOne.clearAllBits();
    return;
  }

  if (Argument *A = dyn_cast<Argument>(V)) {
    unsigned Align = A->getType()->isPointerTy() ? A->getParamAlignment() : 0;

    if (!Align && TD && A->hasStructRetAttr()) {
      // An sret parameter has at least the ABI alignment of the return type.
      Type *EltTy = cast<PointerType>(A->getType())->getElementType();
      if (EltTy->isSized())
        Align = TD->getABITypeAlignment(EltTy);
    }

    if (Align)
      KnownZero = APInt::getLowBitsSet(BitWidth, countTrailingZeros(Align));
    else
      KnownZero.clearAllBits();
    KnownOne.clearAllBits();

    // Don't give up yet... there might be an assumption that provides more
    // information...
    computeKnownBitsFromAssume(V, KnownZero, KnownOne, TD, Depth, Q);
    return;
  }

  // Start out not knowing anything.
  KnownZero.clearAllBits(); KnownOne.clearAllBits();

  // Limit search depth.
  // All recursive calls that increase depth must come after this.
  if (Depth == MaxDepth)
    return;  

  // A weak GlobalAlias is totally unknown. A non-weak GlobalAlias has
  // the bits of its aliasee.
  if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
    if (!GA->mayBeOverridden())
      computeKnownBits(GA->getAliasee(), KnownZero, KnownOne, TD, Depth + 1, Q);
    return;
  }

  // Check whether a nearby assume intrinsic can determine some known bits.
  computeKnownBitsFromAssume(V, KnownZero, KnownOne, TD, Depth, Q);

  Operator *I = dyn_cast<Operator>(V);
  if (!I) return;

  APInt KnownZero2(KnownZero), KnownOne2(KnownOne);
  switch (I->getOpcode()) {
  default: break;
  case Instruction::Load:
    if (MDNode *MD = cast<LoadInst>(I)->getMetadata(LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, KnownZero);
    break;
  case Instruction::And: {
    // If either the LHS or the RHS are Zero, the result is zero.
    computeKnownBits(I->getOperand(1), KnownZero, KnownOne, TD, Depth+1, Q);
    computeKnownBits(I->getOperand(0), KnownZero2, KnownOne2, TD, Depth+1, Q);

    // Output known-1 bits are only known if set in both the LHS & RHS.
    KnownOne &= KnownOne2;
    // Output known-0 are known to be clear if zero in either the LHS | RHS.
    KnownZero |= KnownZero2;
    break;
  }
  case Instruction::Or: {
    computeKnownBits(I->getOperand(1), KnownZero, KnownOne, TD, Depth+1, Q);
    computeKnownBits(I->getOperand(0), KnownZero2, KnownOne2, TD, Depth+1, Q);

    // Output known-0 bits are only known if clear in both the LHS & RHS.
    KnownZero &= KnownZero2;
    // Output known-1 are known to be set if set in either the LHS | RHS.
    KnownOne |= KnownOne2;
    break;
  }
  case Instruction::Xor: {
    computeKnownBits(I->getOperand(1), KnownZero, KnownOne, TD, Depth+1, Q);
    computeKnownBits(I->getOperand(0), KnownZero2, KnownOne2, TD, Depth+1, Q);

    // Output known-0 bits are known if clear or set in both the LHS & RHS.
    APInt KnownZeroOut = (KnownZero & KnownZero2) | (KnownOne & KnownOne2);
    // Output known-1 are known to be set if set in only one of the LHS, RHS.
    KnownOne = (KnownZero & KnownOne2) | (KnownOne & KnownZero2);
    KnownZero = KnownZeroOut;
    break;
  }
  case Instruction::Mul: {
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    computeKnownBitsMul(I->getOperand(0), I->getOperand(1), NSW,
                         KnownZero, KnownOne, KnownZero2, KnownOne2, TD,
                         Depth, Q);
    break;
  }
  case Instruction::UDiv: {
    // For the purposes of computing leading zeros we can conservatively
    // treat a udiv as a logical right shift by the power of 2 known to
    // be less than the denominator.
    computeKnownBits(I->getOperand(0), KnownZero2, KnownOne2, TD, Depth+1, Q);
    unsigned LeadZ = KnownZero2.countLeadingOnes();

    KnownOne2.clearAllBits();
    KnownZero2.clearAllBits();
    computeKnownBits(I->getOperand(1), KnownZero2, KnownOne2, TD, Depth+1, Q);
    unsigned RHSUnknownLeadingOnes = KnownOne2.countLeadingZeros();
    if (RHSUnknownLeadingOnes != BitWidth)
      LeadZ = std::min(BitWidth,
                       LeadZ + BitWidth - RHSUnknownLeadingOnes - 1);

    KnownZero = APInt::getHighBitsSet(BitWidth, LeadZ);
    break;
  }
  case Instruction::Select:
    computeKnownBits(I->getOperand(2), KnownZero, KnownOne, TD, Depth+1, Q);
    computeKnownBits(I->getOperand(1), KnownZero2, KnownOne2, TD, Depth+1, Q);

    // Only known if known in both the LHS and RHS.
    KnownOne &= KnownOne2;
    KnownZero &= KnownZero2;
    break;
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
    break; // Can't work with floating point.
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::AddrSpaceCast: // Pointers could be different sizes.
    // We can't handle these if we don't know the pointer size.
    if (!TD) break;
    // FALL THROUGH and handle them the same as zext/trunc.
  case Instruction::ZExt:
  case Instruction::Trunc: {
    Type *SrcTy = I->getOperand(0)->getType();

    unsigned SrcBitWidth;
    // Note that we handle pointer operands here because of inttoptr/ptrtoint
    // which fall through here.
    if(TD) {
      SrcBitWidth = TD->getTypeSizeInBits(SrcTy->getScalarType());
    } else {
      SrcBitWidth = SrcTy->getScalarSizeInBits();
      if (!SrcBitWidth) break;
    }

    assert(SrcBitWidth && "SrcBitWidth can't be zero");
    KnownZero = KnownZero.zextOrTrunc(SrcBitWidth);
    KnownOne = KnownOne.zextOrTrunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
    KnownZero = KnownZero.zextOrTrunc(BitWidth);
    KnownOne = KnownOne.zextOrTrunc(BitWidth);
    // Any top bits are known to be zero.
    if (BitWidth > SrcBitWidth)
      KnownZero |= APInt::getHighBitsSet(BitWidth, BitWidth - SrcBitWidth);
    break;
  }
  case Instruction::BitCast: {
    Type *SrcTy = I->getOperand(0)->getType();
    if ((SrcTy->isIntegerTy() || SrcTy->isPointerTy()) &&
        // TODO: For now, not handling conversions like:
        // (bitcast i64 %x to <2 x i32>)
        !I->getType()->isVectorTy()) {
      computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
      break;
    }
    break;
  }
  case Instruction::SExt: {
    // Compute the bits in the result that are not present in the input.
    unsigned SrcBitWidth = I->getOperand(0)->getType()->getScalarSizeInBits();

    KnownZero = KnownZero.trunc(SrcBitWidth);
    KnownOne = KnownOne.trunc(SrcBitWidth);
    computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
    KnownZero = KnownZero.zext(BitWidth);
    KnownOne = KnownOne.zext(BitWidth);

    // If the sign bit of the input is known set or clear, then we know the
    // top bits of the result.
    if (KnownZero[SrcBitWidth-1])             // Input sign bit known zero
      KnownZero |= APInt::getHighBitsSet(BitWidth, BitWidth - SrcBitWidth);
    else if (KnownOne[SrcBitWidth-1])           // Input sign bit known set
      KnownOne |= APInt::getHighBitsSet(BitWidth, BitWidth - SrcBitWidth);
    break;
  }
  case Instruction::Shl:
    // (shl X, C1) & C2 == 0   iff   (X & C2 >>u C1) == 0
    if (ConstantInt *SA = dyn_cast<ConstantInt>(I->getOperand(1))) {
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth);
      computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
      KnownZero <<= ShiftAmt;
      KnownOne  <<= ShiftAmt;
      KnownZero |= APInt::getLowBitsSet(BitWidth, ShiftAmt); // low bits known 0
    }
    break;
  case Instruction::LShr:
    // (ushr X, C1) & C2 == 0   iff  (-1 >> C1) & C2 == 0
    if (ConstantInt *SA = dyn_cast<ConstantInt>(I->getOperand(1))) {
      // Compute the new bits that are at the top now.
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth);

      // Unsigned shift right.
      computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
      KnownZero = APIntOps::lshr(KnownZero, ShiftAmt);
      KnownOne  = APIntOps::lshr(KnownOne, ShiftAmt);
      // high bits known zero.
      KnownZero |= APInt::getHighBitsSet(BitWidth, ShiftAmt);
    }
    break;
  case Instruction::AShr:
    // (ashr X, C1) & C2 == 0   iff  (-1 >> C1) & C2 == 0
    if (ConstantInt *SA = dyn_cast<ConstantInt>(I->getOperand(1))) {
      // Compute the new bits that are at the top now.
      uint64_t ShiftAmt = SA->getLimitedValue(BitWidth-1);

      // Signed shift right.
      computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
      KnownZero = APIntOps::lshr(KnownZero, ShiftAmt);
      KnownOne  = APIntOps::lshr(KnownOne, ShiftAmt);

      APInt HighBits(APInt::getHighBitsSet(BitWidth, ShiftAmt));
      if (KnownZero[BitWidth-ShiftAmt-1])    // New bits are known zero.
        KnownZero |= HighBits;
      else if (KnownOne[BitWidth-ShiftAmt-1])  // New bits are known one.
        KnownOne |= HighBits;
    }
    break;
  case Instruction::Sub: {
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    computeKnownBitsAddSub(false, I->getOperand(0), I->getOperand(1), NSW,
                            KnownZero, KnownOne, KnownZero2, KnownOne2, TD,
                            Depth, Q);
    break;
  }
  case Instruction::Add: {
    bool NSW = cast<OverflowingBinaryOperator>(I)->hasNoSignedWrap();
    computeKnownBitsAddSub(true, I->getOperand(0), I->getOperand(1), NSW,
                            KnownZero, KnownOne, KnownZero2, KnownOne2, TD,
                            Depth, Q);
    break;
  }
  case Instruction::SRem:
    if (ConstantInt *Rem = dyn_cast<ConstantInt>(I->getOperand(1))) {
      APInt RA = Rem->getValue().abs();
      if (RA.isPowerOf2()) {
        APInt LowBits = RA - 1;
        computeKnownBits(I->getOperand(0), KnownZero2, KnownOne2, TD,
                         Depth+1, Q);

        // The low bits of the first operand are unchanged by the srem.
        KnownZero = KnownZero2 & LowBits;
        KnownOne = KnownOne2 & LowBits;

        // If the first operand is non-negative or has all low bits zero, then
        // the upper bits are all zero.
        if (KnownZero2[BitWidth-1] || ((KnownZero2 & LowBits) == LowBits))
          KnownZero |= ~LowBits;

        // If the first operand is negative and not all low bits are zero, then
        // the upper bits are all one.
        if (KnownOne2[BitWidth-1] && ((KnownOne2 & LowBits) != 0))
          KnownOne |= ~LowBits;

        assert((KnownZero & KnownOne) == 0 && "Bits known to be one AND zero?");
      }
    }

    // The sign bit is the LHS's sign bit, except when the result of the
    // remainder is zero.
    if (KnownZero.isNonNegative()) {
      APInt LHSKnownZero(BitWidth, 0), LHSKnownOne(BitWidth, 0);
      computeKnownBits(I->getOperand(0), LHSKnownZero, LHSKnownOne, TD,
                       Depth+1, Q);
      // If it's known zero, our sign bit is also zero.
      if (LHSKnownZero.isNegative())
        KnownZero.setBit(BitWidth - 1);
    }

    break;
  case Instruction::URem: {
    if (ConstantInt *Rem = dyn_cast<ConstantInt>(I->getOperand(1))) {
      APInt RA = Rem->getValue();
      if (RA.isPowerOf2()) {
        APInt LowBits = (RA - 1);
        computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD,
                         Depth+1, Q);
        KnownZero |= ~LowBits;
        KnownOne &= LowBits;
        break;
      }
    }

    // Since the result is less than or equal to either operand, any leading
    // zero bits in either operand must also exist in the result.
    computeKnownBits(I->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);
    computeKnownBits(I->getOperand(1), KnownZero2, KnownOne2, TD, Depth+1, Q);

    unsigned Leaders = std::max(KnownZero.countLeadingOnes(),
                                KnownZero2.countLeadingOnes());
    KnownOne.clearAllBits();
    KnownZero = APInt::getHighBitsSet(BitWidth, Leaders);
    break;
  }

  case Instruction::Alloca: {
    AllocaInst *AI = cast<AllocaInst>(V);
    unsigned Align = AI->getAlignment();
    if (Align == 0 && TD)
      Align = TD->getABITypeAlignment(AI->getType()->getElementType());

    if (Align > 0)
      KnownZero = APInt::getLowBitsSet(BitWidth, countTrailingZeros(Align));
    break;
  }
  case Instruction::GetElementPtr: {
    // Analyze all of the subscripts of this getelementptr instruction
    // to determine if we can prove known low zero bits.
    APInt LocalKnownZero(BitWidth, 0), LocalKnownOne(BitWidth, 0);
    computeKnownBits(I->getOperand(0), LocalKnownZero, LocalKnownOne, TD,
                     Depth+1, Q);
    unsigned TrailZ = LocalKnownZero.countTrailingOnes();

    gep_type_iterator GTI = gep_type_begin(I);
    for (unsigned i = 1, e = I->getNumOperands(); i != e; ++i, ++GTI) {
      Value *Index = I->getOperand(i);
      if (StructType *STy = dyn_cast<StructType>(*GTI)) {
        // Handle struct member offset arithmetic.
        if (!TD) {
          TrailZ = 0;
          break;
        }

        // Handle case when index is vector zeroinitializer
        Constant *CIndex = cast<Constant>(Index);
        if (CIndex->isZeroValue())
          continue;

        if (CIndex->getType()->isVectorTy())
          Index = CIndex->getSplatValue();

        unsigned Idx = cast<ConstantInt>(Index)->getZExtValue();
        const StructLayout *SL = TD->getStructLayout(STy);
        uint64_t Offset = SL->getElementOffset(Idx);
        TrailZ = std::min<unsigned>(TrailZ,
                                    countTrailingZeros(Offset));
      } else {
        // Handle array index arithmetic.
        Type *IndexedTy = GTI.getIndexedType();
        if (!IndexedTy->isSized()) {
          TrailZ = 0;
          break;
        }
        unsigned GEPOpiBits = Index->getType()->getScalarSizeInBits();
        uint64_t TypeSize = TD ? TD->getTypeAllocSize(IndexedTy) : 1;
        LocalKnownZero = LocalKnownOne = APInt(GEPOpiBits, 0);
        computeKnownBits(Index, LocalKnownZero, LocalKnownOne, TD, Depth+1, Q);
        TrailZ = std::min(TrailZ,
                          unsigned(countTrailingZeros(TypeSize) +
                                   LocalKnownZero.countTrailingOnes()));
      }
    }

    KnownZero = APInt::getLowBitsSet(BitWidth, TrailZ);
    break;
  }
  case Instruction::PHI: {
    PHINode *P = cast<PHINode>(I);
    // Handle the case of a simple two-predecessor recurrence PHI.
    // There's a lot more that could theoretically be done here, but
    // this is sufficient to catch some interesting cases.
    if (P->getNumIncomingValues() == 2) {
      for (unsigned i = 0; i != 2; ++i) {
        Value *L = P->getIncomingValue(i);
        Value *R = P->getIncomingValue(!i);
        Operator *LU = dyn_cast<Operator>(L);
        if (!LU)
          continue;
        unsigned Opcode = LU->getOpcode();
        // Check for operations that have the property that if
        // both their operands have low zero bits, the result
        // will have low zero bits.
        if (Opcode == Instruction::Add ||
            Opcode == Instruction::Sub ||
            Opcode == Instruction::And ||
            Opcode == Instruction::Or ||
            Opcode == Instruction::Mul) {
          Value *LL = LU->getOperand(0);
          Value *LR = LU->getOperand(1);
          // Find a recurrence.
          if (LL == I)
            L = LR;
          else if (LR == I)
            L = LL;
          else
            break;
          // Ok, we have a PHI of the form L op= R. Check for low
          // zero bits.
          computeKnownBits(R, KnownZero2, KnownOne2, TD, Depth+1, Q);

          // We need to take the minimum number of known bits
          APInt KnownZero3(KnownZero), KnownOne3(KnownOne);
          computeKnownBits(L, KnownZero3, KnownOne3, TD, Depth+1, Q);

          KnownZero = APInt::getLowBitsSet(BitWidth,
                                           std::min(KnownZero2.countTrailingOnes(),
                                                    KnownZero3.countTrailingOnes()));
          break;
        }
      }
    }

    // Unreachable blocks may have zero-operand PHI nodes.
    if (P->getNumIncomingValues() == 0)
      break;

    // Otherwise take the unions of the known bit sets of the operands,
    // taking conservative care to avoid excessive recursion.
    if (Depth < MaxDepth - 1 && !KnownZero && !KnownOne) {
      // Skip if every incoming value references to ourself.
      if (dyn_cast_or_null<UndefValue>(P->hasConstantValue()))
        break;

      KnownZero = APInt::getAllOnesValue(BitWidth);
      KnownOne = APInt::getAllOnesValue(BitWidth);
      for (unsigned i = 0, e = P->getNumIncomingValues(); i != e; ++i) {
        // Skip direct self references.
        if (P->getIncomingValue(i) == P) continue;

        KnownZero2 = APInt(BitWidth, 0);
        KnownOne2 = APInt(BitWidth, 0);
        // Recurse, but cap the recursion to one level, because we don't
        // want to waste time spinning around in loops.
        computeKnownBits(P->getIncomingValue(i), KnownZero2, KnownOne2, TD,
                         MaxDepth-1, Q);
        KnownZero &= KnownZero2;
        KnownOne &= KnownOne2;
        // If all bits have been ruled out, there's no need to check
        // more operands.
        if (!KnownZero && !KnownOne)
          break;
      }
    }
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke:
    if (MDNode *MD = cast<Instruction>(I)->getMetadata(LLVMContext::MD_range))
      computeKnownBitsFromRangeMetadata(*MD, KnownZero);
    // If a range metadata is attached to this IntrinsicInst, intersect the
    // explicit range specified by the metadata and the implicit range of
    // the intrinsic.
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::ctlz:
      case Intrinsic::cttz: {
        unsigned LowBits = Log2_32(BitWidth)+1;
        // If this call is undefined for 0, the result will be less than 2^n.
        if (II->getArgOperand(1) == ConstantInt::getTrue(II->getContext()))
          LowBits -= 1;
        KnownZero |= APInt::getHighBitsSet(BitWidth, BitWidth - LowBits);
        break;
      }
      case Intrinsic::ctpop: {
        unsigned LowBits = Log2_32(BitWidth)+1;
        KnownZero |= APInt::getHighBitsSet(BitWidth, BitWidth - LowBits);
        break;
      }
      case Intrinsic::x86_sse42_crc32_64_64:
        KnownZero |= APInt::getHighBitsSet(64, 32);
        break;
      }
    }
    break;
  case Instruction::ExtractValue:
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I->getOperand(0))) {
      ExtractValueInst *EVI = cast<ExtractValueInst>(I);
      if (EVI->getNumIndices() != 1) break;
      if (EVI->getIndices()[0] == 0) {
        switch (II->getIntrinsicID()) {
        default: break;
        case Intrinsic::uadd_with_overflow:
        case Intrinsic::sadd_with_overflow:
          computeKnownBitsAddSub(true, II->getArgOperand(0),
                                 II->getArgOperand(1), false, KnownZero,
                                 KnownOne, KnownZero2, KnownOne2, TD, Depth, Q);
          break;
        case Intrinsic::usub_with_overflow:
        case Intrinsic::ssub_with_overflow:
          computeKnownBitsAddSub(false, II->getArgOperand(0),
                                 II->getArgOperand(1), false, KnownZero,
                                 KnownOne, KnownZero2, KnownOne2, TD, Depth, Q);
          break;
        case Intrinsic::umul_with_overflow:
        case Intrinsic::smul_with_overflow:
          computeKnownBitsMul(II->getArgOperand(0), II->getArgOperand(1),
                              false, KnownZero, KnownOne,
                              KnownZero2, KnownOne2, TD, Depth, Q);
          break;
        }
      }
    }
  }

  assert((KnownZero & KnownOne) == 0 && "Bits known to be one AND zero?");
}

/// Determine whether the sign bit is known to be zero or one.
/// Convenience wrapper around computeKnownBits.
void ComputeSignBit(Value *V, bool &KnownZero, bool &KnownOne,
                    const DataLayout *TD, unsigned Depth,
                    const Query &Q) {
  unsigned BitWidth = getBitWidth(V->getType(), TD);
  if (!BitWidth) {
    KnownZero = false;
    KnownOne = false;
    return;
  }
  APInt ZeroBits(BitWidth, 0);
  APInt OneBits(BitWidth, 0);
  computeKnownBits(V, ZeroBits, OneBits, TD, Depth, Q);
  KnownOne = OneBits[BitWidth - 1];
  KnownZero = ZeroBits[BitWidth - 1];
}

/// Return true if the given value is known to have exactly one
/// bit set when defined. For vectors return true if every element is known to
/// be a power of two when defined. Supports values with integer or pointer
/// types and vectors of integers.
bool isKnownToBeAPowerOfTwo(Value *V, bool OrZero, unsigned Depth,
                            const Query &Q) {
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (C->isNullValue())
      return OrZero;
    if (ConstantInt *CI = dyn_cast<ConstantInt>(C))
      return CI->getValue().isPowerOf2();
    // TODO: Handle vector constants.
  }

  // 1 << X is clearly a power of two if the one is not shifted off the end.  If
  // it is shifted off the end then the result is undefined.
  if (match(V, m_Shl(m_One(), m_Value())))
    return true;

  // (signbit) >>l X is clearly a power of two if the one is not shifted off the
  // bottom.  If it is shifted off the bottom then the result is undefined.
  if (match(V, m_LShr(m_SignBit(), m_Value())))
    return true;

  // The remaining tests are all recursive, so bail out if we hit the limit.
  if (Depth++ == MaxDepth)
    return false;

  Value *X = nullptr, *Y = nullptr;
  // A shift of a power of two is a power of two or zero.
  if (OrZero && (match(V, m_Shl(m_Value(X), m_Value())) ||
                 match(V, m_Shr(m_Value(X), m_Value()))))
    return isKnownToBeAPowerOfTwo(X, /*OrZero*/true, Depth, Q);

  if (ZExtInst *ZI = dyn_cast<ZExtInst>(V))
    return isKnownToBeAPowerOfTwo(ZI->getOperand(0), OrZero, Depth, Q);

  if (SelectInst *SI = dyn_cast<SelectInst>(V))
    return
      isKnownToBeAPowerOfTwo(SI->getTrueValue(), OrZero, Depth, Q) &&
      isKnownToBeAPowerOfTwo(SI->getFalseValue(), OrZero, Depth, Q);

  if (OrZero && match(V, m_And(m_Value(X), m_Value(Y)))) {
    // A power of two and'd with anything is a power of two or zero.
    if (isKnownToBeAPowerOfTwo(X, /*OrZero*/true, Depth, Q) ||
        isKnownToBeAPowerOfTwo(Y, /*OrZero*/true, Depth, Q))
      return true;
    // X & (-X) is always a power of two or zero.
    if (match(X, m_Neg(m_Specific(Y))) || match(Y, m_Neg(m_Specific(X))))
      return true;
    return false;
  }

  // Adding a power-of-two or zero to the same power-of-two or zero yields
  // either the original power-of-two, a larger power-of-two or zero.
  if (match(V, m_Add(m_Value(X), m_Value(Y)))) {
    OverflowingBinaryOperator *VOBO = cast<OverflowingBinaryOperator>(V);
    if (OrZero || VOBO->hasNoUnsignedWrap() || VOBO->hasNoSignedWrap()) {
      if (match(X, m_And(m_Specific(Y), m_Value())) ||
          match(X, m_And(m_Value(), m_Specific(Y))))
        if (isKnownToBeAPowerOfTwo(Y, OrZero, Depth, Q))
          return true;
      if (match(Y, m_And(m_Specific(X), m_Value())) ||
          match(Y, m_And(m_Value(), m_Specific(X))))
        if (isKnownToBeAPowerOfTwo(X, OrZero, Depth, Q))
          return true;

      unsigned BitWidth = V->getType()->getScalarSizeInBits();
      APInt LHSZeroBits(BitWidth, 0), LHSOneBits(BitWidth, 0);
      computeKnownBits(X, LHSZeroBits, LHSOneBits, nullptr, Depth, Q);

      APInt RHSZeroBits(BitWidth, 0), RHSOneBits(BitWidth, 0);
      computeKnownBits(Y, RHSZeroBits, RHSOneBits, nullptr, Depth, Q);
      // If i8 V is a power of two or zero:
      //  ZeroBits: 1 1 1 0 1 1 1 1
      // ~ZeroBits: 0 0 0 1 0 0 0 0
      if ((~(LHSZeroBits & RHSZeroBits)).isPowerOf2())
        // If OrZero isn't set, we cannot give back a zero result.
        // Make sure either the LHS or RHS has a bit set.
        if (OrZero || RHSOneBits.getBoolValue() || LHSOneBits.getBoolValue())
          return true;
    }
  }

  // An exact divide or right shift can only shift off zero bits, so the result
  // is a power of two only if the first operand is a power of two and not
  // copying a sign bit (sdiv int_min, 2).
  if (match(V, m_Exact(m_LShr(m_Value(), m_Value()))) ||
      match(V, m_Exact(m_UDiv(m_Value(), m_Value())))) {
    return isKnownToBeAPowerOfTwo(cast<Operator>(V)->getOperand(0), OrZero,
                                  Depth, Q);
  }

  return false;
}

/// \brief Test whether a GEP's result is known to be non-null.
///
/// Uses properties inherent in a GEP to try to determine whether it is known
/// to be non-null.
///
/// Currently this routine does not support vector GEPs.
static bool isGEPKnownNonNull(GEPOperator *GEP, const DataLayout *DL,
                              unsigned Depth, const Query &Q) {
  if (!GEP->isInBounds() || GEP->getPointerAddressSpace() != 0)
    return false;

  // FIXME: Support vector-GEPs.
  assert(GEP->getType()->isPointerTy() && "We only support plain pointer GEP");

  // If the base pointer is non-null, we cannot walk to a null address with an
  // inbounds GEP in address space zero.
  if (isKnownNonZero(GEP->getPointerOperand(), DL, Depth, Q))
    return true;

  // Past this, if we don't have DataLayout, we can't do much.
  if (!DL)
    return false;

  // Walk the GEP operands and see if any operand introduces a non-zero offset.
  // If so, then the GEP cannot produce a null pointer, as doing so would
  // inherently violate the inbounds contract within address space zero.
  for (gep_type_iterator GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
       GTI != GTE; ++GTI) {
    // Struct types are easy -- they must always be indexed by a constant.
    if (StructType *STy = dyn_cast<StructType>(*GTI)) {
      ConstantInt *OpC = cast<ConstantInt>(GTI.getOperand());
      unsigned ElementIdx = OpC->getZExtValue();
      const StructLayout *SL = DL->getStructLayout(STy);
      uint64_t ElementOffset = SL->getElementOffset(ElementIdx);
      if (ElementOffset > 0)
        return true;
      continue;
    }

    // If we have a zero-sized type, the index doesn't matter. Keep looping.
    if (DL->getTypeAllocSize(GTI.getIndexedType()) == 0)
      continue;

    // Fast path the constant operand case both for efficiency and so we don't
    // increment Depth when just zipping down an all-constant GEP.
    if (ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand())) {
      if (!OpC->isZero())
        return true;
      continue;
    }

    // We post-increment Depth here because while isKnownNonZero increments it
    // as well, when we pop back up that increment won't persist. We don't want
    // to recurse 10k times just because we have 10k GEP operands. We don't
    // bail completely out because we want to handle constant GEPs regardless
    // of depth.
    if (Depth++ >= MaxDepth)
      continue;

    if (isKnownNonZero(GTI.getOperand(), DL, Depth, Q))
      return true;
  }

  return false;
}

/// Does the 'Range' metadata (which must be a valid MD_range operand list)
/// ensure that the value it's attached to is never Value?  'RangeType' is
/// is the type of the value described by the range.
static bool rangeMetadataExcludesValue(MDNode* Ranges,
                                       const APInt& Value) {
  const unsigned NumRanges = Ranges->getNumOperands() / 2;
  assert(NumRanges >= 1);
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 0));
    ConstantInt *Upper =
        mdconst::extract<ConstantInt>(Ranges->getOperand(2 * i + 1));
    ConstantRange Range(Lower->getValue(), Upper->getValue());
    if (Range.contains(Value))
      return false;
  }
  return true;
}

/// Return true if the given value is known to be non-zero when defined.
/// For vectors return true if every element is known to be non-zero when
/// defined. Supports values with integer or pointer type and vectors of
/// integers.
bool isKnownNonZero(Value *V, const DataLayout *TD, unsigned Depth,
                    const Query &Q) {
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (C->isNullValue())
      return false;
    if (isa<ConstantInt>(C))
      // Must be non-zero due to null test above.
      return true;
    // TODO: Handle vectors
    return false;
  }

  if (Instruction* I = dyn_cast<Instruction>(V)) {
    if (MDNode *Ranges = I->getMetadata(LLVMContext::MD_range)) {
      // If the possible ranges don't contain zero, then the value is
      // definitely non-zero.
      if (IntegerType* Ty = dyn_cast<IntegerType>(V->getType())) {
        const APInt ZeroValue(Ty->getBitWidth(), 0);
        if (rangeMetadataExcludesValue(Ranges, ZeroValue))
          return true;
      }
    }
  }

  // The remaining tests are all recursive, so bail out if we hit the limit.
  if (Depth++ >= MaxDepth)
    return false;

  // Check for pointer simplifications.
  if (V->getType()->isPointerTy()) {
    if (isKnownNonNull(V))
      return true; 
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V))
      if (isGEPKnownNonNull(GEP, TD, Depth, Q))
        return true;
  }

  unsigned BitWidth = getBitWidth(V->getType()->getScalarType(), TD);

  // X | Y != 0 if X != 0 or Y != 0.
  Value *X = nullptr, *Y = nullptr;
  if (match(V, m_Or(m_Value(X), m_Value(Y))))
    return isKnownNonZero(X, TD, Depth, Q) ||
           isKnownNonZero(Y, TD, Depth, Q);

  // ext X != 0 if X != 0.
  if (isa<SExtInst>(V) || isa<ZExtInst>(V))
    return isKnownNonZero(cast<Instruction>(V)->getOperand(0), TD, Depth, Q);

  // shl X, Y != 0 if X is odd.  Note that the value of the shift is undefined
  // if the lowest bit is shifted off the end.
  if (BitWidth && match(V, m_Shl(m_Value(X), m_Value(Y)))) {
    // shl nuw can't remove any non-zero bits.
    OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(V);
    if (BO->hasNoUnsignedWrap())
      return isKnownNonZero(X, TD, Depth, Q);

    APInt KnownZero(BitWidth, 0);
    APInt KnownOne(BitWidth, 0);
    computeKnownBits(X, KnownZero, KnownOne, TD, Depth, Q);
    if (KnownOne[0])
      return true;
  }
  // shr X, Y != 0 if X is negative.  Note that the value of the shift is not
  // defined if the sign bit is shifted off the end.
  else if (match(V, m_Shr(m_Value(X), m_Value(Y)))) {
    // shr exact can only shift out zero bits.
    PossiblyExactOperator *BO = cast<PossiblyExactOperator>(V);
    if (BO->isExact())
      return isKnownNonZero(X, TD, Depth, Q);

    bool XKnownNonNegative, XKnownNegative;
    ComputeSignBit(X, XKnownNonNegative, XKnownNegative, TD, Depth, Q);
    if (XKnownNegative)
      return true;
  }
  // div exact can only produce a zero if the dividend is zero.
  else if (match(V, m_Exact(m_IDiv(m_Value(X), m_Value())))) {
    return isKnownNonZero(X, TD, Depth, Q);
  }
  // X + Y.
  else if (match(V, m_Add(m_Value(X), m_Value(Y)))) {
    bool XKnownNonNegative, XKnownNegative;
    bool YKnownNonNegative, YKnownNegative;
    ComputeSignBit(X, XKnownNonNegative, XKnownNegative, TD, Depth, Q);
    ComputeSignBit(Y, YKnownNonNegative, YKnownNegative, TD, Depth, Q);

    // If X and Y are both non-negative (as signed values) then their sum is not
    // zero unless both X and Y are zero.
    if (XKnownNonNegative && YKnownNonNegative)
      if (isKnownNonZero(X, TD, Depth, Q) ||
          isKnownNonZero(Y, TD, Depth, Q))
        return true;

    // If X and Y are both negative (as signed values) then their sum is not
    // zero unless both X and Y equal INT_MIN.
    if (BitWidth && XKnownNegative && YKnownNegative) {
      APInt KnownZero(BitWidth, 0);
      APInt KnownOne(BitWidth, 0);
      APInt Mask = APInt::getSignedMaxValue(BitWidth);
      // The sign bit of X is set.  If some other bit is set then X is not equal
      // to INT_MIN.
      computeKnownBits(X, KnownZero, KnownOne, TD, Depth, Q);
      if ((KnownOne & Mask) != 0)
        return true;
      // The sign bit of Y is set.  If some other bit is set then Y is not equal
      // to INT_MIN.
      computeKnownBits(Y, KnownZero, KnownOne, TD, Depth, Q);
      if ((KnownOne & Mask) != 0)
        return true;
    }

    // The sum of a non-negative number and a power of two is not zero.
    if (XKnownNonNegative &&
        isKnownToBeAPowerOfTwo(Y, /*OrZero*/false, Depth, Q))
      return true;
    if (YKnownNonNegative &&
        isKnownToBeAPowerOfTwo(X, /*OrZero*/false, Depth, Q))
      return true;
  }
  // X * Y.
  else if (match(V, m_Mul(m_Value(X), m_Value(Y)))) {
    OverflowingBinaryOperator *BO = cast<OverflowingBinaryOperator>(V);
    // If X and Y are non-zero then so is X * Y as long as the multiplication
    // does not overflow.
    if ((BO->hasNoSignedWrap() || BO->hasNoUnsignedWrap()) &&
        isKnownNonZero(X, TD, Depth, Q) &&
        isKnownNonZero(Y, TD, Depth, Q))
      return true;
  }
  // (C ? X : Y) != 0 if X != 0 and Y != 0.
  else if (SelectInst *SI = dyn_cast<SelectInst>(V)) {
    if (isKnownNonZero(SI->getTrueValue(), TD, Depth, Q) &&
        isKnownNonZero(SI->getFalseValue(), TD, Depth, Q))
      return true;
  }

  if (!BitWidth) return false;
  APInt KnownZero(BitWidth, 0);
  APInt KnownOne(BitWidth, 0);
  computeKnownBits(V, KnownZero, KnownOne, TD, Depth, Q);
  return KnownOne != 0;
}

/// Return true if 'V & Mask' is known to be zero.  We use this predicate to
/// simplify operations downstream. Mask is known to be zero for bits that V
/// cannot have.
///
/// This function is defined on values with integer type, values with pointer
/// type (but only if TD is non-null), and vectors of integers.  In the case
/// where V is a vector, the mask, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
bool MaskedValueIsZero(Value *V, const APInt &Mask,
                       const DataLayout *TD, unsigned Depth,
                       const Query &Q) {
  APInt KnownZero(Mask.getBitWidth(), 0), KnownOne(Mask.getBitWidth(), 0);
  computeKnownBits(V, KnownZero, KnownOne, TD, Depth, Q);
  return (KnownZero & Mask) == Mask;
}



/// Return the number of times the sign bit of the register is replicated into
/// the other bits. We know that at least 1 bit is always equal to the sign bit
/// (itself), but other cases can give us information. For example, immediately
/// after an "ashr X, 2", we know that the top 3 bits are all equal to each
/// other, so we return 3.
///
/// 'Op' must have a scalar integer type.
///
unsigned ComputeNumSignBits(Value *V, const DataLayout *TD,
                            unsigned Depth, const Query &Q) {
  assert((TD || V->getType()->isIntOrIntVectorTy()) &&
         "ComputeNumSignBits requires a DataLayout object to operate "
         "on non-integer values!");
  Type *Ty = V->getType();
  unsigned TyBits = TD ? TD->getTypeSizeInBits(V->getType()->getScalarType()) :
                         Ty->getScalarSizeInBits();
  unsigned Tmp, Tmp2;
  unsigned FirstAnswer = 1;

  // Note that ConstantInt is handled by the general computeKnownBits case
  // below.

  if (Depth == 6)
    return 1;  // Limit search depth.

  Operator *U = dyn_cast<Operator>(V);
  switch (Operator::getOpcode(V)) {
  default: break;
  case Instruction::SExt:
    Tmp = TyBits - U->getOperand(0)->getType()->getScalarSizeInBits();
    return ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q) + Tmp;

  case Instruction::AShr: {
    Tmp = ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q);
    // ashr X, C   -> adds C sign bits.  Vectors too.
    const APInt *ShAmt;
    if (match(U->getOperand(1), m_APInt(ShAmt))) {
      Tmp += ShAmt->getZExtValue();
      if (Tmp > TyBits) Tmp = TyBits;
    }
    return Tmp;
  }
  case Instruction::Shl: {
    const APInt *ShAmt;
    if (match(U->getOperand(1), m_APInt(ShAmt))) {
      // shl destroys sign bits.
      Tmp = ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q);
      Tmp2 = ShAmt->getZExtValue();
      if (Tmp2 >= TyBits ||      // Bad shift.
          Tmp2 >= Tmp) break;    // Shifted all sign bits out.
      return Tmp - Tmp2;
    }
    break;
  }
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:    // NOT is handled here.
    // Logical binary ops preserve the number of sign bits at the worst.
    Tmp = ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q);
    if (Tmp != 1) {
      Tmp2 = ComputeNumSignBits(U->getOperand(1), TD, Depth+1, Q);
      FirstAnswer = std::min(Tmp, Tmp2);
      // We computed what we know about the sign bits as our first
      // answer. Now proceed to the generic code that uses
      // computeKnownBits, and pick whichever answer is better.
    }
    break;

  case Instruction::Select:
    Tmp = ComputeNumSignBits(U->getOperand(1), TD, Depth+1, Q);
    if (Tmp == 1) return 1;  // Early out.
    Tmp2 = ComputeNumSignBits(U->getOperand(2), TD, Depth+1, Q);
    return std::min(Tmp, Tmp2);

  case Instruction::Add:
    // Add can have at most one carry bit.  Thus we know that the output
    // is, at worst, one more bit than the inputs.
    Tmp = ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q);
    if (Tmp == 1) return 1;  // Early out.

    // Special case decrementing a value (ADD X, -1):
    if (const auto *CRHS = dyn_cast<Constant>(U->getOperand(1)))
      if (CRHS->isAllOnesValue()) {
        APInt KnownZero(TyBits, 0), KnownOne(TyBits, 0);
        computeKnownBits(U->getOperand(0), KnownZero, KnownOne, TD, Depth+1, Q);

        // If the input is known to be 0 or 1, the output is 0/-1, which is all
        // sign bits set.
        if ((KnownZero | APInt(TyBits, 1)).isAllOnesValue())
          return TyBits;

        // If we are subtracting one from a positive number, there is no carry
        // out of the result.
        if (KnownZero.isNegative())
          return Tmp;
      }

    Tmp2 = ComputeNumSignBits(U->getOperand(1), TD, Depth+1, Q);
    if (Tmp2 == 1) return 1;
    return std::min(Tmp, Tmp2)-1;

  case Instruction::Sub:
    Tmp2 = ComputeNumSignBits(U->getOperand(1), TD, Depth+1, Q);
    if (Tmp2 == 1) return 1;

    // Handle NEG.
    if (const auto *CLHS = dyn_cast<Constant>(U->getOperand(0)))
      if (CLHS->isNullValue()) {
        APInt KnownZero(TyBits, 0), KnownOne(TyBits, 0);
        computeKnownBits(U->getOperand(1), KnownZero, KnownOne, TD, Depth+1, Q);
        // If the input is known to be 0 or 1, the output is 0/-1, which is all
        // sign bits set.
        if ((KnownZero | APInt(TyBits, 1)).isAllOnesValue())
          return TyBits;

        // If the input is known to be positive (the sign bit is known clear),
        // the output of the NEG has the same number of sign bits as the input.
        if (KnownZero.isNegative())
          return Tmp2;

        // Otherwise, we treat this like a SUB.
      }

    // Sub can have at most one carry bit.  Thus we know that the output
    // is, at worst, one more bit than the inputs.
    Tmp = ComputeNumSignBits(U->getOperand(0), TD, Depth+1, Q);
    if (Tmp == 1) return 1;  // Early out.
    return std::min(Tmp, Tmp2)-1;

  case Instruction::PHI: {
    PHINode *PN = cast<PHINode>(U);
    unsigned NumIncomingValues = PN->getNumIncomingValues();
    // Don't analyze large in-degree PHIs.
    if (NumIncomingValues > 4) break;
    // Unreachable blocks may have zero-operand PHI nodes.
    if (NumIncomingValues == 0) break;

    // Take the minimum of all incoming values.  This can't infinitely loop
    // because of our depth threshold.
    Tmp = ComputeNumSignBits(PN->getIncomingValue(0), TD, Depth+1, Q);
    for (unsigned i = 1, e = NumIncomingValues; i != e; ++i) {
      if (Tmp == 1) return Tmp;
      Tmp = std::min(Tmp,
                     ComputeNumSignBits(PN->getIncomingValue(i), TD,
                                        Depth+1, Q));
    }
    return Tmp;
  }

  case Instruction::Trunc:
    // FIXME: it's tricky to do anything useful for this, but it is an important
    // case for targets like X86.
    break;
  }

  // Finally, if we can prove that the top bits of the result are 0's or 1's,
  // use this information.
  APInt KnownZero(TyBits, 0), KnownOne(TyBits, 0);
  APInt Mask;
  computeKnownBits(V, KnownZero, KnownOne, TD, Depth, Q);

  if (KnownZero.isNegative()) {        // sign bit is 0
    Mask = KnownZero;
  } else if (KnownOne.isNegative()) {  // sign bit is 1;
    Mask = KnownOne;
  } else {
    // Nothing known.
    return FirstAnswer;
  }

  // Okay, we know that the sign bit in Mask is set.  Use CLZ to determine
  // the number of identical bits in the top of the input value.
  Mask = ~Mask;
  Mask <<= Mask.getBitWidth()-TyBits;
  // Return # leading zeros.  We use 'min' here in case Val was zero before
  // shifting.  We don't want to return '64' as for an i32 "0".
  return std::max(FirstAnswer, std::min(TyBits, Mask.countLeadingZeros()));
}

/// This function computes the integer multiple of Base that equals V.
/// If successful, it returns true and returns the multiple in
/// Multiple. If unsuccessful, it returns false. It looks
/// through SExt instructions only if LookThroughSExt is true.
bool llvm::ComputeMultiple(Value *V, unsigned Base, Value *&Multiple,
                           bool LookThroughSExt, unsigned Depth) {
  const unsigned MaxDepth = 6;

  assert(V && "No Value?");
  assert(Depth <= MaxDepth && "Limit Search Depth");
  assert(V->getType()->isIntegerTy() && "Not integer or pointer type!");

  Type *T = V->getType();

  ConstantInt *CI = dyn_cast<ConstantInt>(V);

  if (Base == 0)
    return false;

  if (Base == 1) {
    Multiple = V;
    return true;
  }

  ConstantExpr *CO = dyn_cast<ConstantExpr>(V);
  Constant *BaseVal = ConstantInt::get(T, Base);
  if (CO && CO == BaseVal) {
    // Multiple is 1.
    Multiple = ConstantInt::get(T, 1);
    return true;
  }

  if (CI && CI->getZExtValue() % Base == 0) {
    Multiple = ConstantInt::get(T, CI->getZExtValue() / Base);
    return true;
  }

  if (Depth == MaxDepth) return false;  // Limit search depth.

  Operator *I = dyn_cast<Operator>(V);
  if (!I) return false;

  switch (I->getOpcode()) {
  default: break;
  case Instruction::SExt:
    if (!LookThroughSExt) return false;
    // otherwise fall through to ZExt
  case Instruction::ZExt:
    return ComputeMultiple(I->getOperand(0), Base, Multiple,
                           LookThroughSExt, Depth+1);
  case Instruction::Shl:
  case Instruction::Mul: {
    Value *Op0 = I->getOperand(0);
    Value *Op1 = I->getOperand(1);

    if (I->getOpcode() == Instruction::Shl) {
      ConstantInt *Op1CI = dyn_cast<ConstantInt>(Op1);
      if (!Op1CI) return false;
      // Turn Op0 << Op1 into Op0 * 2^Op1
      APInt Op1Int = Op1CI->getValue();
      uint64_t BitToSet = Op1Int.getLimitedValue(Op1Int.getBitWidth() - 1);
      APInt API(Op1Int.getBitWidth(), 0);
      API.setBit(BitToSet);
      Op1 = ConstantInt::get(V->getContext(), API);
    }

    Value *Mul0 = nullptr;
    if (ComputeMultiple(Op0, Base, Mul0, LookThroughSExt, Depth+1)) {
      if (Constant *Op1C = dyn_cast<Constant>(Op1))
        if (Constant *MulC = dyn_cast<Constant>(Mul0)) {
          if (Op1C->getType()->getPrimitiveSizeInBits() <
              MulC->getType()->getPrimitiveSizeInBits())
            Op1C = ConstantExpr::getZExt(Op1C, MulC->getType());
          if (Op1C->getType()->getPrimitiveSizeInBits() >
              MulC->getType()->getPrimitiveSizeInBits())
            MulC = ConstantExpr::getZExt(MulC, Op1C->getType());

          // V == Base * (Mul0 * Op1), so return (Mul0 * Op1)
          Multiple = ConstantExpr::getMul(MulC, Op1C);
          return true;
        }

      if (ConstantInt *Mul0CI = dyn_cast<ConstantInt>(Mul0))
        if (Mul0CI->getValue() == 1) {
          // V == Base * Op1, so return Op1
          Multiple = Op1;
          return true;
        }
    }

    Value *Mul1 = nullptr;
    if (ComputeMultiple(Op1, Base, Mul1, LookThroughSExt, Depth+1)) {
      if (Constant *Op0C = dyn_cast<Constant>(Op0))
        if (Constant *MulC = dyn_cast<Constant>(Mul1)) {
          if (Op0C->getType()->getPrimitiveSizeInBits() <
              MulC->getType()->getPrimitiveSizeInBits())
            Op0C = ConstantExpr::getZExt(Op0C, MulC->getType());
          if (Op0C->getType()->getPrimitiveSizeInBits() >
              MulC->getType()->getPrimitiveSizeInBits())
            MulC = ConstantExpr::getZExt(MulC, Op0C->getType());

          // V == Base * (Mul1 * Op0), so return (Mul1 * Op0)
          Multiple = ConstantExpr::getMul(MulC, Op0C);
          return true;
        }

      if (ConstantInt *Mul1CI = dyn_cast<ConstantInt>(Mul1))
        if (Mul1CI->getValue() == 1) {
          // V == Base * Op0, so return Op0
          Multiple = Op0;
          return true;
        }
    }
  }
  }

  // We could not determine if V is a multiple of Base.
  return false;
}

/// Return true if we can prove that the specified FP value is never equal to
/// -0.0.
///
/// NOTE: this function will need to be revisited when we support non-default
/// rounding modes!
///
bool llvm::CannotBeNegativeZero(const Value *V, unsigned Depth) {
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(V))
    return !CFP->getValueAPF().isNegZero();

  if (Depth == 6)
    return 1;  // Limit search depth.

  const Operator *I = dyn_cast<Operator>(V);
  if (!I) return false;

  // Check if the nsz fast-math flag is set
  if (const FPMathOperator *FPO = dyn_cast<FPMathOperator>(I))
    if (FPO->hasNoSignedZeros())
      return true;

  // (add x, 0.0) is guaranteed to return +0.0, not -0.0.
  if (I->getOpcode() == Instruction::FAdd)
    if (ConstantFP *CFP = dyn_cast<ConstantFP>(I->getOperand(1)))
      if (CFP->isNullValue())
        return true;

  // sitofp and uitofp turn into +0.0 for zero.
  if (isa<SIToFPInst>(I) || isa<UIToFPInst>(I))
    return true;

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    // sqrt(-0.0) = -0.0, no other negative results are possible.
    if (II->getIntrinsicID() == Intrinsic::sqrt)
      return CannotBeNegativeZero(II->getArgOperand(0), Depth+1);

  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (const Function *F = CI->getCalledFunction()) {
      if (F->isDeclaration()) {
        // abs(x) != -0.0
        if (F->getName() == "abs") return true;
        // fabs[lf](x) != -0.0
        if (F->getName() == "fabs") return true;
        if (F->getName() == "fabsf") return true;
        if (F->getName() == "fabsl") return true;
        if (F->getName() == "sqrt" || F->getName() == "sqrtf" ||
            F->getName() == "sqrtl")
          return CannotBeNegativeZero(CI->getArgOperand(0), Depth+1);
      }
    }

  return false;
}

bool llvm::CannotBeOrderedLessThanZero(const Value *V, unsigned Depth) {
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(V))
    return !CFP->getValueAPF().isNegative() || CFP->getValueAPF().isZero();

  if (Depth == 6)
    return false;  // Limit search depth.

  const Operator *I = dyn_cast<Operator>(V);
  if (!I) return false;

  switch (I->getOpcode()) {
  default: break;
  case Instruction::FMul:
    // x*x is always non-negative or a NaN.
    if (I->getOperand(0) == I->getOperand(1)) 
      return true;
    // Fall through
  case Instruction::FAdd:
  case Instruction::FDiv:
  case Instruction::FRem:
    return CannotBeOrderedLessThanZero(I->getOperand(0), Depth+1) &&
           CannotBeOrderedLessThanZero(I->getOperand(1), Depth+1);
  case Instruction::FPExt:
  case Instruction::FPTrunc:
    // Widening/narrowing never change sign.
    return CannotBeOrderedLessThanZero(I->getOperand(0), Depth+1);
  case Instruction::Call: 
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) 
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::exp:
      case Intrinsic::exp2:
      case Intrinsic::fabs:
      case Intrinsic::sqrt:
        return true;
      case Intrinsic::powi: 
        if (ConstantInt *CI = dyn_cast<ConstantInt>(I->getOperand(1))) {
          // powi(x,n) is non-negative if n is even.
          if (CI->getBitWidth() <= 64 && CI->getSExtValue() % 2u == 0)
            return true;
        }
        return CannotBeOrderedLessThanZero(I->getOperand(0), Depth+1);
      case Intrinsic::fma:
      case Intrinsic::fmuladd:
        // x*x+y is non-negative if y is non-negative.
        return I->getOperand(0) == I->getOperand(1) && 
               CannotBeOrderedLessThanZero(I->getOperand(2), Depth+1);
      }
    break;
  }
  return false; 
}

/// If the specified value can be set by repeating the same byte in memory,
/// return the i8 value that it is represented with.  This is
/// true for all i8 values obviously, but is also true for i32 0, i32 -1,
/// i16 0xF0F0, double 0.0 etc.  If the value can't be handled with a repeated
/// byte store (e.g. i16 0x1234), return null.
Value *llvm::isBytewiseValue(Value *V) {
  // All byte-wide stores are splatable, even of arbitrary variables.
  if (V->getType()->isIntegerTy(8)) return V;

  // Handle 'null' ConstantArrayZero etc.
  if (Constant *C = dyn_cast<Constant>(V))
    if (C->isNullValue())
      return Constant::getNullValue(Type::getInt8Ty(V->getContext()));

  // Constant float and double values can be handled as integer values if the
  // corresponding integer value is "byteable".  An important case is 0.0.
  if (ConstantFP *CFP = dyn_cast<ConstantFP>(V)) {
    if (CFP->getType()->isFloatTy())
      V = ConstantExpr::getBitCast(CFP, Type::getInt32Ty(V->getContext()));
    if (CFP->getType()->isDoubleTy())
      V = ConstantExpr::getBitCast(CFP, Type::getInt64Ty(V->getContext()));
    // Don't handle long double formats, which have strange constraints.
  }

  // We can handle constant integers that are multiple of 8 bits.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getBitWidth() % 8 == 0) {
      assert(CI->getBitWidth() > 8 && "8 bits should be handled above!");

      // We can check that all bytes of an integer are equal by making use of a
      // little trick: rotate by 8 and check if it's still the same value.
      if (CI->getValue() != CI->getValue().rotl(8))
        return nullptr;
      return ConstantInt::get(V->getContext(), CI->getValue().trunc(8));
    }
  }

  // A ConstantDataArray/Vector is splatable if all its members are equal and
  // also splatable.
  if (ConstantDataSequential *CA = dyn_cast<ConstantDataSequential>(V)) {
    Value *Elt = CA->getElementAsConstant(0);
    Value *Val = isBytewiseValue(Elt);
    if (!Val)
      return nullptr;

    for (unsigned I = 1, E = CA->getNumElements(); I != E; ++I)
      if (CA->getElementAsConstant(I) != Elt)
        return nullptr;

    return Val;
  }

  // Conceptually, we could handle things like:
  //   %a = zext i8 %X to i16
  //   %b = shl i16 %a, 8
  //   %c = or i16 %a, %b
  // but until there is an example that actually needs this, it doesn't seem
  // worth worrying about.
  return nullptr;
}


// This is the recursive version of BuildSubAggregate. It takes a few different
// arguments. Idxs is the index within the nested struct From that we are
// looking at now (which is of type IndexedType). IdxSkip is the number of
// indices from Idxs that should be left out when inserting into the resulting
// struct. To is the result struct built so far, new insertvalue instructions
// build on that.
static Value *BuildSubAggregate(Value *From, Value* To, Type *IndexedType,
                                SmallVectorImpl<unsigned> &Idxs,
                                unsigned IdxSkip,
                                Instruction *InsertBefore) {
  llvm::StructType *STy = dyn_cast<llvm::StructType>(IndexedType);
  if (STy) {
    // Save the original To argument so we can modify it
    Value *OrigTo = To;
    // General case, the type indexed by Idxs is a struct
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
      // Process each struct element recursively
      Idxs.push_back(i);
      Value *PrevTo = To;
      To = BuildSubAggregate(From, To, STy->getElementType(i), Idxs, IdxSkip,
                             InsertBefore);
      Idxs.pop_back();
      if (!To) {
        // Couldn't find any inserted value for this index? Cleanup
        while (PrevTo != OrigTo) {
          InsertValueInst* Del = cast<InsertValueInst>(PrevTo);
          PrevTo = Del->getAggregateOperand();
          Del->eraseFromParent();
        }
        // Stop processing elements
        break;
      }
    }
    // If we successfully found a value for each of our subaggregates
    if (To)
      return To;
  }
  // Base case, the type indexed by SourceIdxs is not a struct, or not all of
  // the struct's elements had a value that was inserted directly. In the latter
  // case, perhaps we can't determine each of the subelements individually, but
  // we might be able to find the complete struct somewhere.

  // Find the value that is at that particular spot
  Value *V = FindInsertedValue(From, Idxs);

  if (!V)
    return nullptr;

  // Insert the value in the new (sub) aggregrate
  return llvm::InsertValueInst::Create(To, V, makeArrayRef(Idxs).slice(IdxSkip),
                                       "tmp", InsertBefore);
}

// This helper takes a nested struct and extracts a part of it (which is again a
// struct) into a new value. For example, given the struct:
// { a, { b, { c, d }, e } }
// and the indices "1, 1" this returns
// { c, d }.
//
// It does this by inserting an insertvalue for each element in the resulting
// struct, as opposed to just inserting a single struct. This will only work if
// each of the elements of the substruct are known (ie, inserted into From by an
// insertvalue instruction somewhere).
//
// All inserted insertvalue instructions are inserted before InsertBefore
static Value *BuildSubAggregate(Value *From, ArrayRef<unsigned> idx_range,
                                Instruction *InsertBefore) {
  assert(InsertBefore && "Must have someplace to insert!");
  Type *IndexedType = ExtractValueInst::getIndexedType(From->getType(),
                                                             idx_range);
  Value *To = UndefValue::get(IndexedType);
  SmallVector<unsigned, 10> Idxs(idx_range.begin(), idx_range.end());
  unsigned IdxSkip = Idxs.size();

  return BuildSubAggregate(From, To, IndexedType, Idxs, IdxSkip, InsertBefore);
}

/// Given an aggregrate and an sequence of indices, see if
/// the scalar value indexed is already around as a register, for example if it
/// were inserted directly into the aggregrate.
///
/// If InsertBefore is not null, this function will duplicate (modified)
/// insertvalues when a part of a nested struct is extracted.
Value *llvm::FindInsertedValue(Value *V, ArrayRef<unsigned> idx_range,
                               Instruction *InsertBefore) {
  // Nothing to index? Just return V then (this is useful at the end of our
  // recursion).
  if (idx_range.empty())
    return V;
  // We have indices, so V should have an indexable type.
  assert((V->getType()->isStructTy() || V->getType()->isArrayTy()) &&
         "Not looking at a struct or array?");
  assert(ExtractValueInst::getIndexedType(V->getType(), idx_range) &&
         "Invalid indices for type?");

  if (Constant *C = dyn_cast<Constant>(V)) {
    C = C->getAggregateElement(idx_range[0]);
    if (!C) return nullptr;
    return FindInsertedValue(C, idx_range.slice(1), InsertBefore);
  }

  if (InsertValueInst *I = dyn_cast<InsertValueInst>(V)) {
    // Loop the indices for the insertvalue instruction in parallel with the
    // requested indices
    const unsigned *req_idx = idx_range.begin();
    for (const unsigned *i = I->idx_begin(), *e = I->idx_end();
         i != e; ++i, ++req_idx) {
      if (req_idx == idx_range.end()) {
        // We can't handle this without inserting insertvalues
        if (!InsertBefore)
          return nullptr;

        // The requested index identifies a part of a nested aggregate. Handle
        // this specially. For example,
        // %A = insertvalue { i32, {i32, i32 } } undef, i32 10, 1, 0
        // %B = insertvalue { i32, {i32, i32 } } %A, i32 11, 1, 1
        // %C = extractvalue {i32, { i32, i32 } } %B, 1
        // This can be changed into
        // %A = insertvalue {i32, i32 } undef, i32 10, 0
        // %C = insertvalue {i32, i32 } %A, i32 11, 1
        // which allows the unused 0,0 element from the nested struct to be
        // removed.
        return BuildSubAggregate(V, makeArrayRef(idx_range.begin(), req_idx),
                                 InsertBefore);
      }

      // This insert value inserts something else than what we are looking for.
      // See if the (aggregrate) value inserted into has the value we are
      // looking for, then.
      if (*req_idx != *i)
        return FindInsertedValue(I->getAggregateOperand(), idx_range,
                                 InsertBefore);
    }
    // If we end up here, the indices of the insertvalue match with those
    // requested (though possibly only partially). Now we recursively look at
    // the inserted value, passing any remaining indices.
    return FindInsertedValue(I->getInsertedValueOperand(),
                             makeArrayRef(req_idx, idx_range.end()),
                             InsertBefore);
  }

  if (ExtractValueInst *I = dyn_cast<ExtractValueInst>(V)) {
    // If we're extracting a value from an aggregrate that was extracted from
    // something else, we can extract from that something else directly instead.
    // However, we will need to chain I's indices with the requested indices.

    // Calculate the number of indices required
    unsigned size = I->getNumIndices() + idx_range.size();
    // Allocate some space to put the new indices in
    SmallVector<unsigned, 5> Idxs;
    Idxs.reserve(size);
    // Add indices from the extract value instruction
    Idxs.append(I->idx_begin(), I->idx_end());

    // Add requested indices
    Idxs.append(idx_range.begin(), idx_range.end());

    assert(Idxs.size() == size
           && "Number of indices added not correct?");

    return FindInsertedValue(I->getAggregateOperand(), Idxs, InsertBefore);
  }
  // Otherwise, we don't know (such as, extracting from a function return value
  // or load instruction)
  return nullptr;
}

/// Analyze the specified pointer to see if it can be expressed as a base
/// pointer plus a constant offset. Return the base and offset to the caller.
Value *llvm::GetPointerBaseWithConstantOffset(Value *Ptr, int64_t &Offset,
                                              const DataLayout *DL) {
  // Without DataLayout, conservatively assume 64-bit offsets, which is
  // the widest we support.
  unsigned BitWidth = DL ? DL->getPointerTypeSizeInBits(Ptr->getType()) : 64;
  APInt ByteOffset(BitWidth, 0);
  while (1) {
    if (Ptr->getType()->isVectorTy())
      break;

    if (GEPOperator *GEP = dyn_cast<GEPOperator>(Ptr)) {
      if (DL) {
        APInt GEPOffset(BitWidth, 0);
        if (!GEP->accumulateConstantOffset(*DL, GEPOffset))
          break;

        ByteOffset += GEPOffset;
      }

      Ptr = GEP->getPointerOperand();
    } else if (Operator::getOpcode(Ptr) == Instruction::BitCast ||
               Operator::getOpcode(Ptr) == Instruction::AddrSpaceCast) {
      Ptr = cast<Operator>(Ptr)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Ptr)) {
      if (GA->mayBeOverridden())
        break;
      Ptr = GA->getAliasee();
    } else {
      break;
    }
  }
  Offset = ByteOffset.getSExtValue();
  return Ptr;
}


/// This function computes the length of a null-terminated C string pointed to
/// by V. If successful, it returns true and returns the string in Str.
/// If unsuccessful, it returns false.
bool llvm::getConstantStringInfo(const Value *V, StringRef &Str,
                                 uint64_t Offset, bool TrimAtNul) {
  assert(V);

  // Look through bitcast instructions and geps.
  V = V->stripPointerCasts();

  // If the value is a GEP instructionor  constant expression, treat it as an
  // offset.
  if (const GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
    // Make sure the GEP has exactly three arguments.
    if (GEP->getNumOperands() != 3)
      return false;

    // Make sure the index-ee is a pointer to array of i8.
    PointerType *PT = cast<PointerType>(GEP->getOperand(0)->getType());
    ArrayType *AT = dyn_cast<ArrayType>(PT->getElementType());
    if (!AT || !AT->getElementType()->isIntegerTy(8))
      return false;

    // Check to make sure that the first operand of the GEP is an integer and
    // has value 0 so that we are sure we're indexing into the initializer.
    const ConstantInt *FirstIdx = dyn_cast<ConstantInt>(GEP->getOperand(1));
    if (!FirstIdx || !FirstIdx->isZero())
      return false;

    // If the second index isn't a ConstantInt, then this is a variable index
    // into the array.  If this occurs, we can't say anything meaningful about
    // the string.
    uint64_t StartIdx = 0;
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(GEP->getOperand(2)))
      StartIdx = CI->getZExtValue();
    else
      return false;
    return getConstantStringInfo(GEP->getOperand(0), Str, StartIdx+Offset);
  }

  // The GEP instruction, constant or instruction, must reference a global
  // variable that is a constant and is initialized. The referenced constant
  // initializer is the array that we'll use for optimization.
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(V);
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return false;

  // Handle the all-zeros case
  if (GV->getInitializer()->isNullValue()) {
    // This is a degenerate case. The initializer is constant zero so the
    // length of the string must be zero.
    Str = "";
    return true;
  }

  // Must be a Constant Array
  const ConstantDataArray *Array =
    dyn_cast<ConstantDataArray>(GV->getInitializer());
  if (!Array || !Array->isString())
    return false;

  // Get the number of elements in the array
  uint64_t NumElts = Array->getType()->getArrayNumElements();

  // Start out with the entire array in the StringRef.
  Str = Array->getAsString();

  if (Offset > NumElts)
    return false;

  // Skip over 'offset' bytes.
  Str = Str.substr(Offset);

  if (TrimAtNul) {
    // Trim off the \0 and anything after it.  If the array is not nul
    // terminated, we just return the whole end of string.  The client may know
    // some other way that the string is length-bound.
    Str = Str.substr(0, Str.find('\0'));
  }
  return true;
}

// These next two are very similar to the above, but also look through PHI
// nodes.
// TODO: See if we can integrate these two together.

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
static uint64_t GetStringLengthH(Value *V, SmallPtrSetImpl<PHINode*> &PHIs) {
  // Look through noop bitcast instructions.
  V = V->stripPointerCasts();

  // If this is a PHI node, there are two cases: either we have already seen it
  // or we haven't.
  if (PHINode *PN = dyn_cast<PHINode>(V)) {
    if (!PHIs.insert(PN).second)
      return ~0ULL;  // already in the set.

    // If it was new, see if all the input strings are the same length.
    uint64_t LenSoFar = ~0ULL;
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      uint64_t Len = GetStringLengthH(PN->getIncomingValue(i), PHIs);
      if (Len == 0) return 0; // Unknown length -> unknown.

      if (Len == ~0ULL) continue;

      if (Len != LenSoFar && LenSoFar != ~0ULL)
        return 0;    // Disagree -> unknown.
      LenSoFar = Len;
    }

    // Success, all agree.
    return LenSoFar;
  }

  // strlen(select(c,x,y)) -> strlen(x) ^ strlen(y)
  if (SelectInst *SI = dyn_cast<SelectInst>(V)) {
    uint64_t Len1 = GetStringLengthH(SI->getTrueValue(), PHIs);
    if (Len1 == 0) return 0;
    uint64_t Len2 = GetStringLengthH(SI->getFalseValue(), PHIs);
    if (Len2 == 0) return 0;
    if (Len1 == ~0ULL) return Len2;
    if (Len2 == ~0ULL) return Len1;
    if (Len1 != Len2) return 0;
    return Len1;
  }

  // Otherwise, see if we can read the string.
  StringRef StrData;
  if (!getConstantStringInfo(V, StrData))
    return 0;

  return StrData.size()+1;
}

/// If we can compute the length of the string pointed to by
/// the specified pointer, return 'len+1'.  If we can't, return 0.
uint64_t llvm::GetStringLength(Value *V) {
  if (!V->getType()->isPointerTy()) return 0;

  SmallPtrSet<PHINode*, 32> PHIs;
  uint64_t Len = GetStringLengthH(V, PHIs);
  // If Len is ~0ULL, we had an infinite phi cycle: this is dead code, so return
  // an empty string as a length.
  return Len == ~0ULL ? 1 : Len;
}

Value *
llvm::GetUnderlyingObject(Value *V, const DataLayout *TD, unsigned MaxLookup) {
  if (!V->getType()->isPointerTy())
    return V;
  for (unsigned Count = 0; MaxLookup == 0 || Count < MaxLookup; ++Count) {
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->mayBeOverridden())
        return V;
      V = GA->getAliasee();
    } else {
      // See if InstructionSimplify knows any relevant tricks.
      if (Instruction *I = dyn_cast<Instruction>(V))
        // TODO: Acquire a DominatorTree and AssumptionCache and use them.
        if (Value *Simplified = SimplifyInstruction(I, TD, nullptr)) {
          V = Simplified;
          continue;
        }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  }
  return V;
}

void
llvm::GetUnderlyingObjects(Value *V,
                           SmallVectorImpl<Value *> &Objects,
                           const DataLayout *TD,
                           unsigned MaxLookup) {
  SmallPtrSet<Value *, 4> Visited;
  SmallVector<Value *, 4> Worklist;
  Worklist.push_back(V);
  do {
    Value *P = Worklist.pop_back_val();
    P = GetUnderlyingObject(P, TD, MaxLookup);

    if (!Visited.insert(P).second)
      continue;

    if (SelectInst *SI = dyn_cast<SelectInst>(P)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    if (PHINode *PN = dyn_cast<PHINode>(P)) {
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        Worklist.push_back(PN->getIncomingValue(i));
      continue;
    }

    Objects.push_back(P);
  } while (!Worklist.empty());
}

/// Return true if the only users of this pointer are lifetime markers.
bool llvm::onlyUsedByLifetimeMarkers(const Value *V) {
  for (const User *U : V->users()) {
    const IntrinsicInst *II = dyn_cast<IntrinsicInst>(U);
    if (!II) return false;

    if (II->getIntrinsicID() != Intrinsic::lifetime_start &&
        II->getIntrinsicID() != Intrinsic::lifetime_end)
      return false;
  }
  return true;
}

bool llvm::isSafeToSpeculativelyExecute(const Value *V,
                                        const DataLayout *TD) {
  const Operator *Inst = dyn_cast<Operator>(V);
  if (!Inst)
    return false;

  for (unsigned i = 0, e = Inst->getNumOperands(); i != e; ++i)
    if (Constant *C = dyn_cast<Constant>(Inst->getOperand(i)))
      if (C->canTrap())
        return false;

  switch (Inst->getOpcode()) {
  default:
    return true;
  case Instruction::UDiv:
  case Instruction::URem: {
    // x / y is undefined if y == 0.
    const APInt *V;
    if (match(Inst->getOperand(1), m_APInt(V)))
      return *V != 0;
    return false;
  }
  case Instruction::SDiv:
  case Instruction::SRem: {
    // x / y is undefined if y == 0 or x == INT_MIN and y == -1
    const APInt *Numerator, *Denominator;
    if (!match(Inst->getOperand(1), m_APInt(Denominator)))
      return false;
    // We cannot hoist this division if the denominator is 0.
    if (*Denominator == 0)
      return false;
    // It's safe to hoist if the denominator is not 0 or -1.
    if (*Denominator != -1)
      return true;
    // At this point we know that the denominator is -1.  It is safe to hoist as
    // long we know that the numerator is not INT_MIN.
    if (match(Inst->getOperand(0), m_APInt(Numerator)))
      return !Numerator->isMinSignedValue();
    // The numerator *might* be MinSignedValue.
    return false;
  }
  case Instruction::Load: {
    const LoadInst *LI = cast<LoadInst>(Inst);
    if (!LI->isUnordered() ||
        // Speculative load may create a race that did not exist in the source.
        LI->getParent()->getParent()->hasFnAttribute(Attribute::SanitizeThread))
      return false;
    return LI->getPointerOperand()->isDereferenceablePointer(TD);
  }
  case Instruction::Call: {
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
      switch (II->getIntrinsicID()) {
      // These synthetic intrinsics have no side-effects and just mark
      // information about their operands.
      // FIXME: There are other no-op synthetic instructions that potentially
      // should be considered at least *safe* to speculate...
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
        return true;

      case Intrinsic::bswap:
      case Intrinsic::ctlz:
      case Intrinsic::ctpop:
      case Intrinsic::cttz:
      case Intrinsic::objectsize:
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::smul_with_overflow:
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::umul_with_overflow:
      case Intrinsic::usub_with_overflow:
        return true;
      // Sqrt should be OK, since the llvm sqrt intrinsic isn't defined to set
      // errno like libm sqrt would.
      case Intrinsic::sqrt:
      case Intrinsic::fma:
      case Intrinsic::fmuladd:
      case Intrinsic::fabs:
      case Intrinsic::minnum:
      case Intrinsic::maxnum:
        return true;
      // TODO: some fp intrinsics are marked as having the same error handling
      // as libm. They're safe to speculate when they won't error.
      // TODO: are convert_{from,to}_fp16 safe?
      // TODO: can we list target-specific intrinsics here?
      default: break;
      }
    }
    return false; // The called function could have undefined behavior or
                  // side-effects, even if marked readnone nounwind.
  }
  case Instruction::VAArg:
  case Instruction::Alloca:
  case Instruction::Invoke:
  case Instruction::PHI:
  case Instruction::Store:
  case Instruction::Ret:
  case Instruction::Br:
  case Instruction::IndirectBr:
  case Instruction::Switch:
  case Instruction::Unreachable:
  case Instruction::Fence:
  case Instruction::LandingPad:
  case Instruction::AtomicRMW:
  case Instruction::AtomicCmpXchg:
  case Instruction::Resume:
    return false; // Misc instructions which have effects
  }
}

/// Return true if we know that the specified value is never null.
bool llvm::isKnownNonNull(const Value *V, const TargetLibraryInfo *TLI) {
  // Alloca never returns null, malloc might.
  if (isa<AllocaInst>(V)) return true;

  // A byval, inalloca, or nonnull argument is never null.
  if (const Argument *A = dyn_cast<Argument>(V))
    return A->hasByValOrInAllocaAttr() || A->hasNonNullAttr();

  // Global values are not null unless extern weak.
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(V))
    return !GV->hasExternalWeakLinkage();

  // A Load tagged w/nonnull metadata is never null. 
  if (const LoadInst *LI = dyn_cast<LoadInst>(V))
    return LI->getMetadata(LLVMContext::MD_nonnull);

  if (ImmutableCallSite CS = V)
    if (CS.isReturnNonNull())
      return true;

  // operator new never returns null.
  if (isOperatorNewLikeFn(V, TLI, /*LookThroughBitCast=*/true))
    return true;

  return false;
}

OverflowResult llvm::computeOverflowForUnsignedMul(Value *LHS, Value *RHS,
                                                   const DataLayout *DL,
                                                   AssumptionCache *AC,
                                                   const Instruction *CxtI,
                                                   const DominatorTree *DT) {
  // Multiplying n * m significant bits yields a result of n + m significant
  // bits. If the total number of significant bits does not exceed the
  // result bit width (minus 1), there is no overflow.
  // This means if we have enough leading zero bits in the operands
  // we can guarantee that the result does not overflow.
  // Ref: "Hacker's Delight" by Henry Warren
  unsigned BitWidth = LHS->getType()->getScalarSizeInBits();
  APInt LHSKnownZero(BitWidth, 0);
  APInt LHSKnownOne(BitWidth, 0);
  APInt RHSKnownZero(BitWidth, 0);
  APInt RHSKnownOne(BitWidth, 0);
  computeKnownBits(LHS, LHSKnownZero, LHSKnownOne, DL, /*Depth=*/0, AC, CxtI,
                   DT);
  computeKnownBits(RHS, RHSKnownZero, RHSKnownOne, DL, /*Depth=*/0, AC, CxtI,
                   DT);
  // Note that underestimating the number of zero bits gives a more
  // conservative answer.
  unsigned ZeroBits = LHSKnownZero.countLeadingOnes() +
                      RHSKnownZero.countLeadingOnes();
  // First handle the easy case: if we have enough zero bits there's
  // definitely no overflow.
  if (ZeroBits >= BitWidth)
    return OverflowResult::NeverOverflows;

  // Get the largest possible values for each operand.
  APInt LHSMax = ~LHSKnownZero;
  APInt RHSMax = ~RHSKnownZero;

  // We know the multiply operation doesn't overflow if the maximum values for
  // each operand will not overflow after we multiply them together.
  bool MaxOverflow;
  LHSMax.umul_ov(RHSMax, MaxOverflow);
  if (!MaxOverflow)
    return OverflowResult::NeverOverflows;

  // We know it always overflows if multiplying the smallest possible values for
  // the operands also results in overflow.
  bool MinOverflow;
  LHSKnownOne.umul_ov(RHSKnownOne, MinOverflow);
  if (MinOverflow)
    return OverflowResult::AlwaysOverflows;

  return OverflowResult::MayOverflow;
}

OverflowResult llvm::computeOverflowForUnsignedAdd(Value *LHS, Value *RHS,
                                                   const DataLayout *DL,
                                                   AssumptionCache *AC,
                                                   const Instruction *CxtI,
                                                   const DominatorTree *DT) {
  bool LHSKnownNonNegative, LHSKnownNegative;
  ComputeSignBit(LHS, LHSKnownNonNegative, LHSKnownNegative, DL, /*Depth=*/0,
                 AC, CxtI, DT);
  if (LHSKnownNonNegative || LHSKnownNegative) {
    bool RHSKnownNonNegative, RHSKnownNegative;
    ComputeSignBit(RHS, RHSKnownNonNegative, RHSKnownNegative, DL, /*Depth=*/0,
                   AC, CxtI, DT);

    if (LHSKnownNegative && RHSKnownNegative) {
      // The sign bit is set in both cases: this MUST overflow.
      // Create a simple add instruction, and insert it into the struct.
      return OverflowResult::AlwaysOverflows;
    }

    if (LHSKnownNonNegative && RHSKnownNonNegative) {
      // The sign bit is clear in both cases: this CANNOT overflow.
      // Create a simple add instruction, and insert it into the struct.
      return OverflowResult::NeverOverflows;
    }
  }

  return OverflowResult::MayOverflow;
}
