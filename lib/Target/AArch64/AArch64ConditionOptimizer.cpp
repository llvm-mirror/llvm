//=- AArch64ConditionOptimizer.cpp - Remove useless comparisons for AArch64 -=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to make consecutive compares of values use same operands to
// allow CSE pass to remove duplicated instructions.  For this it analyzes
// branches and adjusts comparisons with immediate values by converting:
//  * GE -> GT
//  * GT -> GE
//  * LT -> LE
//  * LE -> LT
// and adjusting immediate values appropriately.  It basically corrects two
// immediate values towards each other to make them equal.
//
// Consider the following example in C:
//
//   if ((a < 5 && ...) || (a > 5 && ...)) {
//        ~~~~~             ~~~~~
//          ^                 ^
//          x                 y
//
// Here both "x" and "y" expressions compare "a" with "5".  When "x" evaluates
// to "false", "y" can just check flags set by the first comparison.  As a
// result of the canonicalization employed by
// SelectionDAGBuilder::visitSwitchCase, DAGCombine, and other target-specific
// code, assembly ends up in the form that is not CSE friendly:
//
//     ...
//     cmp      w8, #4
//     b.gt     .LBB0_3
//     ...
//   .LBB0_3:
//     cmp      w8, #6
//     b.lt     .LBB0_6
//     ...
//
// Same assembly after the pass:
//
//     ...
//     cmp      w8, #5
//     b.ge     .LBB0_3
//     ...
//   .LBB0_3:
//     cmp      w8, #5     // <-- CSE pass removes this instruction
//     b.le     .LBB0_6
//     ...
//
// Currently only SUBS and ADDS followed by b.?? are supported.
//
// TODO: maybe handle TBNZ/TBZ the same way as CMP when used instead for "a < 0"
// TODO: handle other conditional instructions (e.g. CSET)
// TODO: allow second branching to be anything if it doesn't require adjusting
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <cstdlib>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "aarch64-condopt"

STATISTIC(NumConditionsAdjusted, "Number of conditions adjusted");

namespace {
class AArch64ConditionOptimizer : public MachineFunctionPass {
  const TargetInstrInfo *TII;
  MachineDominatorTree *DomTree;

public:
  // Stores immediate, compare instruction opcode and branch condition (in this
  // order) of adjusted comparison.
  typedef std::tuple<int, int, AArch64CC::CondCode> CmpInfo;

  static char ID;
  AArch64ConditionOptimizer() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  MachineInstr *findSuitableCompare(MachineBasicBlock *MBB);
  CmpInfo adjustCmp(MachineInstr *CmpMI, AArch64CC::CondCode Cmp);
  void modifyCmp(MachineInstr *CmpMI, const CmpInfo &Info);
  bool adjustTo(MachineInstr *CmpMI, AArch64CC::CondCode Cmp, MachineInstr *To,
                int ToImm);
  bool runOnMachineFunction(MachineFunction &MF) override;
  const char *getPassName() const override {
    return "AArch64 Condition Optimizer";
  }
};
} // end anonymous namespace

char AArch64ConditionOptimizer::ID = 0;

namespace llvm {
void initializeAArch64ConditionOptimizerPass(PassRegistry &);
}

INITIALIZE_PASS_BEGIN(AArch64ConditionOptimizer, "aarch64-condopt",
                      "AArch64 CondOpt Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(AArch64ConditionOptimizer, "aarch64-condopt",
                    "AArch64 CondOpt Pass", false, false)

FunctionPass *llvm::createAArch64ConditionOptimizerPass() {
  return new AArch64ConditionOptimizer();
}

void AArch64ConditionOptimizer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineDominatorTree>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

// Finds compare instruction that corresponds to supported types of branching.
// Returns the instruction or nullptr on failures or detecting unsupported
// instructions.
MachineInstr *AArch64ConditionOptimizer::findSuitableCompare(
    MachineBasicBlock *MBB) {
  MachineBasicBlock::iterator I = MBB->getFirstTerminator();
  if (I == MBB->end())
    return nullptr;

  if (I->getOpcode() != AArch64::Bcc)
    return nullptr;

  // Now find the instruction controlling the terminator.
  for (MachineBasicBlock::iterator B = MBB->begin(); I != B;) {
    --I;
    assert(!I->isTerminator() && "Spurious terminator");
    switch (I->getOpcode()) {
    // cmp is an alias for subs with a dead destination register.
    case AArch64::SUBSWri:
    case AArch64::SUBSXri:
    // cmn is an alias for adds with a dead destination register.
    case AArch64::ADDSWri:
    case AArch64::ADDSXri:
      if (I->getOperand(0).isDead())
        return I;

      DEBUG(dbgs() << "Destination of cmp is not dead, " << *I << '\n');
      return nullptr;

    // Prevent false positive case like:
    // cmp      w19, #0
    // cinc     w0, w19, gt
    // ...
    // fcmp     d8, #0.0
    // b.gt     .LBB0_5
    case AArch64::FCMPDri:
    case AArch64::FCMPSri:
    case AArch64::FCMPESri:
    case AArch64::FCMPEDri:

    case AArch64::SUBSWrr:
    case AArch64::SUBSXrr:
    case AArch64::ADDSWrr:
    case AArch64::ADDSXrr:
    case AArch64::FCMPSrr:
    case AArch64::FCMPDrr:
    case AArch64::FCMPESrr:
    case AArch64::FCMPEDrr:
      // Skip comparison instructions without immediate operands.
      return nullptr;
    }
  }
  DEBUG(dbgs() << "Flags not defined in BB#" << MBB->getNumber() << '\n');
  return nullptr;
}

// Changes opcode adds <-> subs considering register operand width.
static int getComplementOpc(int Opc) {
  switch (Opc) {
  case AArch64::ADDSWri: return AArch64::SUBSWri;
  case AArch64::ADDSXri: return AArch64::SUBSXri;
  case AArch64::SUBSWri: return AArch64::ADDSWri;
  case AArch64::SUBSXri: return AArch64::ADDSXri;
  default:
    llvm_unreachable("Unexpected opcode");
  }
}

// Changes form of comparison inclusive <-> exclusive.
static AArch64CC::CondCode getAdjustedCmp(AArch64CC::CondCode Cmp) {
  switch (Cmp) {
  case AArch64CC::GT: return AArch64CC::GE;
  case AArch64CC::GE: return AArch64CC::GT;
  case AArch64CC::LT: return AArch64CC::LE;
  case AArch64CC::LE: return AArch64CC::LT;
  default:
    llvm_unreachable("Unexpected condition code");
  }
}

// Transforms GT -> GE, GE -> GT, LT -> LE, LE -> LT by updating comparison
// operator and condition code.
AArch64ConditionOptimizer::CmpInfo AArch64ConditionOptimizer::adjustCmp(
    MachineInstr *CmpMI, AArch64CC::CondCode Cmp) {
  int Opc = CmpMI->getOpcode();

  // CMN (compare with negative immediate) is an alias to ADDS (as
  // "operand - negative" == "operand + positive")
  bool Negative = (Opc == AArch64::ADDSWri || Opc == AArch64::ADDSXri);

  int Correction = (Cmp == AArch64CC::GT) ? 1 : -1;
  // Negate Correction value for comparison with negative immediate (CMN).
  if (Negative) {
    Correction = -Correction;
  }

  const int OldImm = (int)CmpMI->getOperand(2).getImm();
  const int NewImm = std::abs(OldImm + Correction);

  // Handle +0 -> -1 and -0 -> +1 (CMN with 0 immediate) transitions by
  // adjusting compare instruction opcode.
  if (OldImm == 0 && ((Negative && Correction == 1) ||
                      (!Negative && Correction == -1))) {
    Opc = getComplementOpc(Opc);
  }

  return CmpInfo(NewImm, Opc, getAdjustedCmp(Cmp));
}

// Applies changes to comparison instruction suggested by adjustCmp().
void AArch64ConditionOptimizer::modifyCmp(MachineInstr *CmpMI,
    const CmpInfo &Info) {
  int Imm;
  int Opc;
  AArch64CC::CondCode Cmp;
  std::tie(Imm, Opc, Cmp) = Info;

  MachineBasicBlock *const MBB = CmpMI->getParent();

  // Change immediate in comparison instruction (ADDS or SUBS).
  BuildMI(*MBB, CmpMI, CmpMI->getDebugLoc(), TII->get(Opc))
      .addOperand(CmpMI->getOperand(0))
      .addOperand(CmpMI->getOperand(1))
      .addImm(Imm)
      .addOperand(CmpMI->getOperand(3));
  CmpMI->eraseFromParent();

  // The fact that this comparison was picked ensures that it's related to the
  // first terminator instruction.
  MachineInstr *BrMI = MBB->getFirstTerminator();

  // Change condition in branch instruction.
  BuildMI(*MBB, BrMI, BrMI->getDebugLoc(), TII->get(AArch64::Bcc))
      .addImm(Cmp)
      .addOperand(BrMI->getOperand(1));
  BrMI->eraseFromParent();

  MBB->updateTerminator();

  ++NumConditionsAdjusted;
}

// Parse a condition code returned by AnalyzeBranch, and compute the CondCode
// corresponding to TBB.
// Returns true if parsing was successful, otherwise false is returned.
static bool parseCond(ArrayRef<MachineOperand> Cond, AArch64CC::CondCode &CC) {
  // A normal br.cond simply has the condition code.
  if (Cond[0].getImm() != -1) {
    assert(Cond.size() == 1 && "Unknown Cond array format");
    CC = (AArch64CC::CondCode)(int)Cond[0].getImm();
    return true;
  }
  return false;
}

// Adjusts one cmp instruction to another one if result of adjustment will allow
// CSE.  Returns true if compare instruction was changed, otherwise false is
// returned.
bool AArch64ConditionOptimizer::adjustTo(MachineInstr *CmpMI,
  AArch64CC::CondCode Cmp, MachineInstr *To, int ToImm)
{
  CmpInfo Info = adjustCmp(CmpMI, Cmp);
  if (std::get<0>(Info) == ToImm && std::get<1>(Info) == To->getOpcode()) {
    modifyCmp(CmpMI, Info);
    return true;
  }
  return false;
}

bool AArch64ConditionOptimizer::runOnMachineFunction(MachineFunction &MF) {
  DEBUG(dbgs() << "********** AArch64 Conditional Compares **********\n"
               << "********** Function: " << MF.getName() << '\n');
  TII = MF.getSubtarget().getInstrInfo();
  DomTree = &getAnalysis<MachineDominatorTree>();

  bool Changed = false;

  // Visit blocks in dominator tree pre-order. The pre-order enables multiple
  // cmp-conversions from the same head block.
  // Note that updateDomTree() modifies the children of the DomTree node
  // currently being visited. The df_iterator supports that; it doesn't look at
  // child_begin() / child_end() until after a node has been visited.
  for (MachineDomTreeNode *I : depth_first(DomTree)) {
    MachineBasicBlock *HBB = I->getBlock();

    SmallVector<MachineOperand, 4> HeadCond;
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    if (TII->AnalyzeBranch(*HBB, TBB, FBB, HeadCond)) {
      continue;
    }

    // Equivalence check is to skip loops.
    if (!TBB || TBB == HBB) {
      continue;
    }

    SmallVector<MachineOperand, 4> TrueCond;
    MachineBasicBlock *TBB_TBB = nullptr, *TBB_FBB = nullptr;
    if (TII->AnalyzeBranch(*TBB, TBB_TBB, TBB_FBB, TrueCond)) {
      continue;
    }

    MachineInstr *HeadCmpMI = findSuitableCompare(HBB);
    if (!HeadCmpMI) {
      continue;
    }

    MachineInstr *TrueCmpMI = findSuitableCompare(TBB);
    if (!TrueCmpMI) {
      continue;
    }

    AArch64CC::CondCode HeadCmp;
    if (HeadCond.empty() || !parseCond(HeadCond, HeadCmp)) {
      continue;
    }

    AArch64CC::CondCode TrueCmp;
    if (TrueCond.empty() || !parseCond(TrueCond, TrueCmp)) {
      continue;
    }

    const int HeadImm = (int)HeadCmpMI->getOperand(2).getImm();
    const int TrueImm = (int)TrueCmpMI->getOperand(2).getImm();

    DEBUG(dbgs() << "Head branch:\n");
    DEBUG(dbgs() << "\tcondition: "
          << AArch64CC::getCondCodeName(HeadCmp) << '\n');
    DEBUG(dbgs() << "\timmediate: " << HeadImm << '\n');

    DEBUG(dbgs() << "True branch:\n");
    DEBUG(dbgs() << "\tcondition: "
          << AArch64CC::getCondCodeName(TrueCmp) << '\n');
    DEBUG(dbgs() << "\timmediate: " << TrueImm << '\n');

    if (((HeadCmp == AArch64CC::GT && TrueCmp == AArch64CC::LT) ||
         (HeadCmp == AArch64CC::LT && TrueCmp == AArch64CC::GT)) &&
        std::abs(TrueImm - HeadImm) == 2) {
      // This branch transforms machine instructions that correspond to
      //
      // 1) (a > {TrueImm} && ...) || (a < {HeadImm} && ...)
      // 2) (a < {TrueImm} && ...) || (a > {HeadImm} && ...)
      //
      // into
      //
      // 1) (a >= {NewImm} && ...) || (a <= {NewImm} && ...)
      // 2) (a <= {NewImm} && ...) || (a >= {NewImm} && ...)

      CmpInfo HeadCmpInfo = adjustCmp(HeadCmpMI, HeadCmp);
      CmpInfo TrueCmpInfo = adjustCmp(TrueCmpMI, TrueCmp);
      if (std::get<0>(HeadCmpInfo) == std::get<0>(TrueCmpInfo) &&
          std::get<1>(HeadCmpInfo) == std::get<1>(TrueCmpInfo)) {
        modifyCmp(HeadCmpMI, HeadCmpInfo);
        modifyCmp(TrueCmpMI, TrueCmpInfo);
        Changed = true;
      }
    } else if (((HeadCmp == AArch64CC::GT && TrueCmp == AArch64CC::GT) ||
                (HeadCmp == AArch64CC::LT && TrueCmp == AArch64CC::LT)) &&
                std::abs(TrueImm - HeadImm) == 1) {
      // This branch transforms machine instructions that correspond to
      //
      // 1) (a > {TrueImm} && ...) || (a > {HeadImm} && ...)
      // 2) (a < {TrueImm} && ...) || (a < {HeadImm} && ...)
      //
      // into
      //
      // 1) (a <= {NewImm} && ...) || (a >  {NewImm} && ...)
      // 2) (a <  {NewImm} && ...) || (a >= {NewImm} && ...)

      // GT -> GE transformation increases immediate value, so picking the
      // smaller one; LT -> LE decreases immediate value so invert the choice.
      bool adjustHeadCond = (HeadImm < TrueImm);
      if (HeadCmp == AArch64CC::LT) {
          adjustHeadCond = !adjustHeadCond;
      }

      if (adjustHeadCond) {
        Changed |= adjustTo(HeadCmpMI, HeadCmp, TrueCmpMI, TrueImm);
      } else {
        Changed |= adjustTo(TrueCmpMI, TrueCmp, HeadCmpMI, HeadImm);
      }
    }
    // Other transformation cases almost never occur due to generation of < or >
    // comparisons instead of <= and >=.
  }

  return Changed;
}
