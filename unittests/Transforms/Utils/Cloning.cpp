//===- Cloning.cpp - Unit tests for the Cloner ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class CloneInstruction : public ::testing::Test {
protected:
  virtual void SetUp() {
    V = NULL;
  }

  template <typename T>
  T *clone(T *V1) {
    Value *V2 = V1->clone();
    Orig.insert(V1);
    Clones.insert(V2);
    return cast<T>(V2);
  }

  void eraseClones() {
    DeleteContainerPointers(Clones);
  }

  virtual void TearDown() {
    eraseClones();
    DeleteContainerPointers(Orig);
    delete V;
  }

  SmallPtrSet<Value *, 4> Orig;   // Erase on exit
  SmallPtrSet<Value *, 4> Clones; // Erase in eraseClones

  LLVMContext context;
  Value *V;
};

TEST_F(CloneInstruction, OverflowBits) {
  V = new Argument(Type::getInt32Ty(context));

  BinaryOperator *Add = BinaryOperator::Create(Instruction::Add, V, V);
  BinaryOperator *Sub = BinaryOperator::Create(Instruction::Sub, V, V);
  BinaryOperator *Mul = BinaryOperator::Create(Instruction::Mul, V, V);

  BinaryOperator *AddClone = this->clone(Add);
  BinaryOperator *SubClone = this->clone(Sub);
  BinaryOperator *MulClone = this->clone(Mul);

  EXPECT_FALSE(AddClone->hasNoUnsignedWrap());
  EXPECT_FALSE(AddClone->hasNoSignedWrap());
  EXPECT_FALSE(SubClone->hasNoUnsignedWrap());
  EXPECT_FALSE(SubClone->hasNoSignedWrap());
  EXPECT_FALSE(MulClone->hasNoUnsignedWrap());
  EXPECT_FALSE(MulClone->hasNoSignedWrap());

  eraseClones();

  Add->setHasNoUnsignedWrap();
  Sub->setHasNoUnsignedWrap();
  Mul->setHasNoUnsignedWrap();

  AddClone = this->clone(Add);
  SubClone = this->clone(Sub);
  MulClone = this->clone(Mul);

  EXPECT_TRUE(AddClone->hasNoUnsignedWrap());
  EXPECT_FALSE(AddClone->hasNoSignedWrap());
  EXPECT_TRUE(SubClone->hasNoUnsignedWrap());
  EXPECT_FALSE(SubClone->hasNoSignedWrap());
  EXPECT_TRUE(MulClone->hasNoUnsignedWrap());
  EXPECT_FALSE(MulClone->hasNoSignedWrap());

  eraseClones();

  Add->setHasNoSignedWrap();
  Sub->setHasNoSignedWrap();
  Mul->setHasNoSignedWrap();

  AddClone = this->clone(Add);
  SubClone = this->clone(Sub);
  MulClone = this->clone(Mul);

  EXPECT_TRUE(AddClone->hasNoUnsignedWrap());
  EXPECT_TRUE(AddClone->hasNoSignedWrap());
  EXPECT_TRUE(SubClone->hasNoUnsignedWrap());
  EXPECT_TRUE(SubClone->hasNoSignedWrap());
  EXPECT_TRUE(MulClone->hasNoUnsignedWrap());
  EXPECT_TRUE(MulClone->hasNoSignedWrap());

  eraseClones();

  Add->setHasNoUnsignedWrap(false);
  Sub->setHasNoUnsignedWrap(false);
  Mul->setHasNoUnsignedWrap(false);

  AddClone = this->clone(Add);
  SubClone = this->clone(Sub);
  MulClone = this->clone(Mul);

  EXPECT_FALSE(AddClone->hasNoUnsignedWrap());
  EXPECT_TRUE(AddClone->hasNoSignedWrap());
  EXPECT_FALSE(SubClone->hasNoUnsignedWrap());
  EXPECT_TRUE(SubClone->hasNoSignedWrap());
  EXPECT_FALSE(MulClone->hasNoUnsignedWrap());
  EXPECT_TRUE(MulClone->hasNoSignedWrap());
}

TEST_F(CloneInstruction, Inbounds) {
  V = new Argument(Type::getInt32PtrTy(context));

  Constant *Z = Constant::getNullValue(Type::getInt32Ty(context));
  std::vector<Value *> ops;
  ops.push_back(Z);
  GetElementPtrInst *GEP = GetElementPtrInst::Create(V, ops);
  EXPECT_FALSE(this->clone(GEP)->isInBounds());

  GEP->setIsInBounds();
  EXPECT_TRUE(this->clone(GEP)->isInBounds());
}

TEST_F(CloneInstruction, Exact) {
  V = new Argument(Type::getInt32Ty(context));

  BinaryOperator *SDiv = BinaryOperator::Create(Instruction::SDiv, V, V);
  EXPECT_FALSE(this->clone(SDiv)->isExact());

  SDiv->setIsExact(true);
  EXPECT_TRUE(this->clone(SDiv)->isExact());
}

TEST_F(CloneInstruction, Attributes) {
  Type *ArgTy1[] = { Type::getInt32PtrTy(context) };
  FunctionType *FT1 =  FunctionType::get(Type::getVoidTy(context), ArgTy1, false);

  Function *F1 = Function::Create(FT1, Function::ExternalLinkage);
  BasicBlock *BB = BasicBlock::Create(context, "", F1);
  IRBuilder<> Builder(BB);
  Builder.CreateRetVoid();

  Function *F2 = Function::Create(FT1, Function::ExternalLinkage);

  Attribute::AttrKind AK[] = { Attribute::NoCapture };
  AttributeSet AS = AttributeSet::get(context, 0, AK);
  Argument *A = F1->arg_begin();
  A->addAttr(AS);

  SmallVector<ReturnInst*, 4> Returns;
  ValueToValueMapTy VMap;
  VMap[A] = UndefValue::get(A->getType());

  CloneFunctionInto(F2, F1, VMap, false, Returns);
  EXPECT_FALSE(F2->arg_begin()->hasNoCaptureAttr());

  delete F1;
  delete F2;
}

}
