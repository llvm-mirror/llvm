//===- llvm/unittest/IR/VerifierTest.cpp - Verifier unit tests ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Verifier.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

namespace llvm {
namespace {

TEST(VerifierTest, Branch_i1) {
  LLVMContext &C = getGlobalContext();
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(C), /*isVarArg=*/false);
  OwningPtr<Function> F(Function::Create(FTy, GlobalValue::ExternalLinkage));
  BasicBlock *Entry = BasicBlock::Create(C, "entry", F.get());
  BasicBlock *Exit = BasicBlock::Create(C, "exit", F.get());
  ReturnInst::Create(C, Exit);

  // To avoid triggering an assertion in BranchInst::Create, we first create
  // a branch with an 'i1' condition ...

  Constant *False = ConstantInt::getFalse(C);
  BranchInst *BI = BranchInst::Create(Exit, Exit, False, Entry);

  // ... then use setOperand to redirect it to a value of different type.

  Constant *Zero32 = ConstantInt::get(IntegerType::get(C, 32), 0);
  BI->setOperand(0, Zero32);

  EXPECT_TRUE(verifyFunction(*F, ReturnStatusAction));
}

TEST(VerifierTest, AliasUnnamedAddr) {
  LLVMContext &C = getGlobalContext();
  Module M("M", C);
  Type *Ty = Type::getInt8Ty(C);
  Constant *Init = Constant::getNullValue(Ty);
  GlobalVariable *Aliasee = new GlobalVariable(M, Ty, true,
                                               GlobalValue::ExternalLinkage,
                                               Init, "foo");
  GlobalAlias *GA = new GlobalAlias(Type::getInt8PtrTy(C),
                                    GlobalValue::ExternalLinkage,
                                    "bar", Aliasee, &M);
  GA->setUnnamedAddr(true);
  std::string Error;
  EXPECT_TRUE(verifyModule(M, ReturnStatusAction, &Error));
  EXPECT_TRUE(StringRef(Error).startswith("Alias cannot have unnamed_addr"));
}

TEST(VerifierTest, InvalidRetAttribute) {
  LLVMContext &C = getGlobalContext();
  Module M("M", C);
  FunctionType *FTy = FunctionType::get(Type::getInt32Ty(C), /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction("foo", FTy));
  AttributeSet AS = F->getAttributes();
  F->setAttributes(AS.addAttribute(C, AttributeSet::ReturnIndex,
                                   Attribute::UWTable));

  std::string Error;
  EXPECT_TRUE(verifyModule(M, ReturnStatusAction, &Error));
  EXPECT_TRUE(StringRef(Error).
              startswith("Attribute 'uwtable' only applies to functions!"));
}

}
}
