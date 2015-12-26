//===-- FuncletLayout.cpp - Contiguously lay out funclets -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements basic block placement transformations which result in
// funclets being contiguous.
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "funclet-layout"

namespace {
class FuncletLayout : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  FuncletLayout() : MachineFunctionPass(ID) {
    initializeFuncletLayoutPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;
};
}

char FuncletLayout::ID = 0;
char &llvm::FuncletLayoutID = FuncletLayout::ID;
INITIALIZE_PASS(FuncletLayout, "funclet-layout",
                "Contiguously Lay Out Funclets", false, false)

bool FuncletLayout::runOnMachineFunction(MachineFunction &F) {
  DenseMap<const MachineBasicBlock *, int> FuncletMembership =
      getFuncletMembership(F);
  if (FuncletMembership.empty())
    return false;

  F.sort([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto FuncletX = FuncletMembership.find(&X);
    auto FuncletY = FuncletMembership.find(&Y);
    assert(FuncletX != FuncletMembership.end());
    assert(FuncletY != FuncletMembership.end());
    return FuncletX->second < FuncletY->second;
  });

  // Conservatively assume we changed something.
  return true;
}
