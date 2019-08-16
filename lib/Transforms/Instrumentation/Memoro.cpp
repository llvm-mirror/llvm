//===-- Memoro.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Memoro, a tool to detect inefficient heap
// usage patterns. This instrumentation pass was adapted from the
// EfficiencySanitizer instrumentation.
//
// The instrumentation phase is straightforward:
//   - Take action on every memory access: either inlined instrumentation,
//     or Inserted calls to our run-time library.
//   - Optimizations may apply to avoid instrumenting some of the accesses.
//   - Turn mem{set,cpy,move} instrinsics into library calls.
//   - Detect memory allocation calls and attempt to determine the object
//     type that they return
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Analysis/ValueTracking.h"
#include <cxxabi.h>
#include <fstream>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <sys/stat.h>

using namespace llvm;

#define DEBUG_TYPE "memoro"

static cl::opt<bool> ClInstrumentLoadsAndStores(
    "memoro-instrument-loads-and-stores", cl::init(true),
    cl::desc("Instrument loads and stores"), cl::Hidden);
static cl::opt<bool> ClInstrumentMemIntrinsics(
    "memoro-instrument-memintrinsics", cl::init(true),
    cl::desc("Instrument memintrinsics (memset/memcpy/memmove)"), cl::Hidden);

STATISTIC(NumInstrumentedLoads, "Number of instrumented loads");
STATISTIC(NumInstrumentedStores, "Number of instrumented stores");
STATISTIC(NumInstructions, "Number of instructions");
STATISTIC(NumAccessesWithIrregularSize,
          "Number of accesses with a size outside our targeted callout sizes");
STATISTIC(NumAllocaReferencesSkipped,
          "Number of load/store instructions that reference an alloca region");

static const uint64_t MemoroCtorAndDtorPriority = 0;
static const char *const MemoroModuleCtorName = "memoro.module_ctor";
static const char *const MemoroModuleDtorName = "memoro.module_dtor";
static const char *const MemoroInitName = "__memoro_init";
static const char *const MemoroExitName = "__memoro_exit";

// We need to specify the tool to the runtime earlier than
// the ctor is called in some cases, so we set a global variable.

namespace {

/// Memoro: instrument each module to find performance issues.
class Memoro : public ModulePass {
public:
  Memoro() : ModulePass(ID) {}
  ~Memoro() = default;
  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
  static char ID;

private:
  bool initOnModule(Module &M);
  void initializeCallbacks(Module &M);
  void createDestructor(Module &M);
  bool runOnFunction(Function &F, Module &M, raw_fd_ostream &type_file);
  bool instrumentLoadOrStore(Instruction *I, const DataLayout &DL);
  bool instrumentMemIntrinsic(MemIntrinsic *MI);
  bool shouldIgnoreMemoryAccess(Instruction *I);
  int getMemoryAccessFuncIndex(Value *Addr, const DataLayout &DL);
  void maybeInferMallocNewType(CallInst *CI, raw_fd_ostream &type_file);
  void inferMallocNewType(CallInst *CI, StringRef const &name,
                          raw_fd_ostream &type_file);

  LLVMContext *Ctx;
  Type *IntptrTy;
  // Our slowpath involves callouts to the runtime library.
  // Access sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t NumberOfAccessSizes = 5;
  Function *MemoroAlignedLoad[NumberOfAccessSizes];
  Function *MemoroAlignedStore[NumberOfAccessSizes];
  Function *MemoroUnalignedLoad[NumberOfAccessSizes];
  Function *MemoroUnalignedStore[NumberOfAccessSizes];
  // For irregular sizes of any alignment:
  Function *MemoroUnalignedLoadN, *MemoroUnalignedStoreN;
  Function *MemmoveFn, *MemcpyFn, *MemsetFn;
  Function *MemoroCtorFunction;
  Function *MemoroDtorFunction;
  // file we will dump alloc point type info to
  // std::ofstream type_file;
  // raw_fd_ostream type_file;
};
} // namespace

char Memoro::ID = 0;
INITIALIZE_PASS_BEGIN(Memoro, "memoro",
                      "Memoro: finds heap performance issues.", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(Memoro, "memoro", "Memoro: finds heap performance issues.",
                    false, false)

StringRef Memoro::getPassName() const { return "Memoro"; }

void Memoro::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

ModulePass *llvm::createMemoroPass() { return new Memoro(); }

void Memoro::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(M.getContext());
  // Initialize the callbacks.
  for (size_t Idx = 0; Idx < NumberOfAccessSizes; ++Idx) {
    const unsigned ByteSize = 1U << Idx;
    std::string ByteSizeStr = utostr(ByteSize);
    // We'll inline the most common (i.e., aligned and frequent sizes)
    // load + store instrumentation: these callouts are for the slowpath.
    SmallString<32> AlignedLoadName("__memoro_aligned_load" + ByteSizeStr);
    MemoroAlignedLoad[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            AlignedLoadName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> AlignedStoreName("__memoro_aligned_store" + ByteSizeStr);
    MemoroAlignedStore[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            AlignedStoreName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> UnalignedLoadName("__memoro_unaligned_load" + ByteSizeStr);
    MemoroUnalignedLoad[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedLoadName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
    SmallString<32> UnalignedStoreName("__memoro_unaligned_store" +
                                       ByteSizeStr);
    MemoroUnalignedStore[Idx] =
        checkSanitizerInterfaceFunction(M.getOrInsertFunction(
            UnalignedStoreName, IRB.getVoidTy(), IRB.getInt8PtrTy()));
  }
  MemoroUnalignedLoadN = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__memoro_unaligned_loadN", IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemoroUnalignedStoreN = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("__memoro_unaligned_storeN", IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemmoveFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memmove", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemcpyFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memcpy", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IntptrTy));
  MemsetFn = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction("memset", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                            IRB.getInt32Ty(), IntptrTy));

//lsEnableStatistics();
}

void Memoro::createDestructor(Module &M) {
  MemoroDtorFunction =
      Function::Create(FunctionType::get(Type::getVoidTy(*Ctx), false),
                       GlobalValue::InternalLinkage, MemoroModuleDtorName, &M);
  ReturnInst::Create(*Ctx, BasicBlock::Create(*Ctx, "", MemoroDtorFunction));
  IRBuilder<> IRB_Dtor(MemoroDtorFunction->getEntryBlock().getTerminator());
  Function *MemoroExit = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(MemoroExitName, IRB_Dtor.getVoidTy()));
  MemoroExit->setLinkage(Function::ExternalLinkage);
  IRB_Dtor.CreateCall(MemoroExit);
  appendToGlobalDtors(M, MemoroDtorFunction, MemoroCtorAndDtorPriority);
}

bool Memoro::initOnModule(Module &M) {

  Ctx = &M.getContext();
  const DataLayout &DL = M.getDataLayout();
  IntptrTy = DL.getIntPtrType(M.getContext());
  // Constructor
  std::tie(MemoroCtorFunction, std::ignore) =
      createSanitizerCtorAndInitFunctions(M, MemoroModuleCtorName,
                                          MemoroInitName,
                                          /*InitArgTypes=*/{}, {});
  appendToGlobalCtors(M, MemoroCtorFunction, MemoroCtorAndDtorPriority);

  createDestructor(M);

  return true;
}

// Find memory reference that definitely do not access the heap and ignore them.
bool Memoro::shouldIgnoreMemoryAccess(Instruction *I) {
  SmallVector<Value *, 4> Objs;
  DataLayout DL = I->getModule()->getDataLayout();
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    getUnderlyingObjectsForCodeGen(LI->getPointerOperand(), Objs, DL);
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    getUnderlyingObjectsForCodeGen(SI->getPointerOperand(), Objs, DL);
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    getUnderlyingObjectsForCodeGen(RMW->getPointerOperand(), Objs, DL);
  } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    getUnderlyingObjectsForCodeGen(XCHG->getPointerOperand(), Objs, DL);
  }
  else {
    return false;
  }

  if (Objs.empty()) {
    return false;   // Don't know what it references
  }

  // Alloca and global variables are not in the heap. Anything else?
  for (Value *V : Objs) {
    if (!isa<AllocaInst>(V) && !isa<GlobalVariable>(V)) {
      return false;
    }
  }

  NumAllocaReferencesSkipped++;
  return true;
}

bool Memoro::runOnModule(Module &M) {
  // create a file to store the type information inferred from
  // allocation point sites
  // this allows the visualizer to display what type of objects are
  // allocated at a given alloc point

  SmallString<64> dir("typefiles");
  sys::fs::create_directory(dir);
  SmallString<4096> filename(M.getName().str() + ".types");
  // form a unique flattened name for this module
  std::replace(filename.begin(), filename.end(), sys::path::get_separator()[0],
               '.');

  std::error_code EC;
  raw_fd_ostream type_file((dir + sys::path::get_separator() + filename).str(),
                           EC, sys::fs::F_Text);

  bool Res = initOnModule(M);
  initializeCallbacks(M);
  for (auto &F : M) {
    Res |= runOnFunction(F, M, type_file);
  }
  return Res;
}

void Memoro::inferMallocNewType(CallInst *CI, StringRef const &name,
                                raw_fd_ostream &type_file) {

  // detect the type of object allocated by looking for type casts of the
  // returned pointer. If no type casts, assume it was char* Get the type string
  // from debug info, dump it to the typefile for this module

  auto &loc = CI->getDebugLoc();
  auto *diloc = loc.get();

  // if no debug info, cannot output
  if (!diloc)
    return;

  Type *t = nullptr;
  for (auto it = CI->user_begin(); it != CI->user_end(); it++) {
    if (isa<CastInst>(*it)) {
      if (!t)
        t = it->getType();
      if (t && it->getType()->isStructTy())
        t = it->getType();
    }
  }
  if (t) {
    // we can't use isStructTy here because they are all
    // pointer types
    std::string type_name;
    llvm::raw_string_ostream rso(type_name);
    t->print(rso);
    type_name = rso.str();
    size_t pos = type_name.find_first_of('.');
    if (pos != std::string::npos)
      type_name = type_name.substr(pos + 1);
    // remove extraneous `"` sometimes added by LLVM
    type_name.erase(std::remove(type_name.begin(), type_name.end(), '\"'),
                    type_name.end());
    if (diloc->getFilename()[0] == '/') {
      type_file << diloc->getFilename().str() << ":" << diloc->getLine() << ":"
                << diloc->getColumn() << "|" << type_name << "\n";
    } else {
      type_file << diloc->getDirectory().str() << "/"
                << diloc->getFilename().str() << ":" << diloc->getLine() << ":"
                << diloc->getColumn() << "|" << type_name << "\n";
    }
  } else {
    // no cast found means that its basically char*
    if (diloc->getFilename()[0] == '/') {
      type_file << diloc->getFilename().str() << ":" << diloc->getLine() << ":"
                << diloc->getColumn() << "|"
                << "i8*"
                << "\n";
    } else {
      type_file << diloc->getDirectory().str() << "/"
                << diloc->getFilename().str() << ":" << diloc->getLine() << ":"
                << diloc->getColumn() << "|"
                << "i8*"
                << "\n";
    }
  }
}

void Memoro::maybeInferMallocNewType(CallInst *CI, raw_fd_ostream &type_file) {
  // detect if the function is a memory allocation call
  // currently doing this by comparing names, but this is inefficient and feels
  // somewhat brittle and likely doesn't catch all alloc funcs.
  // Open to suggestions
  Function *F = CI->getCalledFunction();
  if (!F || F->getName().empty()) {
    return;
  }
  int status;
  char *realname;
  realname = abi::__cxa_demangle(F->getName().str().c_str(), 0, 0, &status);
  if (F->getName().compare("malloc") == 0 ||
      F->getName().compare("realloc") == 0 ||
      F->getName().compare("calloc") == 0 ||
      (realname && strcmp(realname, "operator new[](unsigned long)") == 0) ||
      (realname && strcmp(realname, "operator new(unsigned long)") == 0)) {
    if (realname) {
      StringRef name(realname);
      inferMallocNewType(CI, name, type_file);
    } else {
      inferMallocNewType(CI, F->getName(), type_file);
    }
  }
  free(realname);
}

bool Memoro::runOnFunction(Function &F, Module &M, raw_fd_ostream &type_file) {
  // This is required to prevent instrumenting the call to __memoro_init from
  // within the module constructor.
  if (&F == MemoroCtorFunction)
    return false;
  SmallVector<Instruction *, 8> LoadsAndStores;
  SmallVector<Instruction *, 8> MemIntrinCalls;
  SmallVector<Instruction *, 8> GetElementPtrs;
  bool Res = false;
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if ((isa<LoadInst>(Inst) || isa<StoreInst>(Inst) ||
           isa<AtomicRMWInst>(Inst) || isa<AtomicCmpXchgInst>(Inst))
          && !shouldIgnoreMemoryAccess(&Inst))
        LoadsAndStores.push_back(&Inst);
      else if (isa<MemIntrinsic>(Inst))
        MemIntrinCalls.push_back(&Inst);
      else if (isa<GetElementPtrInst>(Inst))
        GetElementPtrs.push_back(&Inst);
      else if (CallInst *CI = dyn_cast<CallInst>(&Inst)) {
        maybeMarkSanitizerLibraryCallNoBuiltin(CI, TLI);
        maybeInferMallocNewType(CI, type_file);
      }
    }
  }

  if (ClInstrumentLoadsAndStores) {
    for (auto Inst : LoadsAndStores) {
      Res |= instrumentLoadOrStore(Inst, DL);
    }
  }

  if (ClInstrumentMemIntrinsics) {
    for (auto Inst : MemIntrinCalls) {
      Res |= instrumentMemIntrinsic(cast<MemIntrinsic>(Inst));
    }
  }

  return Res;
}

bool Memoro::instrumentLoadOrStore(Instruction *I, const DataLayout &DL) {
  NumInstructions++;
  IRBuilder<> IRB(I);
  bool IsStore;
  Value *Addr;
  unsigned Alignment;
  if (LoadInst *Load = dyn_cast<LoadInst>(I)) {
    IsStore = false;
    Alignment = Load->getAlignment();
    Addr = Load->getPointerOperand();
    if (isa<AllocaInst>(Addr)) {
      return true;
    }
  } else if (StoreInst *Store = dyn_cast<StoreInst>(I)) {
    IsStore = true;
    Alignment = Store->getAlignment();
    Addr = Store->getPointerOperand();
    if (isa<AllocaInst>(Addr)) {
      return true;
    }
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    IsStore = true;
    Alignment = 0;
    Addr = RMW->getPointerOperand();
    if (isa<AllocaInst>(Addr)) {
      return true;
    }
  } else if (AtomicCmpXchgInst *Xchg = dyn_cast<AtomicCmpXchgInst>(I)) {
    IsStore = true;
    Alignment = 0;
    Addr = Xchg->getPointerOperand();
    if (isa<AllocaInst>(Addr)) {
      return true;
    }
  } else
    llvm_unreachable("Unsupported mem access type");

  Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
  const uint32_t TypeSizeBytes = DL.getTypeStoreSizeInBits(OrigTy) / 8;
  Value *OnAccessFunc = nullptr;

  // Convert 0 to the default alignment.
  if (Alignment == 0)
    Alignment = DL.getPrefTypeAlignment(OrigTy);

  if (IsStore)
    NumInstrumentedStores++;
  else
    NumInstrumentedLoads++;
  int Idx = getMemoryAccessFuncIndex(Addr, DL);
  if (Idx < 0) {
    OnAccessFunc = IsStore ? MemoroUnalignedStoreN : MemoroUnalignedLoadN;
    IRB.CreateCall(OnAccessFunc,
                   {IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()),
                    ConstantInt::get(IntptrTy, TypeSizeBytes)});
  } else {
    if (Alignment == 0 || (Alignment % TypeSizeBytes) == 0)
      OnAccessFunc = IsStore ? MemoroAlignedStore[Idx] : MemoroAlignedLoad[Idx];
    else
      OnAccessFunc =
          IsStore ? MemoroUnalignedStore[Idx] : MemoroUnalignedLoad[Idx];
    IRB.CreateCall(OnAccessFunc,
                   IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
  }
  return true;
}

// It's simplest to replace the memset/memmove/memcpy intrinsics with
// calls that the runtime library intercepts.
// Our pass is late enough that calls should not turn back into intrinsics.
bool Memoro::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  bool Res = false;
  if (isa<MemSetInst>(MI)) {
    IRB.CreateCall(
        MemsetFn,
        {IRB.CreatePointerCast(MI->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getArgOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(MI->getArgOperand(2), IntptrTy, false)});
    MI->eraseFromParent();
    Res = true;
  } else if (isa<MemTransferInst>(MI)) {
    IRB.CreateCall(
        isa<MemCpyInst>(MI) ? MemcpyFn : MemmoveFn,
        {IRB.CreatePointerCast(MI->getArgOperand(0), IRB.getInt8PtrTy()),
         IRB.CreatePointerCast(MI->getArgOperand(1), IRB.getInt8PtrTy()),
         IRB.CreateIntCast(MI->getArgOperand(2), IntptrTy, false)});
    MI->eraseFromParent();
    Res = true;
  } else
    llvm_unreachable("Unsupported mem intrinsic type");
  return Res;
}

int Memoro::getMemoryAccessFuncIndex(Value *Addr, const DataLayout &DL) {
  Type *OrigPtrTy = Addr->getType();
  Type *OrigTy = cast<PointerType>(OrigPtrTy)->getElementType();
  assert(OrigTy->isSized());
  // The size is always a multiple of 8.
  uint32_t TypeSizeBytes = DL.getTypeStoreSizeInBits(OrigTy) / 8;
  if (TypeSizeBytes != 1 && TypeSizeBytes != 2 && TypeSizeBytes != 4 &&
      TypeSizeBytes != 8 && TypeSizeBytes != 16) {
    // Irregular sizes do not have per-size call targets.
    NumAccessesWithIrregularSize++;
    return -1;
  }
  size_t Idx = countTrailingZeros(TypeSizeBytes);
  assert(Idx < NumberOfAccessSizes);
  return Idx;
}
