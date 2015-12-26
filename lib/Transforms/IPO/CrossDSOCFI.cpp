//===-- CrossDSOCFI.cpp - Externalize this module's CFI checks ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass exports all llvm.bitset's found in the module in the form of a
// __cfi_check function, which can be used to verify cross-DSO call targets.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "cross-dso-cfi"

STATISTIC(TypeIds, "Number of unique type identifiers");

namespace {

struct CrossDSOCFI : public ModulePass {
  static char ID;
  CrossDSOCFI() : ModulePass(ID) {
    initializeCrossDSOCFIPass(*PassRegistry::getPassRegistry());
  }

  Module *M;
  MDNode *VeryLikelyWeights;

  ConstantInt *extractBitSetTypeId(MDNode *MD);
  void buildCFICheck();

  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;
};

} // anonymous namespace

INITIALIZE_PASS_BEGIN(CrossDSOCFI, "cross-dso-cfi", "Cross-DSO CFI", false,
                      false)
INITIALIZE_PASS_END(CrossDSOCFI, "cross-dso-cfi", "Cross-DSO CFI", false, false)
char CrossDSOCFI::ID = 0;

ModulePass *llvm::createCrossDSOCFIPass() { return new CrossDSOCFI; }

bool CrossDSOCFI::doInitialization(Module &Mod) {
  M = &Mod;
  VeryLikelyWeights =
      MDBuilder(M->getContext()).createBranchWeights((1U << 20) - 1, 1);

  return false;
}

/// extractBitSetTypeId - Extracts TypeId from a hash-based bitset MDNode.
ConstantInt *CrossDSOCFI::extractBitSetTypeId(MDNode *MD) {
  // This check excludes vtables for classes inside anonymous namespaces.
  auto TM = dyn_cast<ValueAsMetadata>(MD->getOperand(0));
  if (!TM)
    return nullptr;
  auto C = dyn_cast_or_null<ConstantInt>(TM->getValue());
  if (!C) return nullptr;
  // We are looking for i64 constants.
  if (C->getBitWidth() != 64) return nullptr;

  // Sanity check.
  auto FM = dyn_cast_or_null<ValueAsMetadata>(MD->getOperand(1));
  // Can be null if a function was removed by an optimization.
  if (FM) {
    auto F = dyn_cast<Function>(FM->getValue());
    // But can never be a function declaration.
    assert(!F || !F->isDeclaration());
    (void)F; // Suppress unused variable warning in the no-asserts build.
  }
  return C;
}

/// buildCFICheck - emits __cfi_check for the current module.
void CrossDSOCFI::buildCFICheck() {
  // FIXME: verify that __cfi_check ends up near the end of the code section,
  // but before the jump slots created in LowerBitSets.
  llvm::DenseSet<uint64_t> BitSetIds;
  NamedMDNode *BitSetNM = M->getNamedMetadata("llvm.bitsets");

  if (BitSetNM)
    for (unsigned I = 0, E = BitSetNM->getNumOperands(); I != E; ++I)
      if (ConstantInt *TypeId = extractBitSetTypeId(BitSetNM->getOperand(I)))
        BitSetIds.insert(TypeId->getZExtValue());

  LLVMContext &Ctx = M->getContext();
  Constant *C = M->getOrInsertFunction(
      "__cfi_check",
      FunctionType::get(
          Type::getVoidTy(Ctx),
          {Type::getInt64Ty(Ctx), PointerType::getUnqual(Type::getInt8Ty(Ctx))},
          false));
  Function *F = dyn_cast<Function>(C);
  F->setAlignment(4096);
  auto args = F->arg_begin();
  Argument &CallSiteTypeId = *(args++);
  CallSiteTypeId.setName("CallSiteTypeId");
  Argument &Addr = *(args++);
  Addr.setName("Addr");
  assert(args == F->arg_end());

  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);

  BasicBlock *TrapBB = BasicBlock::Create(Ctx, "trap", F);
  IRBuilder<> IRBTrap(TrapBB);
  Function *TrapFn = Intrinsic::getDeclaration(M, Intrinsic::trap);
  llvm::CallInst *TrapCall = IRBTrap.CreateCall(TrapFn);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  IRBTrap.CreateUnreachable();

  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "exit", F);
  IRBuilder<> IRBExit(ExitBB);
  IRBExit.CreateRetVoid();

  IRBuilder<> IRB(BB);
  SwitchInst *SI = IRB.CreateSwitch(&CallSiteTypeId, TrapBB, BitSetIds.size());
  for (uint64_t TypeId : BitSetIds) {
    ConstantInt *CaseTypeId = ConstantInt::get(Type::getInt64Ty(Ctx), TypeId);
    BasicBlock *TestBB = BasicBlock::Create(Ctx, "test", F);
    IRBuilder<> IRBTest(TestBB);
    Function *BitsetTestFn =
        Intrinsic::getDeclaration(M, Intrinsic::bitset_test);

    Value *Test = IRBTest.CreateCall(
        BitsetTestFn, {&Addr, MetadataAsValue::get(
                                  Ctx, ConstantAsMetadata::get(CaseTypeId))});
    BranchInst *BI = IRBTest.CreateCondBr(Test, ExitBB, TrapBB);
    BI->setMetadata(LLVMContext::MD_prof, VeryLikelyWeights);

    SI->addCase(CaseTypeId, TestBB);
    ++TypeIds;
  }
}

bool CrossDSOCFI::runOnModule(Module &M) {
  if (M.getModuleFlag("Cross-DSO CFI") == nullptr)
    return false;
  buildCFICheck();
  return true;
}
