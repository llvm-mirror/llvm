//===-- EVMStackification.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "EVMRegisterInfo.h"
#include "EVMTargetMachine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/LiveIntervals.h"

using namespace llvm;

#define DEBUG_TYPE "evm-stackification"

namespace {

/// A stack for walking the tree of instructions being built, visiting the
/// MachineOperands in DFS order.
// ported over from WebAssembly backend.
class TreeWalkerState {

  using mop_iterator = MachineInstr::mop_iterator;
  using mop_reverse_iterator = std::reverse_iterator<mop_iterator>;
  using RangeTy = iterator_range<mop_reverse_iterator>;
  SmallVector<RangeTy, 4> Worklist;

public:
  explicit TreeWalkerState(MachineInstr *Insert) {
    const iterator_range<mop_iterator> &Range = Insert->explicit_uses();
    if (Range.begin() != Range.end())
      Worklist.push_back(reverse(Range));
  }

  bool done() const { return Worklist.empty(); }

  MachineOperand &pop() {
    RangeTy &Range = Worklist.back();
    MachineOperand &Op = *Range.begin();
    Range = drop_begin(Range, 1);
    if (Range.begin() == Range.end())
      Worklist.pop_back();
    assert((Worklist.empty() ||
            Worklist.back().begin() != Worklist.back().end()) &&
           "Empty ranges shouldn't remain in the worklist");
    return Op;
  }

  /// Push Instr's operands onto the stack to be visited.
  void pushOperands(MachineInstr *Instr) {
    const iterator_range<mop_iterator> &Range(Instr->explicit_uses());
    if (Range.begin() != Range.end())
      Worklist.push_back(reverse(Range));
  }

  /// Some of Instr's operands are on the top of the stack; remove them and
  /// re-insert them starting from the beginning (because we've commuted them).
  void resetTopOperands(MachineInstr *Instr) {
    assert(hasRemainingOperands(Instr) &&
           "Reseting operands should only be done when the instruction has "
           "an operand still on the stack");
    Worklist.back() = reverse(Instr->explicit_uses());
  }

  /// Test whether Instr has operands remaining to be visited at the top of
  /// the stack.
  bool hasRemainingOperands(const MachineInstr *Instr) const {
    if (Worklist.empty())
      return false;
    const RangeTy &Range = Worklist.back();
    return Range.begin() != Range.end() && Range.begin()->getParent() == Instr;
  }

  /// Test whether the given register is present on the stack, indicating an
  /// operand in the tree that we haven't visited yet. Moving a definition of
  /// Reg to a point in the tree after that would change its value.
  ///
  /// This is needed as a consequence of using implicit local.gets for
  /// uses and implicit local.sets for defs.
  bool isOnStack(unsigned Reg) const {
    for (const RangeTy &Range : Worklist)
      for (const MachineOperand &MO : Range)
        if (MO.isReg() && MO.getReg() == Reg)
          return true;
    return false;
  }
};

/// State to keep track of whether commuting is in flight or whether it's been
/// tried for the current instruction and didn't work.
// Ported over from WebAssembly backend.
class CommutingState {
  /// There are effectively three states: the initial state where we haven't
  /// started commuting anything and we don't know anything yet, the tentative
  /// state where we've commuted the operands of the current instruction and are
  /// revisiting it, and the declined state where we've reverted the operands
  /// back to their original order and will no longer commute it further.
  bool TentativelyCommuting = false;
  bool Declined = false;

  /// During the tentative state, these hold the operand indices of the commuted
  /// operands.
  unsigned Operand0, Operand1;

public:
  /// Stackification for an operand was not successful due to ordering
  /// constraints. If possible, and if we haven't already tried it and declined
  /// it, commute Insert's operands and prepare to revisit it.
  void maybeCommute(MachineInstr *Insert, TreeWalkerState &TreeWalker,
                    const EVMInstrInfo *TII) {
    if (TentativelyCommuting) {
      assert(!Declined &&
             "Don't decline commuting until you've finished trying it");
      // Commuting didn't help. Revert it.
      TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
      TentativelyCommuting = false;
      Declined = true;
    } else if (!Declined && TreeWalker.hasRemainingOperands(Insert)) {
      Operand0 = TargetInstrInfo::CommuteAnyOperandIndex;
      Operand1 = TargetInstrInfo::CommuteAnyOperandIndex;
      if (TII->findCommutedOpIndices(*Insert, Operand0, Operand1)) {
        // Tentatively commute the operands and try again.
        TII->commuteInstruction(*Insert, /*NewMI=*/false, Operand0, Operand1);
        TreeWalker.resetTopOperands(Insert);
        TentativelyCommuting = true;
        Declined = false;
      }
    }
  }

  /// Stackification for some operand was successful. Reset to the default
  /// state.
  void reset() {
    TentativelyCommuting = false;
    Declined = false;
  }
};


class EVMStackification final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMStackification() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Add Jumpdest"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMStackification::ID = 0;
INITIALIZE_PASS(EVMStackification, DEBUG_TYPE,
      "Converting register-based instructions into stack instructions",
      false, false)

FunctionPass *llvm::createEVMStackification() {
  return new EVMStackification();
}

bool EVMStackification::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Stackification **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
  const auto *TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  const auto *TRI = MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto &MDT = getAnalysis<MachineDominatorTree>();
  auto &LIS = getAnalysis<LiveIntervals>();

  for (MachineBasicBlock & MBB : MF) {
    // Keep track of the actual stack pointer so we know the offset off the
    // operands and local operands.


    // Don't use a range-based for loop, because we modify the list as we're
    // iterating over it and the end iterator may change.
    for (auto MII = MBB.rbegin(); MII != MBB.rend(); ++MII) {
      MachineInstr *Insert = &*MII;

      // Don't nest anything inside an inline asm, because we don't have
      // constraints for $push inputs.
      if (Insert->isInlineAsm())
        continue;

      // Ignore debugging intrinsics.
      if (Insert->isDebugValue())
        continue;

      // Iterate through the inputs in reverse order, since we'll be pulling
      // operands off the stack in LIFO order.
      CommutingState Commuting;
      TreeWalkerState TreeWalker(Insert);
      while (!TreeWalker.done()) {
        MachineOperand &Op = TreeWalker.pop();

        // We're only interested in explicit virtual register operands.
        if (!Op.isReg())
          continue;

        unsigned Reg = Op.getReg();
        assert(Op.isUse() && "explicit_uses() should only iterate over uses");
        assert(!Op.isImplicit() &&
               "explicit_uses() should only iterate over explicit operands");
      }

    }
  }

  return Changed;
}
