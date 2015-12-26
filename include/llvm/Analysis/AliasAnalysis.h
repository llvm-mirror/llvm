//===- llvm/Analysis/AliasAnalysis.h - Alias Analysis Interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic AliasAnalysis interface, which is used as the
// common interface used by all clients of alias analysis information, and
// implemented by all alias analysis implementations.  Mod/Ref information is
// also captured by this interface.
//
// Implementations of this interface must implement the various virtual methods,
// which automatically provides functionality for the entire suite of client
// APIs.
//
// This API identifies memory regions with the MemoryLocation class. The pointer
// component specifies the base memory address of the region. The Size specifies
// the maximum size (in address units) of the memory region, or
// MemoryLocation::UnknownSize if the size is not known. The TBAA tag
// identifies the "type" of the memory reference; see the
// TypeBasedAliasAnalysis class for details.
//
// Some non-obvious details include:
//  - Pointers that point to two completely different objects in memory never
//    alias, regardless of the value of the Size component.
//  - NoAlias doesn't imply inequal pointers. The most obvious example of this
//    is two pointers to constant memory. Even if they are equal, constant
//    memory is never stored to, so there will never be any dependencies.
//    In this and other situations, the pointers may be both NoAlias and
//    MustAlias at the same time. The current API can only return one result,
//    though this is rarely a problem in practice.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASANALYSIS_H
#define LLVM_ANALYSIS_ALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/MemoryLocation.h"

namespace llvm {
class BasicAAResult;
class LoadInst;
class StoreInst;
class VAArgInst;
class DataLayout;
class TargetLibraryInfo;
class Pass;
class AnalysisUsage;
class MemTransferInst;
class MemIntrinsic;
class DominatorTree;
class OrderedBasicBlock;

/// The possible results of an alias query.
///
/// These results are always computed between two MemoryLocation objects as
/// a query to some alias analysis.
///
/// Note that these are unscoped enumerations because we would like to support
/// implicitly testing a result for the existence of any possible aliasing with
/// a conversion to bool, but an "enum class" doesn't support this. The
/// canonical names from the literature are suffixed and unique anyways, and so
/// they serve as global constants in LLVM for these results.
///
/// See docs/AliasAnalysis.html for more information on the specific meanings
/// of these values.
enum AliasResult {
  /// The two locations do not alias at all.
  ///
  /// This value is arranged to convert to false, while all other values
  /// convert to true. This allows a boolean context to convert the result to
  /// a binary flag indicating whether there is the possibility of aliasing.
  NoAlias = 0,
  /// The two locations may or may not alias. This is the least precise result.
  MayAlias,
  /// The two locations alias, but only due to a partial overlap.
  PartialAlias,
  /// The two locations precisely alias each other.
  MustAlias,
};

/// Flags indicating whether a memory access modifies or references memory.
///
/// This is no access at all, a modification, a reference, or both
/// a modification and a reference. These are specifically structured such that
/// they form a two bit matrix and bit-tests for 'mod' or 'ref' work with any
/// of the possible values.
enum ModRefInfo {
  /// The access neither references nor modifies the value stored in memory.
  MRI_NoModRef = 0,
  /// The access references the value stored in memory.
  MRI_Ref = 1,
  /// The access modifies the value stored in memory.
  MRI_Mod = 2,
  /// The access both references and modifies the value stored in memory.
  MRI_ModRef = MRI_Ref | MRI_Mod
};

/// The locations at which a function might access memory.
///
/// These are primarily used in conjunction with the \c AccessKind bits to
/// describe both the nature of access and the locations of access for a
/// function call.
enum FunctionModRefLocation {
  /// Base case is no access to memory.
  FMRL_Nowhere = 0,
  /// Access to memory via argument pointers.
  FMRL_ArgumentPointees = 4,
  /// Access to any memory.
  FMRL_Anywhere = 8 | FMRL_ArgumentPointees
};

/// Summary of how a function affects memory in the program.
///
/// Loads from constant globals are not considered memory accesses for this
/// interface. Also, functions may freely modify stack space local to their
/// invocation without having to report it through these interfaces.
enum FunctionModRefBehavior {
  /// This function does not perform any non-local loads or stores to memory.
  ///
  /// This property corresponds to the GCC 'const' attribute.
  /// This property corresponds to the LLVM IR 'readnone' attribute.
  /// This property corresponds to the IntrNoMem LLVM intrinsic flag.
  FMRB_DoesNotAccessMemory = FMRL_Nowhere | MRI_NoModRef,

  /// The only memory references in this function (if it has any) are
  /// non-volatile loads from objects pointed to by its pointer-typed
  /// arguments, with arbitrary offsets.
  ///
  /// This property corresponds to the IntrReadArgMem LLVM intrinsic flag.
  FMRB_OnlyReadsArgumentPointees = FMRL_ArgumentPointees | MRI_Ref,

  /// The only memory references in this function (if it has any) are
  /// non-volatile loads and stores from objects pointed to by its
  /// pointer-typed arguments, with arbitrary offsets.
  ///
  /// This property corresponds to the IntrReadWriteArgMem LLVM intrinsic flag.
  FMRB_OnlyAccessesArgumentPointees = FMRL_ArgumentPointees | MRI_ModRef,

  /// This function does not perform any non-local stores or volatile loads,
  /// but may read from any memory location.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  /// This property corresponds to the LLVM IR 'readonly' attribute.
  /// This property corresponds to the IntrReadMem LLVM intrinsic flag.
  FMRB_OnlyReadsMemory = FMRL_Anywhere | MRI_Ref,

  /// This indicates that the function could not be classified into one of the
  /// behaviors above.
  FMRB_UnknownModRefBehavior = FMRL_Anywhere | MRI_ModRef
};

class AAResults {
public:
  // Make these results default constructable and movable. We have to spell
  // these out because MSVC won't synthesize them.
  AAResults() {}
  AAResults(AAResults &&Arg);
  AAResults &operator=(AAResults &&Arg);
  ~AAResults();

  /// Register a specific AA result.
  template <typename AAResultT> void addAAResult(AAResultT &AAResult) {
    // FIXME: We should use a much lighter weight system than the usual
    // polymorphic pattern because we don't own AAResult. It should
    // ideally involve two pointers and no separate allocation.
    AAs.emplace_back(new Model<AAResultT>(AAResult, *this));
  }

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, uint64_t V1Size, const Value *V2,
                    uint64_t V2Size) {
    return alias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, const Value *V2) {
    return alias(V1, MemoryLocation::UnknownSize, V2,
                 MemoryLocation::UnknownSize);
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// no-alias.
  bool isNoAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == NoAlias;
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, uint64_t V1Size, const Value *V2,
                 uint64_t V2Size) {
    return isNoAlias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, const Value *V2) {
    return isNoAlias(MemoryLocation(V1), MemoryLocation(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// must-alias.
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == MustAlias;
  }

  /// A convenience wrapper around the \c isMustAlias helper interface.
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(V1, 1, V2, 1) == MustAlias;
  }

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false);

  /// A convenience wrapper around the primary \c pointsToConstantMemory
  /// interface.
  bool pointsToConstantMemory(const Value *P, bool OrLocal = false) {
    return pointsToConstantMemory(MemoryLocation(P), OrLocal);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Get the ModRef info associated with a pointer argument of a callsite. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  ModRefInfo getArgModRefInfo(ImmutableCallSite CS, unsigned ArgIdx);

  /// Return the behavior of the given call site.
  FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS);

  /// Return the behavior when calling the given function.
  FunctionModRefBehavior getModRefBehavior(const Function *F);

  /// Checks if the specified call is known to never read or write memory.
  ///
  /// Note that if the call only reads from known-constant memory, it is also
  /// legal to return true. Also, calls that unwind the stack are legal for
  /// this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// without worrying about aliasing properties, and many calls have this
  /// property (e.g. calls to 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(ImmutableCallSite CS) {
    return getModRefBehavior(CS) == FMRB_DoesNotAccessMemory;
  }

  /// Checks if the specified function is known to never read or write memory.
  ///
  /// Note that if the function only reads from known-constant memory, it is
  /// also legal to return true. Also, function that unwind the stack are legal
  /// for this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// to such functions without worrying about aliasing properties, and many
  /// functions have this property (e.g. 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(const Function *F) {
    return getModRefBehavior(F) == FMRB_DoesNotAccessMemory;
  }

  /// Checks if the specified call is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Calls that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(ImmutableCallSite CS) {
    return onlyReadsMemory(getModRefBehavior(CS));
  }

  /// Checks if the specified function is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Functions that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(const Function *F) {
    return onlyReadsMemory(getModRefBehavior(F));
  }

  /// Checks if functions with the specified behavior are known to only read
  /// from non-volatile memory (or not access memory at all).
  static bool onlyReadsMemory(FunctionModRefBehavior MRB) {
    return !(MRB & MRI_Mod);
  }

  /// Checks if functions with the specified behavior are known to read and
  /// write at most from objects pointed to by their pointer-typed arguments
  /// (with arbitrary offsets).
  static bool onlyAccessesArgPointees(FunctionModRefBehavior MRB) {
    return !(MRB & FMRL_Anywhere & ~FMRL_ArgumentPointees);
  }

  /// Checks if functions with the specified behavior are known to potentially
  /// read or write from objects pointed to be their pointer-typed arguments
  /// (with arbitrary offsets).
  static bool doesAccessArgPointees(FunctionModRefBehavior MRB) {
    return (MRB & MRI_ModRef) && (MRB & FMRL_ArgumentPointees);
  }

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(ImmutableCallSite CS, const MemoryLocation &Loc);

  /// getModRefInfo (for call sites) - A convenience wrapper.
  ModRefInfo getModRefInfo(ImmutableCallSite CS, const Value *P,
                           uint64_t Size) {
    return getModRefInfo(CS, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for calls) - Return information about whether
  /// a particular call modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CallInst *C, const MemoryLocation &Loc) {
    return getModRefInfo(ImmutableCallSite(C), Loc);
  }

  /// getModRefInfo (for calls) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CallInst *C, const Value *P, uint64_t Size) {
    return getModRefInfo(C, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for invokes) - Return information about whether
  /// a particular invoke modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const InvokeInst *I, const MemoryLocation &Loc) {
    return getModRefInfo(ImmutableCallSite(I), Loc);
  }

  /// getModRefInfo (for invokes) - A convenience wrapper.
  ModRefInfo getModRefInfo(const InvokeInst *I, const Value *P, uint64_t Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for loads) - Return information about whether
  /// a particular load modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const LoadInst *L, const MemoryLocation &Loc);

  /// getModRefInfo (for loads) - A convenience wrapper.
  ModRefInfo getModRefInfo(const LoadInst *L, const Value *P, uint64_t Size) {
    return getModRefInfo(L, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for stores) - Return information about whether
  /// a particular store modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const StoreInst *S, const MemoryLocation &Loc);

  /// getModRefInfo (for stores) - A convenience wrapper.
  ModRefInfo getModRefInfo(const StoreInst *S, const Value *P, uint64_t Size) {
    return getModRefInfo(S, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for fences) - Return information about whether
  /// a particular store modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const FenceInst *S, const MemoryLocation &Loc) {
    // Conservatively correct.  (We could possibly be a bit smarter if
    // Loc is a alloca that doesn't escape.)
    return MRI_ModRef;
  }

  /// getModRefInfo (for fences) - A convenience wrapper.
  ModRefInfo getModRefInfo(const FenceInst *S, const Value *P, uint64_t Size) {
    return getModRefInfo(S, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for cmpxchges) - Return information about whether
  /// a particular cmpxchg modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX,
                           const MemoryLocation &Loc);

  /// getModRefInfo (for cmpxchges) - A convenience wrapper.
  ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX, const Value *P,
                           unsigned Size) {
    return getModRefInfo(CX, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for atomicrmws) - Return information about whether
  /// a particular atomicrmw modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const AtomicRMWInst *RMW, const MemoryLocation &Loc);

  /// getModRefInfo (for atomicrmws) - A convenience wrapper.
  ModRefInfo getModRefInfo(const AtomicRMWInst *RMW, const Value *P,
                           unsigned Size) {
    return getModRefInfo(RMW, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for va_args) - Return information about whether
  /// a particular va_arg modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const VAArgInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for va_args) - A convenience wrapper.
  ModRefInfo getModRefInfo(const VAArgInst *I, const Value *P, uint64_t Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for catchpads) - Return information about whether
  /// a particular catchpad modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CatchPadInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for catchpads) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CatchPadInst *I, const Value *P,
                           uint64_t Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for catchrets) - Return information about whether
  /// a particular catchret modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CatchReturnInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for catchrets) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CatchReturnInst *I, const Value *P,
                           uint64_t Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Check whether or not an instruction may read or write memory (without
  /// regard to a specific location).
  ///
  /// For function calls, this delegates to the alias-analysis specific
  /// call-site mod-ref behavior queries. Otherwise it delegates to the generic
  /// mod ref information query without a location.
  ModRefInfo getModRefInfo(const Instruction *I) {
    if (auto CS = ImmutableCallSite(I)) {
      auto MRB = getModRefBehavior(CS);
      if (MRB & MRI_ModRef)
        return MRI_ModRef;
      else if (MRB & MRI_Ref)
        return MRI_Ref;
      else if (MRB & MRI_Mod)
        return MRI_Mod;
      return MRI_NoModRef;
    }

    return getModRefInfo(I, MemoryLocation());
  }

  /// Check whether or not an instruction may read or write the specified
  /// memory location.
  ///
  /// An instruction that doesn't read or write memory may be trivially LICM'd
  /// for example.
  ///
  /// This primarily delegates to specific helpers above.
  ModRefInfo getModRefInfo(const Instruction *I, const MemoryLocation &Loc) {
    switch (I->getOpcode()) {
    case Instruction::VAArg:  return getModRefInfo((const VAArgInst*)I, Loc);
    case Instruction::Load:   return getModRefInfo((const LoadInst*)I,  Loc);
    case Instruction::Store:  return getModRefInfo((const StoreInst*)I, Loc);
    case Instruction::Fence:  return getModRefInfo((const FenceInst*)I, Loc);
    case Instruction::AtomicCmpXchg:
      return getModRefInfo((const AtomicCmpXchgInst*)I, Loc);
    case Instruction::AtomicRMW:
      return getModRefInfo((const AtomicRMWInst*)I, Loc);
    case Instruction::Call:   return getModRefInfo((const CallInst*)I,  Loc);
    case Instruction::Invoke: return getModRefInfo((const InvokeInst*)I,Loc);
    case Instruction::CatchPad:
      return getModRefInfo((const CatchPadInst *)I, Loc);
    case Instruction::CatchRet:
      return getModRefInfo((const CatchReturnInst *)I, Loc);
    default:
      return MRI_NoModRef;
    }
  }

  /// A convenience wrapper for constructing the memory location.
  ModRefInfo getModRefInfo(const Instruction *I, const Value *P,
                           uint64_t Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Return information about whether a call and an instruction may refer to
  /// the same memory locations.
  ModRefInfo getModRefInfo(Instruction *I, ImmutableCallSite Call);

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  ModRefInfo getModRefInfo(ImmutableCallSite CS1, ImmutableCallSite CS2);

  /// \brief Return information about whether a particular call site modifies
  /// or reads the specified memory location \p MemLoc before instruction \p I
  /// in a BasicBlock. A ordered basic block \p OBB can be used to speed up
  /// instruction ordering queries inside the BasicBlock containing \p I.
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc, DominatorTree *DT,
                                OrderedBasicBlock *OBB = nullptr);

  /// \brief A convenience wrapper to synthesize a memory location.
  ModRefInfo callCapturesBefore(const Instruction *I, const Value *P,
                                uint64_t Size, DominatorTree *DT,
                                OrderedBasicBlock *OBB = nullptr) {
    return callCapturesBefore(I, MemoryLocation(P, Size), DT, OBB);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Higher level methods for querying mod/ref information.
  /// @{

  /// Check if it is possible for execution of the specified basic block to
  /// modify the location Loc.
  bool canBasicBlockModify(const BasicBlock &BB, const MemoryLocation &Loc);

  /// A convenience wrapper synthesizing a memory location.
  bool canBasicBlockModify(const BasicBlock &BB, const Value *P,
                           uint64_t Size) {
    return canBasicBlockModify(BB, MemoryLocation(P, Size));
  }

  /// Check if it is possible for the execution of the specified instructions
  /// to mod\ref (according to the mode) the location Loc.
  ///
  /// The instructions to consider are all of the instructions in the range of
  /// [I1,I2] INCLUSIVE. I1 and I2 must be in the same basic block.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const MemoryLocation &Loc,
                                 const ModRefInfo Mode);

  /// A convenience wrapper synthesizing a memory location.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const Value *Ptr, uint64_t Size,
                                 const ModRefInfo Mode) {
    return canInstructionRangeModRef(I1, I2, MemoryLocation(Ptr, Size), Mode);
  }

private:
  class Concept;
  template <typename T> class Model;

  template <typename T> friend class AAResultBase;

  std::vector<std::unique_ptr<Concept>> AAs;
};

/// Temporary typedef for legacy code that uses a generic \c AliasAnalysis
/// pointer or reference.
typedef AAResults AliasAnalysis;

/// A private abstract base class describing the concept of an individual alias
/// analysis implementation.
///
/// This interface is implemented by any \c Model instantiation. It is also the
/// interface which a type used to instantiate the model must provide.
///
/// All of these methods model methods by the same name in the \c
/// AAResults class. Only differences and specifics to how the
/// implementations are called are documented here.
class AAResults::Concept {
public:
  virtual ~Concept() = 0;

  /// An update API used internally by the AAResults to provide
  /// a handle back to the top level aggregation.
  virtual void setAAResults(AAResults *NewAAR) = 0;

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  virtual AliasResult alias(const MemoryLocation &LocA,
                            const MemoryLocation &LocB) = 0;

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  virtual bool pointsToConstantMemory(const MemoryLocation &Loc,
                                      bool OrLocal) = 0;

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Get the ModRef info associated with a pointer argument of a callsite. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  virtual ModRefInfo getArgModRefInfo(ImmutableCallSite CS,
                                      unsigned ArgIdx) = 0;

  /// Return the behavior of the given call site.
  virtual FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS) = 0;

  /// Return the behavior when calling the given function.
  virtual FunctionModRefBehavior getModRefBehavior(const Function *F) = 0;

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  virtual ModRefInfo getModRefInfo(ImmutableCallSite CS,
                                   const MemoryLocation &Loc) = 0;

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  virtual ModRefInfo getModRefInfo(ImmutableCallSite CS1,
                                   ImmutableCallSite CS2) = 0;

  /// @}
};

/// A private class template which derives from \c Concept and wraps some other
/// type.
///
/// This models the concept by directly forwarding each interface point to the
/// wrapped type which must implement a compatible interface. This provides
/// a type erased binding.
template <typename AAResultT> class AAResults::Model final : public Concept {
  AAResultT &Result;

public:
  explicit Model(AAResultT &Result, AAResults &AAR) : Result(Result) {
    Result.setAAResults(&AAR);
  }
  ~Model() override {}

  void setAAResults(AAResults *NewAAR) override { Result.setAAResults(NewAAR); }

  AliasResult alias(const MemoryLocation &LocA,
                    const MemoryLocation &LocB) override {
    return Result.alias(LocA, LocB);
  }

  bool pointsToConstantMemory(const MemoryLocation &Loc,
                              bool OrLocal) override {
    return Result.pointsToConstantMemory(Loc, OrLocal);
  }

  ModRefInfo getArgModRefInfo(ImmutableCallSite CS, unsigned ArgIdx) override {
    return Result.getArgModRefInfo(CS, ArgIdx);
  }

  FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS) override {
    return Result.getModRefBehavior(CS);
  }

  FunctionModRefBehavior getModRefBehavior(const Function *F) override {
    return Result.getModRefBehavior(F);
  }

  ModRefInfo getModRefInfo(ImmutableCallSite CS,
                           const MemoryLocation &Loc) override {
    return Result.getModRefInfo(CS, Loc);
  }

  ModRefInfo getModRefInfo(ImmutableCallSite CS1,
                           ImmutableCallSite CS2) override {
    return Result.getModRefInfo(CS1, CS2);
  }
};

/// A CRTP-driven "mixin" base class to help implement the function alias
/// analysis results concept.
///
/// Because of the nature of many alias analysis implementations, they often
/// only implement a subset of the interface. This base class will attempt to
/// implement the remaining portions of the interface in terms of simpler forms
/// of the interface where possible, and otherwise provide conservatively
/// correct fallback implementations.
///
/// Implementors of an alias analysis should derive from this CRTP, and then
/// override specific methods that they wish to customize. There is no need to
/// use virtual anywhere, the CRTP base class does static dispatch to the
/// derived type passed into it.
template <typename DerivedT> class AAResultBase {
  // Expose some parts of the interface only to the AAResults::Model
  // for wrapping. Specifically, this allows the model to call our
  // setAAResults method without exposing it as a fully public API.
  friend class AAResults::Model<DerivedT>;

  /// A pointer to the AAResults object that this AAResult is
  /// aggregated within. May be null if not aggregated.
  AAResults *AAR;

  /// Helper to dispatch calls back through the derived type.
  DerivedT &derived() { return static_cast<DerivedT &>(*this); }

  /// A setter for the AAResults pointer, which is used to satisfy the
  /// AAResults::Model contract.
  void setAAResults(AAResults *NewAAR) { AAR = NewAAR; }

protected:
  /// This proxy class models a common pattern where we delegate to either the
  /// top-level \c AAResults aggregation if one is registered, or to the
  /// current result if none are registered.
  class AAResultsProxy {
    AAResults *AAR;
    DerivedT &CurrentResult;

  public:
    AAResultsProxy(AAResults *AAR, DerivedT &CurrentResult)
        : AAR(AAR), CurrentResult(CurrentResult) {}

    AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
      return AAR ? AAR->alias(LocA, LocB) : CurrentResult.alias(LocA, LocB);
    }

    bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal) {
      return AAR ? AAR->pointsToConstantMemory(Loc, OrLocal)
                 : CurrentResult.pointsToConstantMemory(Loc, OrLocal);
    }

    ModRefInfo getArgModRefInfo(ImmutableCallSite CS, unsigned ArgIdx) {
      return AAR ? AAR->getArgModRefInfo(CS, ArgIdx) : CurrentResult.getArgModRefInfo(CS, ArgIdx);
    }

    FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS) {
      return AAR ? AAR->getModRefBehavior(CS) : CurrentResult.getModRefBehavior(CS);
    }

    FunctionModRefBehavior getModRefBehavior(const Function *F) {
      return AAR ? AAR->getModRefBehavior(F) : CurrentResult.getModRefBehavior(F);
    }

    ModRefInfo getModRefInfo(ImmutableCallSite CS, const MemoryLocation &Loc) {
      return AAR ? AAR->getModRefInfo(CS, Loc)
                 : CurrentResult.getModRefInfo(CS, Loc);
    }

    ModRefInfo getModRefInfo(ImmutableCallSite CS1, ImmutableCallSite CS2) {
      return AAR ? AAR->getModRefInfo(CS1, CS2) : CurrentResult.getModRefInfo(CS1, CS2);
    }
  };

  const TargetLibraryInfo &TLI;

  explicit AAResultBase(const TargetLibraryInfo &TLI) : TLI(TLI) {}

  // Provide all the copy and move constructors so that derived types aren't
  // constrained.
  AAResultBase(const AAResultBase &Arg) : TLI(Arg.TLI) {}
  AAResultBase(AAResultBase &&Arg) : TLI(Arg.TLI) {}

  /// Get a proxy for the best AA result set to query at this time.
  ///
  /// When this result is part of a larger aggregation, this will proxy to that
  /// aggregation. When this result is used in isolation, it will just delegate
  /// back to the derived class's implementation.
  AAResultsProxy getBestAAResults() { return AAResultsProxy(AAR, derived()); }

public:
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return MayAlias;
  }

  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal) {
    return false;
  }

  ModRefInfo getArgModRefInfo(ImmutableCallSite CS, unsigned ArgIdx) {
    return MRI_ModRef;
  }

  FunctionModRefBehavior getModRefBehavior(ImmutableCallSite CS) {
    if (!CS.hasOperandBundles())
      // If CS has operand bundles then aliasing attributes from the function it
      // calls do not directly apply to the CallSite.  This can be made more
      // precise in the future.
      if (const Function *F = CS.getCalledFunction())
        return getBestAAResults().getModRefBehavior(F);

    return FMRB_UnknownModRefBehavior;
  }

  FunctionModRefBehavior getModRefBehavior(const Function *F) {
    return FMRB_UnknownModRefBehavior;
  }

  ModRefInfo getModRefInfo(ImmutableCallSite CS, const MemoryLocation &Loc);

  ModRefInfo getModRefInfo(ImmutableCallSite CS1, ImmutableCallSite CS2);
};

/// Synthesize \c ModRefInfo for a call site and memory location by examining
/// the general behavior of the call site and any specific information for its
/// arguments.
///
/// This essentially, delegates across the alias analysis interface to collect
/// information which may be enough to (conservatively) fulfill the query.
template <typename DerivedT>
ModRefInfo AAResultBase<DerivedT>::getModRefInfo(ImmutableCallSite CS,
                                                 const MemoryLocation &Loc) {
  auto MRB = getBestAAResults().getModRefBehavior(CS);
  if (MRB == FMRB_DoesNotAccessMemory)
    return MRI_NoModRef;

  ModRefInfo Mask = MRI_ModRef;
  if (AAResults::onlyReadsMemory(MRB))
    Mask = MRI_Ref;

  if (AAResults::onlyAccessesArgPointees(MRB)) {
    bool DoesAlias = false;
    ModRefInfo AllArgsMask = MRI_NoModRef;
    if (AAResults::doesAccessArgPointees(MRB)) {
      for (ImmutableCallSite::arg_iterator AI = CS.arg_begin(),
                                           AE = CS.arg_end();
           AI != AE; ++AI) {
        const Value *Arg = *AI;
        if (!Arg->getType()->isPointerTy())
          continue;
        unsigned ArgIdx = std::distance(CS.arg_begin(), AI);
        MemoryLocation ArgLoc = MemoryLocation::getForArgument(CS, ArgIdx, TLI);
        AliasResult ArgAlias = getBestAAResults().alias(ArgLoc, Loc);
        if (ArgAlias != NoAlias) {
          ModRefInfo ArgMask = getBestAAResults().getArgModRefInfo(CS, ArgIdx);
          DoesAlias = true;
          AllArgsMask = ModRefInfo(AllArgsMask | ArgMask);
        }
      }
    }
    if (!DoesAlias)
      return MRI_NoModRef;
    Mask = ModRefInfo(Mask & AllArgsMask);
  }

  // If Loc is a constant memory location, the call definitely could not
  // modify the memory location.
  if ((Mask & MRI_Mod) &&
      getBestAAResults().pointsToConstantMemory(Loc, /*OrLocal*/ false))
    Mask = ModRefInfo(Mask & ~MRI_Mod);

  return Mask;
}

/// Synthesize \c ModRefInfo for two call sites by examining the general
/// behavior of the call site and any specific information for its arguments.
///
/// This essentially, delegates across the alias analysis interface to collect
/// information which may be enough to (conservatively) fulfill the query.
template <typename DerivedT>
ModRefInfo AAResultBase<DerivedT>::getModRefInfo(ImmutableCallSite CS1,
                                                 ImmutableCallSite CS2) {
  // If CS1 or CS2 are readnone, they don't interact.
  auto CS1B = getBestAAResults().getModRefBehavior(CS1);
  if (CS1B == FMRB_DoesNotAccessMemory)
    return MRI_NoModRef;

  auto CS2B = getBestAAResults().getModRefBehavior(CS2);
  if (CS2B == FMRB_DoesNotAccessMemory)
    return MRI_NoModRef;

  // If they both only read from memory, there is no dependence.
  if (AAResults::onlyReadsMemory(CS1B) && AAResults::onlyReadsMemory(CS2B))
    return MRI_NoModRef;

  ModRefInfo Mask = MRI_ModRef;

  // If CS1 only reads memory, the only dependence on CS2 can be
  // from CS1 reading memory written by CS2.
  if (AAResults::onlyReadsMemory(CS1B))
    Mask = ModRefInfo(Mask & MRI_Ref);

  // If CS2 only access memory through arguments, accumulate the mod/ref
  // information from CS1's references to the memory referenced by
  // CS2's arguments.
  if (AAResults::onlyAccessesArgPointees(CS2B)) {
    ModRefInfo R = MRI_NoModRef;
    if (AAResults::doesAccessArgPointees(CS2B)) {
      for (ImmutableCallSite::arg_iterator I = CS2.arg_begin(),
                                           E = CS2.arg_end();
           I != E; ++I) {
        const Value *Arg = *I;
        if (!Arg->getType()->isPointerTy())
          continue;
        unsigned CS2ArgIdx = std::distance(CS2.arg_begin(), I);
        auto CS2ArgLoc = MemoryLocation::getForArgument(CS2, CS2ArgIdx, TLI);

        // ArgMask indicates what CS2 might do to CS2ArgLoc, and the dependence
        // of CS1 on that location is the inverse.
        ModRefInfo ArgMask =
            getBestAAResults().getArgModRefInfo(CS2, CS2ArgIdx);
        if (ArgMask == MRI_Mod)
          ArgMask = MRI_ModRef;
        else if (ArgMask == MRI_Ref)
          ArgMask = MRI_Mod;

        ArgMask = ModRefInfo(ArgMask &
                             getBestAAResults().getModRefInfo(CS1, CS2ArgLoc));

        R = ModRefInfo((R | ArgMask) & Mask);
        if (R == Mask)
          break;
      }
    }
    return R;
  }

  // If CS1 only accesses memory through arguments, check if CS2 references
  // any of the memory referenced by CS1's arguments. If not, return NoModRef.
  if (AAResults::onlyAccessesArgPointees(CS1B)) {
    ModRefInfo R = MRI_NoModRef;
    if (AAResults::doesAccessArgPointees(CS1B)) {
      for (ImmutableCallSite::arg_iterator I = CS1.arg_begin(),
                                           E = CS1.arg_end();
           I != E; ++I) {
        const Value *Arg = *I;
        if (!Arg->getType()->isPointerTy())
          continue;
        unsigned CS1ArgIdx = std::distance(CS1.arg_begin(), I);
        auto CS1ArgLoc = MemoryLocation::getForArgument(CS1, CS1ArgIdx, TLI);

        // ArgMask indicates what CS1 might do to CS1ArgLoc; if CS1 might Mod
        // CS1ArgLoc, then we care about either a Mod or a Ref by CS2. If CS1
        // might Ref, then we care only about a Mod by CS2.
        ModRefInfo ArgMask = getBestAAResults().getArgModRefInfo(CS1, CS1ArgIdx);
        ModRefInfo ArgR = getBestAAResults().getModRefInfo(CS2, CS1ArgLoc);
        if (((ArgMask & MRI_Mod) != MRI_NoModRef &&
             (ArgR & MRI_ModRef) != MRI_NoModRef) ||
            ((ArgMask & MRI_Ref) != MRI_NoModRef &&
             (ArgR & MRI_Mod) != MRI_NoModRef))
          R = ModRefInfo((R | ArgMask) & Mask);

        if (R == Mask)
          break;
      }
    }
    return R;
  }

  return Mask;
}

/// isNoAliasCall - Return true if this pointer is returned by a noalias
/// function.
bool isNoAliasCall(const Value *V);

/// isNoAliasArgument - Return true if this is an argument with the noalias
/// attribute.
bool isNoAliasArgument(const Value *V);

/// isIdentifiedObject - Return true if this pointer refers to a distinct and
/// identifiable object.  This returns true for:
///    Global Variables and Functions (but not Global Aliases)
///    Allocas
///    ByVal and NoAlias Arguments
///    NoAlias returns (e.g. calls to malloc)
///
bool isIdentifiedObject(const Value *V);

/// isIdentifiedFunctionLocal - Return true if V is umabigously identified
/// at the function-level. Different IdentifiedFunctionLocals can't alias.
/// Further, an IdentifiedFunctionLocal can not alias with any function
/// arguments other than itself, which is not necessarily true for
/// IdentifiedObjects.
bool isIdentifiedFunctionLocal(const Value *V);

/// A manager for alias analyses.
///
/// This class can have analyses registered with it and when run, it will run
/// all of them and aggregate their results into single AA results interface
/// that dispatches across all of the alias analysis results available.
///
/// Note that the order in which analyses are registered is very significant.
/// That is the order in which the results will be aggregated and queried.
///
/// This manager effectively wraps the AnalysisManager for registering alias
/// analyses. When you register your alias analysis with this manager, it will
/// ensure the analysis itself is registered with its AnalysisManager.
class AAManager {
public:
  typedef AAResults Result;

  // This type hase value semantics. We have to spell these out because MSVC
  // won't synthesize them.
  AAManager() {}
  AAManager(AAManager &&Arg)
      : FunctionResultGetters(std::move(Arg.FunctionResultGetters)) {}
  AAManager(const AAManager &Arg)
      : FunctionResultGetters(Arg.FunctionResultGetters) {}
  AAManager &operator=(AAManager &&RHS) {
    FunctionResultGetters = std::move(RHS.FunctionResultGetters);
    return *this;
  }
  AAManager &operator=(const AAManager &RHS) {
    FunctionResultGetters = RHS.FunctionResultGetters;
    return *this;
  }

  /// Register a specific AA result.
  template <typename AnalysisT> void registerFunctionAnalysis() {
    FunctionResultGetters.push_back(&getFunctionAAResultImpl<AnalysisT>);
  }

  Result run(Function &F, AnalysisManager<Function> &AM) {
    Result R;
    for (auto &Getter : FunctionResultGetters)
      (*Getter)(F, AM, R);
    return R;
  }

private:
  SmallVector<void (*)(Function &F, AnalysisManager<Function> &AM,
                       AAResults &AAResults),
              4> FunctionResultGetters;

  template <typename AnalysisT>
  static void getFunctionAAResultImpl(Function &F,
                                      AnalysisManager<Function> &AM,
                                      AAResults &AAResults) {
    AAResults.addAAResult(AM.template getResult<AnalysisT>(F));
  }
};

/// A wrapper pass to provide the legacy pass manager access to a suitably
/// prepared AAResults object.
class AAResultsWrapperPass : public FunctionPass {
  std::unique_ptr<AAResults> AAR;

public:
  static char ID;

  AAResultsWrapperPass();

  AAResults &getAAResults() { return *AAR; }
  const AAResults &getAAResults() const { return *AAR; }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createAAResultsWrapperPass();

/// A wrapper pass around a callback which can be used to populate the
/// AAResults in the AAResultsWrapperPass from an external AA.
///
/// The callback provided here will be used each time we prepare an AAResults
/// object, and will receive a reference to the function wrapper pass, the
/// function, and the AAResults object to populate. This should be used when
/// setting up a custom pass pipeline to inject a hook into the AA results.
ImmutablePass *createExternalAAWrapperPass(
    std::function<void(Pass &, Function &, AAResults &)> Callback);

/// A helper for the legacy pass manager to create a \c AAResults
/// object populated to the best of our ability for a particular function when
/// inside of a \c ModulePass or a \c CallGraphSCCPass.
AAResults createLegacyPMAAResults(Pass &P, Function &F, BasicAAResult &BAR);

} // End llvm namespace

#endif
