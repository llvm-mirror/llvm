//===- llvm/unittest/IR/ValueTest.cpp - Value unit tests ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"
using namespace llvm;

namespace {

TEST(ValueTest, UsedInBasicBlock) {
  LLVMContext C;

  const char *ModuleString = "define void @f(i32 %x, i32 %y) {\n"
                             "bb0:\n"
                             "  %y1 = add i32 %y, 1\n"
                             "  %y2 = add i32 %y, 1\n"
                             "  %y3 = add i32 %y, 1\n"
                             "  %y4 = add i32 %y, 1\n"
                             "  %y5 = add i32 %y, 1\n"
                             "  %y6 = add i32 %y, 1\n"
                             "  %y7 = add i32 %y, 1\n"
                             "  %y8 = add i32 %x, 1\n"
                             "  ret void\n"
                             "}\n";
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(ModuleString, Err, C);

  Function *F = M->getFunction("f");

  EXPECT_FALSE(F->isUsedInBasicBlock(F->begin()));
  EXPECT_TRUE((++F->arg_begin())->isUsedInBasicBlock(F->begin()));
  EXPECT_TRUE(F->arg_begin()->isUsedInBasicBlock(F->begin()));
}

TEST(GlobalTest, CreateAddressSpace) {
  LLVMContext &Ctx = getGlobalContext();
  std::unique_ptr<Module> M(new Module("TestModule", Ctx));
  Type *Int8Ty = Type::getInt8Ty(Ctx);
  Type *Int32Ty = Type::getInt32Ty(Ctx);

  GlobalVariable *Dummy0
    = new GlobalVariable(*M,
                         Int32Ty,
                         true,
                         GlobalValue::ExternalLinkage,
                         Constant::getAllOnesValue(Int32Ty),
                         "dummy",
                         nullptr,
                         GlobalVariable::NotThreadLocal,
                         1);

  EXPECT_TRUE(Value::MaximumAlignment == 536870912U);
  Dummy0->setAlignment(536870912U);
  EXPECT_EQ(Dummy0->getAlignment(), 536870912U);

  // Make sure the address space isn't dropped when returning this.
  Constant *Dummy1 = M->getOrInsertGlobal("dummy", Int32Ty);
  EXPECT_EQ(Dummy0, Dummy1);
  EXPECT_EQ(1u, Dummy1->getType()->getPointerAddressSpace());


  // This one requires a bitcast, but the address space must also stay the same.
  GlobalVariable *DummyCast0
    = new GlobalVariable(*M,
                         Int32Ty,
                         true,
                         GlobalValue::ExternalLinkage,
                         Constant::getAllOnesValue(Int32Ty),
                         "dummy_cast",
                         nullptr,
                         GlobalVariable::NotThreadLocal,
                         1);

  // Make sure the address space isn't dropped when returning this.
  Constant *DummyCast1 = M->getOrInsertGlobal("dummy_cast", Int8Ty);
  EXPECT_EQ(1u, DummyCast1->getType()->getPointerAddressSpace());
  EXPECT_NE(DummyCast0, DummyCast1) << *DummyCast1;
}

#ifdef GTEST_HAS_DEATH_TEST
#ifndef NDEBUG
TEST(GlobalTest, AlignDeath) {
  LLVMContext &Ctx = getGlobalContext();
  std::unique_ptr<Module> M(new Module("TestModule", Ctx));
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  GlobalVariable *Var =
      new GlobalVariable(*M, Int32Ty, true, GlobalValue::ExternalLinkage,
                         Constant::getAllOnesValue(Int32Ty), "var", nullptr,
                         GlobalVariable::NotThreadLocal, 1);

  EXPECT_DEATH(Var->setAlignment(536870913U), "Alignment is not a power of 2");
  EXPECT_DEATH(Var->setAlignment(1073741824U),
               "Alignment is greater than MaximumAlignment");
}
#endif
#endif

} // end anonymous namespace
