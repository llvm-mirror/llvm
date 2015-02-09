//===- TestPasses.cpp - "buggy" passes used to test bugpoint --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains "buggy" passes that are used to test bugpoint, to check
// that it is narrowing down testcases correctly.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {
  /// CrashOnCalls - This pass is used to test bugpoint.  It intentionally
  /// crashes on any call instructions.
  class CrashOnCalls : public BasicBlockPass {
  public:
    static char ID; // Pass ID, replacement for typeid
    CrashOnCalls() : BasicBlockPass(ID) {}
  private:
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

    bool runOnBasicBlock(BasicBlock &BB) override {
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
        if (isa<CallInst>(*I))
          abort();

      return false;
    }
  };
}

char CrashOnCalls::ID = 0;
static RegisterPass<CrashOnCalls>
  X("bugpoint-crashcalls",
    "BugPoint Test Pass - Intentionally crash on CallInsts");

namespace {
  /// DeleteCalls - This pass is used to test bugpoint.  It intentionally
  /// deletes some call instructions, "misoptimizing" the program.
  class DeleteCalls : public BasicBlockPass {
  public:
    static char ID; // Pass ID, replacement for typeid
    DeleteCalls() : BasicBlockPass(ID) {}
  private:
    bool runOnBasicBlock(BasicBlock &BB) override {
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
        if (CallInst *CI = dyn_cast<CallInst>(I)) {
          if (!CI->use_empty())
            CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
          CI->getParent()->getInstList().erase(CI);
          break;
        }
      return false;
    }
  };
}
 
char DeleteCalls::ID = 0;
static RegisterPass<DeleteCalls>
  Y("bugpoint-deletecalls",
    "BugPoint Test Pass - Intentionally 'misoptimize' CallInsts");
