//===- llvm/unittests/IR/DominatorTreeTest.cpp - Constants unit tests -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace llvm {
  void initializeDPassPass(PassRegistry&);

  namespace {
    struct DPass : public FunctionPass {
      static char ID;
      virtual bool runOnFunction(Function &F) {
        DominatorTree *DT =
            &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
        PostDominatorTree *PDT = &getAnalysis<PostDominatorTree>();
        Function::iterator FI = F.begin();

        BasicBlock *BB0 = FI++;
        BasicBlock::iterator BBI = BB0->begin();
        Instruction *Y1 = BBI++;
        Instruction *Y2 = BBI++;
        Instruction *Y3 = BBI++;

        BasicBlock *BB1 = FI++;
        BBI = BB1->begin();
        Instruction *Y4 = BBI++;

        BasicBlock *BB2 = FI++;
        BBI = BB2->begin();
        Instruction *Y5 = BBI++;

        BasicBlock *BB3 = FI++;
        BBI = BB3->begin();
        Instruction *Y6 = BBI++;
        Instruction *Y7 = BBI++;

        BasicBlock *BB4 = FI++;
        BBI = BB4->begin();
        Instruction *Y8 = BBI++;
        Instruction *Y9 = BBI++;

        // Reachability
        EXPECT_TRUE(DT->isReachableFromEntry(BB0));
        EXPECT_TRUE(DT->isReachableFromEntry(BB1));
        EXPECT_TRUE(DT->isReachableFromEntry(BB2));
        EXPECT_FALSE(DT->isReachableFromEntry(BB3));
        EXPECT_TRUE(DT->isReachableFromEntry(BB4));

        // BB dominance
        EXPECT_TRUE(DT->dominates(BB0, BB0));
        EXPECT_TRUE(DT->dominates(BB0, BB1));
        EXPECT_TRUE(DT->dominates(BB0, BB2));
        EXPECT_TRUE(DT->dominates(BB0, BB3));
        EXPECT_TRUE(DT->dominates(BB0, BB4));

        EXPECT_FALSE(DT->dominates(BB1, BB0));
        EXPECT_TRUE(DT->dominates(BB1, BB1));
        EXPECT_FALSE(DT->dominates(BB1, BB2));
        EXPECT_TRUE(DT->dominates(BB1, BB3));
        EXPECT_FALSE(DT->dominates(BB1, BB4));

        EXPECT_FALSE(DT->dominates(BB2, BB0));
        EXPECT_FALSE(DT->dominates(BB2, BB1));
        EXPECT_TRUE(DT->dominates(BB2, BB2));
        EXPECT_TRUE(DT->dominates(BB2, BB3));
        EXPECT_FALSE(DT->dominates(BB2, BB4));

        EXPECT_FALSE(DT->dominates(BB3, BB0));
        EXPECT_FALSE(DT->dominates(BB3, BB1));
        EXPECT_FALSE(DT->dominates(BB3, BB2));
        EXPECT_TRUE(DT->dominates(BB3, BB3));
        EXPECT_FALSE(DT->dominates(BB3, BB4));

        // BB proper dominance
        EXPECT_FALSE(DT->properlyDominates(BB0, BB0));
        EXPECT_TRUE(DT->properlyDominates(BB0, BB1));
        EXPECT_TRUE(DT->properlyDominates(BB0, BB2));
        EXPECT_TRUE(DT->properlyDominates(BB0, BB3));

        EXPECT_FALSE(DT->properlyDominates(BB1, BB0));
        EXPECT_FALSE(DT->properlyDominates(BB1, BB1));
        EXPECT_FALSE(DT->properlyDominates(BB1, BB2));
        EXPECT_TRUE(DT->properlyDominates(BB1, BB3));

        EXPECT_FALSE(DT->properlyDominates(BB2, BB0));
        EXPECT_FALSE(DT->properlyDominates(BB2, BB1));
        EXPECT_FALSE(DT->properlyDominates(BB2, BB2));
        EXPECT_TRUE(DT->properlyDominates(BB2, BB3));

        EXPECT_FALSE(DT->properlyDominates(BB3, BB0));
        EXPECT_FALSE(DT->properlyDominates(BB3, BB1));
        EXPECT_FALSE(DT->properlyDominates(BB3, BB2));
        EXPECT_FALSE(DT->properlyDominates(BB3, BB3));

        // Instruction dominance in the same reachable BB
        EXPECT_FALSE(DT->dominates(Y1, Y1));
        EXPECT_TRUE(DT->dominates(Y1, Y2));
        EXPECT_FALSE(DT->dominates(Y2, Y1));
        EXPECT_FALSE(DT->dominates(Y2, Y2));

        // Instruction dominance in the same unreachable BB
        EXPECT_TRUE(DT->dominates(Y6, Y6));
        EXPECT_TRUE(DT->dominates(Y6, Y7));
        EXPECT_TRUE(DT->dominates(Y7, Y6));
        EXPECT_TRUE(DT->dominates(Y7, Y7));

        // Invoke
        EXPECT_TRUE(DT->dominates(Y3, Y4));
        EXPECT_FALSE(DT->dominates(Y3, Y5));

        // Phi
        EXPECT_TRUE(DT->dominates(Y2, Y9));
        EXPECT_FALSE(DT->dominates(Y3, Y9));
        EXPECT_FALSE(DT->dominates(Y8, Y9));

        // Anything dominates unreachable
        EXPECT_TRUE(DT->dominates(Y1, Y6));
        EXPECT_TRUE(DT->dominates(Y3, Y6));

        // Unreachable doesn't dominate reachable
        EXPECT_FALSE(DT->dominates(Y6, Y1));

        // Instruction, BB dominance
        EXPECT_FALSE(DT->dominates(Y1, BB0));
        EXPECT_TRUE(DT->dominates(Y1, BB1));
        EXPECT_TRUE(DT->dominates(Y1, BB2));
        EXPECT_TRUE(DT->dominates(Y1, BB3));
        EXPECT_TRUE(DT->dominates(Y1, BB4));

        EXPECT_FALSE(DT->dominates(Y3, BB0));
        EXPECT_TRUE(DT->dominates(Y3, BB1));
        EXPECT_FALSE(DT->dominates(Y3, BB2));
        EXPECT_TRUE(DT->dominates(Y3, BB3));
        EXPECT_FALSE(DT->dominates(Y3, BB4));

        EXPECT_TRUE(DT->dominates(Y6, BB3));

        // Post dominance.
        EXPECT_TRUE(PDT->dominates(BB0, BB0));
        EXPECT_FALSE(PDT->dominates(BB1, BB0));
        EXPECT_FALSE(PDT->dominates(BB2, BB0));
        EXPECT_FALSE(PDT->dominates(BB3, BB0));
        EXPECT_TRUE(PDT->dominates(BB4, BB1));

        // Dominance descendants.
        SmallVector<BasicBlock *, 8> DominatedBBs, PostDominatedBBs;

        DT->getDescendants(BB0, DominatedBBs);
        PDT->getDescendants(BB0, PostDominatedBBs);
        EXPECT_EQ(DominatedBBs.size(), 4UL);
        EXPECT_EQ(PostDominatedBBs.size(), 1UL);

        // BB3 is unreachable. It should have no dominators nor postdominators.
        DominatedBBs.clear();
        PostDominatedBBs.clear();
        DT->getDescendants(BB3, DominatedBBs);
        DT->getDescendants(BB3, PostDominatedBBs);
        EXPECT_EQ(DominatedBBs.size(), 0UL);
        EXPECT_EQ(PostDominatedBBs.size(), 0UL);

        return false;
      }
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<DominatorTreeWrapperPass>();
        AU.addRequired<PostDominatorTree>();
      }
      DPass() : FunctionPass(ID) {
        initializeDPassPass(*PassRegistry::getPassRegistry());
      }
    };
    char DPass::ID = 0;

    std::unique_ptr<Module> makeLLVMModule(DPass *P) {
      const char *ModuleStrig =
        "declare i32 @g()\n" \
        "define void @f(i32 %x) {\n" \
        "bb0:\n" \
        "  %y1 = add i32 %x, 1\n" \
        "  %y2 = add i32 %x, 1\n" \
        "  %y3 = invoke i32 @g() to label %bb1 unwind label %bb2\n" \
        "bb1:\n" \
        "  %y4 = add i32 %x, 1\n" \
        "  br label %bb4\n" \
        "bb2:\n" \
        "  %y5 = landingpad i32 personality i32 ()* @g\n" \
        "          cleanup\n" \
        "  br label %bb4\n" \
        "bb3:\n" \
        "  %y6 = add i32 %x, 1\n" \
        "  %y7 = add i32 %x, 1\n" \
        "  ret void\n" \
        "bb4:\n" \
        "  %y8 = phi i32 [0, %bb2], [%y4, %bb1]\n"
        "  %y9 = phi i32 [0, %bb2], [%y4, %bb1]\n"
        "  ret void\n" \
        "}\n";
      LLVMContext &C = getGlobalContext();
      SMDiagnostic Err;
      return parseAssemblyString(ModuleStrig, Err, C);
    }

    TEST(DominatorTree, Unreachable) {
      DPass *P = new DPass();
      std::unique_ptr<Module> M = makeLLVMModule(P);
      PassManager Passes;
      Passes.add(P);
      Passes.run(*M);
    }
  }
}

INITIALIZE_PASS_BEGIN(DPass, "dpass", "dpass", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTree)
INITIALIZE_PASS_END(DPass, "dpass", "dpass", false, false)
