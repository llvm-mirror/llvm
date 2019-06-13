//===-- EVMStackification.cpp - Optimize stack operands ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file Ported over from WebAssembly backend.
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

#include "EVMTreeWalker.h"

using namespace llvm;

#define DEBUG_TYPE "evm-stackification"

namespace {

class EVMStackification final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMStackification() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Stackification"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
private:
  bool isSingleDefSingleUse(const MachineRegisterInfo &MRI, unsigned RegNo);
};
} // end anonymous namespace

char EVMStackification::ID = 0;
INITIALIZE_PASS(EVMStackification, DEBUG_TYPE,
      "Converting register-based instructions into stack instructions",
      false, false)

FunctionPass *llvm::createEVMStackification() {
  return new EVMStackification();
}

// Identify the definition for this register at this point. This is a
// generalization of MachineRegisterInfo::getUniqueVRegDef that uses
// LiveIntervals to handle complex cases.
static MachineInstr *getVRegDef(unsigned Reg, const MachineInstr *Insert,
                                const MachineRegisterInfo &MRI,
                                const LiveIntervals &LIS) {
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MachineInstr *Def = MRI.getUniqueVRegDef(Reg))
    return Def;

  // MRI doesn't know what the Def is. Try asking LIS.
  if (const VNInfo *ValNo = LIS.getInterval(Reg).getVNInfoBefore(
          LIS.getInstructionIndex(*Insert)))
    return LIS.getInstructionFromIndex(ValNo->def);

  return nullptr;
}

bool EVMStackification::isSingleDefSingleUse(const MachineRegisterInfo &MRI,
                                             unsigned RegNo) {
  return (MRI.hasOneUse(RegNo) && MRI.hasOneDef(RegNo));
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
      if (Insert->isInlineAsm()) {
        llvm_unreachable("unimplemented");
      }

      // Ignore debugging intrinsics.
      if (Insert->isDebugValue()) {
        llvm_unreachable("unimplemented");
      }

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
        assert(!TargetRegisterInfo::isPhysicalRegister(Reg) &&
               "Should not have any physical registers");

        MachineInstr *Def = getVRegDef(Reg, Insert, MRI, LIS);
        if (!Def) {
          llvm_unreachable("cannot find def instruction, needs implementing");
        }

        if (Def->isInlineAsm()) {
          llvm_unreachable("unimplemented");
        }

        // Do not change arguments coming from the stack.
        if (Def->getOpcode() == EVM::pSTACKARG_r) {
          continue;
        }

        bool SameBlock = Def->getParent() == &MBB;
        // TODO: implement isSafeToMove.
        // isSafeToMove is very complicated. This approach is best to be part of the 
        // PostRA scheduler.
        bool CanMove = SameBlock && /*isSafeToMove(Def, Insert, AA, MRI) &&*/
                       !TreeWalker.isOnStack(Reg);

      }

    }
  }

  return Changed;
}
