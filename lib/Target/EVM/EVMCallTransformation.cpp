//===- EVMCallTransformation.cpp - Avoid HW prefetcher pitfalls on Falkor --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file This pass does the following things:
/// 1. split the basicblock that containing a function call into two basicblocks,
/// the splitting point is between the call and the next instruction.
//  2. insert the basicblock label as the first operand to the call.
// ===---------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMInstrInfo.h"
#include "EVMSubtarget.h"
#include "EVMTargetMachine.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

#define DEBUG_TYPE "evm-call-transformation"

namespace {

using FuncMapType = DenseMap<Function*, Function*>;

class EVMCallTransformation : public ModulePass {
public:
  static char ID; // Pass ID, replacement for typeid

  EVMCallTransformation() : ModulePass(ID) {
    initializeEVMCallTransformationPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
  }

  bool runOnModule(Module &M) override;

private:
  FunctionType* rebuildFunctionType(FunctionType *FTy) const;
  Function* rebuildFunction(FunctionType* FuncTy, Function* OldFunc);
  void scanFunction(Function& F, FuncMapType &FuncMap) const;
};


} // end anonymous namespace

FunctionType*
EVMCallTransformation::rebuildFunctionType(FunctionType *OrigType) const {
  Type* RetType = OrigType->getReturnType();

  LLVM_DEBUG({ dbgs() << "    Original FunctionType:     "; OrigType->dump(); });

  // construct the first operand to be a pointer type...

  SmallVector<Type*, 16> ParamTypesVector;

  // For some reason, the pointer type is (i8*) in LLVM IR.
  ParamTypesVector.push_back(Type::getInt8PtrTy(OrigType->getContext()));

  for (Type* type : OrigType->params()) {
    ParamTypesVector.push_back(type);
  }

  FunctionType* FT = FunctionType::get(RetType, ParamTypesVector,
                                       OrigType->isVarArg());

  LLVM_DEBUG({ dbgs() << "    Creating new FunctionType: "; FT->dump(); });
  return FT;
}

void EVMCallTransformation::scanFunction(Function& F,
                                         FuncMapType &FuncMap) const {
  for (inst_iterator It = inst_begin(&F), E = inst_end(F);
       It != E; ++It) {
    Instruction &I = *It;
    if (!isa<CallInst>(I))
      continue;

    inst_iterator PrevIt;
    bool isFirst = false;
    if (It == inst_begin(&F)) {
      isFirst = true;
    } else {
      PrevIt = It;
      --PrevIt;
    }

    const CallInst* CI = dyn_cast<CallInst>(&I);

    // split the basicblock into 2
    BasicBlock* newBB = nullptr;
    {
      BasicBlock::iterator it = I.getIterator();
      ++it;

      if (it == it->getParent()->end()) {
        llvm_unreachable("unimplemented");
      }

      BasicBlock* BB = I.getParent();
      newBB = BB->splitBasicBlock(it);
    }


    unsigned numOpnds = CI->getNumArgOperands();
    assert(numOpnds < 16 &&
           "Do not support more than 15 operands for calls");
    SmallVector<Value*, 16> Opnds;

    // make the fallthrough BB the first operand
    Value *newBBAddr = BlockAddress::get(newBB);   
    Opnds.push_back(newBBAddr);

    for (unsigned i = 0, e = numOpnds; i < e; ++i) {
      Opnds.push_back(CI->getArgOperand(i));
    }

    Function* CalledFunction = CI->getCalledFunction();

    if (EVMSubtarget::shouldSkipFunction(CalledFunction)) {
      continue;
    }
    
    // TODO: We have to change it to accmondate more situations.
    auto NewFuncIter = FuncMap.find(CalledFunction);
    assert(NewFuncIter != FuncMap.end() && "Function not found in FuncMap");
    // Find the newly created map:
    Function* NewFunc = NewFuncIter->second;

    FunctionType* OldFuncTy = CalledFunction->getFunctionType();

    LLVM_DEBUG({
      dbgs() << "Rebuilding Function:" << CalledFunction->getName() << "\n";
    });
    FunctionType* CallTy = rebuildFunctionType(OldFuncTy);

    Instruction* NewCI = CallInst::Create(CallTy, NewFunc, Opnds);
    LLVM_DEBUG({ dbgs() << "Replacing original Call Instruction:\n"; CI->dump(); });


    // replace the original call inst with the new call instruction.
    ReplaceInstWithInst(&I, NewCI);
    LLVM_DEBUG({ dbgs() << "Creating new Call Instruction:\n"; NewCI->dump(); });

    // We have to renew iterator since the old iterator has been removed
    if (isFirst) {
      It = inst_begin(F);
    } else {
      It = PrevIt;
    }
    ++It;
  }
}

Function*
EVMCallTransformation::rebuildFunction(FunctionType* NewFuncTy, Function* OldFunc) {
  // create a new, empty function with the new Function type, and
  // call CloneFunctionInto() to clone it over.
  // Another way to implement it is to look at:
  // /lib/Transforms/IPO/DeadArgumentElimination.cpp

  // change the name so it 
  StringRef funcName = OldFunc->getName();
  OldFunc->setName(OldFunc->getName() + "_old");

  Function* NewFunc = Function::Create(NewFuncTy,
                                       OldFunc->getLinkage(),
                                       OldFunc->getAddressSpace(),
                                       funcName,
                                       OldFunc->getParent());
  LLVM_DEBUG({ dbgs() << "    New function: " << NewFunc->getName() << "\n"; });

  ValueToValueMapTy VMap;
  auto NewFArgIt = NewFunc->arg_begin();

  // TODO: the first arg should be the return.
  NewFArgIt->setName("retaddr");
  NewFArgIt++;

  for (auto &Arg: OldFunc->args()) {
    auto ArgName = Arg.getName();
    NewFArgIt->setName(ArgName);
    VMap[&Arg] = &(*NewFArgIt++);
  }

  SmallVector<ReturnInst*, 2> Returns;
  CloneFunctionInto(NewFunc, OldFunc, VMap, OldFunc->getSubprogram() != nullptr, Returns);

  return NewFunc;
}

bool EVMCallTransformation::runOnModule(Module &M) {

  FuncMapType FuncMap;
  SmallVector<Function *, 16> FuncVector;

  // Record old functions that needs to be transformed.
  for (Function& F : M) {
    // TODO: we should also change function decls.
    if (EVMSubtarget::shouldSkipFunction(&F)) {
      LLVM_DEBUG({
        dbgs() << "Skipping transforming function:     " << F.getName() << "\n";
      });
      continue;
    }

    LLVM_DEBUG({ dbgs() << "Found function to transform:     " << F.getName() << "\n"; });
    FuncVector.push_back(&F);
  }
  
  for (Function *F : FuncVector) {
    // rebuild function:
    FunctionType *OrigTy = F->getFunctionType();
    FunctionType *NewTy = rebuildFunctionType(OrigTy);
    Function* NewFunc = rebuildFunction(NewTy, F);

    // store to the FuncMap 
    FuncMap[F] = NewFunc;
  }

  // iterate over new Functions, and divert their calls
  for (const auto &FuncPair : FuncMap) {
    Function* NewFunc = FuncPair.second;

    // We can convert all function calls inside the NewFunc
    // because they are all referring to old functions.
    scanFunction(*NewFunc, FuncMap);
  }

  // don't forget to convert main function:
  for (Function& F : M) {
    // TODO: we should also change function decls.
    if (EVMSubtarget::isMainFunction(F)) {
      LLVM_DEBUG({
        dbgs() << "Transforming main function:     " << F.getName() << "\n";
      });
      scanFunction(F, FuncMap);
      break;
    }
  }

  // Remove all old functions.
  for (const auto &FuncPair : FuncMap) {
    Function* OldFunc = FuncPair.first;

    OldFunc->replaceAllUsesWith(UndefValue::get(OldFunc->getType()));
    OldFunc->eraseFromParent();
    //OldFunc->deleteBody();
  }

  return true;
}

char EVMCallTransformation::ID = 0;

INITIALIZE_PASS(EVMCallTransformation, DEBUG_TYPE,
                "Convert EVM calls", false, false);

ModulePass *llvm::createEVMCallTransformation() {
  return new EVMCallTransformation();
}

