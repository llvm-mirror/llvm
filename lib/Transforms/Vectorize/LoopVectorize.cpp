//===- LoopVectorize.cpp - A Loop Vectorizer ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has three parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/VectorUtils.h"
#include <algorithm>
#include <map>
#include <tuple>

using namespace llvm;
using namespace llvm::PatternMatch;

#define LV_NAME "loop-vectorize"
#define DEBUG_TYPE LV_NAME

STATISTIC(LoopsVectorized, "Number of loops vectorized");
STATISTIC(LoopsAnalyzed, "Number of loops analyzed for vectorization");

static cl::opt<unsigned>
VectorizationFactor("force-vector-width", cl::init(0), cl::Hidden,
                    cl::desc("Sets the SIMD width. Zero is autoselect."));

static cl::opt<unsigned>
VectorizationInterleave("force-vector-interleave", cl::init(0), cl::Hidden,
                    cl::desc("Sets the vectorization interleave count. "
                             "Zero is autoselect."));

static cl::opt<bool>
EnableIfConversion("enable-if-conversion", cl::init(true), cl::Hidden,
                   cl::desc("Enable if-conversion during vectorization."));

/// We don't vectorize loops with a known constant trip count below this number.
static cl::opt<unsigned>
TinyTripCountVectorThreshold("vectorizer-min-trip-count", cl::init(16),
                             cl::Hidden,
                             cl::desc("Don't vectorize loops with a constant "
                                      "trip count that is smaller than this "
                                      "value."));

/// This enables versioning on the strides of symbolically striding memory
/// accesses in code like the following.
///   for (i = 0; i < N; ++i)
///     A[i * Stride1] += B[i * Stride2] ...
///
/// Will be roughly translated to
///    if (Stride1 == 1 && Stride2 == 1) {
///      for (i = 0; i < N; i+=4)
///       A[i:i+3] += ...
///    } else
///      ...
static cl::opt<bool> EnableMemAccessVersioning(
    "enable-mem-access-versioning", cl::init(true), cl::Hidden,
    cl::desc("Enable symblic stride memory access versioning"));

/// We don't unroll loops with a known constant trip count below this number.
static const unsigned TinyTripCountUnrollThreshold = 128;

/// When performing memory disambiguation checks at runtime do not make more
/// than this number of comparisons.
static const unsigned RuntimeMemoryCheckThreshold = 8;

/// Maximum simd width.
static const unsigned MaxVectorWidth = 64;

static cl::opt<unsigned> ForceTargetNumScalarRegs(
    "force-target-num-scalar-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of scalar registers."));

static cl::opt<unsigned> ForceTargetNumVectorRegs(
    "force-target-num-vector-regs", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's number of vector registers."));

/// Maximum vectorization interleave count.
static const unsigned MaxInterleaveFactor = 16;

static cl::opt<unsigned> ForceTargetMaxScalarInterleaveFactor(
    "force-target-max-scalar-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "scalar loops."));

static cl::opt<unsigned> ForceTargetMaxVectorInterleaveFactor(
    "force-target-max-vector-interleave", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's max interleave factor for "
             "vectorized loops."));

static cl::opt<unsigned> ForceTargetInstructionCost(
    "force-target-instruction-cost", cl::init(0), cl::Hidden,
    cl::desc("A flag that overrides the target's expected cost for "
             "an instruction to a single constant value. Mostly "
             "useful for getting consistent testing."));

static cl::opt<unsigned> SmallLoopCost(
    "small-loop-cost", cl::init(20), cl::Hidden,
    cl::desc("The cost of a loop that is considered 'small' by the unroller."));

static cl::opt<bool> LoopVectorizeWithBlockFrequency(
    "loop-vectorize-with-block-frequency", cl::init(false), cl::Hidden,
    cl::desc("Enable the use of the block frequency analysis to access PGO "
             "heuristics minimizing code growth in cold regions and being more "
             "aggressive in hot regions."));

// Runtime unroll loops for load/store throughput.
static cl::opt<bool> EnableLoadStoreRuntimeUnroll(
    "enable-loadstore-runtime-unroll", cl::init(true), cl::Hidden,
    cl::desc("Enable runtime unrolling until load/store ports are saturated"));

/// The number of stores in a loop that are allowed to need predication.
static cl::opt<unsigned> NumberOfStoresToPredicate(
    "vectorize-num-stores-pred", cl::init(1), cl::Hidden,
    cl::desc("Max number of stores to be predicated behind an if."));

static cl::opt<bool> EnableIndVarRegisterHeur(
    "enable-ind-var-reg-heur", cl::init(true), cl::Hidden,
    cl::desc("Count the induction variable only once when unrolling"));

static cl::opt<bool> EnableCondStoresVectorization(
    "enable-cond-stores-vec", cl::init(false), cl::Hidden,
    cl::desc("Enable if predication of stores during vectorization."));

static cl::opt<unsigned> MaxNestedScalarReductionUF(
    "max-nested-scalar-reduction-unroll", cl::init(2), cl::Hidden,
    cl::desc("The maximum unroll factor to use when unrolling a scalar "
             "reduction in a nested loop."));

namespace {

// Forward declarations.
class LoopVectorizationLegality;
class LoopVectorizationCostModel;
class LoopVectorizeHints;

/// InnerLoopVectorizer vectorizes loops which contain only one basic
/// block to a specified vectorization factor (VF).
/// This class performs the widening of scalars into vectors, or multiple
/// scalars. This class also implements the following features:
/// * It inserts an epilogue loop for handling loops that don't have iteration
///   counts that are known to be a multiple of the vectorization factor.
/// * It handles the code generation for reduction variables.
/// * Scalarization (implementation using scalars) of un-vectorizable
///   instructions.
/// InnerLoopVectorizer does not perform any vectorization-legality
/// checks, and relies on the caller to check for the different legality
/// aspects. The InnerLoopVectorizer relies on the
/// LoopVectorizationLegality class to provide information about the induction
/// and reduction variables that were found to a given vectorization factor.
class InnerLoopVectorizer {
public:
  InnerLoopVectorizer(Loop *OrigLoop, ScalarEvolution *SE, LoopInfo *LI,
                      DominatorTree *DT, const DataLayout *DL,
                      const TargetLibraryInfo *TLI, unsigned VecWidth,
                      unsigned UnrollFactor)
      : OrigLoop(OrigLoop), SE(SE), LI(LI), DT(DT), DL(DL), TLI(TLI),
        VF(VecWidth), UF(UnrollFactor), Builder(SE->getContext()),
        Induction(nullptr), OldInduction(nullptr), WidenMap(UnrollFactor),
        Legal(nullptr) {}

  // Perform the actual loop widening (vectorization).
  void vectorize(LoopVectorizationLegality *L) {
    Legal = L;
    // Create a new empty loop. Unlink the old loop and connect the new one.
    createEmptyLoop();
    // Widen each instruction in the old loop to a new one in the new loop.
    // Use the Legality module to find the induction and reduction variables.
    vectorizeLoop();
    // Register the new loop and update the analysis passes.
    updateAnalysis();
  }

  virtual ~InnerLoopVectorizer() {}

protected:
  /// A small list of PHINodes.
  typedef SmallVector<PHINode*, 4> PhiVector;
  /// When we unroll loops we have multiple vector values for each scalar.
  /// This data structure holds the unrolled and vectorized values that
  /// originated from one scalar instruction.
  typedef SmallVector<Value*, 2> VectorParts;

  // When we if-convert we need create edge masks. We have to cache values so
  // that we don't end up with exponential recursion/IR.
  typedef DenseMap<std::pair<BasicBlock*, BasicBlock*>,
                   VectorParts> EdgeMaskCache;

  /// \brief Add checks for strides that where assumed to be 1.
  ///
  /// Returns the last check instruction and the first check instruction in the
  /// pair as (first, last).
  std::pair<Instruction *, Instruction *> addStrideCheck(Instruction *Loc);

  /// Create an empty loop, based on the loop ranges of the old loop.
  void createEmptyLoop();
  /// Copy and widen the instructions from the old loop.
  virtual void vectorizeLoop();

  /// \brief The Loop exit block may have single value PHI nodes where the
  /// incoming value is 'Undef'. While vectorizing we only handled real values
  /// that were defined inside the loop. Here we fix the 'undef case'.
  /// See PR14725.
  void fixLCSSAPHIs();

  /// A helper function that computes the predicate of the block BB, assuming
  /// that the header block of the loop is set to True. It returns the *entry*
  /// mask for the block BB.
  VectorParts createBlockInMask(BasicBlock *BB);
  /// A helper function that computes the predicate of the edge between SRC
  /// and DST.
  VectorParts createEdgeMask(BasicBlock *Src, BasicBlock *Dst);

  /// A helper function to vectorize a single BB within the innermost loop.
  void vectorizeBlockInLoop(BasicBlock *BB, PhiVector *PV);

  /// Vectorize a single PHINode in a block. This method handles the induction
  /// variable canonicalization. It supports both VF = 1 for unrolled loops and
  /// arbitrary length vectors.
  void widenPHIInstruction(Instruction *PN, VectorParts &Entry,
                           unsigned UF, unsigned VF, PhiVector *PV);

  /// Insert the new loop to the loop hierarchy and pass manager
  /// and update the analysis passes.
  void updateAnalysis();

  /// This instruction is un-vectorizable. Implement it as a sequence
  /// of scalars. If \p IfPredicateStore is true we need to 'hide' each
  /// scalarized instruction behind an if block predicated on the control
  /// dependence of the instruction.
  virtual void scalarizeInstruction(Instruction *Instr,
                                    bool IfPredicateStore=false);

  /// Vectorize Load and Store instructions,
  virtual void vectorizeMemoryInstruction(Instruction *Instr);

  /// Create a broadcast instruction. This method generates a broadcast
  /// instruction (shuffle) for loop invariant values and for the induction
  /// value. If this is the induction variable then we extend it to N, N+1, ...
  /// this is needed because each iteration in the loop corresponds to a SIMD
  /// element.
  virtual Value *getBroadcastInstrs(Value *V);

  /// This function adds (StartIdx, StartIdx + Step, StartIdx + 2*Step, ...)
  /// to each vector element of Val. The sequence starts at StartIndex.
  virtual Value *getStepVector(Value *Val, int StartIdx, Value *Step);

  /// When we go over instructions in the basic block we rely on previous
  /// values within the current basic block or on loop invariant values.
  /// When we widen (vectorize) values we place them in the map. If the values
  /// are not within the map, they have to be loop invariant, so we simply
  /// broadcast them into a vector.
  VectorParts &getVectorValue(Value *V);

  /// Generate a shuffle sequence that will reverse the vector Vec.
  virtual Value *reverseVector(Value *Vec);

  /// This is a helper class that holds the vectorizer state. It maps scalar
  /// instructions to vector instructions. When the code is 'unrolled' then
  /// then a single scalar value is mapped to multiple vector parts. The parts
  /// are stored in the VectorPart type.
  struct ValueMap {
    /// C'tor.  UnrollFactor controls the number of vectors ('parts') that
    /// are mapped.
    ValueMap(unsigned UnrollFactor) : UF(UnrollFactor) {}

    /// \return True if 'Key' is saved in the Value Map.
    bool has(Value *Key) const { return MapStorage.count(Key); }

    /// Initializes a new entry in the map. Sets all of the vector parts to the
    /// save value in 'Val'.
    /// \return A reference to a vector with splat values.
    VectorParts &splat(Value *Key, Value *Val) {
      VectorParts &Entry = MapStorage[Key];
      Entry.assign(UF, Val);
      return Entry;
    }

    ///\return A reference to the value that is stored at 'Key'.
    VectorParts &get(Value *Key) {
      VectorParts &Entry = MapStorage[Key];
      if (Entry.empty())
        Entry.resize(UF);
      assert(Entry.size() == UF);
      return Entry;
    }

  private:
    /// The unroll factor. Each entry in the map stores this number of vector
    /// elements.
    unsigned UF;

    /// Map storage. We use std::map and not DenseMap because insertions to a
    /// dense map invalidates its iterators.
    std::map<Value *, VectorParts> MapStorage;
  };

  /// The original loop.
  Loop *OrigLoop;
  /// Scev analysis to use.
  ScalarEvolution *SE;
  /// Loop Info.
  LoopInfo *LI;
  /// Dominator Tree.
  DominatorTree *DT;
  /// Alias Analysis.
  AliasAnalysis *AA;
  /// Data Layout.
  const DataLayout *DL;
  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// The vectorization SIMD factor to use. Each vector will have this many
  /// vector elements.
  unsigned VF;

protected:
  /// The vectorization unroll factor to use. Each scalar is vectorized to this
  /// many different vector instructions.
  unsigned UF;

  /// The builder that we use
  IRBuilder<> Builder;

  // --- Vectorization state ---

  /// The vector-loop preheader.
  BasicBlock *LoopVectorPreHeader;
  /// The scalar-loop preheader.
  BasicBlock *LoopScalarPreHeader;
  /// Middle Block between the vector and the scalar.
  BasicBlock *LoopMiddleBlock;
  ///The ExitBlock of the scalar loop.
  BasicBlock *LoopExitBlock;
  ///The vector loop body.
  SmallVector<BasicBlock *, 4> LoopVectorBody;
  ///The scalar loop body.
  BasicBlock *LoopScalarBody;
  /// A list of all bypass blocks. The first block is the entry of the loop.
  SmallVector<BasicBlock *, 4> LoopBypassBlocks;

  /// The new Induction variable which was added to the new block.
  PHINode *Induction;
  /// The induction variable of the old basic block.
  PHINode *OldInduction;
  /// Holds the extended (to the widest induction type) start index.
  Value *ExtendedIdx;
  /// Maps scalars to widened vectors.
  ValueMap WidenMap;
  EdgeMaskCache MaskCache;

  LoopVectorizationLegality *Legal;
};

class InnerLoopUnroller : public InnerLoopVectorizer {
public:
  InnerLoopUnroller(Loop *OrigLoop, ScalarEvolution *SE, LoopInfo *LI,
                    DominatorTree *DT, const DataLayout *DL,
                    const TargetLibraryInfo *TLI, unsigned UnrollFactor) :
    InnerLoopVectorizer(OrigLoop, SE, LI, DT, DL, TLI, 1, UnrollFactor) { }

private:
  void scalarizeInstruction(Instruction *Instr,
                            bool IfPredicateStore = false) override;
  void vectorizeMemoryInstruction(Instruction *Instr) override;
  Value *getBroadcastInstrs(Value *V) override;
  Value *getStepVector(Value *Val, int StartIdx, Value *Step) override;
  Value *reverseVector(Value *Vec) override;
};

/// \brief Look for a meaningful debug location on the instruction or it's
/// operands.
static Instruction *getDebugLocFromInstOrOperands(Instruction *I) {
  if (!I)
    return I;

  DebugLoc Empty;
  if (I->getDebugLoc() != Empty)
    return I;

  for (User::op_iterator OI = I->op_begin(), OE = I->op_end(); OI != OE; ++OI) {
    if (Instruction *OpInst = dyn_cast<Instruction>(*OI))
      if (OpInst->getDebugLoc() != Empty)
        return OpInst;
  }

  return I;
}

/// \brief Set the debug location in the builder using the debug location in the
/// instruction.
static void setDebugLocFromInst(IRBuilder<> &B, const Value *Ptr) {
  if (const Instruction *Inst = dyn_cast_or_null<Instruction>(Ptr))
    B.SetCurrentDebugLocation(Inst->getDebugLoc());
  else
    B.SetCurrentDebugLocation(DebugLoc());
}

#ifndef NDEBUG
/// \return string containing a file name and a line # for the given loop.
static std::string getDebugLocString(const Loop *L) {
  std::string Result;
  if (L) {
    raw_string_ostream OS(Result);
    const DebugLoc LoopDbgLoc = L->getStartLoc();
    if (!LoopDbgLoc.isUnknown())
      LoopDbgLoc.print(L->getHeader()->getContext(), OS);
    else
      // Just print the module name.
      OS << L->getHeader()->getParent()->getParent()->getModuleIdentifier();
    OS.flush();
  }
  return Result;
}
#endif

/// \brief Propagate known metadata from one instruction to another.
static void propagateMetadata(Instruction *To, const Instruction *From) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> Metadata;
  From->getAllMetadataOtherThanDebugLoc(Metadata);

  for (auto M : Metadata) {
    unsigned Kind = M.first;

    // These are safe to transfer (this is safe for TBAA, even when we
    // if-convert, because should that metadata have had a control dependency
    // on the condition, and thus actually aliased with some other
    // non-speculated memory access when the condition was false, this would be
    // caught by the runtime overlap checks).
    if (Kind != LLVMContext::MD_tbaa &&
        Kind != LLVMContext::MD_alias_scope &&
        Kind != LLVMContext::MD_noalias &&
        Kind != LLVMContext::MD_fpmath)
      continue;

    To->setMetadata(Kind, M.second);
  }
}

/// \brief Propagate known metadata from one instruction to a vector of others.
static void propagateMetadata(SmallVectorImpl<Value *> &To, const Instruction *From) {
  for (Value *V : To)
    if (Instruction *I = dyn_cast<Instruction>(V))
      propagateMetadata(I, From);
}

/// LoopVectorizationLegality checks if it is legal to vectorize a loop, and
/// to what vectorization factor.
/// This class does not look at the profitability of vectorization, only the
/// legality. This class has two main kinds of checks:
/// * Memory checks - The code in canVectorizeMemory checks if vectorization
///   will change the order of memory accesses in a way that will change the
///   correctness of the program.
/// * Scalars checks - The code in canVectorizeInstrs and canVectorizeMemory
/// checks for a number of different conditions, such as the availability of a
/// single induction variable, that all types are supported and vectorize-able,
/// etc. This code reflects the capabilities of InnerLoopVectorizer.
/// This class is also used by InnerLoopVectorizer for identifying
/// induction variable and the different reduction variables.
class LoopVectorizationLegality {
public:
  LoopVectorizationLegality(Loop *L, ScalarEvolution *SE, const DataLayout *DL,
                            DominatorTree *DT, TargetLibraryInfo *TLI,
                            AliasAnalysis *AA, Function *F,
                            const TargetTransformInfo *TTI)
      : NumPredStores(0), TheLoop(L), SE(SE), DL(DL),
        TLI(TLI), TheFunction(F), TTI(TTI), Induction(nullptr),
        WidestIndTy(nullptr),
        LAA(F, L, SE, DL, TLI, AA, DT,
            LoopAccessAnalysis::VectorizerParams(
                MaxVectorWidth, VectorizationFactor, VectorizationInterleave,
                RuntimeMemoryCheckThreshold)),
        HasFunNoNaNAttr(false) {}

  /// This enum represents the kinds of reductions that we support.
  enum ReductionKind {
    RK_NoReduction, ///< Not a reduction.
    RK_IntegerAdd,  ///< Sum of integers.
    RK_IntegerMult, ///< Product of integers.
    RK_IntegerOr,   ///< Bitwise or logical OR of numbers.
    RK_IntegerAnd,  ///< Bitwise or logical AND of numbers.
    RK_IntegerXor,  ///< Bitwise or logical XOR of numbers.
    RK_IntegerMinMax, ///< Min/max implemented in terms of select(cmp()).
    RK_FloatAdd,    ///< Sum of floats.
    RK_FloatMult,   ///< Product of floats.
    RK_FloatMinMax  ///< Min/max implemented in terms of select(cmp()).
  };

  /// This enum represents the kinds of inductions that we support.
  enum InductionKind {
    IK_NoInduction,  ///< Not an induction variable.
    IK_IntInduction, ///< Integer induction variable. Step = C.
    IK_PtrInduction  ///< Pointer induction var. Step = C / sizeof(elem).
  };

  // This enum represents the kind of minmax reduction.
  enum MinMaxReductionKind {
    MRK_Invalid,
    MRK_UIntMin,
    MRK_UIntMax,
    MRK_SIntMin,
    MRK_SIntMax,
    MRK_FloatMin,
    MRK_FloatMax
  };

  /// This struct holds information about reduction variables.
  struct ReductionDescriptor {
    ReductionDescriptor() : StartValue(nullptr), LoopExitInstr(nullptr),
      Kind(RK_NoReduction), MinMaxKind(MRK_Invalid) {}

    ReductionDescriptor(Value *Start, Instruction *Exit, ReductionKind K,
                        MinMaxReductionKind MK)
        : StartValue(Start), LoopExitInstr(Exit), Kind(K), MinMaxKind(MK) {}

    // The starting value of the reduction.
    // It does not have to be zero!
    TrackingVH<Value> StartValue;
    // The instruction who's value is used outside the loop.
    Instruction *LoopExitInstr;
    // The kind of the reduction.
    ReductionKind Kind;
    // If this a min/max reduction the kind of reduction.
    MinMaxReductionKind MinMaxKind;
  };

  /// This POD struct holds information about a potential reduction operation.
  struct ReductionInstDesc {
    ReductionInstDesc(bool IsRedux, Instruction *I) :
      IsReduction(IsRedux), PatternLastInst(I), MinMaxKind(MRK_Invalid) {}

    ReductionInstDesc(Instruction *I, MinMaxReductionKind K) :
      IsReduction(true), PatternLastInst(I), MinMaxKind(K) {}

    // Is this instruction a reduction candidate.
    bool IsReduction;
    // The last instruction in a min/max pattern (select of the select(icmp())
    // pattern), or the current reduction instruction otherwise.
    Instruction *PatternLastInst;
    // If this is a min/max pattern the comparison predicate.
    MinMaxReductionKind MinMaxKind;
  };

  /// A struct for saving information about induction variables.
  struct InductionInfo {
    InductionInfo(Value *Start, InductionKind K, ConstantInt *Step)
        : StartValue(Start), IK(K), StepValue(Step) {
      assert(IK != IK_NoInduction && "Not an induction");
      assert(StartValue && "StartValue is null");
      assert(StepValue && !StepValue->isZero() && "StepValue is zero");
      assert((IK != IK_PtrInduction || StartValue->getType()->isPointerTy()) &&
             "StartValue is not a pointer for pointer induction");
      assert((IK != IK_IntInduction || StartValue->getType()->isIntegerTy()) &&
             "StartValue is not an integer for integer induction");
      assert(StepValue->getType()->isIntegerTy() &&
             "StepValue is not an integer");
    }
    InductionInfo()
        : StartValue(nullptr), IK(IK_NoInduction), StepValue(nullptr) {}

    /// Get the consecutive direction. Returns:
    ///   0 - unknown or non-consecutive.
    ///   1 - consecutive and increasing.
    ///  -1 - consecutive and decreasing.
    int getConsecutiveDirection() const {
      if (StepValue && (StepValue->isOne() || StepValue->isMinusOne()))
        return StepValue->getSExtValue();
      return 0;
    }

    /// Compute the transformed value of Index at offset StartValue using step
    /// StepValue.
    /// For integer induction, returns StartValue + Index * StepValue.
    /// For pointer induction, returns StartValue[Index * StepValue].
    /// FIXME: The newly created binary instructions should contain nsw/nuw
    /// flags, which can be found from the original scalar operations.
    Value *transform(IRBuilder<> &B, Value *Index) const {
      switch (IK) {
      case IK_IntInduction:
        assert(Index->getType() == StartValue->getType() &&
               "Index type does not match StartValue type");
        if (StepValue->isMinusOne())
          return B.CreateSub(StartValue, Index);
        if (!StepValue->isOne())
          Index = B.CreateMul(Index, StepValue);
        return B.CreateAdd(StartValue, Index);

      case IK_PtrInduction:
        if (StepValue->isMinusOne())
          Index = B.CreateNeg(Index);
        else if (!StepValue->isOne())
          Index = B.CreateMul(Index, StepValue);
        return B.CreateGEP(StartValue, Index);

      case IK_NoInduction:
        return nullptr;
      }
      llvm_unreachable("invalid enum");
    }

    /// Start value.
    TrackingVH<Value> StartValue;
    /// Induction kind.
    InductionKind IK;
    /// Step value.
    ConstantInt *StepValue;
  };

  /// ReductionList contains the reduction descriptors for all
  /// of the reductions that were found in the loop.
  typedef DenseMap<PHINode*, ReductionDescriptor> ReductionList;

  /// InductionList saves induction variables and maps them to the
  /// induction descriptor.
  typedef MapVector<PHINode*, InductionInfo> InductionList;

  /// Returns true if it is legal to vectorize this loop.
  /// This does not mean that it is profitable to vectorize this
  /// loop, only that it is legal to do so.
  bool canVectorize();

  /// Returns the Induction variable.
  PHINode *getInduction() { return Induction; }

  /// Returns the reduction variables found in the loop.
  ReductionList *getReductionVars() { return &Reductions; }

  /// Returns the induction variables found in the loop.
  InductionList *getInductionVars() { return &Inductions; }

  /// Returns the widest induction type.
  Type *getWidestInductionType() { return WidestIndTy; }

  /// Returns True if V is an induction variable in this loop.
  bool isInductionVariable(const Value *V);

  /// Return true if the block BB needs to be predicated in order for the loop
  /// to be vectorized.
  bool blockNeedsPredication(BasicBlock *BB);

  /// Check if this  pointer is consecutive when vectorizing. This happens
  /// when the last index of the GEP is the induction variable, or that the
  /// pointer itself is an induction variable.
  /// This check allows us to vectorize A[idx] into a wide load/store.
  /// Returns:
  /// 0 - Stride is unknown or non-consecutive.
  /// 1 - Address is consecutive.
  /// -1 - Address is consecutive, and decreasing.
  int isConsecutivePtr(Value *Ptr);

  /// Returns true if the value V is uniform within the loop.
  bool isUniform(Value *V);

  /// Returns true if this instruction will remain scalar after vectorization.
  bool isUniformAfterVectorization(Instruction* I) { return Uniforms.count(I); }

  /// Returns the information that we collected about runtime memory check.
  LoopAccessAnalysis::RuntimePointerCheck *getRuntimePointerCheck() {
    return LAA.getRuntimePointerCheck();
  }

  LoopAccessAnalysis *getLAA() {
    return &LAA;
  }

  /// This function returns the identity element (or neutral element) for
  /// the operation K.
  static Constant *getReductionIdentity(ReductionKind K, Type *Tp);

  unsigned getMaxSafeDepDistBytes() { return LAA.getMaxSafeDepDistBytes(); }

  bool hasStride(Value *V) { return StrideSet.count(V); }
  bool mustCheckStrides() { return !StrideSet.empty(); }
  SmallPtrSet<Value *, 8>::iterator strides_begin() {
    return StrideSet.begin();
  }
  SmallPtrSet<Value *, 8>::iterator strides_end() { return StrideSet.end(); }

  /// Returns true if the target machine supports masked store operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedStore(Type *DataType, Value *Ptr) {
    return TTI->isLegalMaskedStore(DataType, isConsecutivePtr(Ptr));
  }
  /// Returns true if the target machine supports masked load operation
  /// for the given \p DataType and kind of access to \p Ptr.
  bool isLegalMaskedLoad(Type *DataType, Value *Ptr) {
    return TTI->isLegalMaskedLoad(DataType, isConsecutivePtr(Ptr));
  }
  /// Returns true if vector representation of the instruction \p I
  /// requires mask.
  bool isMaskRequired(const Instruction* I) {
    return (MaskedOp.count(I) != 0);
  }
  unsigned getNumStores() const {
    return LAA.getNumStores();
  }
  unsigned getNumLoads() const {
    return LAA.getNumLoads();
  }
  unsigned getNumPredStores() const {
    return NumPredStores;
  }
private:
  /// Check if a single basic block loop is vectorizable.
  /// At this point we know that this is a loop with a constant trip count
  /// and we only need to check individual instructions.
  bool canVectorizeInstrs();

  /// When we vectorize loops we may change the order in which
  /// we read and write from memory. This method checks if it is
  /// legal to vectorize the code, considering only memory constrains.
  /// Returns true if the loop is vectorizable
  bool canVectorizeMemory();

  /// Return true if we can vectorize this loop using the IF-conversion
  /// transformation.
  bool canVectorizeWithIfConvert();

  /// Collect the variables that need to stay uniform after vectorization.
  void collectLoopUniforms();

  /// Return true if all of the instructions in the block can be speculatively
  /// executed. \p SafePtrs is a list of addresses that are known to be legal
  /// and we know that we can read from them without segfault.
  bool blockCanBePredicated(BasicBlock *BB, SmallPtrSetImpl<Value *> &SafePtrs);

  /// Returns True, if 'Phi' is the kind of reduction variable for type
  /// 'Kind'. If this is a reduction variable, it adds it to ReductionList.
  bool AddReductionVar(PHINode *Phi, ReductionKind Kind);
  /// Returns a struct describing if the instruction 'I' can be a reduction
  /// variable of type 'Kind'. If the reduction is a min/max pattern of
  /// select(icmp()) this function advances the instruction pointer 'I' from the
  /// compare instruction to the select instruction and stores this pointer in
  /// 'PatternLastInst' member of the returned struct.
  ReductionInstDesc isReductionInstr(Instruction *I, ReductionKind Kind,
                                     ReductionInstDesc &Desc);
  /// Returns true if the instruction is a Select(ICmp(X, Y), X, Y) instruction
  /// pattern corresponding to a min(X, Y) or max(X, Y).
  static ReductionInstDesc isMinMaxSelectCmpPattern(Instruction *I,
                                                    ReductionInstDesc &Prev);
  /// Returns the induction kind of Phi and record the step. This function may
  /// return NoInduction if the PHI is not an induction variable.
  InductionKind isInductionVariable(PHINode *Phi, ConstantInt *&StepValue);

  /// \brief Collect memory access with loop invariant strides.
  ///
  /// Looks for accesses like "a[i * StrideA]" where "StrideA" is loop
  /// invariant.
  void collectStridedAccess(Value *LoadOrStoreInst);

  /// Report an analysis message to assist the user in diagnosing loops that are
  /// not vectorized.
  void emitAnalysis(VectorizationReport &Message) {
    VectorizationReport::emitAnalysis(Message, TheFunction, TheLoop);
  }

  unsigned NumPredStores;

  /// The loop that we evaluate.
  Loop *TheLoop;
  /// Scev analysis.
  ScalarEvolution *SE;
  /// DataLayout analysis.
  const DataLayout *DL;
  /// Target Library Info.
  TargetLibraryInfo *TLI;
  /// Parent function
  Function *TheFunction;
  /// Target Transform Info
  const TargetTransformInfo *TTI;

  //  ---  vectorization state --- //

  /// Holds the integer induction variable. This is the counter of the
  /// loop.
  PHINode *Induction;
  /// Holds the reduction variables.
  ReductionList Reductions;
  /// Holds all of the induction variables that we found in the loop.
  /// Notice that inductions don't need to start at zero and that induction
  /// variables can be pointers.
  InductionList Inductions;
  /// Holds the widest induction type encountered.
  Type *WidestIndTy;

  /// Allowed outside users. This holds the reduction
  /// vars which can be accessed from outside the loop.
  SmallPtrSet<Value*, 4> AllowedExit;
  /// This set holds the variables which are known to be uniform after
  /// vectorization.
  SmallPtrSet<Instruction*, 4> Uniforms;
  LoopAccessAnalysis LAA;
  /// Can we assume the absence of NaNs.
  bool HasFunNoNaNAttr;

  ValueToValueMap Strides;
  SmallPtrSet<Value *, 8> StrideSet;
  
  /// While vectorizing these instructions we have to generate a
  /// call to the appropriate masked intrinsic
  SmallPtrSet<const Instruction*, 8> MaskedOp;
};

/// LoopVectorizationCostModel - estimates the expected speedups due to
/// vectorization.
/// In many cases vectorization is not profitable. This can happen because of
/// a number of reasons. In this class we mainly attempt to predict the
/// expected speedup/slowdowns due to the supported instruction set. We use the
/// TargetTransformInfo to query the different backends for the cost of
/// different operations.
class LoopVectorizationCostModel {
public:
  LoopVectorizationCostModel(Loop *L, ScalarEvolution *SE, LoopInfo *LI,
                             LoopVectorizationLegality *Legal,
                             const TargetTransformInfo &TTI,
                             const DataLayout *DL, const TargetLibraryInfo *TLI,
                             AssumptionCache *AC, const Function *F,
                             const LoopVectorizeHints *Hints)
      : TheLoop(L), SE(SE), LI(LI), Legal(Legal), TTI(TTI), DL(DL), TLI(TLI),
        TheFunction(F), Hints(Hints) {
    CodeMetrics::collectEphemeralValues(L, AC, EphValues);
  }

  /// Information about vectorization costs
  struct VectorizationFactor {
    unsigned Width; // Vector width with best cost
    unsigned Cost; // Cost of the loop with that width
  };
  /// \return The most profitable vectorization factor and the cost of that VF.
  /// This method checks every power of two up to VF. If UserVF is not ZERO
  /// then this vectorization factor will be selected if vectorization is
  /// possible.
  VectorizationFactor selectVectorizationFactor(bool OptForSize);

  /// \return The size (in bits) of the widest type in the code that
  /// needs to be vectorized. We ignore values that remain scalar such as
  /// 64 bit loop indices.
  unsigned getWidestType();

  /// \return The most profitable unroll factor.
  /// If UserUF is non-zero then this method finds the best unroll-factor
  /// based on register pressure and other parameters.
  /// VF and LoopCost are the selected vectorization factor and the cost of the
  /// selected VF.
  unsigned selectUnrollFactor(bool OptForSize, unsigned VF, unsigned LoopCost);

  /// \brief A struct that represents some properties of the register usage
  /// of a loop.
  struct RegisterUsage {
    /// Holds the number of loop invariant values that are used in the loop.
    unsigned LoopInvariantRegs;
    /// Holds the maximum number of concurrent live intervals in the loop.
    unsigned MaxLocalUsers;
    /// Holds the number of instructions in the loop.
    unsigned NumInstructions;
  };

  /// \return  information about the register usage of the loop.
  RegisterUsage calculateRegisterUsage();

private:
  /// Returns the expected execution cost. The unit of the cost does
  /// not matter because we use the 'cost' units to compare different
  /// vector widths. The cost that is returned is *not* normalized by
  /// the factor width.
  unsigned expectedCost(unsigned VF);

  /// Returns the execution time cost of an instruction for a given vector
  /// width. Vector width of one means scalar.
  unsigned getInstructionCost(Instruction *I, unsigned VF);

  /// A helper function for converting Scalar types to vector types.
  /// If the incoming type is void, we return void. If the VF is 1, we return
  /// the scalar type.
  static Type* ToVectorTy(Type *Scalar, unsigned VF);

  /// Returns whether the instruction is a load or store and will be a emitted
  /// as a vector operation.
  bool isConsecutiveLoadOrStore(Instruction *I);

  /// Report an analysis message to assist the user in diagnosing loops that are
  /// not vectorized.
  void emitAnalysis(VectorizationReport &Message) {
    VectorizationReport::emitAnalysis(Message, TheFunction, TheLoop);
  }

  /// Values used only by @llvm.assume calls.
  SmallPtrSet<const Value *, 32> EphValues;

  /// The loop that we evaluate.
  Loop *TheLoop;
  /// Scev analysis.
  ScalarEvolution *SE;
  /// Loop Info analysis.
  LoopInfo *LI;
  /// Vectorization legality.
  LoopVectorizationLegality *Legal;
  /// Vector target information.
  const TargetTransformInfo &TTI;
  /// Target data layout information.
  const DataLayout *DL;
  /// Target Library Info.
  const TargetLibraryInfo *TLI;
  const Function *TheFunction;
  // Loop Vectorize Hint.
  const LoopVectorizeHints *Hints;
};

/// Utility class for getting and setting loop vectorizer hints in the form
/// of loop metadata.
/// This class keeps a number of loop annotations locally (as member variables)
/// and can, upon request, write them back as metadata on the loop. It will
/// initially scan the loop for existing metadata, and will update the local
/// values based on information in the loop.
/// We cannot write all values to metadata, as the mere presence of some info,
/// for example 'force', means a decision has been made. So, we need to be
/// careful NOT to add them if the user hasn't specifically asked so.
class LoopVectorizeHints {
  enum HintKind {
    HK_WIDTH,
    HK_UNROLL,
    HK_FORCE
  };

  /// Hint - associates name and validation with the hint value.
  struct Hint {
    const char * Name;
    unsigned Value; // This may have to change for non-numeric values.
    HintKind Kind;

    Hint(const char * Name, unsigned Value, HintKind Kind)
      : Name(Name), Value(Value), Kind(Kind) { }

    bool validate(unsigned Val) {
      switch (Kind) {
      case HK_WIDTH:
        return isPowerOf2_32(Val) && Val <= MaxVectorWidth;
      case HK_UNROLL:
        return isPowerOf2_32(Val) && Val <= MaxInterleaveFactor;
      case HK_FORCE:
        return (Val <= 1);
      }
      return false;
    }
  };

  /// Vectorization width.
  Hint Width;
  /// Vectorization interleave factor.
  Hint Interleave;
  /// Vectorization forced
  Hint Force;

  /// Return the loop metadata prefix.
  static StringRef Prefix() { return "llvm.loop."; }

public:
  enum ForceKind {
    FK_Undefined = -1, ///< Not selected.
    FK_Disabled = 0,   ///< Forcing disabled.
    FK_Enabled = 1,    ///< Forcing enabled.
  };

  LoopVectorizeHints(const Loop *L, bool DisableInterleaving)
      : Width("vectorize.width", VectorizationFactor, HK_WIDTH),
        Interleave("interleave.count", DisableInterleaving, HK_UNROLL),
        Force("vectorize.enable", FK_Undefined, HK_FORCE),
        TheLoop(L) {
    // Populate values with existing loop metadata.
    getHintsFromMetadata();

    // force-vector-interleave overrides DisableInterleaving.
    if (VectorizationInterleave.getNumOccurrences() > 0)
      Interleave.Value = VectorizationInterleave;

    DEBUG(if (DisableInterleaving && Interleave.Value == 1) dbgs()
          << "LV: Interleaving disabled by the pass manager\n");
  }

  /// Mark the loop L as already vectorized by setting the width to 1.
  void setAlreadyVectorized() {
    Width.Value = Interleave.Value = 1;
    Hint Hints[] = {Width, Interleave};
    writeHintsToMetadata(Hints);
  }

  /// Dumps all the hint information.
  std::string emitRemark() const {
    VectorizationReport R;
    if (Force.Value == LoopVectorizeHints::FK_Disabled)
      R << "vectorization is explicitly disabled";
    else {
      R << "use -Rpass-analysis=loop-vectorize for more info";
      if (Force.Value == LoopVectorizeHints::FK_Enabled) {
        R << " (Force=true";
        if (Width.Value != 0)
          R << ", Vector Width=" << Width.Value;
        if (Interleave.Value != 0)
          R << ", Interleave Count=" << Interleave.Value;
        R << ")";
      }
    }

    return R.str();
  }

  unsigned getWidth() const { return Width.Value; }
  unsigned getInterleave() const { return Interleave.Value; }
  enum ForceKind getForce() const { return (ForceKind)Force.Value; }

private:
  /// Find hints specified in the loop metadata and update local values.
  void getHintsFromMetadata() {
    MDNode *LoopID = TheLoop->getLoopID();
    if (!LoopID)
      return;

    // First operand should refer to the loop id itself.
    assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
    assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

    for (unsigned i = 1, ie = LoopID->getNumOperands(); i < ie; ++i) {
      const MDString *S = nullptr;
      SmallVector<Metadata *, 4> Args;

      // The expected hint is either a MDString or a MDNode with the first
      // operand a MDString.
      if (const MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i))) {
        if (!MD || MD->getNumOperands() == 0)
          continue;
        S = dyn_cast<MDString>(MD->getOperand(0));
        for (unsigned i = 1, ie = MD->getNumOperands(); i < ie; ++i)
          Args.push_back(MD->getOperand(i));
      } else {
        S = dyn_cast<MDString>(LoopID->getOperand(i));
        assert(Args.size() == 0 && "too many arguments for MDString");
      }

      if (!S)
        continue;

      // Check if the hint starts with the loop metadata prefix.
      StringRef Name = S->getString();
      if (Args.size() == 1)
        setHint(Name, Args[0]);
    }
  }

  /// Checks string hint with one operand and set value if valid.
  void setHint(StringRef Name, Metadata *Arg) {
    if (!Name.startswith(Prefix()))
      return;
    Name = Name.substr(Prefix().size(), StringRef::npos);

    const ConstantInt *C = mdconst::dyn_extract<ConstantInt>(Arg);
    if (!C) return;
    unsigned Val = C->getZExtValue();

    Hint *Hints[] = {&Width, &Interleave, &Force};
    for (auto H : Hints) {
      if (Name == H->Name) {
        if (H->validate(Val))
          H->Value = Val;
        else
          DEBUG(dbgs() << "LV: ignoring invalid hint '" << Name << "'\n");
        break;
      }
    }
  }

  /// Create a new hint from name / value pair.
  MDNode *createHintMetadata(StringRef Name, unsigned V) const {
    LLVMContext &Context = TheLoop->getHeader()->getContext();
    Metadata *MDs[] = {MDString::get(Context, Name),
                       ConstantAsMetadata::get(
                           ConstantInt::get(Type::getInt32Ty(Context), V))};
    return MDNode::get(Context, MDs);
  }

  /// Matches metadata with hint name.
  bool matchesHintMetadataName(MDNode *Node, ArrayRef<Hint> HintTypes) {
    MDString* Name = dyn_cast<MDString>(Node->getOperand(0));
    if (!Name)
      return false;

    for (auto H : HintTypes)
      if (Name->getString().endswith(H.Name))
        return true;
    return false;
  }

  /// Sets current hints into loop metadata, keeping other values intact.
  void writeHintsToMetadata(ArrayRef<Hint> HintTypes) {
    if (HintTypes.size() == 0)
      return;

    // Reserve the first element to LoopID (see below).
    SmallVector<Metadata *, 4> MDs(1);
    // If the loop already has metadata, then ignore the existing operands.
    MDNode *LoopID = TheLoop->getLoopID();
    if (LoopID) {
      for (unsigned i = 1, ie = LoopID->getNumOperands(); i < ie; ++i) {
        MDNode *Node = cast<MDNode>(LoopID->getOperand(i));
        // If node in update list, ignore old value.
        if (!matchesHintMetadataName(Node, HintTypes))
          MDs.push_back(Node);
      }
    }

    // Now, add the missing hints.
    for (auto H : HintTypes)
      MDs.push_back(createHintMetadata(Twine(Prefix(), H.Name).str(), H.Value));

    // Replace current metadata node with new one.
    LLVMContext &Context = TheLoop->getHeader()->getContext();
    MDNode *NewLoopID = MDNode::get(Context, MDs);
    // Set operand 0 to refer to the loop id itself.
    NewLoopID->replaceOperandWith(0, NewLoopID);

    TheLoop->setLoopID(NewLoopID);
  }

  /// The loop these hints belong to.
  const Loop *TheLoop;
};

static void emitMissedWarning(Function *F, Loop *L,
                              const LoopVectorizeHints &LH) {
  emitOptimizationRemarkMissed(F->getContext(), DEBUG_TYPE, *F,
                               L->getStartLoc(), LH.emitRemark());

  if (LH.getForce() == LoopVectorizeHints::FK_Enabled) {
    if (LH.getWidth() != 1)
      emitLoopVectorizeWarning(
          F->getContext(), *F, L->getStartLoc(),
          "failed explicitly specified loop vectorization");
    else if (LH.getInterleave() != 1)
      emitLoopInterleaveWarning(
          F->getContext(), *F, L->getStartLoc(),
          "failed explicitly specified loop interleaving");
  }
}

static void addInnerLoop(Loop &L, SmallVectorImpl<Loop *> &V) {
  if (L.empty())
    return V.push_back(&L);

  for (Loop *InnerL : L)
    addInnerLoop(*InnerL, V);
}

/// The LoopVectorize Pass.
struct LoopVectorize : public FunctionPass {
  /// Pass identification, replacement for typeid
  static char ID;

  explicit LoopVectorize(bool NoUnrolling = false, bool AlwaysVectorize = true)
    : FunctionPass(ID),
      DisableUnrolling(NoUnrolling),
      AlwaysVectorize(AlwaysVectorize) {
    initializeLoopVectorizePass(*PassRegistry::getPassRegistry());
  }

  ScalarEvolution *SE;
  const DataLayout *DL;
  LoopInfo *LI;
  TargetTransformInfo *TTI;
  DominatorTree *DT;
  BlockFrequencyInfo *BFI;
  TargetLibraryInfo *TLI;
  AliasAnalysis *AA;
  AssumptionCache *AC;
  bool DisableUnrolling;
  bool AlwaysVectorize;

  BlockFrequency ColdEntryFreq;

  bool runOnFunction(Function &F) override {
    SE = &getAnalysis<ScalarEvolution>();
    DataLayoutPass *DLP = getAnalysisIfAvailable<DataLayoutPass>();
    DL = DLP ? &DLP->getDataLayout() : nullptr;
    LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    BFI = &getAnalysis<BlockFrequencyInfo>();
    auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
    TLI = TLIP ? &TLIP->getTLI() : nullptr;
    AA = &getAnalysis<AliasAnalysis>();
    AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

    // Compute some weights outside of the loop over the loops. Compute this
    // using a BranchProbability to re-use its scaling math.
    const BranchProbability ColdProb(1, 5); // 20%
    ColdEntryFreq = BlockFrequency(BFI->getEntryFreq()) * ColdProb;

    // If the target claims to have no vector registers don't attempt
    // vectorization.
    if (!TTI->getNumberOfRegisters(true))
      return false;

    if (!DL) {
      DEBUG(dbgs() << "\nLV: Not vectorizing " << F.getName()
                   << ": Missing data layout\n");
      return false;
    }

    // Build up a worklist of inner-loops to vectorize. This is necessary as
    // the act of vectorizing or partially unrolling a loop creates new loops
    // and can invalidate iterators across the loops.
    SmallVector<Loop *, 8> Worklist;

    for (Loop *L : *LI)
      addInnerLoop(*L, Worklist);

    LoopsAnalyzed += Worklist.size();

    // Now walk the identified inner loops.
    bool Changed = false;
    while (!Worklist.empty())
      Changed |= processLoop(Worklist.pop_back_val());

    // Process each loop nest in the function.
    return Changed;
  }

  bool processLoop(Loop *L) {
    assert(L->empty() && "Only process inner loops.");

#ifndef NDEBUG
    const std::string DebugLocStr = getDebugLocString(L);
#endif /* NDEBUG */

    DEBUG(dbgs() << "\nLV: Checking a loop in \""
                 << L->getHeader()->getParent()->getName() << "\" from "
                 << DebugLocStr << "\n");

    LoopVectorizeHints Hints(L, DisableUnrolling);

    DEBUG(dbgs() << "LV: Loop hints:"
                 << " force="
                 << (Hints.getForce() == LoopVectorizeHints::FK_Disabled
                         ? "disabled"
                         : (Hints.getForce() == LoopVectorizeHints::FK_Enabled
                                ? "enabled"
                                : "?")) << " width=" << Hints.getWidth()
                 << " unroll=" << Hints.getInterleave() << "\n");

    // Function containing loop
    Function *F = L->getHeader()->getParent();

    // Looking at the diagnostic output is the only way to determine if a loop
    // was vectorized (other than looking at the IR or machine code), so it
    // is important to generate an optimization remark for each loop. Most of
    // these messages are generated by emitOptimizationRemarkAnalysis. Remarks
    // generated by emitOptimizationRemark and emitOptimizationRemarkMissed are
    // less verbose reporting vectorized loops and unvectorized loops that may
    // benefit from vectorization, respectively.

    if (Hints.getForce() == LoopVectorizeHints::FK_Disabled) {
      DEBUG(dbgs() << "LV: Not vectorizing: #pragma vectorize disable.\n");
      emitOptimizationRemarkAnalysis(F->getContext(), DEBUG_TYPE, *F,
                                     L->getStartLoc(), Hints.emitRemark());
      return false;
    }

    if (!AlwaysVectorize && Hints.getForce() != LoopVectorizeHints::FK_Enabled) {
      DEBUG(dbgs() << "LV: Not vectorizing: No #pragma vectorize enable.\n");
      emitOptimizationRemarkAnalysis(F->getContext(), DEBUG_TYPE, *F,
                                     L->getStartLoc(), Hints.emitRemark());
      return false;
    }

    if (Hints.getWidth() == 1 && Hints.getInterleave() == 1) {
      DEBUG(dbgs() << "LV: Not vectorizing: Disabled/already vectorized.\n");
      emitOptimizationRemarkAnalysis(
          F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
          "loop not vectorized: vector width and interleave count are "
          "explicitly set to 1");
      return false;
    }

    // Check the loop for a trip count threshold:
    // do not vectorize loops with a tiny trip count.
    const unsigned TC = SE->getSmallConstantTripCount(L);
    if (TC > 0u && TC < TinyTripCountVectorThreshold) {
      DEBUG(dbgs() << "LV: Found a loop with a very small trip count. "
                   << "This loop is not worth vectorizing.");
      if (Hints.getForce() == LoopVectorizeHints::FK_Enabled)
        DEBUG(dbgs() << " But vectorizing was explicitly forced.\n");
      else {
        DEBUG(dbgs() << "\n");
        emitOptimizationRemarkAnalysis(
            F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
            "vectorization is not beneficial and is not explicitly forced");
        return false;
      }
    }

    // Check if it is legal to vectorize the loop.
    LoopVectorizationLegality LVL(L, SE, DL, DT, TLI, AA, F, TTI);
    if (!LVL.canVectorize()) {
      DEBUG(dbgs() << "LV: Not vectorizing: Cannot prove legality.\n");
      emitMissedWarning(F, L, Hints);
      return false;
    }

    // Use the cost model.
    LoopVectorizationCostModel CM(L, SE, LI, &LVL, *TTI, DL, TLI, AC, F,
                                  &Hints);

    // Check the function attributes to find out if this function should be
    // optimized for size.
    bool OptForSize = Hints.getForce() != LoopVectorizeHints::FK_Enabled &&
                      F->hasFnAttribute(Attribute::OptimizeForSize);

    // Compute the weighted frequency of this loop being executed and see if it
    // is less than 20% of the function entry baseline frequency. Note that we
    // always have a canonical loop here because we think we *can* vectoriez.
    // FIXME: This is hidden behind a flag due to pervasive problems with
    // exactly what block frequency models.
    if (LoopVectorizeWithBlockFrequency) {
      BlockFrequency LoopEntryFreq = BFI->getBlockFreq(L->getLoopPreheader());
      if (Hints.getForce() != LoopVectorizeHints::FK_Enabled &&
          LoopEntryFreq < ColdEntryFreq)
        OptForSize = true;
    }

    // Check the function attributes to see if implicit floats are allowed.a
    // FIXME: This check doesn't seem possibly correct -- what if the loop is
    // an integer loop and the vector instructions selected are purely integer
    // vector instructions?
    if (F->hasFnAttribute(Attribute::NoImplicitFloat)) {
      DEBUG(dbgs() << "LV: Can't vectorize when the NoImplicitFloat"
            "attribute is used.\n");
      emitOptimizationRemarkAnalysis(
          F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
          "loop not vectorized due to NoImplicitFloat attribute");
      emitMissedWarning(F, L, Hints);
      return false;
    }

    // Select the optimal vectorization factor.
    const LoopVectorizationCostModel::VectorizationFactor VF =
        CM.selectVectorizationFactor(OptForSize);

    // Select the unroll factor.
    const unsigned UF =
        CM.selectUnrollFactor(OptForSize, VF.Width, VF.Cost);

    DEBUG(dbgs() << "LV: Found a vectorizable loop (" << VF.Width << ") in "
                 << DebugLocStr << '\n');
    DEBUG(dbgs() << "LV: Unroll Factor is " << UF << '\n');

    if (VF.Width == 1) {
      DEBUG(dbgs() << "LV: Vectorization is possible but not beneficial\n");

      if (UF == 1) {
        emitOptimizationRemarkAnalysis(
            F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
            "not beneficial to vectorize and user disabled interleaving");
        return false;
      }
      DEBUG(dbgs() << "LV: Trying to at least unroll the loops.\n");

      // Report the unrolling decision.
      emitOptimizationRemark(F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
                             Twine("unrolled with interleaving factor " +
                                   Twine(UF) +
                                   " (vectorization not beneficial)"));

      // We decided not to vectorize, but we may want to unroll.

      InnerLoopUnroller Unroller(L, SE, LI, DT, DL, TLI, UF);
      Unroller.vectorize(&LVL);
    } else {
      // If we decided that it is *legal* to vectorize the loop then do it.
      InnerLoopVectorizer LB(L, SE, LI, DT, DL, TLI, VF.Width, UF);
      LB.vectorize(&LVL);
      ++LoopsVectorized;

      // Report the vectorization decision.
      emitOptimizationRemark(
          F->getContext(), DEBUG_TYPE, *F, L->getStartLoc(),
          Twine("vectorized loop (vectorization factor: ") + Twine(VF.Width) +
              ", unrolling interleave factor: " + Twine(UF) + ")");
    }

    // Mark the loop as already vectorized to avoid vectorizing again.
    Hints.setAlreadyVectorized();

    DEBUG(verifyFunction(*L->getHeader()->getParent()));
    return true;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequiredID(LoopSimplifyID);
    AU.addRequiredID(LCSSAID);
    AU.addRequired<BlockFrequencyInfo>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolution>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AliasAnalysis>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<AliasAnalysis>();
  }

};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Implementation of LoopVectorizationLegality, InnerLoopVectorizer and
// LoopVectorizationCostModel.
//===----------------------------------------------------------------------===//

Value *InnerLoopVectorizer::getBroadcastInstrs(Value *V) {
  // We need to place the broadcast of invariant variables outside the loop.
  Instruction *Instr = dyn_cast<Instruction>(V);
  bool NewInstr =
      (Instr && std::find(LoopVectorBody.begin(), LoopVectorBody.end(),
                          Instr->getParent()) != LoopVectorBody.end());
  bool Invariant = OrigLoop->isLoopInvariant(V) && !NewInstr;

  // Place the code for broadcasting invariant variables in the new preheader.
  IRBuilder<>::InsertPointGuard Guard(Builder);
  if (Invariant)
    Builder.SetInsertPoint(LoopVectorPreHeader->getTerminator());

  // Broadcast the scalar into all locations in the vector.
  Value *Shuf = Builder.CreateVectorSplat(VF, V, "broadcast");

  return Shuf;
}

Value *InnerLoopVectorizer::getStepVector(Value *Val, int StartIdx,
                                          Value *Step) {
  assert(Val->getType()->isVectorTy() && "Must be a vector");
  assert(Val->getType()->getScalarType()->isIntegerTy() &&
         "Elem must be an integer");
  assert(Step->getType() == Val->getType()->getScalarType() &&
         "Step has wrong type");
  // Create the types.
  Type *ITy = Val->getType()->getScalarType();
  VectorType *Ty = cast<VectorType>(Val->getType());
  int VLen = Ty->getNumElements();
  SmallVector<Constant*, 8> Indices;

  // Create a vector of consecutive numbers from zero to VF.
  for (int i = 0; i < VLen; ++i)
    Indices.push_back(ConstantInt::get(ITy, StartIdx + i));

  // Add the consecutive indices to the vector value.
  Constant *Cv = ConstantVector::get(Indices);
  assert(Cv->getType() == Val->getType() && "Invalid consecutive vec");
  Step = Builder.CreateVectorSplat(VLen, Step);
  assert(Step->getType() == Val->getType() && "Invalid step vec");
  // FIXME: The newly created binary instructions should contain nsw/nuw flags,
  // which can be found from the original scalar operations.
  Step = Builder.CreateMul(Cv, Step);
  return Builder.CreateAdd(Val, Step, "induction");
}

/// \brief Find the operand of the GEP that should be checked for consecutive
/// stores. This ignores trailing indices that have no effect on the final
/// pointer.
static unsigned getGEPInductionOperand(const DataLayout *DL,
                                       const GetElementPtrInst *Gep) {
  unsigned LastOperand = Gep->getNumOperands() - 1;
  unsigned GEPAllocSize = DL->getTypeAllocSize(
      cast<PointerType>(Gep->getType()->getScalarType())->getElementType());

  // Walk backwards and try to peel off zeros.
  while (LastOperand > 1 && match(Gep->getOperand(LastOperand), m_Zero())) {
    // Find the type we're currently indexing into.
    gep_type_iterator GEPTI = gep_type_begin(Gep);
    std::advance(GEPTI, LastOperand - 1);

    // If it's a type with the same allocation size as the result of the GEP we
    // can peel off the zero index.
    if (DL->getTypeAllocSize(*GEPTI) != GEPAllocSize)
      break;
    --LastOperand;
  }

  return LastOperand;
}

int LoopVectorizationLegality::isConsecutivePtr(Value *Ptr) {
  assert(Ptr->getType()->isPointerTy() && "Unexpected non-ptr");
  // Make sure that the pointer does not point to structs.
  if (Ptr->getType()->getPointerElementType()->isAggregateType())
    return 0;

  // If this value is a pointer induction variable we know it is consecutive.
  PHINode *Phi = dyn_cast_or_null<PHINode>(Ptr);
  if (Phi && Inductions.count(Phi)) {
    InductionInfo II = Inductions[Phi];
    return II.getConsecutiveDirection();
  }

  GetElementPtrInst *Gep = dyn_cast_or_null<GetElementPtrInst>(Ptr);
  if (!Gep)
    return 0;

  unsigned NumOperands = Gep->getNumOperands();
  Value *GpPtr = Gep->getPointerOperand();
  // If this GEP value is a consecutive pointer induction variable and all of
  // the indices are constant then we know it is consecutive. We can
  Phi = dyn_cast<PHINode>(GpPtr);
  if (Phi && Inductions.count(Phi)) {

    // Make sure that the pointer does not point to structs.
    PointerType *GepPtrType = cast<PointerType>(GpPtr->getType());
    if (GepPtrType->getElementType()->isAggregateType())
      return 0;

    // Make sure that all of the index operands are loop invariant.
    for (unsigned i = 1; i < NumOperands; ++i)
      if (!SE->isLoopInvariant(SE->getSCEV(Gep->getOperand(i)), TheLoop))
        return 0;

    InductionInfo II = Inductions[Phi];
    return II.getConsecutiveDirection();
  }

  unsigned InductionOperand = getGEPInductionOperand(DL, Gep);

  // Check that all of the gep indices are uniform except for our induction
  // operand.
  for (unsigned i = 0; i != NumOperands; ++i)
    if (i != InductionOperand &&
        !SE->isLoopInvariant(SE->getSCEV(Gep->getOperand(i)), TheLoop))
      return 0;

  // We can emit wide load/stores only if the last non-zero index is the
  // induction variable.
  const SCEV *Last = nullptr;
  if (!Strides.count(Gep))
    Last = SE->getSCEV(Gep->getOperand(InductionOperand));
  else {
    // Because of the multiplication by a stride we can have a s/zext cast.
    // We are going to replace this stride by 1 so the cast is safe to ignore.
    //
    //  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
    //  %0 = trunc i64 %indvars.iv to i32
    //  %mul = mul i32 %0, %Stride1
    //  %idxprom = zext i32 %mul to i64  << Safe cast.
    //  %arrayidx = getelementptr inbounds i32* %B, i64 %idxprom
    //
    Last = replaceSymbolicStrideSCEV(SE, Strides,
                                     Gep->getOperand(InductionOperand), Gep);
    if (const SCEVCastExpr *C = dyn_cast<SCEVCastExpr>(Last))
      Last =
          (C->getSCEVType() == scSignExtend || C->getSCEVType() == scZeroExtend)
              ? C->getOperand()
              : Last;
  }
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Last)) {
    const SCEV *Step = AR->getStepRecurrence(*SE);

    // The memory is consecutive because the last index is consecutive
    // and all other indices are loop invariant.
    if (Step->isOne())
      return 1;
    if (Step->isAllOnesValue())
      return -1;
  }

  return 0;
}

bool LoopVectorizationLegality::isUniform(Value *V) {
  return LAA.isUniform(V);
}

InnerLoopVectorizer::VectorParts&
InnerLoopVectorizer::getVectorValue(Value *V) {
  assert(V != Induction && "The new induction variable should not be used.");
  assert(!V->getType()->isVectorTy() && "Can't widen a vector");

  // If we have a stride that is replaced by one, do it here.
  if (Legal->hasStride(V))
    V = ConstantInt::get(V->getType(), 1);

  // If we have this scalar in the map, return it.
  if (WidenMap.has(V))
    return WidenMap.get(V);

  // If this scalar is unknown, assume that it is a constant or that it is
  // loop invariant. Broadcast V and save the value for future uses.
  Value *B = getBroadcastInstrs(V);
  return WidenMap.splat(V, B);
}

Value *InnerLoopVectorizer::reverseVector(Value *Vec) {
  assert(Vec->getType()->isVectorTy() && "Invalid type");
  SmallVector<Constant*, 8> ShuffleMask;
  for (unsigned i = 0; i < VF; ++i)
    ShuffleMask.push_back(Builder.getInt32(VF - i - 1));

  return Builder.CreateShuffleVector(Vec, UndefValue::get(Vec->getType()),
                                     ConstantVector::get(ShuffleMask),
                                     "reverse");
}

void InnerLoopVectorizer::vectorizeMemoryInstruction(Instruction *Instr) {
  // Attempt to issue a wide load.
  LoadInst *LI = dyn_cast<LoadInst>(Instr);
  StoreInst *SI = dyn_cast<StoreInst>(Instr);

  assert((LI || SI) && "Invalid Load/Store instruction");

  Type *ScalarDataTy = LI ? LI->getType() : SI->getValueOperand()->getType();
  Type *DataTy = VectorType::get(ScalarDataTy, VF);
  Value *Ptr = LI ? LI->getPointerOperand() : SI->getPointerOperand();
  unsigned Alignment = LI ? LI->getAlignment() : SI->getAlignment();
  // An alignment of 0 means target abi alignment. We need to use the scalar's
  // target abi alignment in such a case.
  if (!Alignment)
    Alignment = DL->getABITypeAlignment(ScalarDataTy);
  unsigned AddressSpace = Ptr->getType()->getPointerAddressSpace();
  unsigned ScalarAllocatedSize = DL->getTypeAllocSize(ScalarDataTy);
  unsigned VectorElementSize = DL->getTypeStoreSize(DataTy)/VF;

  if (SI && Legal->blockNeedsPredication(SI->getParent()) &&
      !Legal->isMaskRequired(SI))
    return scalarizeInstruction(Instr, true);

  if (ScalarAllocatedSize != VectorElementSize)
    return scalarizeInstruction(Instr);

  // If the pointer is loop invariant or if it is non-consecutive,
  // scalarize the load.
  int ConsecutiveStride = Legal->isConsecutivePtr(Ptr);
  bool Reverse = ConsecutiveStride < 0;
  bool UniformLoad = LI && Legal->isUniform(Ptr);
  if (!ConsecutiveStride || UniformLoad)
    return scalarizeInstruction(Instr);

  Constant *Zero = Builder.getInt32(0);
  VectorParts &Entry = WidenMap.get(Instr);

  // Handle consecutive loads/stores.
  GetElementPtrInst *Gep = dyn_cast<GetElementPtrInst>(Ptr);
  if (Gep && Legal->isInductionVariable(Gep->getPointerOperand())) {
    setDebugLocFromInst(Builder, Gep);
    Value *PtrOperand = Gep->getPointerOperand();
    Value *FirstBasePtr = getVectorValue(PtrOperand)[0];
    FirstBasePtr = Builder.CreateExtractElement(FirstBasePtr, Zero);

    // Create the new GEP with the new induction variable.
    GetElementPtrInst *Gep2 = cast<GetElementPtrInst>(Gep->clone());
    Gep2->setOperand(0, FirstBasePtr);
    Gep2->setName("gep.indvar.base");
    Ptr = Builder.Insert(Gep2);
  } else if (Gep) {
    setDebugLocFromInst(Builder, Gep);
    assert(SE->isLoopInvariant(SE->getSCEV(Gep->getPointerOperand()),
                               OrigLoop) && "Base ptr must be invariant");

    // The last index does not have to be the induction. It can be
    // consecutive and be a function of the index. For example A[I+1];
    unsigned NumOperands = Gep->getNumOperands();
    unsigned InductionOperand = getGEPInductionOperand(DL, Gep);
    // Create the new GEP with the new induction variable.
    GetElementPtrInst *Gep2 = cast<GetElementPtrInst>(Gep->clone());

    for (unsigned i = 0; i < NumOperands; ++i) {
      Value *GepOperand = Gep->getOperand(i);
      Instruction *GepOperandInst = dyn_cast<Instruction>(GepOperand);

      // Update last index or loop invariant instruction anchored in loop.
      if (i == InductionOperand ||
          (GepOperandInst && OrigLoop->contains(GepOperandInst))) {
        assert((i == InductionOperand ||
               SE->isLoopInvariant(SE->getSCEV(GepOperandInst), OrigLoop)) &&
               "Must be last index or loop invariant");

        VectorParts &GEPParts = getVectorValue(GepOperand);
        Value *Index = GEPParts[0];
        Index = Builder.CreateExtractElement(Index, Zero);
        Gep2->setOperand(i, Index);
        Gep2->setName("gep.indvar.idx");
      }
    }
    Ptr = Builder.Insert(Gep2);
  } else {
    // Use the induction element ptr.
    assert(isa<PHINode>(Ptr) && "Invalid induction ptr");
    setDebugLocFromInst(Builder, Ptr);
    VectorParts &PtrVal = getVectorValue(Ptr);
    Ptr = Builder.CreateExtractElement(PtrVal[0], Zero);
  }

  VectorParts Mask = createBlockInMask(Instr->getParent());
  // Handle Stores:
  if (SI) {
    assert(!Legal->isUniform(SI->getPointerOperand()) &&
           "We do not allow storing to uniform addresses");
    setDebugLocFromInst(Builder, SI);
    // We don't want to update the value in the map as it might be used in
    // another expression. So don't use a reference type for "StoredVal".
    VectorParts StoredVal = getVectorValue(SI->getValueOperand());
    
    for (unsigned Part = 0; Part < UF; ++Part) {
      // Calculate the pointer for the specific unroll-part.
      Value *PartPtr = Builder.CreateGEP(Ptr, Builder.getInt32(Part * VF));

      if (Reverse) {
        // If we store to reverse consecutive memory locations then we need
        // to reverse the order of elements in the stored value.
        StoredVal[Part] = reverseVector(StoredVal[Part]);
        // If the address is consecutive but reversed, then the
        // wide store needs to start at the last vector element.
        PartPtr = Builder.CreateGEP(Ptr, Builder.getInt32(-Part * VF));
        PartPtr = Builder.CreateGEP(PartPtr, Builder.getInt32(1 - VF));
        Mask[Part] = reverseVector(Mask[Part]);
      }

      Value *VecPtr = Builder.CreateBitCast(PartPtr,
                                            DataTy->getPointerTo(AddressSpace));

      Instruction *NewSI;
      if (Legal->isMaskRequired(SI))
        NewSI = Builder.CreateMaskedStore(StoredVal[Part], VecPtr, Alignment,
                                          Mask[Part]);
      else 
        NewSI = Builder.CreateAlignedStore(StoredVal[Part], VecPtr, Alignment);
      propagateMetadata(NewSI, SI);
    }
    return;
  }

  // Handle loads.
  assert(LI && "Must have a load instruction");
  setDebugLocFromInst(Builder, LI);
  for (unsigned Part = 0; Part < UF; ++Part) {
    // Calculate the pointer for the specific unroll-part.
    Value *PartPtr = Builder.CreateGEP(Ptr, Builder.getInt32(Part * VF));

    if (Reverse) {
      // If the address is consecutive but reversed, then the
      // wide load needs to start at the last vector element.
      PartPtr = Builder.CreateGEP(Ptr, Builder.getInt32(-Part * VF));
      PartPtr = Builder.CreateGEP(PartPtr, Builder.getInt32(1 - VF));
      Mask[Part] = reverseVector(Mask[Part]);
    }

    Instruction* NewLI;
    Value *VecPtr = Builder.CreateBitCast(PartPtr,
                                          DataTy->getPointerTo(AddressSpace));
    if (Legal->isMaskRequired(LI))
      NewLI = Builder.CreateMaskedLoad(VecPtr, Alignment, Mask[Part],
                                       UndefValue::get(DataTy),
                                       "wide.masked.load");
    else
      NewLI = Builder.CreateAlignedLoad(VecPtr, Alignment, "wide.load");
    propagateMetadata(NewLI, LI);
    Entry[Part] = Reverse ? reverseVector(NewLI) :  NewLI;
  }
}

void InnerLoopVectorizer::scalarizeInstruction(Instruction *Instr, bool IfPredicateStore) {
  assert(!Instr->getType()->isAggregateType() && "Can't handle vectors");
  // Holds vector parameters or scalars, in case of uniform vals.
  SmallVector<VectorParts, 4> Params;

  setDebugLocFromInst(Builder, Instr);

  // Find all of the vectorized parameters.
  for (unsigned op = 0, e = Instr->getNumOperands(); op != e; ++op) {
    Value *SrcOp = Instr->getOperand(op);

    // If we are accessing the old induction variable, use the new one.
    if (SrcOp == OldInduction) {
      Params.push_back(getVectorValue(SrcOp));
      continue;
    }

    // Try using previously calculated values.
    Instruction *SrcInst = dyn_cast<Instruction>(SrcOp);

    // If the src is an instruction that appeared earlier in the basic block
    // then it should already be vectorized.
    if (SrcInst && OrigLoop->contains(SrcInst)) {
      assert(WidenMap.has(SrcInst) && "Source operand is unavailable");
      // The parameter is a vector value from earlier.
      Params.push_back(WidenMap.get(SrcInst));
    } else {
      // The parameter is a scalar from outside the loop. Maybe even a constant.
      VectorParts Scalars;
      Scalars.append(UF, SrcOp);
      Params.push_back(Scalars);
    }
  }

  assert(Params.size() == Instr->getNumOperands() &&
         "Invalid number of operands");

  // Does this instruction return a value ?
  bool IsVoidRetTy = Instr->getType()->isVoidTy();

  Value *UndefVec = IsVoidRetTy ? nullptr :
    UndefValue::get(VectorType::get(Instr->getType(), VF));
  // Create a new entry in the WidenMap and initialize it to Undef or Null.
  VectorParts &VecResults = WidenMap.splat(Instr, UndefVec);

  Instruction *InsertPt = Builder.GetInsertPoint();
  BasicBlock *IfBlock = Builder.GetInsertBlock();
  BasicBlock *CondBlock = nullptr;

  VectorParts Cond;
  Loop *VectorLp = nullptr;
  if (IfPredicateStore) {
    assert(Instr->getParent()->getSinglePredecessor() &&
           "Only support single predecessor blocks");
    Cond = createEdgeMask(Instr->getParent()->getSinglePredecessor(),
                          Instr->getParent());
    VectorLp = LI->getLoopFor(IfBlock);
    assert(VectorLp && "Must have a loop for this block");
  }

  // For each vector unroll 'part':
  for (unsigned Part = 0; Part < UF; ++Part) {
    // For each scalar that we create:
    for (unsigned Width = 0; Width < VF; ++Width) {

      // Start if-block.
      Value *Cmp = nullptr;
      if (IfPredicateStore) {
        Cmp = Builder.CreateExtractElement(Cond[Part], Builder.getInt32(Width));
        Cmp = Builder.CreateICmp(ICmpInst::ICMP_EQ, Cmp, ConstantInt::get(Cmp->getType(), 1));
        CondBlock = IfBlock->splitBasicBlock(InsertPt, "cond.store");
        LoopVectorBody.push_back(CondBlock);
        VectorLp->addBasicBlockToLoop(CondBlock, *LI);
        // Update Builder with newly created basic block.
        Builder.SetInsertPoint(InsertPt);
      }

      Instruction *Cloned = Instr->clone();
      if (!IsVoidRetTy)
        Cloned->setName(Instr->getName() + ".cloned");
      // Replace the operands of the cloned instructions with extracted scalars.
      for (unsigned op = 0, e = Instr->getNumOperands(); op != e; ++op) {
        Value *Op = Params[op][Part];
        // Param is a vector. Need to extract the right lane.
        if (Op->getType()->isVectorTy())
          Op = Builder.CreateExtractElement(Op, Builder.getInt32(Width));
        Cloned->setOperand(op, Op);
      }

      // Place the cloned scalar in the new loop.
      Builder.Insert(Cloned);

      // If the original scalar returns a value we need to place it in a vector
      // so that future users will be able to use it.
      if (!IsVoidRetTy)
        VecResults[Part] = Builder.CreateInsertElement(VecResults[Part], Cloned,
                                                       Builder.getInt32(Width));
      // End if-block.
      if (IfPredicateStore) {
         BasicBlock *NewIfBlock = CondBlock->splitBasicBlock(InsertPt, "else");
         LoopVectorBody.push_back(NewIfBlock);
         VectorLp->addBasicBlockToLoop(NewIfBlock, *LI);
         Builder.SetInsertPoint(InsertPt);
         Instruction *OldBr = IfBlock->getTerminator();
         BranchInst::Create(CondBlock, NewIfBlock, Cmp, OldBr);
         OldBr->eraseFromParent();
         IfBlock = NewIfBlock;
      }
    }
  }
}

static Instruction *getFirstInst(Instruction *FirstInst, Value *V,
                                 Instruction *Loc) {
  if (FirstInst)
    return FirstInst;
  if (Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() == Loc->getParent() ? I : nullptr;
  return nullptr;
}

std::pair<Instruction *, Instruction *>
InnerLoopVectorizer::addStrideCheck(Instruction *Loc) {
  Instruction *tnullptr = nullptr;
  if (!Legal->mustCheckStrides())
    return std::pair<Instruction *, Instruction *>(tnullptr, tnullptr);

  IRBuilder<> ChkBuilder(Loc);

  // Emit checks.
  Value *Check = nullptr;
  Instruction *FirstInst = nullptr;
  for (SmallPtrSet<Value *, 8>::iterator SI = Legal->strides_begin(),
                                         SE = Legal->strides_end();
       SI != SE; ++SI) {
    Value *Ptr = stripIntegerCast(*SI);
    Value *C = ChkBuilder.CreateICmpNE(Ptr, ConstantInt::get(Ptr->getType(), 1),
                                       "stride.chk");
    // Store the first instruction we create.
    FirstInst = getFirstInst(FirstInst, C, Loc);
    if (Check)
      Check = ChkBuilder.CreateOr(Check, C);
    else
      Check = C;
  }

  // We have to do this trickery because the IRBuilder might fold the check to a
  // constant expression in which case there is no Instruction anchored in a
  // the block.
  LLVMContext &Ctx = Loc->getContext();
  Instruction *TheCheck =
      BinaryOperator::CreateAnd(Check, ConstantInt::getTrue(Ctx));
  ChkBuilder.Insert(TheCheck, "stride.not.one");
  FirstInst = getFirstInst(FirstInst, TheCheck, Loc);

  return std::make_pair(FirstInst, TheCheck);
}

void InnerLoopVectorizer::createEmptyLoop() {
  /*
   In this function we generate a new loop. The new loop will contain
   the vectorized instructions while the old loop will continue to run the
   scalar remainder.

       [ ] <-- Back-edge taken count overflow check.
    /   |
   /    v
  |    [ ] <-- vector loop bypass (may consist of multiple blocks).
  |  /  |
  | /   v
  ||   [ ]     <-- vector pre header.
  ||    |
  ||    v
  ||   [  ] \
  ||   [  ]_|   <-- vector loop.
  ||    |
  | \   v
  |   >[ ]   <--- middle-block.
  |  /  |
  | /   v
  -|- >[ ]     <--- new preheader.
   |    |
   |    v
   |   [ ] \
   |   [ ]_|   <-- old scalar loop to handle remainder.
    \   |
     \  v
      >[ ]     <-- exit block.
   ...
   */

  BasicBlock *OldBasicBlock = OrigLoop->getHeader();
  BasicBlock *BypassBlock = OrigLoop->getLoopPreheader();
  BasicBlock *ExitBlock = OrigLoop->getExitBlock();
  assert(BypassBlock && "Invalid loop structure");
  assert(ExitBlock && "Must have an exit block");

  // Some loops have a single integer induction variable, while other loops
  // don't. One example is c++ iterators that often have multiple pointer
  // induction variables. In the code below we also support a case where we
  // don't have a single induction variable.
  OldInduction = Legal->getInduction();
  Type *IdxTy = Legal->getWidestInductionType();

  // Find the loop boundaries.
  const SCEV *ExitCount = SE->getBackedgeTakenCount(OrigLoop);
  assert(ExitCount != SE->getCouldNotCompute() && "Invalid loop count");

  // The exit count might have the type of i64 while the phi is i32. This can
  // happen if we have an induction variable that is sign extended before the
  // compare. The only way that we get a backedge taken count is that the
  // induction variable was signed and as such will not overflow. In such a case
  // truncation is legal.
  if (ExitCount->getType()->getPrimitiveSizeInBits() >
      IdxTy->getPrimitiveSizeInBits())
    ExitCount = SE->getTruncateOrNoop(ExitCount, IdxTy);

  const SCEV *BackedgeTakeCount = SE->getNoopOrZeroExtend(ExitCount, IdxTy);
  // Get the total trip count from the count by adding 1.
  ExitCount = SE->getAddExpr(BackedgeTakeCount,
                             SE->getConstant(BackedgeTakeCount->getType(), 1));

  // Expand the trip count and place the new instructions in the preheader.
  // Notice that the pre-header does not change, only the loop body.
  SCEVExpander Exp(*SE, "induction");

  // We need to test whether the backedge-taken count is uint##_max. Adding one
  // to it will cause overflow and an incorrect loop trip count in the vector
  // body. In case of overflow we want to directly jump to the scalar remainder
  // loop.
  Value *BackedgeCount =
      Exp.expandCodeFor(BackedgeTakeCount, BackedgeTakeCount->getType(),
                        BypassBlock->getTerminator());
  if (BackedgeCount->getType()->isPointerTy())
    BackedgeCount = CastInst::CreatePointerCast(BackedgeCount, IdxTy,
                                                "backedge.ptrcnt.to.int",
                                                BypassBlock->getTerminator());
  Instruction *CheckBCOverflow =
      CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_EQ, BackedgeCount,
                      Constant::getAllOnesValue(BackedgeCount->getType()),
                      "backedge.overflow", BypassBlock->getTerminator());

  // The loop index does not have to start at Zero. Find the original start
  // value from the induction PHI node. If we don't have an induction variable
  // then we know that it starts at zero.
  Builder.SetInsertPoint(BypassBlock->getTerminator());
  Value *StartIdx = ExtendedIdx = OldInduction ?
    Builder.CreateZExt(OldInduction->getIncomingValueForBlock(BypassBlock),
                       IdxTy):
    ConstantInt::get(IdxTy, 0);

  // We need an instruction to anchor the overflow check on. StartIdx needs to
  // be defined before the overflow check branch. Because the scalar preheader
  // is going to merge the start index and so the overflow branch block needs to
  // contain a definition of the start index.
  Instruction *OverflowCheckAnchor = BinaryOperator::CreateAdd(
      StartIdx, ConstantInt::get(IdxTy, 0), "overflow.check.anchor",
      BypassBlock->getTerminator());

  // Count holds the overall loop count (N).
  Value *Count = Exp.expandCodeFor(ExitCount, ExitCount->getType(),
                                   BypassBlock->getTerminator());

  LoopBypassBlocks.push_back(BypassBlock);

  // Split the single block loop into the two loop structure described above.
  BasicBlock *VectorPH =
  BypassBlock->splitBasicBlock(BypassBlock->getTerminator(), "vector.ph");
  BasicBlock *VecBody =
  VectorPH->splitBasicBlock(VectorPH->getTerminator(), "vector.body");
  BasicBlock *MiddleBlock =
  VecBody->splitBasicBlock(VecBody->getTerminator(), "middle.block");
  BasicBlock *ScalarPH =
  MiddleBlock->splitBasicBlock(MiddleBlock->getTerminator(), "scalar.ph");

  // Create and register the new vector loop.
  Loop* Lp = new Loop();
  Loop *ParentLoop = OrigLoop->getParentLoop();

  // Insert the new loop into the loop nest and register the new basic blocks
  // before calling any utilities such as SCEV that require valid LoopInfo.
  if (ParentLoop) {
    ParentLoop->addChildLoop(Lp);
    ParentLoop->addBasicBlockToLoop(ScalarPH, *LI);
    ParentLoop->addBasicBlockToLoop(VectorPH, *LI);
    ParentLoop->addBasicBlockToLoop(MiddleBlock, *LI);
  } else {
    LI->addTopLevelLoop(Lp);
  }
  Lp->addBasicBlockToLoop(VecBody, *LI);

  // Use this IR builder to create the loop instructions (Phi, Br, Cmp)
  // inside the loop.
  Builder.SetInsertPoint(VecBody->getFirstNonPHI());

  // Generate the induction variable.
  setDebugLocFromInst(Builder, getDebugLocFromInstOrOperands(OldInduction));
  Induction = Builder.CreatePHI(IdxTy, 2, "index");
  // The loop step is equal to the vectorization factor (num of SIMD elements)
  // times the unroll factor (num of SIMD instructions).
  Constant *Step = ConstantInt::get(IdxTy, VF * UF);

  // This is the IR builder that we use to add all of the logic for bypassing
  // the new vector loop.
  IRBuilder<> BypassBuilder(BypassBlock->getTerminator());
  setDebugLocFromInst(BypassBuilder,
                      getDebugLocFromInstOrOperands(OldInduction));

  // We may need to extend the index in case there is a type mismatch.
  // We know that the count starts at zero and does not overflow.
  if (Count->getType() != IdxTy) {
    // The exit count can be of pointer type. Convert it to the correct
    // integer type.
    if (ExitCount->getType()->isPointerTy())
      Count = BypassBuilder.CreatePointerCast(Count, IdxTy, "ptrcnt.to.int");
    else
      Count = BypassBuilder.CreateZExtOrTrunc(Count, IdxTy, "cnt.cast");
  }

  // Add the start index to the loop count to get the new end index.
  Value *IdxEnd = BypassBuilder.CreateAdd(Count, StartIdx, "end.idx");

  // Now we need to generate the expression for N - (N % VF), which is
  // the part that the vectorized body will execute.
  Value *R = BypassBuilder.CreateURem(Count, Step, "n.mod.vf");
  Value *CountRoundDown = BypassBuilder.CreateSub(Count, R, "n.vec");
  Value *IdxEndRoundDown = BypassBuilder.CreateAdd(CountRoundDown, StartIdx,
                                                     "end.idx.rnd.down");

  // Now, compare the new count to zero. If it is zero skip the vector loop and
  // jump to the scalar loop.
  Value *Cmp =
      BypassBuilder.CreateICmpEQ(IdxEndRoundDown, StartIdx, "cmp.zero");

  BasicBlock *LastBypassBlock = BypassBlock;

  // Generate code to check that the loops trip count that we computed by adding
  // one to the backedge-taken count will not overflow.
  {
    auto PastOverflowCheck =
        std::next(BasicBlock::iterator(OverflowCheckAnchor));
    BasicBlock *CheckBlock =
      LastBypassBlock->splitBasicBlock(PastOverflowCheck, "overflow.checked");
    if (ParentLoop)
      ParentLoop->addBasicBlockToLoop(CheckBlock, *LI);
    LoopBypassBlocks.push_back(CheckBlock);
    Instruction *OldTerm = LastBypassBlock->getTerminator();
    BranchInst::Create(ScalarPH, CheckBlock, CheckBCOverflow, OldTerm);
    OldTerm->eraseFromParent();
    LastBypassBlock = CheckBlock;
  }

  // Generate the code to check that the strides we assumed to be one are really
  // one. We want the new basic block to start at the first instruction in a
  // sequence of instructions that form a check.
  Instruction *StrideCheck;
  Instruction *FirstCheckInst;
  std::tie(FirstCheckInst, StrideCheck) =
      addStrideCheck(LastBypassBlock->getTerminator());
  if (StrideCheck) {
    // Create a new block containing the stride check.
    BasicBlock *CheckBlock =
        LastBypassBlock->splitBasicBlock(FirstCheckInst, "vector.stridecheck");
    if (ParentLoop)
      ParentLoop->addBasicBlockToLoop(CheckBlock, *LI);
    LoopBypassBlocks.push_back(CheckBlock);

    // Replace the branch into the memory check block with a conditional branch
    // for the "few elements case".
    Instruction *OldTerm = LastBypassBlock->getTerminator();
    BranchInst::Create(MiddleBlock, CheckBlock, Cmp, OldTerm);
    OldTerm->eraseFromParent();

    Cmp = StrideCheck;
    LastBypassBlock = CheckBlock;
  }

  // Generate the code that checks in runtime if arrays overlap. We put the
  // checks into a separate block to make the more common case of few elements
  // faster.
  Instruction *MemRuntimeCheck;
  std::tie(FirstCheckInst, MemRuntimeCheck) =
    Legal->getLAA()->addRuntimeCheck(LastBypassBlock->getTerminator());
  if (MemRuntimeCheck) {
    // Create a new block containing the memory check.
    BasicBlock *CheckBlock =
        LastBypassBlock->splitBasicBlock(FirstCheckInst, "vector.memcheck");
    if (ParentLoop)
      ParentLoop->addBasicBlockToLoop(CheckBlock, *LI);
    LoopBypassBlocks.push_back(CheckBlock);

    // Replace the branch into the memory check block with a conditional branch
    // for the "few elements case".
    Instruction *OldTerm = LastBypassBlock->getTerminator();
    BranchInst::Create(MiddleBlock, CheckBlock, Cmp, OldTerm);
    OldTerm->eraseFromParent();

    Cmp = MemRuntimeCheck;
    LastBypassBlock = CheckBlock;
  }

  LastBypassBlock->getTerminator()->eraseFromParent();
  BranchInst::Create(MiddleBlock, VectorPH, Cmp,
                     LastBypassBlock);

  // We are going to resume the execution of the scalar loop.
  // Go over all of the induction variables that we found and fix the
  // PHIs that are left in the scalar version of the loop.
  // The starting values of PHI nodes depend on the counter of the last
  // iteration in the vectorized loop.
  // If we come from a bypass edge then we need to start from the original
  // start value.

  // This variable saves the new starting index for the scalar loop.
  PHINode *ResumeIndex = nullptr;
  LoopVectorizationLegality::InductionList::iterator I, E;
  LoopVectorizationLegality::InductionList *List = Legal->getInductionVars();
  // Set builder to point to last bypass block.
  BypassBuilder.SetInsertPoint(LoopBypassBlocks.back()->getTerminator());
  for (I = List->begin(), E = List->end(); I != E; ++I) {
    PHINode *OrigPhi = I->first;
    LoopVectorizationLegality::InductionInfo II = I->second;

    Type *ResumeValTy = (OrigPhi == OldInduction) ? IdxTy : OrigPhi->getType();
    PHINode *ResumeVal = PHINode::Create(ResumeValTy, 2, "resume.val",
                                         MiddleBlock->getTerminator());
    // We might have extended the type of the induction variable but we need a
    // truncated version for the scalar loop.
    PHINode *TruncResumeVal = (OrigPhi == OldInduction) ?
      PHINode::Create(OrigPhi->getType(), 2, "trunc.resume.val",
                      MiddleBlock->getTerminator()) : nullptr;

    // Create phi nodes to merge from the  backedge-taken check block.
    PHINode *BCResumeVal = PHINode::Create(ResumeValTy, 3, "bc.resume.val",
                                           ScalarPH->getTerminator());
    BCResumeVal->addIncoming(ResumeVal, MiddleBlock);

    PHINode *BCTruncResumeVal = nullptr;
    if (OrigPhi == OldInduction) {
      BCTruncResumeVal =
          PHINode::Create(OrigPhi->getType(), 2, "bc.trunc.resume.val",
                          ScalarPH->getTerminator());
      BCTruncResumeVal->addIncoming(TruncResumeVal, MiddleBlock);
    }

    Value *EndValue = nullptr;
    switch (II.IK) {
    case LoopVectorizationLegality::IK_NoInduction:
      llvm_unreachable("Unknown induction");
    case LoopVectorizationLegality::IK_IntInduction: {
      // Handle the integer induction counter.
      assert(OrigPhi->getType()->isIntegerTy() && "Invalid type");

      // We have the canonical induction variable.
      if (OrigPhi == OldInduction) {
        // Create a truncated version of the resume value for the scalar loop,
        // we might have promoted the type to a larger width.
        EndValue =
          BypassBuilder.CreateTrunc(IdxEndRoundDown, OrigPhi->getType());
        // The new PHI merges the original incoming value, in case of a bypass,
        // or the value at the end of the vectorized loop.
        for (unsigned I = 1, E = LoopBypassBlocks.size(); I != E; ++I)
          TruncResumeVal->addIncoming(II.StartValue, LoopBypassBlocks[I]);
        TruncResumeVal->addIncoming(EndValue, VecBody);

        BCTruncResumeVal->addIncoming(II.StartValue, LoopBypassBlocks[0]);

        // We know what the end value is.
        EndValue = IdxEndRoundDown;
        // We also know which PHI node holds it.
        ResumeIndex = ResumeVal;
        break;
      }

      // Not the canonical induction variable - add the vector loop count to the
      // start value.
      Value *CRD = BypassBuilder.CreateSExtOrTrunc(CountRoundDown,
                                                   II.StartValue->getType(),
                                                   "cast.crd");
      EndValue = II.transform(BypassBuilder, CRD);
      EndValue->setName("ind.end");
      break;
    }
    case LoopVectorizationLegality::IK_PtrInduction: {
      EndValue = II.transform(BypassBuilder, CountRoundDown);
      EndValue->setName("ptr.ind.end");
      break;
    }
    }// end of case

    // The new PHI merges the original incoming value, in case of a bypass,
    // or the value at the end of the vectorized loop.
    for (unsigned I = 1, E = LoopBypassBlocks.size(); I != E; ++I) {
      if (OrigPhi == OldInduction)
        ResumeVal->addIncoming(StartIdx, LoopBypassBlocks[I]);
      else
        ResumeVal->addIncoming(II.StartValue, LoopBypassBlocks[I]);
    }
    ResumeVal->addIncoming(EndValue, VecBody);

    // Fix the scalar body counter (PHI node).
    unsigned BlockIdx = OrigPhi->getBasicBlockIndex(ScalarPH);

    // The old induction's phi node in the scalar body needs the truncated
    // value.
    if (OrigPhi == OldInduction) {
      BCResumeVal->addIncoming(StartIdx, LoopBypassBlocks[0]);
      OrigPhi->setIncomingValue(BlockIdx, BCTruncResumeVal);
    } else {
      BCResumeVal->addIncoming(II.StartValue, LoopBypassBlocks[0]);
      OrigPhi->setIncomingValue(BlockIdx, BCResumeVal);
    }
  }

  // If we are generating a new induction variable then we also need to
  // generate the code that calculates the exit value. This value is not
  // simply the end of the counter because we may skip the vectorized body
  // in case of a runtime check.
  if (!OldInduction){
    assert(!ResumeIndex && "Unexpected resume value found");
    ResumeIndex = PHINode::Create(IdxTy, 2, "new.indc.resume.val",
                                  MiddleBlock->getTerminator());
    for (unsigned I = 1, E = LoopBypassBlocks.size(); I != E; ++I)
      ResumeIndex->addIncoming(StartIdx, LoopBypassBlocks[I]);
    ResumeIndex->addIncoming(IdxEndRoundDown, VecBody);
  }

  // Make sure that we found the index where scalar loop needs to continue.
  assert(ResumeIndex && ResumeIndex->getType()->isIntegerTy() &&
         "Invalid resume Index");

  // Add a check in the middle block to see if we have completed
  // all of the iterations in the first vector loop.
  // If (N - N%VF) == N, then we *don't* need to run the remainder.
  Value *CmpN = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_EQ, IdxEnd,
                                ResumeIndex, "cmp.n",
                                MiddleBlock->getTerminator());

  BranchInst::Create(ExitBlock, ScalarPH, CmpN, MiddleBlock->getTerminator());
  // Remove the old terminator.
  MiddleBlock->getTerminator()->eraseFromParent();

  // Create i+1 and fill the PHINode.
  Value *NextIdx = Builder.CreateAdd(Induction, Step, "index.next");
  Induction->addIncoming(StartIdx, VectorPH);
  Induction->addIncoming(NextIdx, VecBody);
  // Create the compare.
  Value *ICmp = Builder.CreateICmpEQ(NextIdx, IdxEndRoundDown);
  Builder.CreateCondBr(ICmp, MiddleBlock, VecBody);

  // Now we have two terminators. Remove the old one from the block.
  VecBody->getTerminator()->eraseFromParent();

  // Get ready to start creating new instructions into the vectorized body.
  Builder.SetInsertPoint(VecBody->getFirstInsertionPt());

  // Save the state.
  LoopVectorPreHeader = VectorPH;
  LoopScalarPreHeader = ScalarPH;
  LoopMiddleBlock = MiddleBlock;
  LoopExitBlock = ExitBlock;
  LoopVectorBody.push_back(VecBody);
  LoopScalarBody = OldBasicBlock;

  LoopVectorizeHints Hints(Lp, true);
  Hints.setAlreadyVectorized();
}

/// This function returns the identity element (or neutral element) for
/// the operation K.
Constant*
LoopVectorizationLegality::getReductionIdentity(ReductionKind K, Type *Tp) {
  switch (K) {
  case RK_IntegerXor:
  case RK_IntegerAdd:
  case RK_IntegerOr:
    // Adding, Xoring, Oring zero to a number does not change it.
    return ConstantInt::get(Tp, 0);
  case RK_IntegerMult:
    // Multiplying a number by 1 does not change it.
    return ConstantInt::get(Tp, 1);
  case RK_IntegerAnd:
    // AND-ing a number with an all-1 value does not change it.
    return ConstantInt::get(Tp, -1, true);
  case  RK_FloatMult:
    // Multiplying a number by 1 does not change it.
    return ConstantFP::get(Tp, 1.0L);
  case  RK_FloatAdd:
    // Adding zero to a number does not change it.
    return ConstantFP::get(Tp, 0.0L);
  default:
    llvm_unreachable("Unknown reduction kind");
  }
}

/// This function translates the reduction kind to an LLVM binary operator.
static unsigned
getReductionBinOp(LoopVectorizationLegality::ReductionKind Kind) {
  switch (Kind) {
    case LoopVectorizationLegality::RK_IntegerAdd:
      return Instruction::Add;
    case LoopVectorizationLegality::RK_IntegerMult:
      return Instruction::Mul;
    case LoopVectorizationLegality::RK_IntegerOr:
      return Instruction::Or;
    case LoopVectorizationLegality::RK_IntegerAnd:
      return Instruction::And;
    case LoopVectorizationLegality::RK_IntegerXor:
      return Instruction::Xor;
    case LoopVectorizationLegality::RK_FloatMult:
      return Instruction::FMul;
    case LoopVectorizationLegality::RK_FloatAdd:
      return Instruction::FAdd;
    case LoopVectorizationLegality::RK_IntegerMinMax:
      return Instruction::ICmp;
    case LoopVectorizationLegality::RK_FloatMinMax:
      return Instruction::FCmp;
    default:
      llvm_unreachable("Unknown reduction operation");
  }
}

Value *createMinMaxOp(IRBuilder<> &Builder,
                      LoopVectorizationLegality::MinMaxReductionKind RK,
                      Value *Left,
                      Value *Right) {
  CmpInst::Predicate P = CmpInst::ICMP_NE;
  switch (RK) {
  default:
    llvm_unreachable("Unknown min/max reduction kind");
  case LoopVectorizationLegality::MRK_UIntMin:
    P = CmpInst::ICMP_ULT;
    break;
  case LoopVectorizationLegality::MRK_UIntMax:
    P = CmpInst::ICMP_UGT;
    break;
  case LoopVectorizationLegality::MRK_SIntMin:
    P = CmpInst::ICMP_SLT;
    break;
  case LoopVectorizationLegality::MRK_SIntMax:
    P = CmpInst::ICMP_SGT;
    break;
  case LoopVectorizationLegality::MRK_FloatMin:
    P = CmpInst::FCMP_OLT;
    break;
  case LoopVectorizationLegality::MRK_FloatMax:
    P = CmpInst::FCMP_OGT;
    break;
  }

  Value *Cmp;
  if (RK == LoopVectorizationLegality::MRK_FloatMin ||
      RK == LoopVectorizationLegality::MRK_FloatMax)
    Cmp = Builder.CreateFCmp(P, Left, Right, "rdx.minmax.cmp");
  else
    Cmp = Builder.CreateICmp(P, Left, Right, "rdx.minmax.cmp");

  Value *Select = Builder.CreateSelect(Cmp, Left, Right, "rdx.minmax.select");
  return Select;
}

namespace {
struct CSEDenseMapInfo {
  static bool canHandle(Instruction *I) {
    return isa<InsertElementInst>(I) || isa<ExtractElementInst>(I) ||
           isa<ShuffleVectorInst>(I) || isa<GetElementPtrInst>(I);
  }
  static inline Instruction *getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }
  static inline Instruction *getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }
  static unsigned getHashValue(Instruction *I) {
    assert(canHandle(I) && "Unknown instruction!");
    return hash_combine(I->getOpcode(), hash_combine_range(I->value_op_begin(),
                                                           I->value_op_end()));
  }
  static bool isEqual(Instruction *LHS, Instruction *RHS) {
    if (LHS == getEmptyKey() || RHS == getEmptyKey() ||
        LHS == getTombstoneKey() || RHS == getTombstoneKey())
      return LHS == RHS;
    return LHS->isIdenticalTo(RHS);
  }
};
}

/// \brief Check whether this block is a predicated block.
/// Due to if predication of stores we might create a sequence of "if(pred) a[i]
/// = ...;  " blocks. We start with one vectorized basic block. For every
/// conditional block we split this vectorized block. Therefore, every second
/// block will be a predicated one.
static bool isPredicatedBlock(unsigned BlockNum) {
  return BlockNum % 2;
}

///\brief Perform cse of induction variable instructions.
static void cse(SmallVector<BasicBlock *, 4> &BBs) {
  // Perform simple cse.
  SmallDenseMap<Instruction *, Instruction *, 4, CSEDenseMapInfo> CSEMap;
  for (unsigned i = 0, e = BBs.size(); i != e; ++i) {
    BasicBlock *BB = BBs[i];
    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;) {
      Instruction *In = I++;

      if (!CSEDenseMapInfo::canHandle(In))
        continue;

      // Check if we can replace this instruction with any of the
      // visited instructions.
      if (Instruction *V = CSEMap.lookup(In)) {
        In->replaceAllUsesWith(V);
        In->eraseFromParent();
        continue;
      }
      // Ignore instructions in conditional blocks. We create "if (pred) a[i] =
      // ...;" blocks for predicated stores. Every second block is a predicated
      // block.
      if (isPredicatedBlock(i))
        continue;

      CSEMap[In] = In;
    }
  }
}

/// \brief Adds a 'fast' flag to floating point operations.
static Value *addFastMathFlag(Value *V) {
  if (isa<FPMathOperator>(V)){
    FastMathFlags Flags;
    Flags.setUnsafeAlgebra();
    cast<Instruction>(V)->setFastMathFlags(Flags);
  }
  return V;
}

void InnerLoopVectorizer::vectorizeLoop() {
  //===------------------------------------------------===//
  //
  // Notice: any optimization or new instruction that go
  // into the code below should be also be implemented in
  // the cost-model.
  //
  //===------------------------------------------------===//
  Constant *Zero = Builder.getInt32(0);

  // In order to support reduction variables we need to be able to vectorize
  // Phi nodes. Phi nodes have cycles, so we need to vectorize them in two
  // stages. First, we create a new vector PHI node with no incoming edges.
  // We use this value when we vectorize all of the instructions that use the
  // PHI. Next, after all of the instructions in the block are complete we
  // add the new incoming edges to the PHI. At this point all of the
  // instructions in the basic block are vectorized, so we can use them to
  // construct the PHI.
  PhiVector RdxPHIsToFix;

  // Scan the loop in a topological order to ensure that defs are vectorized
  // before users.
  LoopBlocksDFS DFS(OrigLoop);
  DFS.perform(LI);

  // Vectorize all of the blocks in the original loop.
  for (LoopBlocksDFS::RPOIterator bb = DFS.beginRPO(),
       be = DFS.endRPO(); bb != be; ++bb)
    vectorizeBlockInLoop(*bb, &RdxPHIsToFix);

  // At this point every instruction in the original loop is widened to
  // a vector form. We are almost done. Now, we need to fix the PHI nodes
  // that we vectorized. The PHI nodes are currently empty because we did
  // not want to introduce cycles. Notice that the remaining PHI nodes
  // that we need to fix are reduction variables.

  // Create the 'reduced' values for each of the induction vars.
  // The reduced values are the vector values that we scalarize and combine
  // after the loop is finished.
  for (PhiVector::iterator it = RdxPHIsToFix.begin(), e = RdxPHIsToFix.end();
       it != e; ++it) {
    PHINode *RdxPhi = *it;
    assert(RdxPhi && "Unable to recover vectorized PHI");

    // Find the reduction variable descriptor.
    assert(Legal->getReductionVars()->count(RdxPhi) &&
           "Unable to find the reduction variable");
    LoopVectorizationLegality::ReductionDescriptor RdxDesc =
    (*Legal->getReductionVars())[RdxPhi];

    setDebugLocFromInst(Builder, RdxDesc.StartValue);

    // We need to generate a reduction vector from the incoming scalar.
    // To do so, we need to generate the 'identity' vector and override
    // one of the elements with the incoming scalar reduction. We need
    // to do it in the vector-loop preheader.
    Builder.SetInsertPoint(LoopBypassBlocks[1]->getTerminator());

    // This is the vector-clone of the value that leaves the loop.
    VectorParts &VectorExit = getVectorValue(RdxDesc.LoopExitInstr);
    Type *VecTy = VectorExit[0]->getType();

    // Find the reduction identity variable. Zero for addition, or, xor,
    // one for multiplication, -1 for And.
    Value *Identity;
    Value *VectorStart;
    if (RdxDesc.Kind == LoopVectorizationLegality::RK_IntegerMinMax ||
        RdxDesc.Kind == LoopVectorizationLegality::RK_FloatMinMax) {
      // MinMax reduction have the start value as their identify.
      if (VF == 1) {
        VectorStart = Identity = RdxDesc.StartValue;
      } else {
        VectorStart = Identity = Builder.CreateVectorSplat(VF,
                                                           RdxDesc.StartValue,
                                                           "minmax.ident");
      }
    } else {
      // Handle other reduction kinds:
      Constant *Iden =
      LoopVectorizationLegality::getReductionIdentity(RdxDesc.Kind,
                                                      VecTy->getScalarType());
      if (VF == 1) {
        Identity = Iden;
        // This vector is the Identity vector where the first element is the
        // incoming scalar reduction.
        VectorStart = RdxDesc.StartValue;
      } else {
        Identity = ConstantVector::getSplat(VF, Iden);

        // This vector is the Identity vector where the first element is the
        // incoming scalar reduction.
        VectorStart = Builder.CreateInsertElement(Identity,
                                                  RdxDesc.StartValue, Zero);
      }
    }

    // Fix the vector-loop phi.

    // Reductions do not have to start at zero. They can start with
    // any loop invariant values.
    VectorParts &VecRdxPhi = WidenMap.get(RdxPhi);
    BasicBlock *Latch = OrigLoop->getLoopLatch();
    Value *LoopVal = RdxPhi->getIncomingValueForBlock(Latch);
    VectorParts &Val = getVectorValue(LoopVal);
    for (unsigned part = 0; part < UF; ++part) {
      // Make sure to add the reduction stat value only to the
      // first unroll part.
      Value *StartVal = (part == 0) ? VectorStart : Identity;
      cast<PHINode>(VecRdxPhi[part])->addIncoming(StartVal,
                                                  LoopVectorPreHeader);
      cast<PHINode>(VecRdxPhi[part])->addIncoming(Val[part],
                                                  LoopVectorBody.back());
    }

    // Before each round, move the insertion point right between
    // the PHIs and the values we are going to write.
    // This allows us to write both PHINodes and the extractelement
    // instructions.
    Builder.SetInsertPoint(LoopMiddleBlock->getFirstInsertionPt());

    VectorParts RdxParts;
    setDebugLocFromInst(Builder, RdxDesc.LoopExitInstr);
    for (unsigned part = 0; part < UF; ++part) {
      // This PHINode contains the vectorized reduction variable, or
      // the initial value vector, if we bypass the vector loop.
      VectorParts &RdxExitVal = getVectorValue(RdxDesc.LoopExitInstr);
      PHINode *NewPhi = Builder.CreatePHI(VecTy, 2, "rdx.vec.exit.phi");
      Value *StartVal = (part == 0) ? VectorStart : Identity;
      for (unsigned I = 1, E = LoopBypassBlocks.size(); I != E; ++I)
        NewPhi->addIncoming(StartVal, LoopBypassBlocks[I]);
      NewPhi->addIncoming(RdxExitVal[part],
                          LoopVectorBody.back());
      RdxParts.push_back(NewPhi);
    }

    // Reduce all of the unrolled parts into a single vector.
    Value *ReducedPartRdx = RdxParts[0];
    unsigned Op = getReductionBinOp(RdxDesc.Kind);
    setDebugLocFromInst(Builder, ReducedPartRdx);
    for (unsigned part = 1; part < UF; ++part) {
      if (Op != Instruction::ICmp && Op != Instruction::FCmp)
        // Floating point operations had to be 'fast' to enable the reduction.
        ReducedPartRdx = addFastMathFlag(
            Builder.CreateBinOp((Instruction::BinaryOps)Op, RdxParts[part],
                                ReducedPartRdx, "bin.rdx"));
      else
        ReducedPartRdx = createMinMaxOp(Builder, RdxDesc.MinMaxKind,
                                        ReducedPartRdx, RdxParts[part]);
    }

    if (VF > 1) {
      // VF is a power of 2 so we can emit the reduction using log2(VF) shuffles
      // and vector ops, reducing the set of values being computed by half each
      // round.
      assert(isPowerOf2_32(VF) &&
             "Reduction emission only supported for pow2 vectors!");
      Value *TmpVec = ReducedPartRdx;
      SmallVector<Constant*, 32> ShuffleMask(VF, nullptr);
      for (unsigned i = VF; i != 1; i >>= 1) {
        // Move the upper half of the vector to the lower half.
        for (unsigned j = 0; j != i/2; ++j)
          ShuffleMask[j] = Builder.getInt32(i/2 + j);

        // Fill the rest of the mask with undef.
        std::fill(&ShuffleMask[i/2], ShuffleMask.end(),
                  UndefValue::get(Builder.getInt32Ty()));

        Value *Shuf =
        Builder.CreateShuffleVector(TmpVec,
                                    UndefValue::get(TmpVec->getType()),
                                    ConstantVector::get(ShuffleMask),
                                    "rdx.shuf");

        if (Op != Instruction::ICmp && Op != Instruction::FCmp)
          // Floating point operations had to be 'fast' to enable the reduction.
          TmpVec = addFastMathFlag(Builder.CreateBinOp(
              (Instruction::BinaryOps)Op, TmpVec, Shuf, "bin.rdx"));
        else
          TmpVec = createMinMaxOp(Builder, RdxDesc.MinMaxKind, TmpVec, Shuf);
      }

      // The result is in the first element of the vector.
      ReducedPartRdx = Builder.CreateExtractElement(TmpVec,
                                                    Builder.getInt32(0));
    }

    // Create a phi node that merges control-flow from the backedge-taken check
    // block and the middle block.
    PHINode *BCBlockPhi = PHINode::Create(RdxPhi->getType(), 2, "bc.merge.rdx",
                                          LoopScalarPreHeader->getTerminator());
    BCBlockPhi->addIncoming(RdxDesc.StartValue, LoopBypassBlocks[0]);
    BCBlockPhi->addIncoming(ReducedPartRdx, LoopMiddleBlock);

    // Now, we need to fix the users of the reduction variable
    // inside and outside of the scalar remainder loop.
    // We know that the loop is in LCSSA form. We need to update the
    // PHI nodes in the exit blocks.
    for (BasicBlock::iterator LEI = LoopExitBlock->begin(),
         LEE = LoopExitBlock->end(); LEI != LEE; ++LEI) {
      PHINode *LCSSAPhi = dyn_cast<PHINode>(LEI);
      if (!LCSSAPhi) break;

      // All PHINodes need to have a single entry edge, or two if
      // we already fixed them.
      assert(LCSSAPhi->getNumIncomingValues() < 3 && "Invalid LCSSA PHI");

      // We found our reduction value exit-PHI. Update it with the
      // incoming bypass edge.
      if (LCSSAPhi->getIncomingValue(0) == RdxDesc.LoopExitInstr) {
        // Add an edge coming from the bypass.
        LCSSAPhi->addIncoming(ReducedPartRdx, LoopMiddleBlock);
        break;
      }
    }// end of the LCSSA phi scan.

    // Fix the scalar loop reduction variable with the incoming reduction sum
    // from the vector body and from the backedge value.
    int IncomingEdgeBlockIdx =
    (RdxPhi)->getBasicBlockIndex(OrigLoop->getLoopLatch());
    assert(IncomingEdgeBlockIdx >= 0 && "Invalid block index");
    // Pick the other block.
    int SelfEdgeBlockIdx = (IncomingEdgeBlockIdx ? 0 : 1);
    (RdxPhi)->setIncomingValue(SelfEdgeBlockIdx, BCBlockPhi);
    (RdxPhi)->setIncomingValue(IncomingEdgeBlockIdx, RdxDesc.LoopExitInstr);
  }// end of for each redux variable.

  fixLCSSAPHIs();

  // Remove redundant induction instructions.
  cse(LoopVectorBody);
}

void InnerLoopVectorizer::fixLCSSAPHIs() {
  for (BasicBlock::iterator LEI = LoopExitBlock->begin(),
       LEE = LoopExitBlock->end(); LEI != LEE; ++LEI) {
    PHINode *LCSSAPhi = dyn_cast<PHINode>(LEI);
    if (!LCSSAPhi) break;
    if (LCSSAPhi->getNumIncomingValues() == 1)
      LCSSAPhi->addIncoming(UndefValue::get(LCSSAPhi->getType()),
                            LoopMiddleBlock);
  }
}

InnerLoopVectorizer::VectorParts
InnerLoopVectorizer::createEdgeMask(BasicBlock *Src, BasicBlock *Dst) {
  assert(std::find(pred_begin(Dst), pred_end(Dst), Src) != pred_end(Dst) &&
         "Invalid edge");

  // Look for cached value.
  std::pair<BasicBlock*, BasicBlock*> Edge(Src, Dst);
  EdgeMaskCache::iterator ECEntryIt = MaskCache.find(Edge);
  if (ECEntryIt != MaskCache.end())
    return ECEntryIt->second;

  VectorParts SrcMask = createBlockInMask(Src);

  // The terminator has to be a branch inst!
  BranchInst *BI = dyn_cast<BranchInst>(Src->getTerminator());
  assert(BI && "Unexpected terminator found");

  if (BI->isConditional()) {
    VectorParts EdgeMask = getVectorValue(BI->getCondition());

    if (BI->getSuccessor(0) != Dst)
      for (unsigned part = 0; part < UF; ++part)
        EdgeMask[part] = Builder.CreateNot(EdgeMask[part]);

    for (unsigned part = 0; part < UF; ++part)
      EdgeMask[part] = Builder.CreateAnd(EdgeMask[part], SrcMask[part]);

    MaskCache[Edge] = EdgeMask;
    return EdgeMask;
  }

  MaskCache[Edge] = SrcMask;
  return SrcMask;
}

InnerLoopVectorizer::VectorParts
InnerLoopVectorizer::createBlockInMask(BasicBlock *BB) {
  assert(OrigLoop->contains(BB) && "Block is not a part of a loop");

  // Loop incoming mask is all-one.
  if (OrigLoop->getHeader() == BB) {
    Value *C = ConstantInt::get(IntegerType::getInt1Ty(BB->getContext()), 1);
    return getVectorValue(C);
  }

  // This is the block mask. We OR all incoming edges, and with zero.
  Value *Zero = ConstantInt::get(IntegerType::getInt1Ty(BB->getContext()), 0);
  VectorParts BlockMask = getVectorValue(Zero);

  // For each pred:
  for (pred_iterator it = pred_begin(BB), e = pred_end(BB); it != e; ++it) {
    VectorParts EM = createEdgeMask(*it, BB);
    for (unsigned part = 0; part < UF; ++part)
      BlockMask[part] = Builder.CreateOr(BlockMask[part], EM[part]);
  }

  return BlockMask;
}

void InnerLoopVectorizer::widenPHIInstruction(Instruction *PN,
                                              InnerLoopVectorizer::VectorParts &Entry,
                                              unsigned UF, unsigned VF, PhiVector *PV) {
  PHINode* P = cast<PHINode>(PN);
  // Handle reduction variables:
  if (Legal->getReductionVars()->count(P)) {
    for (unsigned part = 0; part < UF; ++part) {
      // This is phase one of vectorizing PHIs.
      Type *VecTy = (VF == 1) ? PN->getType() :
      VectorType::get(PN->getType(), VF);
      Entry[part] = PHINode::Create(VecTy, 2, "vec.phi",
                                    LoopVectorBody.back()-> getFirstInsertionPt());
    }
    PV->push_back(P);
    return;
  }

  setDebugLocFromInst(Builder, P);
  // Check for PHI nodes that are lowered to vector selects.
  if (P->getParent() != OrigLoop->getHeader()) {
    // We know that all PHIs in non-header blocks are converted into
    // selects, so we don't have to worry about the insertion order and we
    // can just use the builder.
    // At this point we generate the predication tree. There may be
    // duplications since this is a simple recursive scan, but future
    // optimizations will clean it up.

    unsigned NumIncoming = P->getNumIncomingValues();

    // Generate a sequence of selects of the form:
    // SELECT(Mask3, In3,
    //      SELECT(Mask2, In2,
    //                   ( ...)))
    for (unsigned In = 0; In < NumIncoming; In++) {
      VectorParts Cond = createEdgeMask(P->getIncomingBlock(In),
                                        P->getParent());
      VectorParts &In0 = getVectorValue(P->getIncomingValue(In));

      for (unsigned part = 0; part < UF; ++part) {
        // We might have single edge PHIs (blocks) - use an identity
        // 'select' for the first PHI operand.
        if (In == 0)
          Entry[part] = Builder.CreateSelect(Cond[part], In0[part],
                                             In0[part]);
        else
          // Select between the current value and the previous incoming edge
          // based on the incoming mask.
          Entry[part] = Builder.CreateSelect(Cond[part], In0[part],
                                             Entry[part], "predphi");
      }
    }
    return;
  }

  // This PHINode must be an induction variable.
  // Make sure that we know about it.
  assert(Legal->getInductionVars()->count(P) &&
         "Not an induction variable");

  LoopVectorizationLegality::InductionInfo II =
  Legal->getInductionVars()->lookup(P);

  // FIXME: The newly created binary instructions should contain nsw/nuw flags,
  // which can be found from the original scalar operations.
  switch (II.IK) {
    case LoopVectorizationLegality::IK_NoInduction:
      llvm_unreachable("Unknown induction");
    case LoopVectorizationLegality::IK_IntInduction: {
      assert(P->getType() == II.StartValue->getType() && "Types must match");
      Type *PhiTy = P->getType();
      Value *Broadcasted;
      if (P == OldInduction) {
        // Handle the canonical induction variable. We might have had to
        // extend the type.
        Broadcasted = Builder.CreateTrunc(Induction, PhiTy);
      } else {
        // Handle other induction variables that are now based on the
        // canonical one.
        Value *NormalizedIdx = Builder.CreateSub(Induction, ExtendedIdx,
                                                 "normalized.idx");
        NormalizedIdx = Builder.CreateSExtOrTrunc(NormalizedIdx, PhiTy);
        Broadcasted = II.transform(Builder, NormalizedIdx);
        Broadcasted->setName("offset.idx");
      }
      Broadcasted = getBroadcastInstrs(Broadcasted);
      // After broadcasting the induction variable we need to make the vector
      // consecutive by adding 0, 1, 2, etc.
      for (unsigned part = 0; part < UF; ++part)
        Entry[part] = getStepVector(Broadcasted, VF * part, II.StepValue);
      return;
    }
    case LoopVectorizationLegality::IK_PtrInduction:
      // Handle the pointer induction variable case.
      assert(P->getType()->isPointerTy() && "Unexpected type.");
      // This is the normalized GEP that starts counting at zero.
      Value *NormalizedIdx =
          Builder.CreateSub(Induction, ExtendedIdx, "normalized.idx");
      // This is the vector of results. Notice that we don't generate
      // vector geps because scalar geps result in better code.
      for (unsigned part = 0; part < UF; ++part) {
        if (VF == 1) {
          int EltIndex = part;
          Constant *Idx = ConstantInt::get(Induction->getType(), EltIndex);
          Value *GlobalIdx = Builder.CreateAdd(NormalizedIdx, Idx);
          Value *SclrGep = II.transform(Builder, GlobalIdx);
          SclrGep->setName("next.gep");
          Entry[part] = SclrGep;
          continue;
        }

        Value *VecVal = UndefValue::get(VectorType::get(P->getType(), VF));
        for (unsigned int i = 0; i < VF; ++i) {
          int EltIndex = i + part * VF;
          Constant *Idx = ConstantInt::get(Induction->getType(), EltIndex);
          Value *GlobalIdx = Builder.CreateAdd(NormalizedIdx, Idx);
          Value *SclrGep = II.transform(Builder, GlobalIdx);
          SclrGep->setName("next.gep");
          VecVal = Builder.CreateInsertElement(VecVal, SclrGep,
                                               Builder.getInt32(i),
                                               "insert.gep");
        }
        Entry[part] = VecVal;
      }
      return;
  }
}

void InnerLoopVectorizer::vectorizeBlockInLoop(BasicBlock *BB, PhiVector *PV) {
  // For each instruction in the old loop.
  for (BasicBlock::iterator it = BB->begin(), e = BB->end(); it != e; ++it) {
    VectorParts &Entry = WidenMap.get(it);
    switch (it->getOpcode()) {
    case Instruction::Br:
      // Nothing to do for PHIs and BR, since we already took care of the
      // loop control flow instructions.
      continue;
    case Instruction::PHI: {
      // Vectorize PHINodes.
      widenPHIInstruction(it, Entry, UF, VF, PV);
      continue;
    }// End of PHI.

    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
      // Just widen binops.
      BinaryOperator *BinOp = dyn_cast<BinaryOperator>(it);
      setDebugLocFromInst(Builder, BinOp);
      VectorParts &A = getVectorValue(it->getOperand(0));
      VectorParts &B = getVectorValue(it->getOperand(1));

      // Use this vector value for all users of the original instruction.
      for (unsigned Part = 0; Part < UF; ++Part) {
        Value *V = Builder.CreateBinOp(BinOp->getOpcode(), A[Part], B[Part]);

        if (BinaryOperator *VecOp = dyn_cast<BinaryOperator>(V))
          VecOp->copyIRFlags(BinOp);

        Entry[Part] = V;
      }

      propagateMetadata(Entry, it);
      break;
    }
    case Instruction::Select: {
      // Widen selects.
      // If the selector is loop invariant we can create a select
      // instruction with a scalar condition. Otherwise, use vector-select.
      bool InvariantCond = SE->isLoopInvariant(SE->getSCEV(it->getOperand(0)),
                                               OrigLoop);
      setDebugLocFromInst(Builder, it);

      // The condition can be loop invariant  but still defined inside the
      // loop. This means that we can't just use the original 'cond' value.
      // We have to take the 'vectorized' value and pick the first lane.
      // Instcombine will make this a no-op.
      VectorParts &Cond = getVectorValue(it->getOperand(0));
      VectorParts &Op0  = getVectorValue(it->getOperand(1));
      VectorParts &Op1  = getVectorValue(it->getOperand(2));

      Value *ScalarCond = (VF == 1) ? Cond[0] :
        Builder.CreateExtractElement(Cond[0], Builder.getInt32(0));

      for (unsigned Part = 0; Part < UF; ++Part) {
        Entry[Part] = Builder.CreateSelect(
          InvariantCond ? ScalarCond : Cond[Part],
          Op0[Part],
          Op1[Part]);
      }

      propagateMetadata(Entry, it);
      break;
    }

    case Instruction::ICmp:
    case Instruction::FCmp: {
      // Widen compares. Generate vector compares.
      bool FCmp = (it->getOpcode() == Instruction::FCmp);
      CmpInst *Cmp = dyn_cast<CmpInst>(it);
      setDebugLocFromInst(Builder, it);
      VectorParts &A = getVectorValue(it->getOperand(0));
      VectorParts &B = getVectorValue(it->getOperand(1));
      for (unsigned Part = 0; Part < UF; ++Part) {
        Value *C = nullptr;
        if (FCmp)
          C = Builder.CreateFCmp(Cmp->getPredicate(), A[Part], B[Part]);
        else
          C = Builder.CreateICmp(Cmp->getPredicate(), A[Part], B[Part]);
        Entry[Part] = C;
      }

      propagateMetadata(Entry, it);
      break;
    }

    case Instruction::Store:
    case Instruction::Load:
      vectorizeMemoryInstruction(it);
        break;
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::SIToFP:
    case Instruction::UIToFP:
    case Instruction::Trunc:
    case Instruction::FPTrunc:
    case Instruction::BitCast: {
      CastInst *CI = dyn_cast<CastInst>(it);
      setDebugLocFromInst(Builder, it);
      /// Optimize the special case where the source is the induction
      /// variable. Notice that we can only optimize the 'trunc' case
      /// because: a. FP conversions lose precision, b. sext/zext may wrap,
      /// c. other casts depend on pointer size.
      if (CI->getOperand(0) == OldInduction &&
          it->getOpcode() == Instruction::Trunc) {
        Value *ScalarCast = Builder.CreateCast(CI->getOpcode(), Induction,
                                               CI->getType());
        Value *Broadcasted = getBroadcastInstrs(ScalarCast);
        LoopVectorizationLegality::InductionInfo II =
            Legal->getInductionVars()->lookup(OldInduction);
        Constant *Step =
            ConstantInt::getSigned(CI->getType(), II.StepValue->getSExtValue());
        for (unsigned Part = 0; Part < UF; ++Part)
          Entry[Part] = getStepVector(Broadcasted, VF * Part, Step);
        propagateMetadata(Entry, it);
        break;
      }
      /// Vectorize casts.
      Type *DestTy = (VF == 1) ? CI->getType() :
                                 VectorType::get(CI->getType(), VF);

      VectorParts &A = getVectorValue(it->getOperand(0));
      for (unsigned Part = 0; Part < UF; ++Part)
        Entry[Part] = Builder.CreateCast(CI->getOpcode(), A[Part], DestTy);
      propagateMetadata(Entry, it);
      break;
    }

    case Instruction::Call: {
      // Ignore dbg intrinsics.
      if (isa<DbgInfoIntrinsic>(it))
        break;
      setDebugLocFromInst(Builder, it);

      Module *M = BB->getParent()->getParent();
      CallInst *CI = cast<CallInst>(it);
      Intrinsic::ID ID = getIntrinsicIDForCall(CI, TLI);
      assert(ID && "Not an intrinsic call!");
      switch (ID) {
      case Intrinsic::assume:
      case Intrinsic::lifetime_end:
      case Intrinsic::lifetime_start:
        scalarizeInstruction(it);
        break;
      default:
        bool HasScalarOpd = hasVectorInstrinsicScalarOpd(ID, 1);
        for (unsigned Part = 0; Part < UF; ++Part) {
          SmallVector<Value *, 4> Args;
          for (unsigned i = 0, ie = CI->getNumArgOperands(); i != ie; ++i) {
            if (HasScalarOpd && i == 1) {
              Args.push_back(CI->getArgOperand(i));
              continue;
            }
            VectorParts &Arg = getVectorValue(CI->getArgOperand(i));
            Args.push_back(Arg[Part]);
          }
          Type *Tys[] = {CI->getType()};
          if (VF > 1)
            Tys[0] = VectorType::get(CI->getType()->getScalarType(), VF);

          Function *F = Intrinsic::getDeclaration(M, ID, Tys);
          Entry[Part] = Builder.CreateCall(F, Args);
        }

        propagateMetadata(Entry, it);
        break;
      }
      break;
    }

    default:
      // All other instructions are unsupported. Scalarize them.
      scalarizeInstruction(it);
      break;
    }// end of switch.
  }// end of for_each instr.
}

void InnerLoopVectorizer::updateAnalysis() {
  // Forget the original basic block.
  SE->forgetLoop(OrigLoop);

  // Update the dominator tree information.
  assert(DT->properlyDominates(LoopBypassBlocks.front(), LoopExitBlock) &&
         "Entry does not dominate exit.");

  for (unsigned I = 1, E = LoopBypassBlocks.size(); I != E; ++I)
    DT->addNewBlock(LoopBypassBlocks[I], LoopBypassBlocks[I-1]);
  DT->addNewBlock(LoopVectorPreHeader, LoopBypassBlocks.back());

  // Due to if predication of stores we might create a sequence of "if(pred)
  // a[i] = ...;  " blocks.
  for (unsigned i = 0, e = LoopVectorBody.size(); i != e; ++i) {
    if (i == 0)
      DT->addNewBlock(LoopVectorBody[0], LoopVectorPreHeader);
    else if (isPredicatedBlock(i)) {
      DT->addNewBlock(LoopVectorBody[i], LoopVectorBody[i-1]);
    } else {
      DT->addNewBlock(LoopVectorBody[i], LoopVectorBody[i-2]);
    }
  }

  DT->addNewBlock(LoopMiddleBlock, LoopBypassBlocks[1]);
  DT->addNewBlock(LoopScalarPreHeader, LoopBypassBlocks[0]);
  DT->changeImmediateDominator(LoopScalarBody, LoopScalarPreHeader);
  DT->changeImmediateDominator(LoopExitBlock, LoopBypassBlocks[0]);

  DEBUG(DT->verifyDomTree());
}

/// \brief Check whether it is safe to if-convert this phi node.
///
/// Phi nodes with constant expressions that can trap are not safe to if
/// convert.
static bool canIfConvertPHINodes(BasicBlock *BB) {
  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
    PHINode *Phi = dyn_cast<PHINode>(I);
    if (!Phi)
      return true;
    for (unsigned p = 0, e = Phi->getNumIncomingValues(); p != e; ++p)
      if (Constant *C = dyn_cast<Constant>(Phi->getIncomingValue(p)))
        if (C->canTrap())
          return false;
  }
  return true;
}

bool LoopVectorizationLegality::canVectorizeWithIfConvert() {
  if (!EnableIfConversion) {
    emitAnalysis(VectorizationReport() << "if-conversion is disabled");
    return false;
  }

  assert(TheLoop->getNumBlocks() > 1 && "Single block loops are vectorizable");

  // A list of pointers that we can safely read and write to.
  SmallPtrSet<Value *, 8> SafePointes;

  // Collect safe addresses.
  for (Loop::block_iterator BI = TheLoop->block_begin(),
         BE = TheLoop->block_end(); BI != BE; ++BI) {
    BasicBlock *BB = *BI;

    if (blockNeedsPredication(BB))
      continue;

    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
      if (LoadInst *LI = dyn_cast<LoadInst>(I))
        SafePointes.insert(LI->getPointerOperand());
      else if (StoreInst *SI = dyn_cast<StoreInst>(I))
        SafePointes.insert(SI->getPointerOperand());
    }
  }

  // Collect the blocks that need predication.
  BasicBlock *Header = TheLoop->getHeader();
  for (Loop::block_iterator BI = TheLoop->block_begin(),
         BE = TheLoop->block_end(); BI != BE; ++BI) {
    BasicBlock *BB = *BI;

    // We don't support switch statements inside loops.
    if (!isa<BranchInst>(BB->getTerminator())) {
      emitAnalysis(VectorizationReport(BB->getTerminator())
                   << "loop contains a switch statement");
      return false;
    }

    // We must be able to predicate all blocks that need to be predicated.
    if (blockNeedsPredication(BB)) {
      if (!blockCanBePredicated(BB, SafePointes)) {
        emitAnalysis(VectorizationReport(BB->getTerminator())
                     << "control flow cannot be substituted for a select");
        return false;
      }
    } else if (BB != Header && !canIfConvertPHINodes(BB)) {
      emitAnalysis(VectorizationReport(BB->getTerminator())
                   << "control flow cannot be substituted for a select");
      return false;
    }
  }

  // We can if-convert this loop.
  return true;
}

bool LoopVectorizationLegality::canVectorize() {
  // We must have a loop in canonical form. Loops with indirectbr in them cannot
  // be canonicalized.
  if (!TheLoop->getLoopPreheader()) {
    emitAnalysis(
        VectorizationReport() <<
        "loop control flow is not understood by vectorizer");
    return false;
  }

  // We can only vectorize innermost loops.
  if (!TheLoop->getSubLoopsVector().empty()) {
    emitAnalysis(VectorizationReport() << "loop is not the innermost loop");
    return false;
  }

  // We must have a single backedge.
  if (TheLoop->getNumBackEdges() != 1) {
    emitAnalysis(
        VectorizationReport() <<
        "loop control flow is not understood by vectorizer");
    return false;
  }

  // We must have a single exiting block.
  if (!TheLoop->getExitingBlock()) {
    emitAnalysis(
        VectorizationReport() <<
        "loop control flow is not understood by vectorizer");
    return false;
  }

  // We only handle bottom-tested loops, i.e. loop in which the condition is
  // checked at the end of each iteration. With that we can assume that all
  // instructions in the loop are executed the same number of times.
  if (TheLoop->getExitingBlock() != TheLoop->getLoopLatch()) {
    emitAnalysis(
        VectorizationReport() <<
        "loop control flow is not understood by vectorizer");
    return false;
  }

  // We need to have a loop header.
  DEBUG(dbgs() << "LV: Found a loop: " <<
        TheLoop->getHeader()->getName() << '\n');

  // Check if we can if-convert non-single-bb loops.
  unsigned NumBlocks = TheLoop->getNumBlocks();
  if (NumBlocks != 1 && !canVectorizeWithIfConvert()) {
    DEBUG(dbgs() << "LV: Can't if-convert the loop.\n");
    return false;
  }

  // ScalarEvolution needs to be able to find the exit count.
  const SCEV *ExitCount = SE->getBackedgeTakenCount(TheLoop);
  if (ExitCount == SE->getCouldNotCompute()) {
    emitAnalysis(VectorizationReport() <<
                 "could not determine number of loop iterations");
    DEBUG(dbgs() << "LV: SCEV could not compute the loop exit count.\n");
    return false;
  }

  // Check if we can vectorize the instructions and CFG in this loop.
  if (!canVectorizeInstrs()) {
    DEBUG(dbgs() << "LV: Can't vectorize the instructions or CFG\n");
    return false;
  }

  // Go over each instruction and look at memory deps.
  if (!canVectorizeMemory()) {
    DEBUG(dbgs() << "LV: Can't vectorize due to memory conflicts\n");
    return false;
  }

  // Collect all of the variables that remain uniform after vectorization.
  collectLoopUniforms();

  DEBUG(dbgs() << "LV: We can vectorize this loop" <<
        (LAA.getRuntimePointerCheck()->Need ? " (with a runtime bound check)" :
         "")
        <<"!\n");

  // Okay! We can vectorize. At this point we don't have any other mem analysis
  // which may limit our maximum vectorization factor, so just return true with
  // no restrictions.
  return true;
}

static Type *convertPointerToIntegerType(const DataLayout &DL, Type *Ty) {
  if (Ty->isPointerTy())
    return DL.getIntPtrType(Ty);

  // It is possible that char's or short's overflow when we ask for the loop's
  // trip count, work around this by changing the type size.
  if (Ty->getScalarSizeInBits() < 32)
    return Type::getInt32Ty(Ty->getContext());

  return Ty;
}

static Type* getWiderType(const DataLayout &DL, Type *Ty0, Type *Ty1) {
  Ty0 = convertPointerToIntegerType(DL, Ty0);
  Ty1 = convertPointerToIntegerType(DL, Ty1);
  if (Ty0->getScalarSizeInBits() > Ty1->getScalarSizeInBits())
    return Ty0;
  return Ty1;
}

/// \brief Check that the instruction has outside loop users and is not an
/// identified reduction variable.
static bool hasOutsideLoopUser(const Loop *TheLoop, Instruction *Inst,
                               SmallPtrSetImpl<Value *> &Reductions) {
  // Reduction instructions are allowed to have exit users. All other
  // instructions must not have external users.
  if (!Reductions.count(Inst))
    //Check that all of the users of the loop are inside the BB.
    for (User *U : Inst->users()) {
      Instruction *UI = cast<Instruction>(U);
      // This user may be a reduction exit value.
      if (!TheLoop->contains(UI)) {
        DEBUG(dbgs() << "LV: Found an outside user for : " << *UI << '\n');
        return true;
      }
    }
  return false;
}

bool LoopVectorizationLegality::canVectorizeInstrs() {
  BasicBlock *PreHeader = TheLoop->getLoopPreheader();
  BasicBlock *Header = TheLoop->getHeader();

  // Look for the attribute signaling the absence of NaNs.
  Function &F = *Header->getParent();
  if (F.hasFnAttribute("no-nans-fp-math"))
    HasFunNoNaNAttr = F.getAttributes().getAttribute(
      AttributeSet::FunctionIndex,
      "no-nans-fp-math").getValueAsString() == "true";

  // For each block in the loop.
  for (Loop::block_iterator bb = TheLoop->block_begin(),
       be = TheLoop->block_end(); bb != be; ++bb) {

    // Scan the instructions in the block and look for hazards.
    for (BasicBlock::iterator it = (*bb)->begin(), e = (*bb)->end(); it != e;
         ++it) {

      if (PHINode *Phi = dyn_cast<PHINode>(it)) {
        Type *PhiTy = Phi->getType();
        // Check that this PHI type is allowed.
        if (!PhiTy->isIntegerTy() &&
            !PhiTy->isFloatingPointTy() &&
            !PhiTy->isPointerTy()) {
          emitAnalysis(VectorizationReport(it)
                       << "loop control flow is not understood by vectorizer");
          DEBUG(dbgs() << "LV: Found an non-int non-pointer PHI.\n");
          return false;
        }

        // If this PHINode is not in the header block, then we know that we
        // can convert it to select during if-conversion. No need to check if
        // the PHIs in this block are induction or reduction variables.
        if (*bb != Header) {
          // Check that this instruction has no outside users or is an
          // identified reduction value with an outside user.
          if (!hasOutsideLoopUser(TheLoop, it, AllowedExit))
            continue;
          emitAnalysis(VectorizationReport(it) <<
                       "value could not be identified as "
                       "an induction or reduction variable");
          return false;
        }

        // We only allow if-converted PHIs with exactly two incoming values.
        if (Phi->getNumIncomingValues() != 2) {
          emitAnalysis(VectorizationReport(it)
                       << "control flow not understood by vectorizer");
          DEBUG(dbgs() << "LV: Found an invalid PHI.\n");
          return false;
        }

        // This is the value coming from the preheader.
        Value *StartValue = Phi->getIncomingValueForBlock(PreHeader);
        ConstantInt *StepValue = nullptr;
        // Check if this is an induction variable.
        InductionKind IK = isInductionVariable(Phi, StepValue);

        if (IK_NoInduction != IK) {
          // Get the widest type.
          if (!WidestIndTy)
            WidestIndTy = convertPointerToIntegerType(*DL, PhiTy);
          else
            WidestIndTy = getWiderType(*DL, PhiTy, WidestIndTy);

          // Int inductions are special because we only allow one IV.
          if (IK == IK_IntInduction && StepValue->isOne()) {
            // Use the phi node with the widest type as induction. Use the last
            // one if there are multiple (no good reason for doing this other
            // than it is expedient).
            if (!Induction || PhiTy == WidestIndTy)
              Induction = Phi;
          }

          DEBUG(dbgs() << "LV: Found an induction variable.\n");
          Inductions[Phi] = InductionInfo(StartValue, IK, StepValue);

          // Until we explicitly handle the case of an induction variable with
          // an outside loop user we have to give up vectorizing this loop.
          if (hasOutsideLoopUser(TheLoop, it, AllowedExit)) {
            emitAnalysis(VectorizationReport(it) <<
                         "use of induction value outside of the "
                         "loop is not handled by vectorizer");
            return false;
          }

          continue;
        }

        if (AddReductionVar(Phi, RK_IntegerAdd)) {
          DEBUG(dbgs() << "LV: Found an ADD reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_IntegerMult)) {
          DEBUG(dbgs() << "LV: Found a MUL reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_IntegerOr)) {
          DEBUG(dbgs() << "LV: Found an OR reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_IntegerAnd)) {
          DEBUG(dbgs() << "LV: Found an AND reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_IntegerXor)) {
          DEBUG(dbgs() << "LV: Found a XOR reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_IntegerMinMax)) {
          DEBUG(dbgs() << "LV: Found a MINMAX reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_FloatMult)) {
          DEBUG(dbgs() << "LV: Found an FMult reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_FloatAdd)) {
          DEBUG(dbgs() << "LV: Found an FAdd reduction PHI."<< *Phi <<"\n");
          continue;
        }
        if (AddReductionVar(Phi, RK_FloatMinMax)) {
          DEBUG(dbgs() << "LV: Found an float MINMAX reduction PHI."<< *Phi <<
                "\n");
          continue;
        }

        emitAnalysis(VectorizationReport(it) <<
                     "value that could not be identified as "
                     "reduction is used outside the loop");
        DEBUG(dbgs() << "LV: Found an unidentified PHI."<< *Phi <<"\n");
        return false;
      }// end of PHI handling

      // We still don't handle functions. However, we can ignore dbg intrinsic
      // calls and we do handle certain intrinsic and libm functions.
      CallInst *CI = dyn_cast<CallInst>(it);
      if (CI && !getIntrinsicIDForCall(CI, TLI) && !isa<DbgInfoIntrinsic>(CI)) {
        emitAnalysis(VectorizationReport(it) <<
                     "call instruction cannot be vectorized");
        DEBUG(dbgs() << "LV: Found a call site.\n");
        return false;
      }

      // Intrinsics such as powi,cttz and ctlz are legal to vectorize if the
      // second argument is the same (i.e. loop invariant)
      if (CI &&
          hasVectorInstrinsicScalarOpd(getIntrinsicIDForCall(CI, TLI), 1)) {
        if (!SE->isLoopInvariant(SE->getSCEV(CI->getOperand(1)), TheLoop)) {
          emitAnalysis(VectorizationReport(it)
                       << "intrinsic instruction cannot be vectorized");
          DEBUG(dbgs() << "LV: Found unvectorizable intrinsic " << *CI << "\n");
          return false;
        }
      }

      // Check that the instruction return type is vectorizable.
      // Also, we can't vectorize extractelement instructions.
      if ((!VectorType::isValidElementType(it->getType()) &&
           !it->getType()->isVoidTy()) || isa<ExtractElementInst>(it)) {
        emitAnalysis(VectorizationReport(it)
                     << "instruction return type cannot be vectorized");
        DEBUG(dbgs() << "LV: Found unvectorizable type.\n");
        return false;
      }

      // Check that the stored type is vectorizable.
      if (StoreInst *ST = dyn_cast<StoreInst>(it)) {
        Type *T = ST->getValueOperand()->getType();
        if (!VectorType::isValidElementType(T)) {
          emitAnalysis(VectorizationReport(ST) <<
                       "store instruction cannot be vectorized");
          return false;
        }
        if (EnableMemAccessVersioning)
          collectStridedAccess(ST);
      }

      if (EnableMemAccessVersioning)
        if (LoadInst *LI = dyn_cast<LoadInst>(it))
          collectStridedAccess(LI);

      // Reduction instructions are allowed to have exit users.
      // All other instructions must not have external users.
      if (hasOutsideLoopUser(TheLoop, it, AllowedExit)) {
        emitAnalysis(VectorizationReport(it) <<
                     "value cannot be used outside the loop");
        return false;
      }

    } // next instr.

  }

  if (!Induction) {
    DEBUG(dbgs() << "LV: Did not find one integer induction var.\n");
    if (Inductions.empty()) {
      emitAnalysis(VectorizationReport()
                   << "loop induction variable could not be identified");
      return false;
    }
  }

  return true;
}

///\brief Remove GEPs whose indices but the last one are loop invariant and
/// return the induction operand of the gep pointer.
static Value *stripGetElementPtr(Value *Ptr, ScalarEvolution *SE,
                                 const DataLayout *DL, Loop *Lp) {
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP)
    return Ptr;

  unsigned InductionOperand = getGEPInductionOperand(DL, GEP);

  // Check that all of the gep indices are uniform except for our induction
  // operand.
  for (unsigned i = 0, e = GEP->getNumOperands(); i != e; ++i)
    if (i != InductionOperand &&
        !SE->isLoopInvariant(SE->getSCEV(GEP->getOperand(i)), Lp))
      return Ptr;
  return GEP->getOperand(InductionOperand);
}

///\brief Look for a cast use of the passed value.
static Value *getUniqueCastUse(Value *Ptr, Loop *Lp, Type *Ty) {
  Value *UniqueCast = nullptr;
  for (User *U : Ptr->users()) {
    CastInst *CI = dyn_cast<CastInst>(U);
    if (CI && CI->getType() == Ty) {
      if (!UniqueCast)
        UniqueCast = CI;
      else
        return nullptr;
    }
  }
  return UniqueCast;
}

///\brief Get the stride of a pointer access in a loop.
/// Looks for symbolic strides "a[i*stride]". Returns the symbolic stride as a
/// pointer to the Value, or null otherwise.
static Value *getStrideFromPointer(Value *Ptr, ScalarEvolution *SE,
                                   const DataLayout *DL, Loop *Lp) {
  const PointerType *PtrTy = dyn_cast<PointerType>(Ptr->getType());
  if (!PtrTy || PtrTy->isAggregateType())
    return nullptr;

  // Try to remove a gep instruction to make the pointer (actually index at this
  // point) easier analyzable. If OrigPtr is equal to Ptr we are analzying the
  // pointer, otherwise, we are analyzing the index.
  Value *OrigPtr = Ptr;

  // The size of the pointer access.
  int64_t PtrAccessSize = 1;

  Ptr = stripGetElementPtr(Ptr, SE, DL, Lp);
  const SCEV *V = SE->getSCEV(Ptr);

  if (Ptr != OrigPtr)
    // Strip off casts.
    while (const SCEVCastExpr *C = dyn_cast<SCEVCastExpr>(V))
      V = C->getOperand();

  const SCEVAddRecExpr *S = dyn_cast<SCEVAddRecExpr>(V);
  if (!S)
    return nullptr;

  V = S->getStepRecurrence(*SE);
  if (!V)
    return nullptr;

  // Strip off the size of access multiplication if we are still analyzing the
  // pointer.
  if (OrigPtr == Ptr) {
    DL->getTypeAllocSize(PtrTy->getElementType());
    if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(V)) {
      if (M->getOperand(0)->getSCEVType() != scConstant)
        return nullptr;

      const APInt &APStepVal =
          cast<SCEVConstant>(M->getOperand(0))->getValue()->getValue();

      // Huge step value - give up.
      if (APStepVal.getBitWidth() > 64)
        return nullptr;

      int64_t StepVal = APStepVal.getSExtValue();
      if (PtrAccessSize != StepVal)
        return nullptr;
      V = M->getOperand(1);
    }
  }

  // Strip off casts.
  Type *StripedOffRecurrenceCast = nullptr;
  if (const SCEVCastExpr *C = dyn_cast<SCEVCastExpr>(V)) {
    StripedOffRecurrenceCast = C->getType();
    V = C->getOperand();
  }

  // Look for the loop invariant symbolic value.
  const SCEVUnknown *U = dyn_cast<SCEVUnknown>(V);
  if (!U)
    return nullptr;

  Value *Stride = U->getValue();
  if (!Lp->isLoopInvariant(Stride))
    return nullptr;

  // If we have stripped off the recurrence cast we have to make sure that we
  // return the value that is used in this loop so that we can replace it later.
  if (StripedOffRecurrenceCast)
    Stride = getUniqueCastUse(Stride, Lp, StripedOffRecurrenceCast);

  return Stride;
}

void LoopVectorizationLegality::collectStridedAccess(Value *MemAccess) {
  Value *Ptr = nullptr;
  if (LoadInst *LI = dyn_cast<LoadInst>(MemAccess))
    Ptr = LI->getPointerOperand();
  else if (StoreInst *SI = dyn_cast<StoreInst>(MemAccess))
    Ptr = SI->getPointerOperand();
  else
    return;

  Value *Stride = getStrideFromPointer(Ptr, SE, DL, TheLoop);
  if (!Stride)
    return;

  DEBUG(dbgs() << "LV: Found a strided access that we can version");
  DEBUG(dbgs() << "  Ptr: " << *Ptr << " Stride: " << *Stride << "\n");
  Strides[Ptr] = Stride;
  StrideSet.insert(Stride);
}

void LoopVectorizationLegality::collectLoopUniforms() {
  // We now know that the loop is vectorizable!
  // Collect variables that will remain uniform after vectorization.
  std::vector<Value*> Worklist;
  BasicBlock *Latch = TheLoop->getLoopLatch();

  // Start with the conditional branch and walk up the block.
  Worklist.push_back(Latch->getTerminator()->getOperand(0));

  // Also add all consecutive pointer values; these values will be uniform
  // after vectorization (and subsequent cleanup) and, until revectorization is
  // supported, all dependencies must also be uniform.
  for (Loop::block_iterator B = TheLoop->block_begin(),
       BE = TheLoop->block_end(); B != BE; ++B)
    for (BasicBlock::iterator I = (*B)->begin(), IE = (*B)->end();
         I != IE; ++I)
      if (I->getType()->isPointerTy() && isConsecutivePtr(I))
        Worklist.insert(Worklist.end(), I->op_begin(), I->op_end());

  while (!Worklist.empty()) {
    Instruction *I = dyn_cast<Instruction>(Worklist.back());
    Worklist.pop_back();

    // Look at instructions inside this loop.
    // Stop when reaching PHI nodes.
    // TODO: we need to follow values all over the loop, not only in this block.
    if (!I || !TheLoop->contains(I) || isa<PHINode>(I))
      continue;

    // This is a known uniform.
    Uniforms.insert(I);

    // Insert all operands.
    Worklist.insert(Worklist.end(), I->op_begin(), I->op_end());
  }
}

bool LoopVectorizationLegality::canVectorizeMemory() {
  return LAA.canVectorizeMemory(Strides);
}

static bool hasMultipleUsesOf(Instruction *I,
                              SmallPtrSetImpl<Instruction *> &Insts) {
  unsigned NumUses = 0;
  for(User::op_iterator Use = I->op_begin(), E = I->op_end(); Use != E; ++Use) {
    if (Insts.count(dyn_cast<Instruction>(*Use)))
      ++NumUses;
    if (NumUses > 1)
      return true;
  }

  return false;
}

static bool areAllUsesIn(Instruction *I, SmallPtrSetImpl<Instruction *> &Set) {
  for(User::op_iterator Use = I->op_begin(), E = I->op_end(); Use != E; ++Use)
    if (!Set.count(dyn_cast<Instruction>(*Use)))
      return false;
  return true;
}

bool LoopVectorizationLegality::AddReductionVar(PHINode *Phi,
                                                ReductionKind Kind) {
  if (Phi->getNumIncomingValues() != 2)
    return false;

  // Reduction variables are only found in the loop header block.
  if (Phi->getParent() != TheLoop->getHeader())
    return false;

  // Obtain the reduction start value from the value that comes from the loop
  // preheader.
  Value *RdxStart = Phi->getIncomingValueForBlock(TheLoop->getLoopPreheader());

  // ExitInstruction is the single value which is used outside the loop.
  // We only allow for a single reduction value to be used outside the loop.
  // This includes users of the reduction, variables (which form a cycle
  // which ends in the phi node).
  Instruction *ExitInstruction = nullptr;
  // Indicates that we found a reduction operation in our scan.
  bool FoundReduxOp = false;

  // We start with the PHI node and scan for all of the users of this
  // instruction. All users must be instructions that can be used as reduction
  // variables (such as ADD). We must have a single out-of-block user. The cycle
  // must include the original PHI.
  bool FoundStartPHI = false;

  // To recognize min/max patterns formed by a icmp select sequence, we store
  // the number of instruction we saw from the recognized min/max pattern,
  //  to make sure we only see exactly the two instructions.
  unsigned NumCmpSelectPatternInst = 0;
  ReductionInstDesc ReduxDesc(false, nullptr);

  SmallPtrSet<Instruction *, 8> VisitedInsts;
  SmallVector<Instruction *, 8> Worklist;
  Worklist.push_back(Phi);
  VisitedInsts.insert(Phi);

  // A value in the reduction can be used:
  //  - By the reduction:
  //      - Reduction operation:
  //        - One use of reduction value (safe).
  //        - Multiple use of reduction value (not safe).
  //      - PHI:
  //        - All uses of the PHI must be the reduction (safe).
  //        - Otherwise, not safe.
  //  - By one instruction outside of the loop (safe).
  //  - By further instructions outside of the loop (not safe).
  //  - By an instruction that is not part of the reduction (not safe).
  //    This is either:
  //      * An instruction type other than PHI or the reduction operation.
  //      * A PHI in the header other than the initial PHI.
  while (!Worklist.empty()) {
    Instruction *Cur = Worklist.back();
    Worklist.pop_back();

    // No Users.
    // If the instruction has no users then this is a broken chain and can't be
    // a reduction variable.
    if (Cur->use_empty())
      return false;

    bool IsAPhi = isa<PHINode>(Cur);

    // A header PHI use other than the original PHI.
    if (Cur != Phi && IsAPhi && Cur->getParent() == Phi->getParent())
      return false;

    // Reductions of instructions such as Div, and Sub is only possible if the
    // LHS is the reduction variable.
    if (!Cur->isCommutative() && !IsAPhi && !isa<SelectInst>(Cur) &&
        !isa<ICmpInst>(Cur) && !isa<FCmpInst>(Cur) &&
        !VisitedInsts.count(dyn_cast<Instruction>(Cur->getOperand(0))))
      return false;

    // Any reduction instruction must be of one of the allowed kinds.
    ReduxDesc = isReductionInstr(Cur, Kind, ReduxDesc);
    if (!ReduxDesc.IsReduction)
      return false;

    // A reduction operation must only have one use of the reduction value.
    if (!IsAPhi && Kind != RK_IntegerMinMax && Kind != RK_FloatMinMax &&
        hasMultipleUsesOf(Cur, VisitedInsts))
      return false;

    // All inputs to a PHI node must be a reduction value.
    if(IsAPhi && Cur != Phi && !areAllUsesIn(Cur, VisitedInsts))
      return false;

    if (Kind == RK_IntegerMinMax && (isa<ICmpInst>(Cur) ||
                                     isa<SelectInst>(Cur)))
      ++NumCmpSelectPatternInst;
    if (Kind == RK_FloatMinMax && (isa<FCmpInst>(Cur) ||
                                   isa<SelectInst>(Cur)))
      ++NumCmpSelectPatternInst;

    // Check  whether we found a reduction operator.
    FoundReduxOp |= !IsAPhi;

    // Process users of current instruction. Push non-PHI nodes after PHI nodes
    // onto the stack. This way we are going to have seen all inputs to PHI
    // nodes once we get to them.
    SmallVector<Instruction *, 8> NonPHIs;
    SmallVector<Instruction *, 8> PHIs;
    for (User *U : Cur->users()) {
      Instruction *UI = cast<Instruction>(U);

      // Check if we found the exit user.
      BasicBlock *Parent = UI->getParent();
      if (!TheLoop->contains(Parent)) {
        // Exit if you find multiple outside users or if the header phi node is
        // being used. In this case the user uses the value of the previous
        // iteration, in which case we would loose "VF-1" iterations of the
        // reduction operation if we vectorize.
        if (ExitInstruction != nullptr || Cur == Phi)
          return false;

        // The instruction used by an outside user must be the last instruction
        // before we feed back to the reduction phi. Otherwise, we loose VF-1
        // operations on the value.
        if (std::find(Phi->op_begin(), Phi->op_end(), Cur) == Phi->op_end())
         return false;

        ExitInstruction = Cur;
        continue;
      }

      // Process instructions only once (termination). Each reduction cycle
      // value must only be used once, except by phi nodes and min/max
      // reductions which are represented as a cmp followed by a select.
      ReductionInstDesc IgnoredVal(false, nullptr);
      if (VisitedInsts.insert(UI).second) {
        if (isa<PHINode>(UI))
          PHIs.push_back(UI);
        else
          NonPHIs.push_back(UI);
      } else if (!isa<PHINode>(UI) &&
                 ((!isa<FCmpInst>(UI) &&
                   !isa<ICmpInst>(UI) &&
                   !isa<SelectInst>(UI)) ||
                  !isMinMaxSelectCmpPattern(UI, IgnoredVal).IsReduction))
        return false;

      // Remember that we completed the cycle.
      if (UI == Phi)
        FoundStartPHI = true;
    }
    Worklist.append(PHIs.begin(), PHIs.end());
    Worklist.append(NonPHIs.begin(), NonPHIs.end());
  }

  // This means we have seen one but not the other instruction of the
  // pattern or more than just a select and cmp.
  if ((Kind == RK_IntegerMinMax || Kind == RK_FloatMinMax) &&
      NumCmpSelectPatternInst != 2)
    return false;

  if (!FoundStartPHI || !FoundReduxOp || !ExitInstruction)
    return false;

  // We found a reduction var if we have reached the original phi node and we
  // only have a single instruction with out-of-loop users.

  // This instruction is allowed to have out-of-loop users.
  AllowedExit.insert(ExitInstruction);

  // Save the description of this reduction variable.
  ReductionDescriptor RD(RdxStart, ExitInstruction, Kind,
                         ReduxDesc.MinMaxKind);
  Reductions[Phi] = RD;
  // We've ended the cycle. This is a reduction variable if we have an
  // outside user and it has a binary op.

  return true;
}

/// Returns true if the instruction is a Select(ICmp(X, Y), X, Y) instruction
/// pattern corresponding to a min(X, Y) or max(X, Y).
LoopVectorizationLegality::ReductionInstDesc
LoopVectorizationLegality::isMinMaxSelectCmpPattern(Instruction *I,
                                                    ReductionInstDesc &Prev) {

  assert((isa<ICmpInst>(I) || isa<FCmpInst>(I) || isa<SelectInst>(I)) &&
         "Expect a select instruction");
  Instruction *Cmp = nullptr;
  SelectInst *Select = nullptr;

  // We must handle the select(cmp()) as a single instruction. Advance to the
  // select.
  if ((Cmp = dyn_cast<ICmpInst>(I)) || (Cmp = dyn_cast<FCmpInst>(I))) {
    if (!Cmp->hasOneUse() || !(Select = dyn_cast<SelectInst>(*I->user_begin())))
      return ReductionInstDesc(false, I);
    return ReductionInstDesc(Select, Prev.MinMaxKind);
  }

  // Only handle single use cases for now.
  if (!(Select = dyn_cast<SelectInst>(I)))
    return ReductionInstDesc(false, I);
  if (!(Cmp = dyn_cast<ICmpInst>(I->getOperand(0))) &&
      !(Cmp = dyn_cast<FCmpInst>(I->getOperand(0))))
    return ReductionInstDesc(false, I);
  if (!Cmp->hasOneUse())
    return ReductionInstDesc(false, I);

  Value *CmpLeft;
  Value *CmpRight;

  // Look for a min/max pattern.
  if (m_UMin(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_UIntMin);
  else if (m_UMax(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_UIntMax);
  else if (m_SMax(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_SIntMax);
  else if (m_SMin(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_SIntMin);
  else if (m_OrdFMin(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_FloatMin);
  else if (m_OrdFMax(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_FloatMax);
  else if (m_UnordFMin(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_FloatMin);
  else if (m_UnordFMax(m_Value(CmpLeft), m_Value(CmpRight)).match(Select))
    return ReductionInstDesc(Select, MRK_FloatMax);

  return ReductionInstDesc(false, I);
}

LoopVectorizationLegality::ReductionInstDesc
LoopVectorizationLegality::isReductionInstr(Instruction *I,
                                            ReductionKind Kind,
                                            ReductionInstDesc &Prev) {
  bool FP = I->getType()->isFloatingPointTy();
  bool FastMath = FP && I->hasUnsafeAlgebra();
  switch (I->getOpcode()) {
  default:
    return ReductionInstDesc(false, I);
  case Instruction::PHI:
      if (FP && (Kind != RK_FloatMult && Kind != RK_FloatAdd &&
                 Kind != RK_FloatMinMax))
        return ReductionInstDesc(false, I);
    return ReductionInstDesc(I, Prev.MinMaxKind);
  case Instruction::Sub:
  case Instruction::Add:
    return ReductionInstDesc(Kind == RK_IntegerAdd, I);
  case Instruction::Mul:
    return ReductionInstDesc(Kind == RK_IntegerMult, I);
  case Instruction::And:
    return ReductionInstDesc(Kind == RK_IntegerAnd, I);
  case Instruction::Or:
    return ReductionInstDesc(Kind == RK_IntegerOr, I);
  case Instruction::Xor:
    return ReductionInstDesc(Kind == RK_IntegerXor, I);
  case Instruction::FMul:
    return ReductionInstDesc(Kind == RK_FloatMult && FastMath, I);
  case Instruction::FSub:
  case Instruction::FAdd:
    return ReductionInstDesc(Kind == RK_FloatAdd && FastMath, I);
  case Instruction::FCmp:
  case Instruction::ICmp:
  case Instruction::Select:
    if (Kind != RK_IntegerMinMax &&
        (!HasFunNoNaNAttr || Kind != RK_FloatMinMax))
      return ReductionInstDesc(false, I);
    return isMinMaxSelectCmpPattern(I, Prev);
  }
}

LoopVectorizationLegality::InductionKind
LoopVectorizationLegality::isInductionVariable(PHINode *Phi,
                                               ConstantInt *&StepValue) {
  Type *PhiTy = Phi->getType();
  // We only handle integer and pointer inductions variables.
  if (!PhiTy->isIntegerTy() && !PhiTy->isPointerTy())
    return IK_NoInduction;

  // Check that the PHI is consecutive.
  const SCEV *PhiScev = SE->getSCEV(Phi);
  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(PhiScev);
  if (!AR) {
    DEBUG(dbgs() << "LV: PHI is not a poly recurrence.\n");
    return IK_NoInduction;
  }

  const SCEV *Step = AR->getStepRecurrence(*SE);
  // Calculate the pointer stride and check if it is consecutive.
  const SCEVConstant *C = dyn_cast<SCEVConstant>(Step);
  if (!C)
    return IK_NoInduction;

  ConstantInt *CV = C->getValue();
  if (PhiTy->isIntegerTy()) {
    StepValue = CV;
    return IK_IntInduction;
  }

  assert(PhiTy->isPointerTy() && "The PHI must be a pointer");
  Type *PointerElementType = PhiTy->getPointerElementType();
  // The pointer stride cannot be determined if the pointer element type is not
  // sized.
  if (!PointerElementType->isSized())
    return IK_NoInduction;

  int64_t Size = static_cast<int64_t>(DL->getTypeAllocSize(PointerElementType));
  int64_t CVSize = CV->getSExtValue();
  if (CVSize % Size)
    return IK_NoInduction;
  StepValue = ConstantInt::getSigned(CV->getType(), CVSize / Size);
  return IK_PtrInduction;
}

bool LoopVectorizationLegality::isInductionVariable(const Value *V) {
  Value *In0 = const_cast<Value*>(V);
  PHINode *PN = dyn_cast_or_null<PHINode>(In0);
  if (!PN)
    return false;

  return Inductions.count(PN);
}

bool LoopVectorizationLegality::blockNeedsPredication(BasicBlock *BB)  {
  return LAA.blockNeedsPredication(BB);
}

bool LoopVectorizationLegality::blockCanBePredicated(BasicBlock *BB,
                                           SmallPtrSetImpl<Value *> &SafePtrs) {
  
  for (BasicBlock::iterator it = BB->begin(), e = BB->end(); it != e; ++it) {
    // Check that we don't have a constant expression that can trap as operand.
    for (Instruction::op_iterator OI = it->op_begin(), OE = it->op_end();
         OI != OE; ++OI) {
      if (Constant *C = dyn_cast<Constant>(*OI))
        if (C->canTrap())
          return false;
    }
    // We might be able to hoist the load.
    if (it->mayReadFromMemory()) {
      LoadInst *LI = dyn_cast<LoadInst>(it);
      if (!LI)
        return false;
      if (!SafePtrs.count(LI->getPointerOperand())) {
        if (isLegalMaskedLoad(LI->getType(), LI->getPointerOperand())) {
          MaskedOp.insert(LI);
          continue;
        }
        return false;
      }
    }

    // We don't predicate stores at the moment.
    if (it->mayWriteToMemory()) {
      StoreInst *SI = dyn_cast<StoreInst>(it);
      // We only support predication of stores in basic blocks with one
      // predecessor.
      if (!SI)
        return false;

      bool isSafePtr = (SafePtrs.count(SI->getPointerOperand()) != 0);
      bool isSinglePredecessor = SI->getParent()->getSinglePredecessor();
      
      if (++NumPredStores > NumberOfStoresToPredicate || !isSafePtr ||
          !isSinglePredecessor) {
        // Build a masked store if it is legal for the target, otherwise scalarize
        // the block.
        bool isLegalMaskedOp =
          isLegalMaskedStore(SI->getValueOperand()->getType(),
                             SI->getPointerOperand());
        if (isLegalMaskedOp) {
          --NumPredStores;
          MaskedOp.insert(SI);
          continue;
        }
        return false;
      }
    }
    if (it->mayThrow())
      return false;

    // The instructions below can trap.
    switch (it->getOpcode()) {
    default: continue;
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
      return false;
    }
  }

  return true;
}

LoopVectorizationCostModel::VectorizationFactor
LoopVectorizationCostModel::selectVectorizationFactor(bool OptForSize) {
  // Width 1 means no vectorize
  VectorizationFactor Factor = { 1U, 0U };
  if (OptForSize && Legal->getRuntimePointerCheck()->Need) {
    emitAnalysis(VectorizationReport() <<
                 "runtime pointer checks needed. Enable vectorization of this "
                 "loop with '#pragma clang loop vectorize(enable)' when "
                 "compiling with -Os");
    DEBUG(dbgs() << "LV: Aborting. Runtime ptr check is required in Os.\n");
    return Factor;
  }

  if (!EnableCondStoresVectorization && Legal->getNumPredStores()) {
    emitAnalysis(VectorizationReport() <<
                 "store that is conditionally executed prevents vectorization");
    DEBUG(dbgs() << "LV: No vectorization. There are conditional stores.\n");
    return Factor;
  }

  // Find the trip count.
  unsigned TC = SE->getSmallConstantTripCount(TheLoop);
  DEBUG(dbgs() << "LV: Found trip count: " << TC << '\n');

  unsigned WidestType = getWidestType();
  unsigned WidestRegister = TTI.getRegisterBitWidth(true);
  unsigned MaxSafeDepDist = -1U;
  if (Legal->getMaxSafeDepDistBytes() != -1U)
    MaxSafeDepDist = Legal->getMaxSafeDepDistBytes() * 8;
  WidestRegister = ((WidestRegister < MaxSafeDepDist) ?
                    WidestRegister : MaxSafeDepDist);
  unsigned MaxVectorSize = WidestRegister / WidestType;
  DEBUG(dbgs() << "LV: The Widest type: " << WidestType << " bits.\n");
  DEBUG(dbgs() << "LV: The Widest register is: "
          << WidestRegister << " bits.\n");

  if (MaxVectorSize == 0) {
    DEBUG(dbgs() << "LV: The target has no vector registers.\n");
    MaxVectorSize = 1;
  }

  assert(MaxVectorSize <= 64 && "Did not expect to pack so many elements"
         " into one vector!");

  unsigned VF = MaxVectorSize;

  // If we optimize the program for size, avoid creating the tail loop.
  if (OptForSize) {
    // If we are unable to calculate the trip count then don't try to vectorize.
    if (TC < 2) {
      emitAnalysis
        (VectorizationReport() <<
         "unable to calculate the loop count due to complex control flow");
      DEBUG(dbgs() << "LV: Aborting. A tail loop is required in Os.\n");
      return Factor;
    }

    // Find the maximum SIMD width that can fit within the trip count.
    VF = TC % MaxVectorSize;

    if (VF == 0)
      VF = MaxVectorSize;

    // If the trip count that we found modulo the vectorization factor is not
    // zero then we require a tail.
    if (VF < 2) {
      emitAnalysis(VectorizationReport() <<
                   "cannot optimize for size and vectorize at the "
                   "same time. Enable vectorization of this loop "
                   "with '#pragma clang loop vectorize(enable)' "
                   "when compiling with -Os");
      DEBUG(dbgs() << "LV: Aborting. A tail loop is required in Os.\n");
      return Factor;
    }
  }

  int UserVF = Hints->getWidth();
  if (UserVF != 0) {
    assert(isPowerOf2_32(UserVF) && "VF needs to be a power of two");
    DEBUG(dbgs() << "LV: Using user VF " << UserVF << ".\n");

    Factor.Width = UserVF;
    return Factor;
  }

  float Cost = expectedCost(1);
#ifndef NDEBUG
  const float ScalarCost = Cost;
#endif /* NDEBUG */
  unsigned Width = 1;
  DEBUG(dbgs() << "LV: Scalar loop costs: " << (int)ScalarCost << ".\n");

  bool ForceVectorization = Hints->getForce() == LoopVectorizeHints::FK_Enabled;
  // Ignore scalar width, because the user explicitly wants vectorization.
  if (ForceVectorization && VF > 1) {
    Width = 2;
    Cost = expectedCost(Width) / (float)Width;
  }

  for (unsigned i=2; i <= VF; i*=2) {
    // Notice that the vector loop needs to be executed less times, so
    // we need to divide the cost of the vector loops by the width of
    // the vector elements.
    float VectorCost = expectedCost(i) / (float)i;
    DEBUG(dbgs() << "LV: Vector loop of width " << i << " costs: " <<
          (int)VectorCost << ".\n");
    if (VectorCost < Cost) {
      Cost = VectorCost;
      Width = i;
    }
  }

  DEBUG(if (ForceVectorization && Width > 1 && Cost >= ScalarCost) dbgs()
        << "LV: Vectorization seems to be not beneficial, "
        << "but was forced by a user.\n");
  DEBUG(dbgs() << "LV: Selecting VF: "<< Width << ".\n");
  Factor.Width = Width;
  Factor.Cost = Width * Cost;
  return Factor;
}

unsigned LoopVectorizationCostModel::getWidestType() {
  unsigned MaxWidth = 8;

  // For each block.
  for (Loop::block_iterator bb = TheLoop->block_begin(),
       be = TheLoop->block_end(); bb != be; ++bb) {
    BasicBlock *BB = *bb;

    // For each instruction in the loop.
    for (BasicBlock::iterator it = BB->begin(), e = BB->end(); it != e; ++it) {
      Type *T = it->getType();

      // Ignore ephemeral values.
      if (EphValues.count(it))
        continue;

      // Only examine Loads, Stores and PHINodes.
      if (!isa<LoadInst>(it) && !isa<StoreInst>(it) && !isa<PHINode>(it))
        continue;

      // Examine PHI nodes that are reduction variables.
      if (PHINode *PN = dyn_cast<PHINode>(it))
        if (!Legal->getReductionVars()->count(PN))
          continue;

      // Examine the stored values.
      if (StoreInst *ST = dyn_cast<StoreInst>(it))
        T = ST->getValueOperand()->getType();

      // Ignore loaded pointer types and stored pointer types that are not
      // consecutive. However, we do want to take consecutive stores/loads of
      // pointer vectors into account.
      if (T->isPointerTy() && !isConsecutiveLoadOrStore(it))
        continue;

      MaxWidth = std::max(MaxWidth,
                          (unsigned)DL->getTypeSizeInBits(T->getScalarType()));
    }
  }

  return MaxWidth;
}

unsigned
LoopVectorizationCostModel::selectUnrollFactor(bool OptForSize,
                                               unsigned VF,
                                               unsigned LoopCost) {

  // -- The unroll heuristics --
  // We unroll the loop in order to expose ILP and reduce the loop overhead.
  // There are many micro-architectural considerations that we can't predict
  // at this level. For example, frontend pressure (on decode or fetch) due to
  // code size, or the number and capabilities of the execution ports.
  //
  // We use the following heuristics to select the unroll factor:
  // 1. If the code has reductions, then we unroll in order to break the cross
  // iteration dependency.
  // 2. If the loop is really small, then we unroll in order to reduce the loop
  // overhead.
  // 3. We don't unroll if we think that we will spill registers to memory due
  // to the increased register pressure.

  // Use the user preference, unless 'auto' is selected.
  int UserUF = Hints->getInterleave();
  if (UserUF != 0)
    return UserUF;

  // When we optimize for size, we don't unroll.
  if (OptForSize)
    return 1;

  // We used the distance for the unroll factor.
  if (Legal->getMaxSafeDepDistBytes() != -1U)
    return 1;

  // Do not unroll loops with a relatively small trip count.
  unsigned TC = SE->getSmallConstantTripCount(TheLoop);
  if (TC > 1 && TC < TinyTripCountUnrollThreshold)
    return 1;

  unsigned TargetNumRegisters = TTI.getNumberOfRegisters(VF > 1);
  DEBUG(dbgs() << "LV: The target has " << TargetNumRegisters <<
        " registers\n");

  if (VF == 1) {
    if (ForceTargetNumScalarRegs.getNumOccurrences() > 0)
      TargetNumRegisters = ForceTargetNumScalarRegs;
  } else {
    if (ForceTargetNumVectorRegs.getNumOccurrences() > 0)
      TargetNumRegisters = ForceTargetNumVectorRegs;
  }

  LoopVectorizationCostModel::RegisterUsage R = calculateRegisterUsage();
  // We divide by these constants so assume that we have at least one
  // instruction that uses at least one register.
  R.MaxLocalUsers = std::max(R.MaxLocalUsers, 1U);
  R.NumInstructions = std::max(R.NumInstructions, 1U);

  // We calculate the unroll factor using the following formula.
  // Subtract the number of loop invariants from the number of available
  // registers. These registers are used by all of the unrolled instances.
  // Next, divide the remaining registers by the number of registers that is
  // required by the loop, in order to estimate how many parallel instances
  // fit without causing spills. All of this is rounded down if necessary to be
  // a power of two. We want power of two unroll factors to simplify any
  // addressing operations or alignment considerations.
  unsigned UF = PowerOf2Floor((TargetNumRegisters - R.LoopInvariantRegs) /
                              R.MaxLocalUsers);

  // Don't count the induction variable as unrolled.
  if (EnableIndVarRegisterHeur)
    UF = PowerOf2Floor((TargetNumRegisters - R.LoopInvariantRegs - 1) /
                       std::max(1U, (R.MaxLocalUsers - 1)));

  // Clamp the unroll factor ranges to reasonable factors.
  unsigned MaxInterleaveSize = TTI.getMaxInterleaveFactor();

  // Check if the user has overridden the unroll max.
  if (VF == 1) {
    if (ForceTargetMaxScalarInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveSize = ForceTargetMaxScalarInterleaveFactor;
  } else {
    if (ForceTargetMaxVectorInterleaveFactor.getNumOccurrences() > 0)
      MaxInterleaveSize = ForceTargetMaxVectorInterleaveFactor;
  }

  // If we did not calculate the cost for VF (because the user selected the VF)
  // then we calculate the cost of VF here.
  if (LoopCost == 0)
    LoopCost = expectedCost(VF);

  // Clamp the calculated UF to be between the 1 and the max unroll factor
  // that the target allows.
  if (UF > MaxInterleaveSize)
    UF = MaxInterleaveSize;
  else if (UF < 1)
    UF = 1;

  // Unroll if we vectorized this loop and there is a reduction that could
  // benefit from unrolling.
  if (VF > 1 && Legal->getReductionVars()->size()) {
    DEBUG(dbgs() << "LV: Unrolling because of reductions.\n");
    return UF;
  }

  // Note that if we've already vectorized the loop we will have done the
  // runtime check and so unrolling won't require further checks.
  bool UnrollingRequiresRuntimePointerCheck =
      (VF == 1 && Legal->getRuntimePointerCheck()->Need);

  // We want to unroll small loops in order to reduce the loop overhead and
  // potentially expose ILP opportunities.
  DEBUG(dbgs() << "LV: Loop cost is " << LoopCost << '\n');
  if (!UnrollingRequiresRuntimePointerCheck &&
      LoopCost < SmallLoopCost) {
    // We assume that the cost overhead is 1 and we use the cost model
    // to estimate the cost of the loop and unroll until the cost of the
    // loop overhead is about 5% of the cost of the loop.
    unsigned SmallUF = std::min(UF, (unsigned)PowerOf2Floor(SmallLoopCost / LoopCost));

    // Unroll until store/load ports (estimated by max unroll factor) are
    // saturated.
    unsigned NumStores = Legal->getNumStores();
    unsigned NumLoads = Legal->getNumLoads();
    unsigned StoresUF = UF / (NumStores ? NumStores : 1);
    unsigned LoadsUF = UF /  (NumLoads ? NumLoads : 1);

    // If we have a scalar reduction (vector reductions are already dealt with
    // by this point), we can increase the critical path length if the loop
    // we're unrolling is inside another loop. Limit, by default to 2, so the
    // critical path only gets increased by one reduction operation.
    if (Legal->getReductionVars()->size() &&
        TheLoop->getLoopDepth() > 1) {
      unsigned F = static_cast<unsigned>(MaxNestedScalarReductionUF);
      SmallUF = std::min(SmallUF, F);
      StoresUF = std::min(StoresUF, F);
      LoadsUF = std::min(LoadsUF, F);
    }

    if (EnableLoadStoreRuntimeUnroll && std::max(StoresUF, LoadsUF) > SmallUF) {
      DEBUG(dbgs() << "LV: Unrolling to saturate store or load ports.\n");
      return std::max(StoresUF, LoadsUF);
    }

    DEBUG(dbgs() << "LV: Unrolling to reduce branch cost.\n");
    return SmallUF;
  }

  DEBUG(dbgs() << "LV: Not Unrolling.\n");
  return 1;
}

LoopVectorizationCostModel::RegisterUsage
LoopVectorizationCostModel::calculateRegisterUsage() {
  // This function calculates the register usage by measuring the highest number
  // of values that are alive at a single location. Obviously, this is a very
  // rough estimation. We scan the loop in a topological order in order and
  // assign a number to each instruction. We use RPO to ensure that defs are
  // met before their users. We assume that each instruction that has in-loop
  // users starts an interval. We record every time that an in-loop value is
  // used, so we have a list of the first and last occurrences of each
  // instruction. Next, we transpose this data structure into a multi map that
  // holds the list of intervals that *end* at a specific location. This multi
  // map allows us to perform a linear search. We scan the instructions linearly
  // and record each time that a new interval starts, by placing it in a set.
  // If we find this value in the multi-map then we remove it from the set.
  // The max register usage is the maximum size of the set.
  // We also search for instructions that are defined outside the loop, but are
  // used inside the loop. We need this number separately from the max-interval
  // usage number because when we unroll, loop-invariant values do not take
  // more register.
  LoopBlocksDFS DFS(TheLoop);
  DFS.perform(LI);

  RegisterUsage R;
  R.NumInstructions = 0;

  // Each 'key' in the map opens a new interval. The values
  // of the map are the index of the 'last seen' usage of the
  // instruction that is the key.
  typedef DenseMap<Instruction*, unsigned> IntervalMap;
  // Maps instruction to its index.
  DenseMap<unsigned, Instruction*> IdxToInstr;
  // Marks the end of each interval.
  IntervalMap EndPoint;
  // Saves the list of instruction indices that are used in the loop.
  SmallSet<Instruction*, 8> Ends;
  // Saves the list of values that are used in the loop but are
  // defined outside the loop, such as arguments and constants.
  SmallPtrSet<Value*, 8> LoopInvariants;

  unsigned Index = 0;
  for (LoopBlocksDFS::RPOIterator bb = DFS.beginRPO(),
       be = DFS.endRPO(); bb != be; ++bb) {
    R.NumInstructions += (*bb)->size();
    for (BasicBlock::iterator it = (*bb)->begin(), e = (*bb)->end(); it != e;
         ++it) {
      Instruction *I = it;
      IdxToInstr[Index++] = I;

      // Save the end location of each USE.
      for (unsigned i = 0; i < I->getNumOperands(); ++i) {
        Value *U = I->getOperand(i);
        Instruction *Instr = dyn_cast<Instruction>(U);

        // Ignore non-instruction values such as arguments, constants, etc.
        if (!Instr) continue;

        // If this instruction is outside the loop then record it and continue.
        if (!TheLoop->contains(Instr)) {
          LoopInvariants.insert(Instr);
          continue;
        }

        // Overwrite previous end points.
        EndPoint[Instr] = Index;
        Ends.insert(Instr);
      }
    }
  }

  // Saves the list of intervals that end with the index in 'key'.
  typedef SmallVector<Instruction*, 2> InstrList;
  DenseMap<unsigned, InstrList> TransposeEnds;

  // Transpose the EndPoints to a list of values that end at each index.
  for (IntervalMap::iterator it = EndPoint.begin(), e = EndPoint.end();
       it != e; ++it)
    TransposeEnds[it->second].push_back(it->first);

  SmallSet<Instruction*, 8> OpenIntervals;
  unsigned MaxUsage = 0;


  DEBUG(dbgs() << "LV(REG): Calculating max register usage:\n");
  for (unsigned int i = 0; i < Index; ++i) {
    Instruction *I = IdxToInstr[i];
    // Ignore instructions that are never used within the loop.
    if (!Ends.count(I)) continue;

    // Ignore ephemeral values.
    if (EphValues.count(I))
      continue;

    // Remove all of the instructions that end at this location.
    InstrList &List = TransposeEnds[i];
    for (unsigned int j=0, e = List.size(); j < e; ++j)
      OpenIntervals.erase(List[j]);

    // Count the number of live interals.
    MaxUsage = std::max(MaxUsage, OpenIntervals.size());

    DEBUG(dbgs() << "LV(REG): At #" << i << " Interval # " <<
          OpenIntervals.size() << '\n');

    // Add the current instruction to the list of open intervals.
    OpenIntervals.insert(I);
  }

  unsigned Invariant = LoopInvariants.size();
  DEBUG(dbgs() << "LV(REG): Found max usage: " << MaxUsage << '\n');
  DEBUG(dbgs() << "LV(REG): Found invariant usage: " << Invariant << '\n');
  DEBUG(dbgs() << "LV(REG): LoopSize: " << R.NumInstructions << '\n');

  R.LoopInvariantRegs = Invariant;
  R.MaxLocalUsers = MaxUsage;
  return R;
}

unsigned LoopVectorizationCostModel::expectedCost(unsigned VF) {
  unsigned Cost = 0;

  // For each block.
  for (Loop::block_iterator bb = TheLoop->block_begin(),
       be = TheLoop->block_end(); bb != be; ++bb) {
    unsigned BlockCost = 0;
    BasicBlock *BB = *bb;

    // For each instruction in the old loop.
    for (BasicBlock::iterator it = BB->begin(), e = BB->end(); it != e; ++it) {
      // Skip dbg intrinsics.
      if (isa<DbgInfoIntrinsic>(it))
        continue;

      // Ignore ephemeral values.
      if (EphValues.count(it))
        continue;

      unsigned C = getInstructionCost(it, VF);

      // Check if we should override the cost.
      if (ForceTargetInstructionCost.getNumOccurrences() > 0)
        C = ForceTargetInstructionCost;

      BlockCost += C;
      DEBUG(dbgs() << "LV: Found an estimated cost of " << C << " for VF " <<
            VF << " For instruction: " << *it << '\n');
    }

    // We assume that if-converted blocks have a 50% chance of being executed.
    // When the code is scalar then some of the blocks are avoided due to CF.
    // When the code is vectorized we execute all code paths.
    if (VF == 1 && Legal->blockNeedsPredication(*bb))
      BlockCost /= 2;

    Cost += BlockCost;
  }

  return Cost;
}

/// \brief Check whether the address computation for a non-consecutive memory
/// access looks like an unlikely candidate for being merged into the indexing
/// mode.
///
/// We look for a GEP which has one index that is an induction variable and all
/// other indices are loop invariant. If the stride of this access is also
/// within a small bound we decide that this address computation can likely be
/// merged into the addressing mode.
/// In all other cases, we identify the address computation as complex.
static bool isLikelyComplexAddressComputation(Value *Ptr,
                                              LoopVectorizationLegality *Legal,
                                              ScalarEvolution *SE,
                                              const Loop *TheLoop) {
  GetElementPtrInst *Gep = dyn_cast<GetElementPtrInst>(Ptr);
  if (!Gep)
    return true;

  // We are looking for a gep with all loop invariant indices except for one
  // which should be an induction variable.
  unsigned NumOperands = Gep->getNumOperands();
  for (unsigned i = 1; i < NumOperands; ++i) {
    Value *Opd = Gep->getOperand(i);
    if (!SE->isLoopInvariant(SE->getSCEV(Opd), TheLoop) &&
        !Legal->isInductionVariable(Opd))
      return true;
  }

  // Now we know we have a GEP ptr, %inv, %ind, %inv. Make sure that the step
  // can likely be merged into the address computation.
  unsigned MaxMergeDistance = 64;

  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Ptr));
  if (!AddRec)
    return true;

  // Check the step is constant.
  const SCEV *Step = AddRec->getStepRecurrence(*SE);
  // Calculate the pointer stride and check if it is consecutive.
  const SCEVConstant *C = dyn_cast<SCEVConstant>(Step);
  if (!C)
    return true;

  const APInt &APStepVal = C->getValue()->getValue();

  // Huge step value - give up.
  if (APStepVal.getBitWidth() > 64)
    return true;

  int64_t StepVal = APStepVal.getSExtValue();

  return StepVal > MaxMergeDistance;
}

static bool isStrideMul(Instruction *I, LoopVectorizationLegality *Legal) {
  if (Legal->hasStride(I->getOperand(0)) || Legal->hasStride(I->getOperand(1)))
    return true;
  return false;
}

unsigned
LoopVectorizationCostModel::getInstructionCost(Instruction *I, unsigned VF) {
  // If we know that this instruction will remain uniform, check the cost of
  // the scalar version.
  if (Legal->isUniformAfterVectorization(I))
    VF = 1;

  Type *RetTy = I->getType();
  Type *VectorTy = ToVectorTy(RetTy, VF);

  // TODO: We need to estimate the cost of intrinsic calls.
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
    // We mark this instruction as zero-cost because the cost of GEPs in
    // vectorized code depends on whether the corresponding memory instruction
    // is scalarized or not. Therefore, we handle GEPs with the memory
    // instruction cost.
    return 0;
  case Instruction::Br: {
    return TTI.getCFInstrCost(I->getOpcode());
  }
  case Instruction::PHI:
    //TODO: IF-converted IFs become selects.
    return 0;
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    // Since we will replace the stride by 1 the multiplication should go away.
    if (I->getOpcode() == Instruction::Mul && isStrideMul(I, Legal))
      return 0;
    // Certain instructions can be cheaper to vectorize if they have a constant
    // second vector operand. One example of this are shifts on x86.
    TargetTransformInfo::OperandValueKind Op1VK =
      TargetTransformInfo::OK_AnyValue;
    TargetTransformInfo::OperandValueKind Op2VK =
      TargetTransformInfo::OK_AnyValue;
    TargetTransformInfo::OperandValueProperties Op1VP =
        TargetTransformInfo::OP_None;
    TargetTransformInfo::OperandValueProperties Op2VP =
        TargetTransformInfo::OP_None;
    Value *Op2 = I->getOperand(1);

    // Check for a splat of a constant or for a non uniform vector of constants.
    if (isa<ConstantInt>(Op2)) {
      ConstantInt *CInt = cast<ConstantInt>(Op2);
      if (CInt && CInt->getValue().isPowerOf2())
        Op2VP = TargetTransformInfo::OP_PowerOf2;
      Op2VK = TargetTransformInfo::OK_UniformConstantValue;
    } else if (isa<ConstantVector>(Op2) || isa<ConstantDataVector>(Op2)) {
      Op2VK = TargetTransformInfo::OK_NonUniformConstantValue;
      Constant *SplatValue = cast<Constant>(Op2)->getSplatValue();
      if (SplatValue) {
        ConstantInt *CInt = dyn_cast<ConstantInt>(SplatValue);
        if (CInt && CInt->getValue().isPowerOf2())
          Op2VP = TargetTransformInfo::OP_PowerOf2;
        Op2VK = TargetTransformInfo::OK_UniformConstantValue;
      }
    }

    return TTI.getArithmeticInstrCost(I->getOpcode(), VectorTy, Op1VK, Op2VK,
                                      Op1VP, Op2VP);
  }
  case Instruction::Select: {
    SelectInst *SI = cast<SelectInst>(I);
    const SCEV *CondSCEV = SE->getSCEV(SI->getCondition());
    bool ScalarCond = (SE->isLoopInvariant(CondSCEV, TheLoop));
    Type *CondTy = SI->getCondition()->getType();
    if (!ScalarCond)
      CondTy = VectorType::get(CondTy, VF);

    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy, CondTy);
  }
  case Instruction::ICmp:
  case Instruction::FCmp: {
    Type *ValTy = I->getOperand(0)->getType();
    VectorTy = ToVectorTy(ValTy, VF);
    return TTI.getCmpSelInstrCost(I->getOpcode(), VectorTy);
  }
  case Instruction::Store:
  case Instruction::Load: {
    StoreInst *SI = dyn_cast<StoreInst>(I);
    LoadInst *LI = dyn_cast<LoadInst>(I);
    Type *ValTy = (SI ? SI->getValueOperand()->getType() :
                   LI->getType());
    VectorTy = ToVectorTy(ValTy, VF);

    unsigned Alignment = SI ? SI->getAlignment() : LI->getAlignment();
    unsigned AS = SI ? SI->getPointerAddressSpace() :
      LI->getPointerAddressSpace();
    Value *Ptr = SI ? SI->getPointerOperand() : LI->getPointerOperand();
    // We add the cost of address computation here instead of with the gep
    // instruction because only here we know whether the operation is
    // scalarized.
    if (VF == 1)
      return TTI.getAddressComputationCost(VectorTy) +
        TTI.getMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS);

    // Scalarized loads/stores.
    int ConsecutiveStride = Legal->isConsecutivePtr(Ptr);
    bool Reverse = ConsecutiveStride < 0;
    unsigned ScalarAllocatedSize = DL->getTypeAllocSize(ValTy);
    unsigned VectorElementSize = DL->getTypeStoreSize(VectorTy)/VF;
    if (!ConsecutiveStride || ScalarAllocatedSize != VectorElementSize) {
      bool IsComplexComputation =
        isLikelyComplexAddressComputation(Ptr, Legal, SE, TheLoop);
      unsigned Cost = 0;
      // The cost of extracting from the value vector and pointer vector.
      Type *PtrTy = ToVectorTy(Ptr->getType(), VF);
      for (unsigned i = 0; i < VF; ++i) {
        //  The cost of extracting the pointer operand.
        Cost += TTI.getVectorInstrCost(Instruction::ExtractElement, PtrTy, i);
        // In case of STORE, the cost of ExtractElement from the vector.
        // In case of LOAD, the cost of InsertElement into the returned
        // vector.
        Cost += TTI.getVectorInstrCost(SI ? Instruction::ExtractElement :
                                            Instruction::InsertElement,
                                            VectorTy, i);
      }

      // The cost of the scalar loads/stores.
      Cost += VF * TTI.getAddressComputationCost(PtrTy, IsComplexComputation);
      Cost += VF * TTI.getMemoryOpCost(I->getOpcode(), ValTy->getScalarType(),
                                       Alignment, AS);
      return Cost;
    }

    // Wide load/stores.
    unsigned Cost = TTI.getAddressComputationCost(VectorTy);
    Cost += TTI.getMemoryOpCost(I->getOpcode(), VectorTy, Alignment, AS);

    if (Reverse)
      Cost += TTI.getShuffleCost(TargetTransformInfo::SK_Reverse,
                                  VectorTy, 0);
    return Cost;
  }
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast: {
    // We optimize the truncation of induction variable.
    // The cost of these is the same as the scalar operation.
    if (I->getOpcode() == Instruction::Trunc &&
        Legal->isInductionVariable(I->getOperand(0)))
      return TTI.getCastInstrCost(I->getOpcode(), I->getType(),
                                  I->getOperand(0)->getType());

    Type *SrcVecTy = ToVectorTy(I->getOperand(0)->getType(), VF);
    return TTI.getCastInstrCost(I->getOpcode(), VectorTy, SrcVecTy);
  }
  case Instruction::Call: {
    CallInst *CI = cast<CallInst>(I);
    Intrinsic::ID ID = getIntrinsicIDForCall(CI, TLI);
    assert(ID && "Not an intrinsic call!");
    Type *RetTy = ToVectorTy(CI->getType(), VF);
    SmallVector<Type*, 4> Tys;
    for (unsigned i = 0, ie = CI->getNumArgOperands(); i != ie; ++i)
      Tys.push_back(ToVectorTy(CI->getArgOperand(i)->getType(), VF));
    return TTI.getIntrinsicInstrCost(ID, RetTy, Tys);
  }
  default: {
    // We are scalarizing the instruction. Return the cost of the scalar
    // instruction, plus the cost of insert and extract into vector
    // elements, times the vector width.
    unsigned Cost = 0;

    if (!RetTy->isVoidTy() && VF != 1) {
      unsigned InsCost = TTI.getVectorInstrCost(Instruction::InsertElement,
                                                VectorTy);
      unsigned ExtCost = TTI.getVectorInstrCost(Instruction::ExtractElement,
                                                VectorTy);

      // The cost of inserting the results plus extracting each one of the
      // operands.
      Cost += VF * (InsCost + ExtCost * I->getNumOperands());
    }

    // The cost of executing VF copies of the scalar instruction. This opcode
    // is unknown. Assume that it is the same as 'mul'.
    Cost += VF * TTI.getArithmeticInstrCost(Instruction::Mul, VectorTy);
    return Cost;
  }
  }// end of switch.
}

Type* LoopVectorizationCostModel::ToVectorTy(Type *Scalar, unsigned VF) {
  if (Scalar->isVoidTy() || VF == 1)
    return Scalar;
  return VectorType::get(Scalar, VF);
}

char LoopVectorize::ID = 0;
static const char lv_name[] = "Loop Vectorization";
INITIALIZE_PASS_BEGIN(LoopVectorize, LV_NAME, lv_name, false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution)
INITIALIZE_PASS_DEPENDENCY(LCSSA)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(LoopVectorize, LV_NAME, lv_name, false, false)

namespace llvm {
  Pass *createLoopVectorizePass(bool NoUnrolling, bool AlwaysVectorize) {
    return new LoopVectorize(NoUnrolling, AlwaysVectorize);
  }
}

bool LoopVectorizationCostModel::isConsecutiveLoadOrStore(Instruction *Inst) {
  // Check for a store.
  if (StoreInst *ST = dyn_cast<StoreInst>(Inst))
    return Legal->isConsecutivePtr(ST->getPointerOperand()) != 0;

  // Check for a load.
  if (LoadInst *LI = dyn_cast<LoadInst>(Inst))
    return Legal->isConsecutivePtr(LI->getPointerOperand()) != 0;

  return false;
}


void InnerLoopUnroller::scalarizeInstruction(Instruction *Instr,
                                             bool IfPredicateStore) {
  assert(!Instr->getType()->isAggregateType() && "Can't handle vectors");
  // Holds vector parameters or scalars, in case of uniform vals.
  SmallVector<VectorParts, 4> Params;

  setDebugLocFromInst(Builder, Instr);

  // Find all of the vectorized parameters.
  for (unsigned op = 0, e = Instr->getNumOperands(); op != e; ++op) {
    Value *SrcOp = Instr->getOperand(op);

    // If we are accessing the old induction variable, use the new one.
    if (SrcOp == OldInduction) {
      Params.push_back(getVectorValue(SrcOp));
      continue;
    }

    // Try using previously calculated values.
    Instruction *SrcInst = dyn_cast<Instruction>(SrcOp);

    // If the src is an instruction that appeared earlier in the basic block
    // then it should already be vectorized.
    if (SrcInst && OrigLoop->contains(SrcInst)) {
      assert(WidenMap.has(SrcInst) && "Source operand is unavailable");
      // The parameter is a vector value from earlier.
      Params.push_back(WidenMap.get(SrcInst));
    } else {
      // The parameter is a scalar from outside the loop. Maybe even a constant.
      VectorParts Scalars;
      Scalars.append(UF, SrcOp);
      Params.push_back(Scalars);
    }
  }

  assert(Params.size() == Instr->getNumOperands() &&
         "Invalid number of operands");

  // Does this instruction return a value ?
  bool IsVoidRetTy = Instr->getType()->isVoidTy();

  Value *UndefVec = IsVoidRetTy ? nullptr :
  UndefValue::get(Instr->getType());
  // Create a new entry in the WidenMap and initialize it to Undef or Null.
  VectorParts &VecResults = WidenMap.splat(Instr, UndefVec);

  Instruction *InsertPt = Builder.GetInsertPoint();
  BasicBlock *IfBlock = Builder.GetInsertBlock();
  BasicBlock *CondBlock = nullptr;

  VectorParts Cond;
  Loop *VectorLp = nullptr;
  if (IfPredicateStore) {
    assert(Instr->getParent()->getSinglePredecessor() &&
           "Only support single predecessor blocks");
    Cond = createEdgeMask(Instr->getParent()->getSinglePredecessor(),
                          Instr->getParent());
    VectorLp = LI->getLoopFor(IfBlock);
    assert(VectorLp && "Must have a loop for this block");
  }

  // For each vector unroll 'part':
  for (unsigned Part = 0; Part < UF; ++Part) {
    // For each scalar that we create:

    // Start an "if (pred) a[i] = ..." block.
    Value *Cmp = nullptr;
    if (IfPredicateStore) {
      if (Cond[Part]->getType()->isVectorTy())
        Cond[Part] =
            Builder.CreateExtractElement(Cond[Part], Builder.getInt32(0));
      Cmp = Builder.CreateICmp(ICmpInst::ICMP_EQ, Cond[Part],
                               ConstantInt::get(Cond[Part]->getType(), 1));
      CondBlock = IfBlock->splitBasicBlock(InsertPt, "cond.store");
      LoopVectorBody.push_back(CondBlock);
      VectorLp->addBasicBlockToLoop(CondBlock, *LI);
      // Update Builder with newly created basic block.
      Builder.SetInsertPoint(InsertPt);
    }

    Instruction *Cloned = Instr->clone();
      if (!IsVoidRetTy)
        Cloned->setName(Instr->getName() + ".cloned");
      // Replace the operands of the cloned instructions with extracted scalars.
      for (unsigned op = 0, e = Instr->getNumOperands(); op != e; ++op) {
        Value *Op = Params[op][Part];
        Cloned->setOperand(op, Op);
      }

      // Place the cloned scalar in the new loop.
      Builder.Insert(Cloned);

      // If the original scalar returns a value we need to place it in a vector
      // so that future users will be able to use it.
      if (!IsVoidRetTy)
        VecResults[Part] = Cloned;

    // End if-block.
      if (IfPredicateStore) {
        BasicBlock *NewIfBlock = CondBlock->splitBasicBlock(InsertPt, "else");
        LoopVectorBody.push_back(NewIfBlock);
        VectorLp->addBasicBlockToLoop(NewIfBlock, *LI);
        Builder.SetInsertPoint(InsertPt);
        Instruction *OldBr = IfBlock->getTerminator();
        BranchInst::Create(CondBlock, NewIfBlock, Cmp, OldBr);
        OldBr->eraseFromParent();
        IfBlock = NewIfBlock;
      }
  }
}

void InnerLoopUnroller::vectorizeMemoryInstruction(Instruction *Instr) {
  StoreInst *SI = dyn_cast<StoreInst>(Instr);
  bool IfPredicateStore = (SI && Legal->blockNeedsPredication(SI->getParent()));

  return scalarizeInstruction(Instr, IfPredicateStore);
}

Value *InnerLoopUnroller::reverseVector(Value *Vec) {
  return Vec;
}

Value *InnerLoopUnroller::getBroadcastInstrs(Value *V) {
  return V;
}

Value *InnerLoopUnroller::getStepVector(Value *Val, int StartIdx, Value *Step) {
  // When unrolling and the VF is 1, we only need to add a simple scalar.
  Type *ITy = Val->getType();
  assert(!ITy->isVectorTy() && "Val must be a scalar");
  Constant *C = ConstantInt::get(ITy, StartIdx);
  return Builder.CreateAdd(Val, Builder.CreateMul(C, Step), "induction");
}
