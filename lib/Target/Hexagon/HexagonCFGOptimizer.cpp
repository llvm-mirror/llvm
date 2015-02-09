//===-- HexagonCFGOptimizer.cpp - CFG optimizations -----------------------===//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "hexagon_cfg"

namespace llvm {
  void initializeHexagonCFGOptimizerPass(PassRegistry&);
}


namespace {

class HexagonCFGOptimizer : public MachineFunctionPass {

private:
  void InvertAndChangeJumpTarget(MachineInstr*, MachineBasicBlock*);

 public:
  static char ID;
  HexagonCFGOptimizer() : MachineFunctionPass(ID) {
    initializeHexagonCFGOptimizerPass(*PassRegistry::getPassRegistry());
  }

  const char *getPassName() const override {
    return "Hexagon CFG Optimizer";
  }
  bool runOnMachineFunction(MachineFunction &Fn) override;
};


char HexagonCFGOptimizer::ID = 0;

static bool IsConditionalBranch(int Opc) {
  return (Opc == Hexagon::J2_jumpt) || (Opc == Hexagon::J2_jumpf)
    || (Opc == Hexagon::J2_jumptnewpt) || (Opc == Hexagon::J2_jumpfnewpt);
}


static bool IsUnconditionalJump(int Opc) {
  return (Opc == Hexagon::J2_jump);
}


void
HexagonCFGOptimizer::InvertAndChangeJumpTarget(MachineInstr* MI,
                                               MachineBasicBlock* NewTarget) {
  const TargetInstrInfo *TII =
      MI->getParent()->getParent()->getSubtarget().getInstrInfo();
  int NewOpcode = 0;
  switch(MI->getOpcode()) {
  case Hexagon::J2_jumpt:
    NewOpcode = Hexagon::J2_jumpf;
    break;

  case Hexagon::J2_jumpf:
    NewOpcode = Hexagon::J2_jumpt;
    break;

  case Hexagon::J2_jumptnewpt:
    NewOpcode = Hexagon::J2_jumpfnewpt;
    break;

  case Hexagon::J2_jumpfnewpt:
    NewOpcode = Hexagon::J2_jumptnewpt;
    break;

  default:
    llvm_unreachable("Cannot handle this case");
  }

  MI->setDesc(TII->get(NewOpcode));
  MI->getOperand(1).setMBB(NewTarget);
}


bool HexagonCFGOptimizer::runOnMachineFunction(MachineFunction &Fn) {
  // Loop over all of the basic blocks.
  for (MachineFunction::iterator MBBb = Fn.begin(), MBBe = Fn.end();
       MBBb != MBBe; ++MBBb) {
    MachineBasicBlock* MBB = MBBb;

    // Traverse the basic block.
    MachineBasicBlock::iterator MII = MBB->getFirstTerminator();
    if (MII != MBB->end()) {
      MachineInstr *MI = MII;
      int Opc = MI->getOpcode();
      if (IsConditionalBranch(Opc)) {

        //
        // (Case 1) Transform the code if the following condition occurs:
        //   BB1: if (p0) jump BB3
        //   ...falls-through to BB2 ...
        //   BB2: jump BB4
        //   ...next block in layout is BB3...
        //   BB3: ...
        //
        //  Transform this to:
        //  BB1: if (!p0) jump BB4
        //  Remove BB2
        //  BB3: ...
        //
        // (Case 2) A variation occurs when BB3 contains a JMP to BB4:
        //   BB1: if (p0) jump BB3
        //   ...falls-through to BB2 ...
        //   BB2: jump BB4
        //   ...other basic blocks ...
        //   BB4:
        //   ...not a fall-thru
        //   BB3: ...
        //     jump BB4
        //
        // Transform this to:
        //   BB1: if (!p0) jump BB4
        //   Remove BB2
        //   BB3: ...
        //   BB4: ...
        //
        unsigned NumSuccs = MBB->succ_size();
        MachineBasicBlock::succ_iterator SI = MBB->succ_begin();
        MachineBasicBlock* FirstSucc = *SI;
        MachineBasicBlock* SecondSucc = *(++SI);
        MachineBasicBlock* LayoutSucc = nullptr;
        MachineBasicBlock* JumpAroundTarget = nullptr;

        if (MBB->isLayoutSuccessor(FirstSucc)) {
          LayoutSucc = FirstSucc;
          JumpAroundTarget = SecondSucc;
        } else if (MBB->isLayoutSuccessor(SecondSucc)) {
          LayoutSucc = SecondSucc;
          JumpAroundTarget = FirstSucc;
        } else {
          // Odd case...cannot handle.
        }

        // The target of the unconditional branch must be JumpAroundTarget.
        // TODO: If not, we should not invert the unconditional branch.
        MachineBasicBlock* CondBranchTarget = nullptr;
        if ((MI->getOpcode() == Hexagon::J2_jumpt) ||
            (MI->getOpcode() == Hexagon::J2_jumpf)) {
          CondBranchTarget = MI->getOperand(1).getMBB();
        }

        if (!LayoutSucc || (CondBranchTarget != JumpAroundTarget)) {
          continue;
        }

        if ((NumSuccs == 2) && LayoutSucc && (LayoutSucc->pred_size() == 1)) {

          // Ensure that BB2 has one instruction -- an unconditional jump.
          if ((LayoutSucc->size() == 1) &&
              IsUnconditionalJump(LayoutSucc->front().getOpcode())) {
            MachineBasicBlock* UncondTarget =
              LayoutSucc->front().getOperand(0).getMBB();
            // Check if the layout successor of BB2 is BB3.
            bool case1 = LayoutSucc->isLayoutSuccessor(JumpAroundTarget);
            bool case2 = JumpAroundTarget->isSuccessor(UncondTarget) &&
              JumpAroundTarget->size() >= 1 &&
              IsUnconditionalJump(JumpAroundTarget->back().getOpcode()) &&
              JumpAroundTarget->pred_size() == 1 &&
              JumpAroundTarget->succ_size() == 1;

            if (case1 || case2) {
              InvertAndChangeJumpTarget(MI, UncondTarget);
              MBB->removeSuccessor(JumpAroundTarget);
              MBB->addSuccessor(UncondTarget);

              // Remove the unconditional branch in LayoutSucc.
              LayoutSucc->erase(LayoutSucc->begin());
              LayoutSucc->removeSuccessor(UncondTarget);
              LayoutSucc->addSuccessor(JumpAroundTarget);

              // This code performs the conversion for case 2, which moves
              // the block to the fall-thru case (BB3 in the code above).
              if (case2 && !case1) {
                JumpAroundTarget->moveAfter(LayoutSucc);
                // only move a block if it doesn't have a fall-thru. otherwise
                // the CFG will be incorrect.
                if (!UncondTarget->canFallThrough()) {
                  UncondTarget->moveAfter(JumpAroundTarget);
                }
              }

              //
              // Correct live-in information. Is used by post-RA scheduler
              // The live-in to LayoutSucc is now all values live-in to
              // JumpAroundTarget.
              //
              std::vector<unsigned> OrigLiveIn(LayoutSucc->livein_begin(),
                                               LayoutSucc->livein_end());
              std::vector<unsigned> NewLiveIn(JumpAroundTarget->livein_begin(),
                                              JumpAroundTarget->livein_end());
              for (unsigned i = 0; i < OrigLiveIn.size(); ++i) {
                LayoutSucc->removeLiveIn(OrigLiveIn[i]);
              }
              for (unsigned i = 0; i < NewLiveIn.size(); ++i) {
                LayoutSucc->addLiveIn(NewLiveIn[i]);
              }
            }
          }
        }
      }
    }
  }
  return true;
}
}


//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

static void initializePassOnce(PassRegistry &Registry) {
  PassInfo *PI = new PassInfo("Hexagon CFG Optimizer", "hexagon-cfg",
                              &HexagonCFGOptimizer::ID, nullptr, false, false);
  Registry.registerPass(*PI, true);
}

void llvm::initializeHexagonCFGOptimizerPass(PassRegistry &Registry) {
  CALL_ONCE_INITIALIZATION(initializePassOnce)
}

FunctionPass *llvm::createHexagonCFGOptimizer() {
  return new HexagonCFGOptimizer();
}
