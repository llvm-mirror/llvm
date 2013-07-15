//===-- MemorySanitizer.cpp - detector of uninitialized reads -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file is a part of MemorySanitizer, a detector of uninitialized
/// reads.
///
/// Status: early prototype.
///
/// The algorithm of the tool is similar to Memcheck
/// (http://goo.gl/QKbem). We associate a few shadow bits with every
/// byte of the application memory, poison the shadow of the malloc-ed
/// or alloca-ed memory, load the shadow bits on every memory read,
/// propagate the shadow bits through some of the arithmetic
/// instruction (including MOV), store the shadow bits on every memory
/// write, report a bug on some other instructions (e.g. JMP) if the
/// associated shadow is poisoned.
///
/// But there are differences too. The first and the major one:
/// compiler instrumentation instead of binary instrumentation. This
/// gives us much better register allocation, possible compiler
/// optimizations and a fast start-up. But this brings the major issue
/// as well: msan needs to see all program events, including system
/// calls and reads/writes in system libraries, so we either need to
/// compile *everything* with msan or use a binary translation
/// component (e.g. DynamoRIO) to instrument pre-built libraries.
/// Another difference from Memcheck is that we use 8 shadow bits per
/// byte of application memory and use a direct shadow mapping. This
/// greatly simplifies the instrumentation code and avoids races on
/// shadow updates (Memcheck is single-threaded so races are not a
/// concern there. Memcheck uses 2 shadow bits per byte with a slow
/// path storage that uses 8 bits per byte).
///
/// The default value of shadow is 0, which means "clean" (not poisoned).
///
/// Every module initializer should call __msan_init to ensure that the
/// shadow memory is ready. On error, __msan_warning is called. Since
/// parameters and return values may be passed via registers, we have a
/// specialized thread-local shadow for return values
/// (__msan_retval_tls) and parameters (__msan_param_tls).
///
///                           Origin tracking.
///
/// MemorySanitizer can track origins (allocation points) of all uninitialized
/// values. This behavior is controlled with a flag (msan-track-origins) and is
/// disabled by default.
///
/// Origins are 4-byte values created and interpreted by the runtime library.
/// They are stored in a second shadow mapping, one 4-byte value for 4 bytes
/// of application memory. Propagation of origins is basically a bunch of
/// "select" instructions that pick the origin of a dirty argument, if an
/// instruction has one.
///
/// Every 4 aligned, consecutive bytes of application memory have one origin
/// value associated with them. If these bytes contain uninitialized data
/// coming from 2 different allocations, the last store wins. Because of this,
/// MemorySanitizer reports can show unrelated origins, but this is unlikely in
/// practice.
///
/// Origins are meaningless for fully initialized values, so MemorySanitizer
/// avoids storing origin to memory when a fully initialized value is stored.
/// This way it avoids needless overwritting origin of the 4-byte region on
/// a short (i.e. 1 byte) clean store, and it is also good for performance.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "msan"

#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/InstVisitor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/SpecialCaseList.h"

using namespace llvm;

static const uint64_t kShadowMask32 = 1ULL << 31;
static const uint64_t kShadowMask64 = 1ULL << 46;
static const uint64_t kOriginOffset32 = 1ULL << 30;
static const uint64_t kOriginOffset64 = 1ULL << 45;
static const unsigned kMinOriginAlignment = 4;
static const unsigned kShadowTLSAlignment = 8;

/// \brief Track origins of uninitialized values.
///
/// Adds a section to MemorySanitizer report that points to the allocation
/// (stack or heap) the uninitialized bits came from originally.
static cl::opt<bool> ClTrackOrigins("msan-track-origins",
       cl::desc("Track origins (allocation sites) of poisoned memory"),
       cl::Hidden, cl::init(false));
static cl::opt<bool> ClKeepGoing("msan-keep-going",
       cl::desc("keep going after reporting a UMR"),
       cl::Hidden, cl::init(false));
static cl::opt<bool> ClPoisonStack("msan-poison-stack",
       cl::desc("poison uninitialized stack variables"),
       cl::Hidden, cl::init(true));
static cl::opt<bool> ClPoisonStackWithCall("msan-poison-stack-with-call",
       cl::desc("poison uninitialized stack variables with a call"),
       cl::Hidden, cl::init(false));
static cl::opt<int> ClPoisonStackPattern("msan-poison-stack-pattern",
       cl::desc("poison uninitialized stack variables with the given patter"),
       cl::Hidden, cl::init(0xff));
static cl::opt<bool> ClPoisonUndef("msan-poison-undef",
       cl::desc("poison undef temps"),
       cl::Hidden, cl::init(true));

static cl::opt<bool> ClHandleICmp("msan-handle-icmp",
       cl::desc("propagate shadow through ICmpEQ and ICmpNE"),
       cl::Hidden, cl::init(true));

static cl::opt<bool> ClHandleICmpExact("msan-handle-icmp-exact",
       cl::desc("exact handling of relational integer ICmp"),
       cl::Hidden, cl::init(false));

static cl::opt<bool> ClStoreCleanOrigin("msan-store-clean-origin",
       cl::desc("store origin for clean (fully initialized) values"),
       cl::Hidden, cl::init(false));

// This flag controls whether we check the shadow of the address
// operand of load or store. Such bugs are very rare, since load from
// a garbage address typically results in SEGV, but still happen
// (e.g. only lower bits of address are garbage, or the access happens
// early at program startup where malloc-ed memory is more likely to
// be zeroed. As of 2012-08-28 this flag adds 20% slowdown.
static cl::opt<bool> ClCheckAccessAddress("msan-check-access-address",
       cl::desc("report accesses through a pointer which has poisoned shadow"),
       cl::Hidden, cl::init(true));

static cl::opt<bool> ClDumpStrictInstructions("msan-dump-strict-instructions",
       cl::desc("print out instructions with default strict semantics"),
       cl::Hidden, cl::init(false));

static cl::opt<std::string>  ClBlacklistFile("msan-blacklist",
       cl::desc("File containing the list of functions where MemorySanitizer "
                "should not report bugs"), cl::Hidden);

namespace {

/// \brief An instrumentation pass implementing detection of uninitialized
/// reads.
///
/// MemorySanitizer: instrument the code in module to find
/// uninitialized reads.
class MemorySanitizer : public FunctionPass {
 public:
  MemorySanitizer(bool TrackOrigins = false,
                  StringRef BlacklistFile = StringRef())
    : FunctionPass(ID),
      TrackOrigins(TrackOrigins || ClTrackOrigins),
      TD(0),
      WarningFn(0),
      BlacklistFile(BlacklistFile.empty() ? ClBlacklistFile
                                          : BlacklistFile) { }
  const char *getPassName() const { return "MemorySanitizer"; }
  bool runOnFunction(Function &F);
  bool doInitialization(Module &M);
  static char ID;  // Pass identification, replacement for typeid.

 private:
  void initializeCallbacks(Module &M);

  /// \brief Track origins (allocation points) of uninitialized values.
  bool TrackOrigins;

  DataLayout *TD;
  LLVMContext *C;
  Type *IntptrTy;
  Type *OriginTy;
  /// \brief Thread-local shadow storage for function parameters.
  GlobalVariable *ParamTLS;
  /// \brief Thread-local origin storage for function parameters.
  GlobalVariable *ParamOriginTLS;
  /// \brief Thread-local shadow storage for function return value.
  GlobalVariable *RetvalTLS;
  /// \brief Thread-local origin storage for function return value.
  GlobalVariable *RetvalOriginTLS;
  /// \brief Thread-local shadow storage for in-register va_arg function
  /// parameters (x86_64-specific).
  GlobalVariable *VAArgTLS;
  /// \brief Thread-local shadow storage for va_arg overflow area
  /// (x86_64-specific).
  GlobalVariable *VAArgOverflowSizeTLS;
  /// \brief Thread-local space used to pass origin value to the UMR reporting
  /// function.
  GlobalVariable *OriginTLS;

  /// \brief The run-time callback to print a warning.
  Value *WarningFn;
  /// \brief Run-time helper that copies origin info for a memory range.
  Value *MsanCopyOriginFn;
  /// \brief Run-time helper that generates a new origin value for a stack
  /// allocation.
  Value *MsanSetAllocaOriginFn;
  /// \brief Run-time helper that poisons stack on function entry.
  Value *MsanPoisonStackFn;
  /// \brief MSan runtime replacements for memmove, memcpy and memset.
  Value *MemmoveFn, *MemcpyFn, *MemsetFn;

  /// \brief Address mask used in application-to-shadow address calculation.
  /// ShadowAddr is computed as ApplicationAddr & ~ShadowMask.
  uint64_t ShadowMask;
  /// \brief Offset of the origin shadow from the "normal" shadow.
  /// OriginAddr is computed as (ShadowAddr + OriginOffset) & ~3ULL
  uint64_t OriginOffset;
  /// \brief Branch weights for error reporting.
  MDNode *ColdCallWeights;
  /// \brief Branch weights for origin store.
  MDNode *OriginStoreWeights;
  /// \brief Path to blacklist file.
  SmallString<64> BlacklistFile;
  /// \brief The blacklist.
  OwningPtr<SpecialCaseList> BL;
  /// \brief An empty volatile inline asm that prevents callback merge.
  InlineAsm *EmptyAsm;

  friend struct MemorySanitizerVisitor;
  friend struct VarArgAMD64Helper;
};
}  // namespace

char MemorySanitizer::ID = 0;
INITIALIZE_PASS(MemorySanitizer, "msan",
                "MemorySanitizer: detects uninitialized reads.",
                false, false)

FunctionPass *llvm::createMemorySanitizerPass(bool TrackOrigins,
                                              StringRef BlacklistFile) {
  return new MemorySanitizer(TrackOrigins, BlacklistFile);
}

/// \brief Create a non-const global initialized with the given string.
///
/// Creates a writable global for Str so that we can pass it to the
/// run-time lib. Runtime uses first 4 bytes of the string to store the
/// frame ID, so the string needs to be mutable.
static GlobalVariable *createPrivateNonConstGlobalForString(Module &M,
                                                            StringRef Str) {
  Constant *StrConst = ConstantDataArray::getString(M.getContext(), Str);
  return new GlobalVariable(M, StrConst->getType(), /*isConstant=*/false,
                            GlobalValue::PrivateLinkage, StrConst, "");
}


/// \brief Insert extern declaration of runtime-provided functions and globals.
void MemorySanitizer::initializeCallbacks(Module &M) {
  // Only do this once.
  if (WarningFn)
    return;

  IRBuilder<> IRB(*C);
  // Create the callback.
  // FIXME: this function should have "Cold" calling conv,
  // which is not yet implemented.
  StringRef WarningFnName = ClKeepGoing ? "__msan_warning"
                                        : "__msan_warning_noreturn";
  WarningFn = M.getOrInsertFunction(WarningFnName, IRB.getVoidTy(), NULL);

  MsanCopyOriginFn = M.getOrInsertFunction(
    "__msan_copy_origin", IRB.getVoidTy(), IRB.getInt8PtrTy(),
    IRB.getInt8PtrTy(), IntptrTy, NULL);
  MsanSetAllocaOriginFn = M.getOrInsertFunction(
    "__msan_set_alloca_origin", IRB.getVoidTy(), IRB.getInt8PtrTy(), IntptrTy,
    IRB.getInt8PtrTy(), NULL);
  MsanPoisonStackFn = M.getOrInsertFunction(
    "__msan_poison_stack", IRB.getVoidTy(), IRB.getInt8PtrTy(), IntptrTy, NULL);
  MemmoveFn = M.getOrInsertFunction(
    "__msan_memmove", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
    IRB.getInt8PtrTy(), IntptrTy, NULL);
  MemcpyFn = M.getOrInsertFunction(
    "__msan_memcpy", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
    IntptrTy, NULL);
  MemsetFn = M.getOrInsertFunction(
    "__msan_memset", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt32Ty(),
    IntptrTy, NULL);

  // Create globals.
  RetvalTLS = new GlobalVariable(
    M, ArrayType::get(IRB.getInt64Ty(), 8), false,
    GlobalVariable::ExternalLinkage, 0, "__msan_retval_tls", 0,
    GlobalVariable::InitialExecTLSModel);
  RetvalOriginTLS = new GlobalVariable(
    M, OriginTy, false, GlobalVariable::ExternalLinkage, 0,
    "__msan_retval_origin_tls", 0, GlobalVariable::InitialExecTLSModel);

  ParamTLS = new GlobalVariable(
    M, ArrayType::get(IRB.getInt64Ty(), 1000), false,
    GlobalVariable::ExternalLinkage, 0, "__msan_param_tls", 0,
    GlobalVariable::InitialExecTLSModel);
  ParamOriginTLS = new GlobalVariable(
    M, ArrayType::get(OriginTy, 1000), false, GlobalVariable::ExternalLinkage,
    0, "__msan_param_origin_tls", 0, GlobalVariable::InitialExecTLSModel);

  VAArgTLS = new GlobalVariable(
    M, ArrayType::get(IRB.getInt64Ty(), 1000), false,
    GlobalVariable::ExternalLinkage, 0, "__msan_va_arg_tls", 0,
    GlobalVariable::InitialExecTLSModel);
  VAArgOverflowSizeTLS = new GlobalVariable(
    M, IRB.getInt64Ty(), false, GlobalVariable::ExternalLinkage, 0,
    "__msan_va_arg_overflow_size_tls", 0,
    GlobalVariable::InitialExecTLSModel);
  OriginTLS = new GlobalVariable(
    M, IRB.getInt32Ty(), false, GlobalVariable::ExternalLinkage, 0,
    "__msan_origin_tls", 0, GlobalVariable::InitialExecTLSModel);

  // We insert an empty inline asm after __msan_report* to avoid callback merge.
  EmptyAsm = InlineAsm::get(FunctionType::get(IRB.getVoidTy(), false),
                            StringRef(""), StringRef(""),
                            /*hasSideEffects=*/true);
}

/// \brief Module-level initialization.
///
/// inserts a call to __msan_init to the module's constructor list.
bool MemorySanitizer::doInitialization(Module &M) {
  TD = getAnalysisIfAvailable<DataLayout>();
  if (!TD)
    return false;
  BL.reset(new SpecialCaseList(BlacklistFile));
  C = &(M.getContext());
  unsigned PtrSize = TD->getPointerSizeInBits(/* AddressSpace */0);
  switch (PtrSize) {
    case 64:
      ShadowMask = kShadowMask64;
      OriginOffset = kOriginOffset64;
      break;
    case 32:
      ShadowMask = kShadowMask32;
      OriginOffset = kOriginOffset32;
      break;
    default:
      report_fatal_error("unsupported pointer size");
      break;
  }

  IRBuilder<> IRB(*C);
  IntptrTy = IRB.getIntPtrTy(TD);
  OriginTy = IRB.getInt32Ty();

  ColdCallWeights = MDBuilder(*C).createBranchWeights(1, 1000);
  OriginStoreWeights = MDBuilder(*C).createBranchWeights(1, 1000);

  // Insert a call to __msan_init/__msan_track_origins into the module's CTORs.
  appendToGlobalCtors(M, cast<Function>(M.getOrInsertFunction(
                      "__msan_init", IRB.getVoidTy(), NULL)), 0);

  if (TrackOrigins)
    new GlobalVariable(M, IRB.getInt32Ty(), true, GlobalValue::WeakODRLinkage,
                       IRB.getInt32(TrackOrigins), "__msan_track_origins");

  if (ClKeepGoing)
    new GlobalVariable(M, IRB.getInt32Ty(), true, GlobalValue::WeakODRLinkage,
                       IRB.getInt32(ClKeepGoing), "__msan_keep_going");

  return true;
}

namespace {

/// \brief A helper class that handles instrumentation of VarArg
/// functions on a particular platform.
///
/// Implementations are expected to insert the instrumentation
/// necessary to propagate argument shadow through VarArg function
/// calls. Visit* methods are called during an InstVisitor pass over
/// the function, and should avoid creating new basic blocks. A new
/// instance of this class is created for each instrumented function.
struct VarArgHelper {
  /// \brief Visit a CallSite.
  virtual void visitCallSite(CallSite &CS, IRBuilder<> &IRB) = 0;

  /// \brief Visit a va_start call.
  virtual void visitVAStartInst(VAStartInst &I) = 0;

  /// \brief Visit a va_copy call.
  virtual void visitVACopyInst(VACopyInst &I) = 0;

  /// \brief Finalize function instrumentation.
  ///
  /// This method is called after visiting all interesting (see above)
  /// instructions in a function.
  virtual void finalizeInstrumentation() = 0;

  virtual ~VarArgHelper() {}
};

struct MemorySanitizerVisitor;

VarArgHelper*
CreateVarArgHelper(Function &Func, MemorySanitizer &Msan,
                   MemorySanitizerVisitor &Visitor);

/// This class does all the work for a given function. Store and Load
/// instructions store and load corresponding shadow and origin
/// values. Most instructions propagate shadow from arguments to their
/// return values. Certain instructions (most importantly, BranchInst)
/// test their argument shadow and print reports (with a runtime call) if it's
/// non-zero.
struct MemorySanitizerVisitor : public InstVisitor<MemorySanitizerVisitor> {
  Function &F;
  MemorySanitizer &MS;
  SmallVector<PHINode *, 16> ShadowPHINodes, OriginPHINodes;
  ValueMap<Value*, Value*> ShadowMap, OriginMap;
  bool InsertChecks;
  bool LoadShadow;
  bool PoisonStack;
  bool PoisonUndef;
  OwningPtr<VarArgHelper> VAHelper;

  struct ShadowOriginAndInsertPoint {
    Instruction *Shadow;
    Instruction *Origin;
    Instruction *OrigIns;
    ShadowOriginAndInsertPoint(Instruction *S, Instruction *O, Instruction *I)
      : Shadow(S), Origin(O), OrigIns(I) { }
    ShadowOriginAndInsertPoint() : Shadow(0), Origin(0), OrigIns(0) { }
  };
  SmallVector<ShadowOriginAndInsertPoint, 16> InstrumentationList;
  SmallVector<Instruction*, 16> StoreList;

  MemorySanitizerVisitor(Function &F, MemorySanitizer &MS)
      : F(F), MS(MS), VAHelper(CreateVarArgHelper(F, MS, *this)) {
    bool SanitizeFunction = !MS.BL->isIn(F) && F.getAttributes().hasAttribute(
                                                   AttributeSet::FunctionIndex,
                                                   Attribute::SanitizeMemory);
    InsertChecks = SanitizeFunction;
    LoadShadow = SanitizeFunction;
    PoisonStack = SanitizeFunction && ClPoisonStack;
    PoisonUndef = SanitizeFunction && ClPoisonUndef;

    DEBUG(if (!InsertChecks)
          dbgs() << "MemorySanitizer is not inserting checks into '"
                 << F.getName() << "'\n");
  }

  void materializeStores() {
    for (size_t i = 0, n = StoreList.size(); i < n; i++) {
      StoreInst& I = *dyn_cast<StoreInst>(StoreList[i]);

      IRBuilder<> IRB(&I);
      Value *Val = I.getValueOperand();
      Value *Addr = I.getPointerOperand();
      Value *Shadow = getShadow(Val);
      Value *ShadowPtr = getShadowPtr(Addr, Shadow->getType(), IRB);

      StoreInst *NewSI =
        IRB.CreateAlignedStore(Shadow, ShadowPtr, I.getAlignment());
      DEBUG(dbgs() << "  STORE: " << *NewSI << "\n");
      (void)NewSI;

      if (ClCheckAccessAddress)
        insertCheck(Addr, &I);

      if (MS.TrackOrigins) {
        unsigned Alignment = std::max(kMinOriginAlignment, I.getAlignment());
        if (ClStoreCleanOrigin || isa<StructType>(Shadow->getType())) {
          IRB.CreateAlignedStore(getOrigin(Val), getOriginPtr(Addr, IRB),
                                 Alignment);
        } else {
          Value *ConvertedShadow = convertToShadowTyNoVec(Shadow, IRB);

          Constant *Cst = dyn_cast_or_null<Constant>(ConvertedShadow);
          // TODO(eugenis): handle non-zero constant shadow by inserting an
          // unconditional check (can not simply fail compilation as this could
          // be in the dead code).
          if (Cst)
            continue;

          Value *Cmp = IRB.CreateICmpNE(ConvertedShadow,
              getCleanShadow(ConvertedShadow), "_mscmp");
          Instruction *CheckTerm =
            SplitBlockAndInsertIfThen(cast<Instruction>(Cmp), false,
                                      MS.OriginStoreWeights);
          IRBuilder<> IRBNew(CheckTerm);
          IRBNew.CreateAlignedStore(getOrigin(Val), getOriginPtr(Addr, IRBNew),
                                    Alignment);
        }
      }
    }
  }

  void materializeChecks() {
    for (size_t i = 0, n = InstrumentationList.size(); i < n; i++) {
      Instruction *Shadow = InstrumentationList[i].Shadow;
      Instruction *OrigIns = InstrumentationList[i].OrigIns;
      IRBuilder<> IRB(OrigIns);
      DEBUG(dbgs() << "  SHAD0 : " << *Shadow << "\n");
      Value *ConvertedShadow = convertToShadowTyNoVec(Shadow, IRB);
      DEBUG(dbgs() << "  SHAD1 : " << *ConvertedShadow << "\n");
      Value *Cmp = IRB.CreateICmpNE(ConvertedShadow,
                                    getCleanShadow(ConvertedShadow), "_mscmp");
      Instruction *CheckTerm =
        SplitBlockAndInsertIfThen(cast<Instruction>(Cmp),
                                  /* Unreachable */ !ClKeepGoing,
                                  MS.ColdCallWeights);

      IRB.SetInsertPoint(CheckTerm);
      if (MS.TrackOrigins) {
        Instruction *Origin = InstrumentationList[i].Origin;
        IRB.CreateStore(Origin ? (Value*)Origin : (Value*)IRB.getInt32(0),
                        MS.OriginTLS);
      }
      CallInst *Call = IRB.CreateCall(MS.WarningFn);
      Call->setDebugLoc(OrigIns->getDebugLoc());
      IRB.CreateCall(MS.EmptyAsm);
      DEBUG(dbgs() << "  CHECK: " << *Cmp << "\n");
    }
    DEBUG(dbgs() << "DONE:\n" << F);
  }

  /// \brief Add MemorySanitizer instrumentation to a function.
  bool runOnFunction() {
    MS.initializeCallbacks(*F.getParent());
    if (!MS.TD) return false;

    // In the presence of unreachable blocks, we may see Phi nodes with
    // incoming nodes from such blocks. Since InstVisitor skips unreachable
    // blocks, such nodes will not have any shadow value associated with them.
    // It's easier to remove unreachable blocks than deal with missing shadow.
    removeUnreachableBlocks(F);

    // Iterate all BBs in depth-first order and create shadow instructions
    // for all instructions (where applicable).
    // For PHI nodes we create dummy shadow PHIs which will be finalized later.
    for (df_iterator<BasicBlock*> DI = df_begin(&F.getEntryBlock()),
         DE = df_end(&F.getEntryBlock()); DI != DE; ++DI) {
      BasicBlock *BB = *DI;
      visit(*BB);
    }

    // Finalize PHI nodes.
    for (size_t i = 0, n = ShadowPHINodes.size(); i < n; i++) {
      PHINode *PN = ShadowPHINodes[i];
      PHINode *PNS = cast<PHINode>(getShadow(PN));
      PHINode *PNO = MS.TrackOrigins ? cast<PHINode>(getOrigin(PN)) : 0;
      size_t NumValues = PN->getNumIncomingValues();
      for (size_t v = 0; v < NumValues; v++) {
        PNS->addIncoming(getShadow(PN, v), PN->getIncomingBlock(v));
        if (PNO)
          PNO->addIncoming(getOrigin(PN, v), PN->getIncomingBlock(v));
      }
    }

    VAHelper->finalizeInstrumentation();

    // Delayed instrumentation of StoreInst.
    // This may add new checks to be inserted later.
    materializeStores();

    // Insert shadow value checks.
    materializeChecks();

    return true;
  }

  /// \brief Compute the shadow type that corresponds to a given Value.
  Type *getShadowTy(Value *V) {
    return getShadowTy(V->getType());
  }

  /// \brief Compute the shadow type that corresponds to a given Type.
  Type *getShadowTy(Type *OrigTy) {
    if (!OrigTy->isSized()) {
      return 0;
    }
    // For integer type, shadow is the same as the original type.
    // This may return weird-sized types like i1.
    if (IntegerType *IT = dyn_cast<IntegerType>(OrigTy))
      return IT;
    if (VectorType *VT = dyn_cast<VectorType>(OrigTy)) {
      uint32_t EltSize = MS.TD->getTypeSizeInBits(VT->getElementType());
      return VectorType::get(IntegerType::get(*MS.C, EltSize),
                             VT->getNumElements());
    }
    if (StructType *ST = dyn_cast<StructType>(OrigTy)) {
      SmallVector<Type*, 4> Elements;
      for (unsigned i = 0, n = ST->getNumElements(); i < n; i++)
        Elements.push_back(getShadowTy(ST->getElementType(i)));
      StructType *Res = StructType::get(*MS.C, Elements, ST->isPacked());
      DEBUG(dbgs() << "getShadowTy: " << *ST << " ===> " << *Res << "\n");
      return Res;
    }
    uint32_t TypeSize = MS.TD->getTypeSizeInBits(OrigTy);
    return IntegerType::get(*MS.C, TypeSize);
  }

  /// \brief Flatten a vector type.
  Type *getShadowTyNoVec(Type *ty) {
    if (VectorType *vt = dyn_cast<VectorType>(ty))
      return IntegerType::get(*MS.C, vt->getBitWidth());
    return ty;
  }

  /// \brief Convert a shadow value to it's flattened variant.
  Value *convertToShadowTyNoVec(Value *V, IRBuilder<> &IRB) {
    Type *Ty = V->getType();
    Type *NoVecTy = getShadowTyNoVec(Ty);
    if (Ty == NoVecTy) return V;
    return IRB.CreateBitCast(V, NoVecTy);
  }

  /// \brief Compute the shadow address that corresponds to a given application
  /// address.
  ///
  /// Shadow = Addr & ~ShadowMask.
  Value *getShadowPtr(Value *Addr, Type *ShadowTy,
                      IRBuilder<> &IRB) {
    Value *ShadowLong =
      IRB.CreateAnd(IRB.CreatePointerCast(Addr, MS.IntptrTy),
                    ConstantInt::get(MS.IntptrTy, ~MS.ShadowMask));
    return IRB.CreateIntToPtr(ShadowLong, PointerType::get(ShadowTy, 0));
  }

  /// \brief Compute the origin address that corresponds to a given application
  /// address.
  ///
  /// OriginAddr = (ShadowAddr + OriginOffset) & ~3ULL
  Value *getOriginPtr(Value *Addr, IRBuilder<> &IRB) {
    Value *ShadowLong =
      IRB.CreateAnd(IRB.CreatePointerCast(Addr, MS.IntptrTy),
                    ConstantInt::get(MS.IntptrTy, ~MS.ShadowMask));
    Value *Add =
      IRB.CreateAdd(ShadowLong,
                    ConstantInt::get(MS.IntptrTy, MS.OriginOffset));
    Value *SecondAnd =
      IRB.CreateAnd(Add, ConstantInt::get(MS.IntptrTy, ~3ULL));
    return IRB.CreateIntToPtr(SecondAnd, PointerType::get(IRB.getInt32Ty(), 0));
  }

  /// \brief Compute the shadow address for a given function argument.
  ///
  /// Shadow = ParamTLS+ArgOffset.
  Value *getShadowPtrForArgument(Value *A, IRBuilder<> &IRB,
                                 int ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.ParamTLS, MS.IntptrTy);
    Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, PointerType::get(getShadowTy(A), 0),
                              "_msarg");
  }

  /// \brief Compute the origin address for a given function argument.
  Value *getOriginPtrForArgument(Value *A, IRBuilder<> &IRB,
                                 int ArgOffset) {
    if (!MS.TrackOrigins) return 0;
    Value *Base = IRB.CreatePointerCast(MS.ParamOriginTLS, MS.IntptrTy);
    Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, PointerType::get(MS.OriginTy, 0),
                              "_msarg_o");
  }

  /// \brief Compute the shadow address for a retval.
  Value *getShadowPtrForRetval(Value *A, IRBuilder<> &IRB) {
    Value *Base = IRB.CreatePointerCast(MS.RetvalTLS, MS.IntptrTy);
    return IRB.CreateIntToPtr(Base, PointerType::get(getShadowTy(A), 0),
                              "_msret");
  }

  /// \brief Compute the origin address for a retval.
  Value *getOriginPtrForRetval(IRBuilder<> &IRB) {
    // We keep a single origin for the entire retval. Might be too optimistic.
    return MS.RetvalOriginTLS;
  }

  /// \brief Set SV to be the shadow value for V.
  void setShadow(Value *V, Value *SV) {
    assert(!ShadowMap.count(V) && "Values may only have one shadow");
    ShadowMap[V] = SV;
  }

  /// \brief Set Origin to be the origin value for V.
  void setOrigin(Value *V, Value *Origin) {
    if (!MS.TrackOrigins) return;
    assert(!OriginMap.count(V) && "Values may only have one origin");
    DEBUG(dbgs() << "ORIGIN: " << *V << "  ==> " << *Origin << "\n");
    OriginMap[V] = Origin;
  }

  /// \brief Create a clean shadow value for a given value.
  ///
  /// Clean shadow (all zeroes) means all bits of the value are defined
  /// (initialized).
  Constant *getCleanShadow(Value *V) {
    Type *ShadowTy = getShadowTy(V);
    if (!ShadowTy)
      return 0;
    return Constant::getNullValue(ShadowTy);
  }

  /// \brief Create a dirty shadow of a given shadow type.
  Constant *getPoisonedShadow(Type *ShadowTy) {
    assert(ShadowTy);
    if (isa<IntegerType>(ShadowTy) || isa<VectorType>(ShadowTy))
      return Constant::getAllOnesValue(ShadowTy);
    StructType *ST = cast<StructType>(ShadowTy);
    SmallVector<Constant *, 4> Vals;
    for (unsigned i = 0, n = ST->getNumElements(); i < n; i++)
      Vals.push_back(getPoisonedShadow(ST->getElementType(i)));
    return ConstantStruct::get(ST, Vals);
  }

  /// \brief Create a dirty shadow for a given value.
  Constant *getPoisonedShadow(Value *V) {
    Type *ShadowTy = getShadowTy(V);
    if (!ShadowTy)
      return 0;
    return getPoisonedShadow(ShadowTy);
  }

  /// \brief Create a clean (zero) origin.
  Value *getCleanOrigin() {
    return Constant::getNullValue(MS.OriginTy);
  }

  /// \brief Get the shadow value for a given Value.
  ///
  /// This function either returns the value set earlier with setShadow,
  /// or extracts if from ParamTLS (for function arguments).
  Value *getShadow(Value *V) {
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      // For instructions the shadow is already stored in the map.
      Value *Shadow = ShadowMap[V];
      if (!Shadow) {
        DEBUG(dbgs() << "No shadow: " << *V << "\n" << *(I->getParent()));
        (void)I;
        assert(Shadow && "No shadow for a value");
      }
      return Shadow;
    }
    if (UndefValue *U = dyn_cast<UndefValue>(V)) {
      Value *AllOnes = PoisonUndef ? getPoisonedShadow(V) : getCleanShadow(V);
      DEBUG(dbgs() << "Undef: " << *U << " ==> " << *AllOnes << "\n");
      (void)U;
      return AllOnes;
    }
    if (Argument *A = dyn_cast<Argument>(V)) {
      // For arguments we compute the shadow on demand and store it in the map.
      Value **ShadowPtr = &ShadowMap[V];
      if (*ShadowPtr)
        return *ShadowPtr;
      Function *F = A->getParent();
      IRBuilder<> EntryIRB(F->getEntryBlock().getFirstNonPHI());
      unsigned ArgOffset = 0;
      for (Function::arg_iterator AI = F->arg_begin(), AE = F->arg_end();
           AI != AE; ++AI) {
        if (!AI->getType()->isSized()) {
          DEBUG(dbgs() << "Arg is not sized\n");
          continue;
        }
        unsigned Size = AI->hasByValAttr()
          ? MS.TD->getTypeAllocSize(AI->getType()->getPointerElementType())
          : MS.TD->getTypeAllocSize(AI->getType());
        if (A == AI) {
          Value *Base = getShadowPtrForArgument(AI, EntryIRB, ArgOffset);
          if (AI->hasByValAttr()) {
            // ByVal pointer itself has clean shadow. We copy the actual
            // argument shadow to the underlying memory.
            // Figure out maximal valid memcpy alignment.
            unsigned ArgAlign = AI->getParamAlignment();
            if (ArgAlign == 0) {
              Type *EltType = A->getType()->getPointerElementType();
              ArgAlign = MS.TD->getABITypeAlignment(EltType);
            }
            unsigned CopyAlign = std::min(ArgAlign, kShadowTLSAlignment);
            Value *Cpy = EntryIRB.CreateMemCpy(
                getShadowPtr(V, EntryIRB.getInt8Ty(), EntryIRB), Base, Size,
                CopyAlign);
            DEBUG(dbgs() << "  ByValCpy: " << *Cpy << "\n");
            (void)Cpy;
            *ShadowPtr = getCleanShadow(V);
          } else {
            *ShadowPtr = EntryIRB.CreateAlignedLoad(Base, kShadowTLSAlignment);
          }
          DEBUG(dbgs() << "  ARG:    "  << *AI << " ==> " <<
                **ShadowPtr << "\n");
          if (MS.TrackOrigins) {
            Value* OriginPtr = getOriginPtrForArgument(AI, EntryIRB, ArgOffset);
            setOrigin(A, EntryIRB.CreateLoad(OriginPtr));
          }
        }
        ArgOffset += DataLayout::RoundUpAlignment(Size, kShadowTLSAlignment);
      }
      assert(*ShadowPtr && "Could not find shadow for an argument");
      return *ShadowPtr;
    }
    // For everything else the shadow is zero.
    return getCleanShadow(V);
  }

  /// \brief Get the shadow for i-th argument of the instruction I.
  Value *getShadow(Instruction *I, int i) {
    return getShadow(I->getOperand(i));
  }

  /// \brief Get the origin for a value.
  Value *getOrigin(Value *V) {
    if (!MS.TrackOrigins) return 0;
    if (isa<Instruction>(V) || isa<Argument>(V)) {
      Value *Origin = OriginMap[V];
      if (!Origin) {
        DEBUG(dbgs() << "NO ORIGIN: " << *V << "\n");
        Origin = getCleanOrigin();
      }
      return Origin;
    }
    return getCleanOrigin();
  }

  /// \brief Get the origin for i-th argument of the instruction I.
  Value *getOrigin(Instruction *I, int i) {
    return getOrigin(I->getOperand(i));
  }

  /// \brief Remember the place where a shadow check should be inserted.
  ///
  /// This location will be later instrumented with a check that will print a
  /// UMR warning in runtime if the value is not fully defined.
  void insertCheck(Value *Val, Instruction *OrigIns) {
    assert(Val);
    if (!InsertChecks) return;
    Instruction *Shadow = dyn_cast_or_null<Instruction>(getShadow(Val));
    if (!Shadow) return;
#ifndef NDEBUG
    Type *ShadowTy = Shadow->getType();
    assert((isa<IntegerType>(ShadowTy) || isa<VectorType>(ShadowTy)) &&
           "Can only insert checks for integer and vector shadow types");
#endif
    Instruction *Origin = dyn_cast_or_null<Instruction>(getOrigin(Val));
    InstrumentationList.push_back(
      ShadowOriginAndInsertPoint(Shadow, Origin, OrigIns));
  }

  // ------------------- Visitors.

  /// \brief Instrument LoadInst
  ///
  /// Loads the corresponding shadow and (optionally) origin.
  /// Optionally, checks that the load address is fully defined.
  void visitLoadInst(LoadInst &I) {
    assert(I.getType()->isSized() && "Load type must have size");
    IRBuilder<> IRB(&I);
    Type *ShadowTy = getShadowTy(&I);
    Value *Addr = I.getPointerOperand();
    if (LoadShadow) {
      Value *ShadowPtr = getShadowPtr(Addr, ShadowTy, IRB);
      setShadow(&I,
                IRB.CreateAlignedLoad(ShadowPtr, I.getAlignment(), "_msld"));
    } else {
      setShadow(&I, getCleanShadow(&I));
    }

    if (ClCheckAccessAddress)
      insertCheck(I.getPointerOperand(), &I);

    if (MS.TrackOrigins) {
      if (LoadShadow) {
        unsigned Alignment = std::max(kMinOriginAlignment, I.getAlignment());
        setOrigin(&I,
                  IRB.CreateAlignedLoad(getOriginPtr(Addr, IRB), Alignment));
      } else {
        setOrigin(&I, getCleanOrigin());
      }
    }
  }

  /// \brief Instrument StoreInst
  ///
  /// Stores the corresponding shadow and (optionally) origin.
  /// Optionally, checks that the store address is fully defined.
  void visitStoreInst(StoreInst &I) {
    StoreList.push_back(&I);
  }

  // Vector manipulation.
  void visitExtractElementInst(ExtractElementInst &I) {
    insertCheck(I.getOperand(1), &I);
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateExtractElement(getShadow(&I, 0), I.getOperand(1),
              "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitInsertElementInst(InsertElementInst &I) {
    insertCheck(I.getOperand(2), &I);
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateInsertElement(getShadow(&I, 0), getShadow(&I, 1),
              I.getOperand(2), "_msprop"));
    setOriginForNaryOp(I);
  }

  void visitShuffleVectorInst(ShuffleVectorInst &I) {
    insertCheck(I.getOperand(2), &I);
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateShuffleVector(getShadow(&I, 0), getShadow(&I, 1),
              I.getOperand(2), "_msprop"));
    setOriginForNaryOp(I);
  }

  // Casts.
  void visitSExtInst(SExtInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateSExt(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitZExtInst(ZExtInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateZExt(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitTruncInst(TruncInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateTrunc(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitBitCastInst(BitCastInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateBitCast(getShadow(&I, 0), getShadowTy(&I)));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitPtrToIntInst(PtrToIntInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateIntCast(getShadow(&I, 0), getShadowTy(&I), false,
             "_msprop_ptrtoint"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitIntToPtrInst(IntToPtrInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateIntCast(getShadow(&I, 0), getShadowTy(&I), false,
             "_msprop_inttoptr"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitFPToSIInst(CastInst& I) { handleShadowOr(I); }
  void visitFPToUIInst(CastInst& I) { handleShadowOr(I); }
  void visitSIToFPInst(CastInst& I) { handleShadowOr(I); }
  void visitUIToFPInst(CastInst& I) { handleShadowOr(I); }
  void visitFPExtInst(CastInst& I) { handleShadowOr(I); }
  void visitFPTruncInst(CastInst& I) { handleShadowOr(I); }

  /// \brief Propagate shadow for bitwise AND.
  ///
  /// This code is exact, i.e. if, for example, a bit in the left argument
  /// is defined and 0, then neither the value not definedness of the
  /// corresponding bit in B don't affect the resulting shadow.
  void visitAnd(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    //  "And" of 0 and a poisoned value results in unpoisoned value.
    //  1&1 => 1;     0&1 => 0;     p&1 => p;
    //  1&0 => 0;     0&0 => 0;     p&0 => 0;
    //  1&p => p;     0&p => 0;     p&p => p;
    //  S = (S1 & S2) | (V1 & S2) | (S1 & V2)
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *V1 = I.getOperand(0);
    Value *V2 = I.getOperand(1);
    if (V1->getType() != S1->getType()) {
      V1 = IRB.CreateIntCast(V1, S1->getType(), false);
      V2 = IRB.CreateIntCast(V2, S2->getType(), false);
    }
    Value *S1S2 = IRB.CreateAnd(S1, S2);
    Value *V1S2 = IRB.CreateAnd(V1, S2);
    Value *S1V2 = IRB.CreateAnd(S1, V2);
    setShadow(&I, IRB.CreateOr(S1S2, IRB.CreateOr(V1S2, S1V2)));
    setOriginForNaryOp(I);
  }

  void visitOr(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    //  "Or" of 1 and a poisoned value results in unpoisoned value.
    //  1|1 => 1;     0|1 => 1;     p|1 => 1;
    //  1|0 => 1;     0|0 => 0;     p|0 => p;
    //  1|p => 1;     0|p => p;     p|p => p;
    //  S = (S1 & S2) | (~V1 & S2) | (S1 & ~V2)
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *V1 = IRB.CreateNot(I.getOperand(0));
    Value *V2 = IRB.CreateNot(I.getOperand(1));
    if (V1->getType() != S1->getType()) {
      V1 = IRB.CreateIntCast(V1, S1->getType(), false);
      V2 = IRB.CreateIntCast(V2, S2->getType(), false);
    }
    Value *S1S2 = IRB.CreateAnd(S1, S2);
    Value *V1S2 = IRB.CreateAnd(V1, S2);
    Value *S1V2 = IRB.CreateAnd(S1, V2);
    setShadow(&I, IRB.CreateOr(S1S2, IRB.CreateOr(V1S2, S1V2)));
    setOriginForNaryOp(I);
  }

  /// \brief Default propagation of shadow and/or origin.
  ///
  /// This class implements the general case of shadow propagation, used in all
  /// cases where we don't know and/or don't care about what the operation
  /// actually does. It converts all input shadow values to a common type
  /// (extending or truncating as necessary), and bitwise OR's them.
  ///
  /// This is much cheaper than inserting checks (i.e. requiring inputs to be
  /// fully initialized), and less prone to false positives.
  ///
  /// This class also implements the general case of origin propagation. For a
  /// Nary operation, result origin is set to the origin of an argument that is
  /// not entirely initialized. If there is more than one such arguments, the
  /// rightmost of them is picked. It does not matter which one is picked if all
  /// arguments are initialized.
  template <bool CombineShadow>
  class Combiner {
    Value *Shadow;
    Value *Origin;
    IRBuilder<> &IRB;
    MemorySanitizerVisitor *MSV;

  public:
    Combiner(MemorySanitizerVisitor *MSV, IRBuilder<> &IRB) :
      Shadow(0), Origin(0), IRB(IRB), MSV(MSV) {}

    /// \brief Add a pair of shadow and origin values to the mix.
    Combiner &Add(Value *OpShadow, Value *OpOrigin) {
      if (CombineShadow) {
        assert(OpShadow);
        if (!Shadow)
          Shadow = OpShadow;
        else {
          OpShadow = MSV->CreateShadowCast(IRB, OpShadow, Shadow->getType());
          Shadow = IRB.CreateOr(Shadow, OpShadow, "_msprop");
        }
      }

      if (MSV->MS.TrackOrigins) {
        assert(OpOrigin);
        if (!Origin) {
          Origin = OpOrigin;
        } else {
          Value *FlatShadow = MSV->convertToShadowTyNoVec(OpShadow, IRB);
          Value *Cond = IRB.CreateICmpNE(FlatShadow,
                                         MSV->getCleanShadow(FlatShadow));
          Origin = IRB.CreateSelect(Cond, OpOrigin, Origin);
        }
      }
      return *this;
    }

    /// \brief Add an application value to the mix.
    Combiner &Add(Value *V) {
      Value *OpShadow = MSV->getShadow(V);
      Value *OpOrigin = MSV->MS.TrackOrigins ? MSV->getOrigin(V) : 0;
      return Add(OpShadow, OpOrigin);
    }

    /// \brief Set the current combined values as the given instruction's shadow
    /// and origin.
    void Done(Instruction *I) {
      if (CombineShadow) {
        assert(Shadow);
        Shadow = MSV->CreateShadowCast(IRB, Shadow, MSV->getShadowTy(I));
        MSV->setShadow(I, Shadow);
      }
      if (MSV->MS.TrackOrigins) {
        assert(Origin);
        MSV->setOrigin(I, Origin);
      }
    }
  };

  typedef Combiner<true> ShadowAndOriginCombiner;
  typedef Combiner<false> OriginCombiner;

  /// \brief Propagate origin for arbitrary operation.
  void setOriginForNaryOp(Instruction &I) {
    if (!MS.TrackOrigins) return;
    IRBuilder<> IRB(&I);
    OriginCombiner OC(this, IRB);
    for (Instruction::op_iterator OI = I.op_begin(); OI != I.op_end(); ++OI)
      OC.Add(OI->get());
    OC.Done(&I);
  }

  size_t VectorOrPrimitiveTypeSizeInBits(Type *Ty) {
    assert(!(Ty->isVectorTy() && Ty->getScalarType()->isPointerTy()) &&
           "Vector of pointers is not a valid shadow type");
    return Ty->isVectorTy() ?
      Ty->getVectorNumElements() * Ty->getScalarSizeInBits() :
      Ty->getPrimitiveSizeInBits();
  }

  /// \brief Cast between two shadow types, extending or truncating as
  /// necessary.
  Value *CreateShadowCast(IRBuilder<> &IRB, Value *V, Type *dstTy) {
    Type *srcTy = V->getType();
    if (dstTy->isIntegerTy() && srcTy->isIntegerTy())
      return IRB.CreateIntCast(V, dstTy, false);
    if (dstTy->isVectorTy() && srcTy->isVectorTy() &&
        dstTy->getVectorNumElements() == srcTy->getVectorNumElements())
      return IRB.CreateIntCast(V, dstTy, false);
    size_t srcSizeInBits = VectorOrPrimitiveTypeSizeInBits(srcTy);
    size_t dstSizeInBits = VectorOrPrimitiveTypeSizeInBits(dstTy);
    Value *V1 = IRB.CreateBitCast(V, Type::getIntNTy(*MS.C, srcSizeInBits));
    Value *V2 =
      IRB.CreateIntCast(V1, Type::getIntNTy(*MS.C, dstSizeInBits), false);
    return IRB.CreateBitCast(V2, dstTy);
    // TODO: handle struct types.
  }

  /// \brief Propagate shadow for arbitrary operation.
  void handleShadowOr(Instruction &I) {
    IRBuilder<> IRB(&I);
    ShadowAndOriginCombiner SC(this, IRB);
    for (Instruction::op_iterator OI = I.op_begin(); OI != I.op_end(); ++OI)
      SC.Add(OI->get());
    SC.Done(&I);
  }

  void visitFAdd(BinaryOperator &I) { handleShadowOr(I); }
  void visitFSub(BinaryOperator &I) { handleShadowOr(I); }
  void visitFMul(BinaryOperator &I) { handleShadowOr(I); }
  void visitAdd(BinaryOperator &I) { handleShadowOr(I); }
  void visitSub(BinaryOperator &I) { handleShadowOr(I); }
  void visitXor(BinaryOperator &I) { handleShadowOr(I); }
  void visitMul(BinaryOperator &I) { handleShadowOr(I); }

  void handleDiv(Instruction &I) {
    IRBuilder<> IRB(&I);
    // Strict on the second argument.
    insertCheck(I.getOperand(1), &I);
    setShadow(&I, getShadow(&I, 0));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitUDiv(BinaryOperator &I) { handleDiv(I); }
  void visitSDiv(BinaryOperator &I) { handleDiv(I); }
  void visitFDiv(BinaryOperator &I) { handleDiv(I); }
  void visitURem(BinaryOperator &I) { handleDiv(I); }
  void visitSRem(BinaryOperator &I) { handleDiv(I); }
  void visitFRem(BinaryOperator &I) { handleDiv(I); }

  /// \brief Instrument == and != comparisons.
  ///
  /// Sometimes the comparison result is known even if some of the bits of the
  /// arguments are not.
  void handleEqualityComparison(ICmpInst &I) {
    IRBuilder<> IRB(&I);
    Value *A = I.getOperand(0);
    Value *B = I.getOperand(1);
    Value *Sa = getShadow(A);
    Value *Sb = getShadow(B);

    // Get rid of pointers and vectors of pointers.
    // For ints (and vectors of ints), types of A and Sa match,
    // and this is a no-op.
    A = IRB.CreatePointerCast(A, Sa->getType());
    B = IRB.CreatePointerCast(B, Sb->getType());

    // A == B  <==>  (C = A^B) == 0
    // A != B  <==>  (C = A^B) != 0
    // Sc = Sa | Sb
    Value *C = IRB.CreateXor(A, B);
    Value *Sc = IRB.CreateOr(Sa, Sb);
    // Now dealing with i = (C == 0) comparison (or C != 0, does not matter now)
    // Result is defined if one of the following is true
    // * there is a defined 1 bit in C
    // * C is fully defined
    // Si = !(C & ~Sc) && Sc
    Value *Zero = Constant::getNullValue(Sc->getType());
    Value *MinusOne = Constant::getAllOnesValue(Sc->getType());
    Value *Si =
      IRB.CreateAnd(IRB.CreateICmpNE(Sc, Zero),
                    IRB.CreateICmpEQ(
                      IRB.CreateAnd(IRB.CreateXor(Sc, MinusOne), C), Zero));
    Si->setName("_msprop_icmp");
    setShadow(&I, Si);
    setOriginForNaryOp(I);
  }

  /// \brief Build the lowest possible value of V, taking into account V's
  ///        uninitialized bits.
  Value *getLowestPossibleValue(IRBuilder<> &IRB, Value *A, Value *Sa,
                                bool isSigned) {
    if (isSigned) {
      // Split shadow into sign bit and other bits.
      Value *SaOtherBits = IRB.CreateLShr(IRB.CreateShl(Sa, 1), 1);
      Value *SaSignBit = IRB.CreateXor(Sa, SaOtherBits);
      // Maximise the undefined shadow bit, minimize other undefined bits.
      return
        IRB.CreateOr(IRB.CreateAnd(A, IRB.CreateNot(SaOtherBits)), SaSignBit);
    } else {
      // Minimize undefined bits.
      return IRB.CreateAnd(A, IRB.CreateNot(Sa));
    }
  }

  /// \brief Build the highest possible value of V, taking into account V's
  ///        uninitialized bits.
  Value *getHighestPossibleValue(IRBuilder<> &IRB, Value *A, Value *Sa,
                                bool isSigned) {
    if (isSigned) {
      // Split shadow into sign bit and other bits.
      Value *SaOtherBits = IRB.CreateLShr(IRB.CreateShl(Sa, 1), 1);
      Value *SaSignBit = IRB.CreateXor(Sa, SaOtherBits);
      // Minimise the undefined shadow bit, maximise other undefined bits.
      return
        IRB.CreateOr(IRB.CreateAnd(A, IRB.CreateNot(SaSignBit)), SaOtherBits);
    } else {
      // Maximize undefined bits.
      return IRB.CreateOr(A, Sa);
    }
  }

  /// \brief Instrument relational comparisons.
  ///
  /// This function does exact shadow propagation for all relational
  /// comparisons of integers, pointers and vectors of those.
  /// FIXME: output seems suboptimal when one of the operands is a constant
  void handleRelationalComparisonExact(ICmpInst &I) {
    IRBuilder<> IRB(&I);
    Value *A = I.getOperand(0);
    Value *B = I.getOperand(1);
    Value *Sa = getShadow(A);
    Value *Sb = getShadow(B);

    // Get rid of pointers and vectors of pointers.
    // For ints (and vectors of ints), types of A and Sa match,
    // and this is a no-op.
    A = IRB.CreatePointerCast(A, Sa->getType());
    B = IRB.CreatePointerCast(B, Sb->getType());

    // Let [a0, a1] be the interval of possible values of A, taking into account
    // its undefined bits. Let [b0, b1] be the interval of possible values of B.
    // Then (A cmp B) is defined iff (a0 cmp b1) == (a1 cmp b0).
    bool IsSigned = I.isSigned();
    Value *S1 = IRB.CreateICmp(I.getPredicate(),
                               getLowestPossibleValue(IRB, A, Sa, IsSigned),
                               getHighestPossibleValue(IRB, B, Sb, IsSigned));
    Value *S2 = IRB.CreateICmp(I.getPredicate(),
                               getHighestPossibleValue(IRB, A, Sa, IsSigned),
                               getLowestPossibleValue(IRB, B, Sb, IsSigned));
    Value *Si = IRB.CreateXor(S1, S2);
    setShadow(&I, Si);
    setOriginForNaryOp(I);
  }

  /// \brief Instrument signed relational comparisons.
  ///
  /// Handle (x<0) and (x>=0) comparisons (essentially, sign bit tests) by
  /// propagating the highest bit of the shadow. Everything else is delegated
  /// to handleShadowOr().
  void handleSignedRelationalComparison(ICmpInst &I) {
    Constant *constOp0 = dyn_cast<Constant>(I.getOperand(0));
    Constant *constOp1 = dyn_cast<Constant>(I.getOperand(1));
    Value* op = NULL;
    CmpInst::Predicate pre = I.getPredicate();
    if (constOp0 && constOp0->isNullValue() &&
        (pre == CmpInst::ICMP_SGT || pre == CmpInst::ICMP_SLE)) {
      op = I.getOperand(1);
    } else if (constOp1 && constOp1->isNullValue() &&
               (pre == CmpInst::ICMP_SLT || pre == CmpInst::ICMP_SGE)) {
      op = I.getOperand(0);
    }
    if (op) {
      IRBuilder<> IRB(&I);
      Value* Shadow =
        IRB.CreateICmpSLT(getShadow(op), getCleanShadow(op), "_msprop_icmpslt");
      setShadow(&I, Shadow);
      setOrigin(&I, getOrigin(op));
    } else {
      handleShadowOr(I);
    }
  }

  void visitICmpInst(ICmpInst &I) {
    if (!ClHandleICmp) {
      handleShadowOr(I);
      return;
    }
    if (I.isEquality()) {
      handleEqualityComparison(I);
      return;
    }

    assert(I.isRelational());
    if (ClHandleICmpExact) {
      handleRelationalComparisonExact(I);
      return;
    }
    if (I.isSigned()) {
      handleSignedRelationalComparison(I);
      return;
    }

    assert(I.isUnsigned());
    if ((isa<Constant>(I.getOperand(0)) || isa<Constant>(I.getOperand(1)))) {
      handleRelationalComparisonExact(I);
      return;
    }

    handleShadowOr(I);
  }

  void visitFCmpInst(FCmpInst &I) {
    handleShadowOr(I);
  }

  void handleShift(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    // If any of the S2 bits are poisoned, the whole thing is poisoned.
    // Otherwise perform the same shift on S1.
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *S2Conv = IRB.CreateSExt(IRB.CreateICmpNE(S2, getCleanShadow(S2)),
                                   S2->getType());
    Value *V2 = I.getOperand(1);
    Value *Shift = IRB.CreateBinOp(I.getOpcode(), S1, V2);
    setShadow(&I, IRB.CreateOr(Shift, S2Conv));
    setOriginForNaryOp(I);
  }

  void visitShl(BinaryOperator &I) { handleShift(I); }
  void visitAShr(BinaryOperator &I) { handleShift(I); }
  void visitLShr(BinaryOperator &I) { handleShift(I); }

  /// \brief Instrument llvm.memmove
  ///
  /// At this point we don't know if llvm.memmove will be inlined or not.
  /// If we don't instrument it and it gets inlined,
  /// our interceptor will not kick in and we will lose the memmove.
  /// If we instrument the call here, but it does not get inlined,
  /// we will memove the shadow twice: which is bad in case
  /// of overlapping regions. So, we simply lower the intrinsic to a call.
  ///
  /// Similar situation exists for memcpy and memset.
  void visitMemMoveInst(MemMoveInst &I) {
    IRBuilder<> IRB(&I);
    IRB.CreateCall3(
      MS.MemmoveFn,
      IRB.CreatePointerCast(I.getArgOperand(0), IRB.getInt8PtrTy()),
      IRB.CreatePointerCast(I.getArgOperand(1), IRB.getInt8PtrTy()),
      IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false));
    I.eraseFromParent();
  }

  // Similar to memmove: avoid copying shadow twice.
  // This is somewhat unfortunate as it may slowdown small constant memcpys.
  // FIXME: consider doing manual inline for small constant sizes and proper
  // alignment.
  void visitMemCpyInst(MemCpyInst &I) {
    IRBuilder<> IRB(&I);
    IRB.CreateCall3(
      MS.MemcpyFn,
      IRB.CreatePointerCast(I.getArgOperand(0), IRB.getInt8PtrTy()),
      IRB.CreatePointerCast(I.getArgOperand(1), IRB.getInt8PtrTy()),
      IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false));
    I.eraseFromParent();
  }

  // Same as memcpy.
  void visitMemSetInst(MemSetInst &I) {
    IRBuilder<> IRB(&I);
    IRB.CreateCall3(
      MS.MemsetFn,
      IRB.CreatePointerCast(I.getArgOperand(0), IRB.getInt8PtrTy()),
      IRB.CreateIntCast(I.getArgOperand(1), IRB.getInt32Ty(), false),
      IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false));
    I.eraseFromParent();
  }

  void visitVAStartInst(VAStartInst &I) {
    VAHelper->visitVAStartInst(I);
  }

  void visitVACopyInst(VACopyInst &I) {
    VAHelper->visitVACopyInst(I);
  }

  enum IntrinsicKind {
    IK_DoesNotAccessMemory,
    IK_OnlyReadsMemory,
    IK_WritesMemory
  };

  static IntrinsicKind getIntrinsicKind(Intrinsic::ID iid) {
    const int DoesNotAccessMemory = IK_DoesNotAccessMemory;
    const int OnlyReadsArgumentPointees = IK_OnlyReadsMemory;
    const int OnlyReadsMemory = IK_OnlyReadsMemory;
    const int OnlyAccessesArgumentPointees = IK_WritesMemory;
    const int UnknownModRefBehavior = IK_WritesMemory;
#define GET_INTRINSIC_MODREF_BEHAVIOR
#define ModRefBehavior IntrinsicKind
#include "llvm/IR/Intrinsics.gen"
#undef ModRefBehavior
#undef GET_INTRINSIC_MODREF_BEHAVIOR
  }

  /// \brief Handle vector store-like intrinsics.
  ///
  /// Instrument intrinsics that look like a simple SIMD store: writes memory,
  /// has 1 pointer argument and 1 vector argument, returns void.
  bool handleVectorStoreIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value* Addr = I.getArgOperand(0);
    Value *Shadow = getShadow(&I, 1);
    Value *ShadowPtr = getShadowPtr(Addr, Shadow->getType(), IRB);

    // We don't know the pointer alignment (could be unaligned SSE store!).
    // Have to assume to worst case.
    IRB.CreateAlignedStore(Shadow, ShadowPtr, 1);

    if (ClCheckAccessAddress)
      insertCheck(Addr, &I);

    // FIXME: use ClStoreCleanOrigin
    // FIXME: factor out common code from materializeStores
    if (MS.TrackOrigins)
      IRB.CreateStore(getOrigin(&I, 1), getOriginPtr(Addr, IRB));
    return true;
  }

  /// \brief Handle vector load-like intrinsics.
  ///
  /// Instrument intrinsics that look like a simple SIMD load: reads memory,
  /// has 1 pointer argument, returns a vector.
  bool handleVectorLoadIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Addr = I.getArgOperand(0);

    Type *ShadowTy = getShadowTy(&I);
    if (LoadShadow) {
      Value *ShadowPtr = getShadowPtr(Addr, ShadowTy, IRB);
      // We don't know the pointer alignment (could be unaligned SSE load!).
      // Have to assume to worst case.
      setShadow(&I, IRB.CreateAlignedLoad(ShadowPtr, 1, "_msld"));
    } else {
      setShadow(&I, getCleanShadow(&I));
    }


    if (ClCheckAccessAddress)
      insertCheck(Addr, &I);

    if (MS.TrackOrigins) {
      if (LoadShadow)
        setOrigin(&I, IRB.CreateLoad(getOriginPtr(Addr, IRB)));
      else
        setOrigin(&I, getCleanOrigin());
    }
    return true;
  }

  /// \brief Handle (SIMD arithmetic)-like intrinsics.
  ///
  /// Instrument intrinsics with any number of arguments of the same type,
  /// equal to the return type. The type should be simple (no aggregates or
  /// pointers; vectors are fine).
  /// Caller guarantees that this intrinsic does not access memory.
  bool maybeHandleSimpleNomemIntrinsic(IntrinsicInst &I) {
    Type *RetTy = I.getType();
    if (!(RetTy->isIntOrIntVectorTy() ||
          RetTy->isFPOrFPVectorTy() ||
          RetTy->isX86_MMXTy()))
      return false;

    unsigned NumArgOperands = I.getNumArgOperands();

    for (unsigned i = 0; i < NumArgOperands; ++i) {
      Type *Ty = I.getArgOperand(i)->getType();
      if (Ty != RetTy)
        return false;
    }

    IRBuilder<> IRB(&I);
    ShadowAndOriginCombiner SC(this, IRB);
    for (unsigned i = 0; i < NumArgOperands; ++i)
      SC.Add(I.getArgOperand(i));
    SC.Done(&I);

    return true;
  }

  /// \brief Heuristically instrument unknown intrinsics.
  ///
  /// The main purpose of this code is to do something reasonable with all
  /// random intrinsics we might encounter, most importantly - SIMD intrinsics.
  /// We recognize several classes of intrinsics by their argument types and
  /// ModRefBehaviour and apply special intrumentation when we are reasonably
  /// sure that we know what the intrinsic does.
  ///
  /// We special-case intrinsics where this approach fails. See llvm.bswap
  /// handling as an example of that.
  bool handleUnknownIntrinsic(IntrinsicInst &I) {
    unsigned NumArgOperands = I.getNumArgOperands();
    if (NumArgOperands == 0)
      return false;

    Intrinsic::ID iid = I.getIntrinsicID();
    IntrinsicKind IK = getIntrinsicKind(iid);
    bool OnlyReadsMemory = IK == IK_OnlyReadsMemory;
    bool WritesMemory = IK == IK_WritesMemory;
    assert(!(OnlyReadsMemory && WritesMemory));

    if (NumArgOperands == 2 &&
        I.getArgOperand(0)->getType()->isPointerTy() &&
        I.getArgOperand(1)->getType()->isVectorTy() &&
        I.getType()->isVoidTy() &&
        WritesMemory) {
      // This looks like a vector store.
      return handleVectorStoreIntrinsic(I);
    }

    if (NumArgOperands == 1 &&
        I.getArgOperand(0)->getType()->isPointerTy() &&
        I.getType()->isVectorTy() &&
        OnlyReadsMemory) {
      // This looks like a vector load.
      return handleVectorLoadIntrinsic(I);
    }

    if (!OnlyReadsMemory && !WritesMemory)
      if (maybeHandleSimpleNomemIntrinsic(I))
        return true;

    // FIXME: detect and handle SSE maskstore/maskload
    return false;
  }

  void handleBswap(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Op = I.getArgOperand(0);
    Type *OpType = Op->getType();
    Function *BswapFunc = Intrinsic::getDeclaration(
      F.getParent(), Intrinsic::bswap, ArrayRef<Type*>(&OpType, 1));
    setShadow(&I, IRB.CreateCall(BswapFunc, getShadow(Op)));
    setOrigin(&I, getOrigin(Op));
  }

  void visitIntrinsicInst(IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case llvm::Intrinsic::bswap:
      handleBswap(I);
      break;
    default:
      if (!handleUnknownIntrinsic(I))
        visitInstruction(I);
      break;
    }
  }

  void visitCallSite(CallSite CS) {
    Instruction &I = *CS.getInstruction();
    assert((CS.isCall() || CS.isInvoke()) && "Unknown type of CallSite");
    if (CS.isCall()) {
      CallInst *Call = cast<CallInst>(&I);

      // For inline asm, do the usual thing: check argument shadow and mark all
      // outputs as clean. Note that any side effects of the inline asm that are
      // not immediately visible in its constraints are not handled.
      if (Call->isInlineAsm()) {
        visitInstruction(I);
        return;
      }

      // Allow only tail calls with the same types, otherwise
      // we may have a false positive: shadow for a non-void RetVal
      // will get propagated to a void RetVal.
      if (Call->isTailCall() && Call->getType() != Call->getParent()->getType())
        Call->setTailCall(false);

      assert(!isa<IntrinsicInst>(&I) && "intrinsics are handled elsewhere");

      // We are going to insert code that relies on the fact that the callee
      // will become a non-readonly function after it is instrumented by us. To
      // prevent this code from being optimized out, mark that function
      // non-readonly in advance.
      if (Function *Func = Call->getCalledFunction()) {
        // Clear out readonly/readnone attributes.
        AttrBuilder B;
        B.addAttribute(Attribute::ReadOnly)
          .addAttribute(Attribute::ReadNone);
        Func->removeAttributes(AttributeSet::FunctionIndex,
                               AttributeSet::get(Func->getContext(),
                                                 AttributeSet::FunctionIndex,
                                                 B));
      }
    }
    IRBuilder<> IRB(&I);
    unsigned ArgOffset = 0;
    DEBUG(dbgs() << "  CallSite: " << I << "\n");
    for (CallSite::arg_iterator ArgIt = CS.arg_begin(), End = CS.arg_end();
         ArgIt != End; ++ArgIt) {
      Value *A = *ArgIt;
      unsigned i = ArgIt - CS.arg_begin();
      if (!A->getType()->isSized()) {
        DEBUG(dbgs() << "Arg " << i << " is not sized: " << I << "\n");
        continue;
      }
      unsigned Size = 0;
      Value *Store = 0;
      // Compute the Shadow for arg even if it is ByVal, because
      // in that case getShadow() will copy the actual arg shadow to
      // __msan_param_tls.
      Value *ArgShadow = getShadow(A);
      Value *ArgShadowBase = getShadowPtrForArgument(A, IRB, ArgOffset);
      DEBUG(dbgs() << "  Arg#" << i << ": " << *A <<
            " Shadow: " << *ArgShadow << "\n");
      if (CS.paramHasAttr(i + 1, Attribute::ByVal)) {
        assert(A->getType()->isPointerTy() &&
               "ByVal argument is not a pointer!");
        Size = MS.TD->getTypeAllocSize(A->getType()->getPointerElementType());
        unsigned Alignment = CS.getParamAlignment(i + 1);
        Store = IRB.CreateMemCpy(ArgShadowBase,
                                 getShadowPtr(A, Type::getInt8Ty(*MS.C), IRB),
                                 Size, Alignment);
      } else {
        Size = MS.TD->getTypeAllocSize(A->getType());
        Store = IRB.CreateAlignedStore(ArgShadow, ArgShadowBase,
                                       kShadowTLSAlignment);
      }
      if (MS.TrackOrigins)
        IRB.CreateStore(getOrigin(A),
                        getOriginPtrForArgument(A, IRB, ArgOffset));
      (void)Store;
      assert(Size != 0 && Store != 0);
      DEBUG(dbgs() << "  Param:" << *Store << "\n");
      ArgOffset += DataLayout::RoundUpAlignment(Size, 8);
    }
    DEBUG(dbgs() << "  done with call args\n");

    FunctionType *FT =
      cast<FunctionType>(CS.getCalledValue()->getType()-> getContainedType(0));
    if (FT->isVarArg()) {
      VAHelper->visitCallSite(CS, IRB);
    }

    // Now, get the shadow for the RetVal.
    if (!I.getType()->isSized()) return;
    IRBuilder<> IRBBefore(&I);
    // Untill we have full dynamic coverage, make sure the retval shadow is 0.
    Value *Base = getShadowPtrForRetval(&I, IRBBefore);
    IRBBefore.CreateAlignedStore(getCleanShadow(&I), Base, kShadowTLSAlignment);
    Instruction *NextInsn = 0;
    if (CS.isCall()) {
      NextInsn = I.getNextNode();
    } else {
      BasicBlock *NormalDest = cast<InvokeInst>(&I)->getNormalDest();
      if (!NormalDest->getSinglePredecessor()) {
        // FIXME: this case is tricky, so we are just conservative here.
        // Perhaps we need to split the edge between this BB and NormalDest,
        // but a naive attempt to use SplitEdge leads to a crash.
        setShadow(&I, getCleanShadow(&I));
        setOrigin(&I, getCleanOrigin());
        return;
      }
      NextInsn = NormalDest->getFirstInsertionPt();
      assert(NextInsn &&
             "Could not find insertion point for retval shadow load");
    }
    IRBuilder<> IRBAfter(NextInsn);
    Value *RetvalShadow =
      IRBAfter.CreateAlignedLoad(getShadowPtrForRetval(&I, IRBAfter),
                                 kShadowTLSAlignment, "_msret");
    setShadow(&I, RetvalShadow);
    if (MS.TrackOrigins)
      setOrigin(&I, IRBAfter.CreateLoad(getOriginPtrForRetval(IRBAfter)));
  }

  void visitReturnInst(ReturnInst &I) {
    IRBuilder<> IRB(&I);
    if (Value *RetVal = I.getReturnValue()) {
      // Set the shadow for the RetVal.
      Value *Shadow = getShadow(RetVal);
      Value *ShadowPtr = getShadowPtrForRetval(RetVal, IRB);
      DEBUG(dbgs() << "Return: " << *Shadow << "\n" << *ShadowPtr << "\n");
      IRB.CreateAlignedStore(Shadow, ShadowPtr, kShadowTLSAlignment);
      if (MS.TrackOrigins)
        IRB.CreateStore(getOrigin(RetVal), getOriginPtrForRetval(IRB));
    }
  }

  void visitPHINode(PHINode &I) {
    IRBuilder<> IRB(&I);
    ShadowPHINodes.push_back(&I);
    setShadow(&I, IRB.CreatePHI(getShadowTy(&I), I.getNumIncomingValues(),
                                "_msphi_s"));
    if (MS.TrackOrigins)
      setOrigin(&I, IRB.CreatePHI(MS.OriginTy, I.getNumIncomingValues(),
                                  "_msphi_o"));
  }

  void visitAllocaInst(AllocaInst &I) {
    setShadow(&I, getCleanShadow(&I));
    IRBuilder<> IRB(I.getNextNode());
    uint64_t Size = MS.TD->getTypeAllocSize(I.getAllocatedType());
    if (PoisonStack && ClPoisonStackWithCall) {
      IRB.CreateCall2(MS.MsanPoisonStackFn,
                      IRB.CreatePointerCast(&I, IRB.getInt8PtrTy()),
                      ConstantInt::get(MS.IntptrTy, Size));
    } else {
      Value *ShadowBase = getShadowPtr(&I, Type::getInt8PtrTy(*MS.C), IRB);
      Value *PoisonValue = IRB.getInt8(PoisonStack ? ClPoisonStackPattern : 0);
      IRB.CreateMemSet(ShadowBase, PoisonValue, Size, I.getAlignment());
    }

    if (PoisonStack && MS.TrackOrigins) {
      setOrigin(&I, getCleanOrigin());
      SmallString<2048> StackDescriptionStorage;
      raw_svector_ostream StackDescription(StackDescriptionStorage);
      // We create a string with a description of the stack allocation and
      // pass it into __msan_set_alloca_origin.
      // It will be printed by the run-time if stack-originated UMR is found.
      // The first 4 bytes of the string are set to '----' and will be replaced
      // by __msan_va_arg_overflow_size_tls at the first call.
      StackDescription << "----" << I.getName() << "@" << F.getName();
      Value *Descr =
          createPrivateNonConstGlobalForString(*F.getParent(),
                                               StackDescription.str());
      IRB.CreateCall3(MS.MsanSetAllocaOriginFn,
                      IRB.CreatePointerCast(&I, IRB.getInt8PtrTy()),
                      ConstantInt::get(MS.IntptrTy, Size),
                      IRB.CreatePointerCast(Descr, IRB.getInt8PtrTy()));
    }
  }

  void visitSelectInst(SelectInst& I) {
    IRBuilder<> IRB(&I);
    setShadow(&I,  IRB.CreateSelect(I.getCondition(),
              getShadow(I.getTrueValue()), getShadow(I.getFalseValue()),
              "_msprop"));
    if (MS.TrackOrigins) {
      // Origins are always i32, so any vector conditions must be flattened.
      // FIXME: consider tracking vector origins for app vectors?
      Value *Cond = I.getCondition();
      if (Cond->getType()->isVectorTy()) {
        Value *ConvertedShadow = convertToShadowTyNoVec(Cond, IRB);
        Cond = IRB.CreateICmpNE(ConvertedShadow,
                                getCleanShadow(ConvertedShadow), "_mso_select");
      }
      setOrigin(&I, IRB.CreateSelect(Cond,
                getOrigin(I.getTrueValue()), getOrigin(I.getFalseValue())));
    }
  }

  void visitLandingPadInst(LandingPadInst &I) {
    // Do nothing.
    // See http://code.google.com/p/memory-sanitizer/issues/detail?id=1
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    handleShadowOr(I);
  }

  void visitExtractValueInst(ExtractValueInst &I) {
    IRBuilder<> IRB(&I);
    Value *Agg = I.getAggregateOperand();
    DEBUG(dbgs() << "ExtractValue:  " << I << "\n");
    Value *AggShadow = getShadow(Agg);
    DEBUG(dbgs() << "   AggShadow:  " << *AggShadow << "\n");
    Value *ResShadow = IRB.CreateExtractValue(AggShadow, I.getIndices());
    DEBUG(dbgs() << "   ResShadow:  " << *ResShadow << "\n");
    setShadow(&I, ResShadow);
    setOrigin(&I, getCleanOrigin());
  }

  void visitInsertValueInst(InsertValueInst &I) {
    IRBuilder<> IRB(&I);
    DEBUG(dbgs() << "InsertValue:  " << I << "\n");
    Value *AggShadow = getShadow(I.getAggregateOperand());
    Value *InsShadow = getShadow(I.getInsertedValueOperand());
    DEBUG(dbgs() << "   AggShadow:  " << *AggShadow << "\n");
    DEBUG(dbgs() << "   InsShadow:  " << *InsShadow << "\n");
    Value *Res = IRB.CreateInsertValue(AggShadow, InsShadow, I.getIndices());
    DEBUG(dbgs() << "   Res:        " << *Res << "\n");
    setShadow(&I, Res);
    setOrigin(&I, getCleanOrigin());
  }

  void dumpInst(Instruction &I) {
    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      errs() << "ZZZ call " << CI->getCalledFunction()->getName() << "\n";
    } else {
      errs() << "ZZZ " << I.getOpcodeName() << "\n";
    }
    errs() << "QQQ " << I << "\n";
  }

  void visitResumeInst(ResumeInst &I) {
    DEBUG(dbgs() << "Resume: " << I << "\n");
    // Nothing to do here.
  }

  void visitInstruction(Instruction &I) {
    // Everything else: stop propagating and check for poisoned shadow.
    if (ClDumpStrictInstructions)
      dumpInst(I);
    DEBUG(dbgs() << "DEFAULT: " << I << "\n");
    for (size_t i = 0, n = I.getNumOperands(); i < n; i++)
      insertCheck(I.getOperand(i), &I);
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }
};

/// \brief AMD64-specific implementation of VarArgHelper.
struct VarArgAMD64Helper : public VarArgHelper {
  // An unfortunate workaround for asymmetric lowering of va_arg stuff.
  // See a comment in visitCallSite for more details.
  static const unsigned AMD64GpEndOffset = 48;  // AMD64 ABI Draft 0.99.6 p3.5.7
  static const unsigned AMD64FpEndOffset = 176;

  Function &F;
  MemorySanitizer &MS;
  MemorySanitizerVisitor &MSV;
  Value *VAArgTLSCopy;
  Value *VAArgOverflowSize;

  SmallVector<CallInst*, 16> VAStartInstrumentationList;

  VarArgAMD64Helper(Function &F, MemorySanitizer &MS,
                    MemorySanitizerVisitor &MSV)
    : F(F), MS(MS), MSV(MSV), VAArgTLSCopy(0), VAArgOverflowSize(0) { }

  enum ArgKind { AK_GeneralPurpose, AK_FloatingPoint, AK_Memory };

  ArgKind classifyArgument(Value* arg) {
    // A very rough approximation of X86_64 argument classification rules.
    Type *T = arg->getType();
    if (T->isFPOrFPVectorTy() || T->isX86_MMXTy())
      return AK_FloatingPoint;
    if (T->isIntegerTy() && T->getPrimitiveSizeInBits() <= 64)
      return AK_GeneralPurpose;
    if (T->isPointerTy())
      return AK_GeneralPurpose;
    return AK_Memory;
  }

  // For VarArg functions, store the argument shadow in an ABI-specific format
  // that corresponds to va_list layout.
  // We do this because Clang lowers va_arg in the frontend, and this pass
  // only sees the low level code that deals with va_list internals.
  // A much easier alternative (provided that Clang emits va_arg instructions)
  // would have been to associate each live instance of va_list with a copy of
  // MSanParamTLS, and extract shadow on va_arg() call in the argument list
  // order.
  void visitCallSite(CallSite &CS, IRBuilder<> &IRB) {
    unsigned GpOffset = 0;
    unsigned FpOffset = AMD64GpEndOffset;
    unsigned OverflowOffset = AMD64FpEndOffset;
    for (CallSite::arg_iterator ArgIt = CS.arg_begin(), End = CS.arg_end();
         ArgIt != End; ++ArgIt) {
      Value *A = *ArgIt;
      ArgKind AK = classifyArgument(A);
      if (AK == AK_GeneralPurpose && GpOffset >= AMD64GpEndOffset)
        AK = AK_Memory;
      if (AK == AK_FloatingPoint && FpOffset >= AMD64FpEndOffset)
        AK = AK_Memory;
      Value *Base;
      switch (AK) {
      case AK_GeneralPurpose:
        Base = getShadowPtrForVAArgument(A, IRB, GpOffset);
        GpOffset += 8;
        break;
      case AK_FloatingPoint:
        Base = getShadowPtrForVAArgument(A, IRB, FpOffset);
        FpOffset += 16;
        break;
      case AK_Memory:
        uint64_t ArgSize = MS.TD->getTypeAllocSize(A->getType());
        Base = getShadowPtrForVAArgument(A, IRB, OverflowOffset);
        OverflowOffset += DataLayout::RoundUpAlignment(ArgSize, 8);
      }
      IRB.CreateAlignedStore(MSV.getShadow(A), Base, kShadowTLSAlignment);
    }
    Constant *OverflowSize =
      ConstantInt::get(IRB.getInt64Ty(), OverflowOffset - AMD64FpEndOffset);
    IRB.CreateStore(OverflowSize, MS.VAArgOverflowSizeTLS);
  }

  /// \brief Compute the shadow address for a given va_arg.
  Value *getShadowPtrForVAArgument(Value *A, IRBuilder<> &IRB,
                                   int ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.VAArgTLS, MS.IntptrTy);
    Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, PointerType::get(MSV.getShadowTy(A), 0),
                              "_msarg");
  }

  void visitVAStartInst(VAStartInst &I) {
    IRBuilder<> IRB(&I);
    VAStartInstrumentationList.push_back(&I);
    Value *VAListTag = I.getArgOperand(0);
    Value *ShadowPtr = MSV.getShadowPtr(VAListTag, IRB.getInt8Ty(), IRB);

    // Unpoison the whole __va_list_tag.
    // FIXME: magic ABI constants.
    IRB.CreateMemSet(ShadowPtr, Constant::getNullValue(IRB.getInt8Ty()),
                     /* size */24, /* alignment */8, false);
  }

  void visitVACopyInst(VACopyInst &I) {
    IRBuilder<> IRB(&I);
    Value *VAListTag = I.getArgOperand(0);
    Value *ShadowPtr = MSV.getShadowPtr(VAListTag, IRB.getInt8Ty(), IRB);

    // Unpoison the whole __va_list_tag.
    // FIXME: magic ABI constants.
    IRB.CreateMemSet(ShadowPtr, Constant::getNullValue(IRB.getInt8Ty()),
                     /* size */24, /* alignment */8, false);
  }

  void finalizeInstrumentation() {
    assert(!VAArgOverflowSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.
      IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
      VAArgOverflowSize = IRB.CreateLoad(MS.VAArgOverflowSizeTLS);
      Value *CopySize =
        IRB.CreateAdd(ConstantInt::get(MS.IntptrTy, AMD64FpEndOffset),
                      VAArgOverflowSize);
      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      IRB.CreateMemCpy(VAArgTLSCopy, MS.VAArgTLS, CopySize, 8);
    }

    // Instrument va_start.
    // Copy va_list shadow from the backup copy of the TLS contents.
    for (size_t i = 0, n = VAStartInstrumentationList.size(); i < n; i++) {
      CallInst *OrigInst = VAStartInstrumentationList[i];
      IRBuilder<> IRB(OrigInst->getNextNode());
      Value *VAListTag = OrigInst->getArgOperand(0);

      Value *RegSaveAreaPtrPtr =
        IRB.CreateIntToPtr(
          IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                        ConstantInt::get(MS.IntptrTy, 16)),
          Type::getInt64PtrTy(*MS.C));
      Value *RegSaveAreaPtr = IRB.CreateLoad(RegSaveAreaPtrPtr);
      Value *RegSaveAreaShadowPtr =
        MSV.getShadowPtr(RegSaveAreaPtr, IRB.getInt8Ty(), IRB);
      IRB.CreateMemCpy(RegSaveAreaShadowPtr, VAArgTLSCopy,
                       AMD64FpEndOffset, 16);

      Value *OverflowArgAreaPtrPtr =
        IRB.CreateIntToPtr(
          IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                        ConstantInt::get(MS.IntptrTy, 8)),
          Type::getInt64PtrTy(*MS.C));
      Value *OverflowArgAreaPtr = IRB.CreateLoad(OverflowArgAreaPtrPtr);
      Value *OverflowArgAreaShadowPtr =
        MSV.getShadowPtr(OverflowArgAreaPtr, IRB.getInt8Ty(), IRB);
      Value *SrcPtr =
        getShadowPtrForVAArgument(VAArgTLSCopy, IRB, AMD64FpEndOffset);
      IRB.CreateMemCpy(OverflowArgAreaShadowPtr, SrcPtr, VAArgOverflowSize, 16);
    }
  }
};

/// \brief A no-op implementation of VarArgHelper.
struct VarArgNoOpHelper : public VarArgHelper {
  VarArgNoOpHelper(Function &F, MemorySanitizer &MS,
                   MemorySanitizerVisitor &MSV) {}

  void visitCallSite(CallSite &CS, IRBuilder<> &IRB) {}

  void visitVAStartInst(VAStartInst &I) {}

  void visitVACopyInst(VACopyInst &I) {}

  void finalizeInstrumentation() {}
};

VarArgHelper *CreateVarArgHelper(Function &Func, MemorySanitizer &Msan,
                                 MemorySanitizerVisitor &Visitor) {
  // VarArg handling is only implemented on AMD64. False positives are possible
  // on other platforms.
  llvm::Triple TargetTriple(Func.getParent()->getTargetTriple());
  if (TargetTriple.getArch() == llvm::Triple::x86_64)
    return new VarArgAMD64Helper(Func, Msan, Visitor);
  else
    return new VarArgNoOpHelper(Func, Msan, Visitor);
}

}  // namespace

bool MemorySanitizer::runOnFunction(Function &F) {
  MemorySanitizerVisitor Visitor(F, *this);

  // Clear out readonly/readnone attributes.
  AttrBuilder B;
  B.addAttribute(Attribute::ReadOnly)
    .addAttribute(Attribute::ReadNone);
  F.removeAttributes(AttributeSet::FunctionIndex,
                     AttributeSet::get(F.getContext(),
                                       AttributeSet::FunctionIndex, B));

  return Visitor.runOnFunction();
}
