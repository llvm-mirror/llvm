//===-- ARMOptimizeBarriersPass - two DMBs without a memory access in between,
//removed one -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===------------------------------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMInstrInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
using namespace llvm;

#define DEBUG_TYPE "double barriers"

STATISTIC(NumDMBsRemoved, "Number of DMBs removed");

namespace {
class ARMOptimizeBarriersPass : public MachineFunctionPass {
public:
  static char ID;
  ARMOptimizeBarriersPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

  const char *getPassName() const override {
    return "optimise barriers pass";
  }

private:
};
char ARMOptimizeBarriersPass::ID = 0;
}

// Returns whether the instruction can safely move past a DMB instruction
// The current implementation allows this iif MI does not have any possible
// memory access
static bool CanMovePastDMB(const MachineInstr *MI) {
  return !(MI->mayLoad() ||
          MI->mayStore() ||
          MI->hasUnmodeledSideEffects() ||
          MI->isCall() ||
          MI->isReturn());
}

bool ARMOptimizeBarriersPass::runOnMachineFunction(MachineFunction &MF) {
  // Vector to store the DMBs we will remove after the first iteration
  std::vector<MachineInstr *> ToRemove;
  // DMBType is the Imm value of the first operand. It determines whether it's a
  // DMB ish, dmb sy, dmb osh, etc
  int64_t DMBType = -1;

  // Find a dmb. If we can move it until the next dmb, tag the second one for
  // removal
  for (auto &MBB : MF) {
    // Will be true when we have seen a DMB, and not seen any instruction since
    // that cannot move past a DMB
    bool IsRemovableNextDMB = false;
    for (auto &MI : MBB) {
      if (MI.getOpcode() == ARM::DMB) {
        if (IsRemovableNextDMB) {
          // If the Imm of this DMB is the same as that of the last DMB, we can
          // tag this second DMB for removal
          if (MI.getOperand(0).getImm() == DMBType) {
            ToRemove.push_back(&MI);
          } else {
            // If it has a different DMBType, we cannot remove it, but will scan
            // for the next DMB, recording this DMB's type as last seen DMB type
            DMBType = MI.getOperand(0).getImm();
          }
        } else {
          // After we see a DMB, a next one is removable
          IsRemovableNextDMB = true;
          DMBType = MI.getOperand(0).getImm();
        }
      } else if (!CanMovePastDMB(&MI)) {
        // If we find an instruction unable to pass past a DMB, a next DMB is
        // not removable
        IsRemovableNextDMB = false;
      }
    }
  }
  // Remove the tagged DMB
  for (auto MI : ToRemove) {
    MI->eraseFromParent();
    ++NumDMBsRemoved;
  }

  return NumDMBsRemoved > 0;
}

/// createARMOptimizeBarriersPass - Returns an instance of the remove double
/// barriers
/// pass.
FunctionPass *llvm::createARMOptimizeBarriersPass() {
  return new ARMOptimizeBarriersPass();
}
