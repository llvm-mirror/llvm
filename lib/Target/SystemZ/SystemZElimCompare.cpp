//===-- SystemZElimCompare.cpp - Eliminate comparison instructions --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass:
// (1) tries to remove compares if CC already contains the required information
// (2) fuses compares and branches into COMPARE AND BRANCH instructions
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-elim-compare"

STATISTIC(BranchOnCounts, "Number of branch-on-count instructions");
STATISTIC(EliminatedComparisons, "Number of eliminated comparisons");
STATISTIC(FusedComparisons, "Number of fused compare-and-branch instructions");

namespace {
// Represents the references to a particular register in one or more
// instructions.
struct Reference {
  Reference()
    : Def(false), Use(false), IndirectDef(false), IndirectUse(false) {}

  Reference &operator|=(const Reference &Other) {
    Def |= Other.Def;
    IndirectDef |= Other.IndirectDef;
    Use |= Other.Use;
    IndirectUse |= Other.IndirectUse;
    return *this;
  }

  LLVM_EXPLICIT operator bool() const { return Def || Use; }

  // True if the register is defined or used in some form, either directly or
  // via a sub- or super-register.
  bool Def;
  bool Use;

  // True if the register is defined or used indirectly, by a sub- or
  // super-register.
  bool IndirectDef;
  bool IndirectUse;
};

class SystemZElimCompare : public MachineFunctionPass {
public:
  static char ID;
  SystemZElimCompare(const SystemZTargetMachine &tm)
    : MachineFunctionPass(ID), TII(nullptr), TRI(nullptr) {}

  const char *getPassName() const override {
    return "SystemZ Comparison Elimination";
  }

  bool processBlock(MachineBasicBlock &MBB);
  bool runOnMachineFunction(MachineFunction &F) override;

private:
  Reference getRegReferences(MachineInstr *MI, unsigned Reg);
  bool convertToBRCT(MachineInstr *MI, MachineInstr *Compare,
                     SmallVectorImpl<MachineInstr *> &CCUsers);
  bool convertToLoadAndTest(MachineInstr *MI);
  bool adjustCCMasksForInstr(MachineInstr *MI, MachineInstr *Compare,
                             SmallVectorImpl<MachineInstr *> &CCUsers);
  bool optimizeCompareZero(MachineInstr *Compare,
                           SmallVectorImpl<MachineInstr *> &CCUsers);
  bool fuseCompareAndBranch(MachineInstr *Compare,
                            SmallVectorImpl<MachineInstr *> &CCUsers);

  const SystemZInstrInfo *TII;
  const TargetRegisterInfo *TRI;
};

char SystemZElimCompare::ID = 0;
} // end anonymous namespace

FunctionPass *llvm::createSystemZElimComparePass(SystemZTargetMachine &TM) {
  return new SystemZElimCompare(TM);
}

// Return true if CC is live out of MBB.
static bool isCCLiveOut(MachineBasicBlock &MBB) {
  for (auto SI = MBB.succ_begin(), SE = MBB.succ_end(); SI != SE; ++SI)
    if ((*SI)->isLiveIn(SystemZ::CC))
      return true;
  return false;
}

// Return true if any CC result of MI would reflect the value of subreg
// SubReg of Reg.
static bool resultTests(MachineInstr *MI, unsigned Reg, unsigned SubReg) {
  if (MI->getNumOperands() > 0 &&
      MI->getOperand(0).isReg() &&
      MI->getOperand(0).isDef() &&
      MI->getOperand(0).getReg() == Reg &&
      MI->getOperand(0).getSubReg() == SubReg)
    return true;

  switch (MI->getOpcode()) {
  case SystemZ::LR:
  case SystemZ::LGR:
  case SystemZ::LGFR:
  case SystemZ::LTR:
  case SystemZ::LTGR:
  case SystemZ::LTGFR:
  case SystemZ::LER:
  case SystemZ::LDR:
  case SystemZ::LXR:
  case SystemZ::LTEBR:
  case SystemZ::LTDBR:
  case SystemZ::LTXBR:
    if (MI->getOperand(1).getReg() == Reg &&
        MI->getOperand(1).getSubReg() == SubReg)
      return true;
  }

  return false;
}

// Describe the references to Reg in MI, including sub- and super-registers.
Reference SystemZElimCompare::getRegReferences(MachineInstr *MI, unsigned Reg) {
  Reference Ref;
  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);
    if (MO.isReg()) {
      if (unsigned MOReg = MO.getReg()) {
        if (MOReg == Reg || TRI->regsOverlap(MOReg, Reg)) {
          if (MO.isUse()) {
            Ref.Use = true;
            Ref.IndirectUse |= (MOReg != Reg);
          }
          if (MO.isDef()) {
            Ref.Def = true;
            Ref.IndirectDef |= (MOReg != Reg);
          }
        }
      }
    }
  }
  return Ref;
}

// Compare compares the result of MI against zero.  If MI is an addition
// of -1 and if CCUsers is a single branch on nonzero, eliminate the addition
// and convert the branch to a BRCT(G).  Return true on success.
bool
SystemZElimCompare::convertToBRCT(MachineInstr *MI, MachineInstr *Compare,
                                  SmallVectorImpl<MachineInstr *> &CCUsers) {
  // Check whether we have an addition of -1.
  unsigned Opcode = MI->getOpcode();
  unsigned BRCT;
  if (Opcode == SystemZ::AHI)
    BRCT = SystemZ::BRCT;
  else if (Opcode == SystemZ::AGHI)
    BRCT = SystemZ::BRCTG;
  else
    return false;
  if (MI->getOperand(2).getImm() != -1)
    return false;

  // Check whether we have a single JLH.
  if (CCUsers.size() != 1)
    return false;
  MachineInstr *Branch = CCUsers[0];
  if (Branch->getOpcode() != SystemZ::BRC ||
      Branch->getOperand(0).getImm() != SystemZ::CCMASK_ICMP ||
      Branch->getOperand(1).getImm() != SystemZ::CCMASK_CMP_NE)
    return false;

  // We already know that there are no references to the register between
  // MI and Compare.  Make sure that there are also no references between
  // Compare and Branch.
  unsigned SrcReg = Compare->getOperand(0).getReg();
  MachineBasicBlock::iterator MBBI = Compare, MBBE = Branch;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (getRegReferences(MBBI, SrcReg))
      return false;

  // The transformation is OK.  Rebuild Branch as a BRCT(G).
  MachineOperand Target(Branch->getOperand(2));
  Branch->RemoveOperand(2);
  Branch->RemoveOperand(1);
  Branch->RemoveOperand(0);
  Branch->setDesc(TII->get(BRCT));
  MachineInstrBuilder(*Branch->getParent()->getParent(), Branch)
    .addOperand(MI->getOperand(0))
    .addOperand(MI->getOperand(1))
    .addOperand(Target)
    .addReg(SystemZ::CC, RegState::ImplicitDefine);
  MI->removeFromParent();
  return true;
}

// If MI is a load instruction, try to convert it into a LOAD AND TEST.
// Return true on success.
bool SystemZElimCompare::convertToLoadAndTest(MachineInstr *MI) {
  unsigned Opcode = TII->getLoadAndTest(MI->getOpcode());
  if (!Opcode)
    return false;

  MI->setDesc(TII->get(Opcode));
  MachineInstrBuilder(*MI->getParent()->getParent(), MI)
    .addReg(SystemZ::CC, RegState::ImplicitDefine);
  return true;
}

// The CC users in CCUsers are testing the result of a comparison of some
// value X against zero and we know that any CC value produced by MI
// would also reflect the value of X.  Try to adjust CCUsers so that
// they test the result of MI directly, returning true on success.
// Leave everything unchanged on failure.
bool SystemZElimCompare::
adjustCCMasksForInstr(MachineInstr *MI, MachineInstr *Compare,
                      SmallVectorImpl<MachineInstr *> &CCUsers) {
  int Opcode = MI->getOpcode();
  const MCInstrDesc &Desc = TII->get(Opcode);
  unsigned MIFlags = Desc.TSFlags;

  // See which compare-style condition codes are available.
  unsigned ReusableCCMask = SystemZII::getCompareZeroCCMask(MIFlags);

  // For unsigned comparisons with zero, only equality makes sense.
  unsigned CompareFlags = Compare->getDesc().TSFlags;
  if (CompareFlags & SystemZII::IsLogical)
    ReusableCCMask &= SystemZ::CCMASK_CMP_EQ;

  if (ReusableCCMask == 0)
    return false;

  unsigned CCValues = SystemZII::getCCValues(MIFlags);
  assert((ReusableCCMask & ~CCValues) == 0 && "Invalid CCValues");

  // Now check whether these flags are enough for all users.
  SmallVector<MachineOperand *, 4> AlterMasks;
  for (unsigned int I = 0, E = CCUsers.size(); I != E; ++I) {
    MachineInstr *MI = CCUsers[I];

    // Fail if this isn't a use of CC that we understand.
    unsigned Flags = MI->getDesc().TSFlags;
    unsigned FirstOpNum;
    if (Flags & SystemZII::CCMaskFirst)
      FirstOpNum = 0;
    else if (Flags & SystemZII::CCMaskLast)
      FirstOpNum = MI->getNumExplicitOperands() - 2;
    else
      return false;

    // Check whether the instruction predicate treats all CC values
    // outside of ReusableCCMask in the same way.  In that case it
    // doesn't matter what those CC values mean.
    unsigned CCValid = MI->getOperand(FirstOpNum).getImm();
    unsigned CCMask = MI->getOperand(FirstOpNum + 1).getImm();
    unsigned OutValid = ~ReusableCCMask & CCValid;
    unsigned OutMask = ~ReusableCCMask & CCMask;
    if (OutMask != 0 && OutMask != OutValid)
      return false;

    AlterMasks.push_back(&MI->getOperand(FirstOpNum));
    AlterMasks.push_back(&MI->getOperand(FirstOpNum + 1));
  }

  // All users are OK.  Adjust the masks for MI.
  for (unsigned I = 0, E = AlterMasks.size(); I != E; I += 2) {
    AlterMasks[I]->setImm(CCValues);
    unsigned CCMask = AlterMasks[I + 1]->getImm();
    if (CCMask & ~ReusableCCMask)
      AlterMasks[I + 1]->setImm((CCMask & ReusableCCMask) |
                                (CCValues & ~ReusableCCMask));
  }

  // CC is now live after MI.
  int CCDef = MI->findRegisterDefOperandIdx(SystemZ::CC, false, true, TRI);
  assert(CCDef >= 0 && "Couldn't find CC set");
  MI->getOperand(CCDef).setIsDead(false);

  // Clear any intervening kills of CC.
  MachineBasicBlock::iterator MBBI = MI, MBBE = Compare;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    MBBI->clearRegisterKills(SystemZ::CC, TRI);

  return true;
}

// Return true if Compare is a comparison against zero.
static bool isCompareZero(MachineInstr *Compare) {
  switch (Compare->getOpcode()) {
  case SystemZ::LTEBRCompare:
  case SystemZ::LTDBRCompare:
  case SystemZ::LTXBRCompare:
    return true;

  default:
    return (Compare->getNumExplicitOperands() == 2 &&
            Compare->getOperand(1).isImm() &&
            Compare->getOperand(1).getImm() == 0);
  }
}

// Try to optimize cases where comparison instruction Compare is testing
// a value against zero.  Return true on success and if Compare should be
// deleted as dead.  CCUsers is the list of instructions that use the CC
// value produced by Compare.
bool SystemZElimCompare::
optimizeCompareZero(MachineInstr *Compare,
                    SmallVectorImpl<MachineInstr *> &CCUsers) {
  if (!isCompareZero(Compare))
    return false;

  // Search back for CC results that are based on the first operand.
  unsigned SrcReg = Compare->getOperand(0).getReg();
  unsigned SrcSubReg = Compare->getOperand(0).getSubReg();
  MachineBasicBlock &MBB = *Compare->getParent();
  MachineBasicBlock::iterator MBBI = Compare, MBBE = MBB.begin();
  Reference CCRefs;
  Reference SrcRefs;
  while (MBBI != MBBE) {
    --MBBI;
    MachineInstr *MI = MBBI;
    if (resultTests(MI, SrcReg, SrcSubReg)) {
      // Try to remove both MI and Compare by converting a branch to BRCT(G).
      // We don't care in this case whether CC is modified between MI and
      // Compare.
      if (!CCRefs.Use && !SrcRefs && convertToBRCT(MI, Compare, CCUsers)) {
        BranchOnCounts += 1;
        return true;
      }
      // Try to eliminate Compare by reusing a CC result from MI.
      if ((!CCRefs && convertToLoadAndTest(MI)) ||
          (!CCRefs.Def && adjustCCMasksForInstr(MI, Compare, CCUsers))) {
        EliminatedComparisons += 1;
        return true;
      }
    }
    SrcRefs |= getRegReferences(MI, SrcReg);
    if (SrcRefs.Def)
      return false;
    CCRefs |= getRegReferences(MI, SystemZ::CC);
    if (CCRefs.Use && CCRefs.Def)
      return false;
  }
  return false;
}

// Try to fuse comparison instruction Compare into a later branch.
// Return true on success and if Compare is therefore redundant.
bool SystemZElimCompare::
fuseCompareAndBranch(MachineInstr *Compare,
                     SmallVectorImpl<MachineInstr *> &CCUsers) {
  // See whether we have a comparison that can be fused.
  unsigned FusedOpcode = TII->getCompareAndBranch(Compare->getOpcode(),
                                                  Compare);
  if (!FusedOpcode)
    return false;

  // See whether we have a single branch with which to fuse.
  if (CCUsers.size() != 1)
    return false;
  MachineInstr *Branch = CCUsers[0];
  if (Branch->getOpcode() != SystemZ::BRC)
    return false;

  // Make sure that the operands are available at the branch.
  unsigned SrcReg = Compare->getOperand(0).getReg();
  unsigned SrcReg2 = (Compare->getOperand(1).isReg() ?
                      Compare->getOperand(1).getReg() : 0);
  MachineBasicBlock::iterator MBBI = Compare, MBBE = Branch;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (MBBI->modifiesRegister(SrcReg, TRI) ||
        (SrcReg2 && MBBI->modifiesRegister(SrcReg2, TRI)))
      return false;

  // Read the branch mask and target.
  MachineOperand CCMask(MBBI->getOperand(1));
  MachineOperand Target(MBBI->getOperand(2));
  assert((CCMask.getImm() & ~SystemZ::CCMASK_ICMP) == 0 &&
         "Invalid condition-code mask for integer comparison");

  // Clear out all current operands.
  int CCUse = MBBI->findRegisterUseOperandIdx(SystemZ::CC, false, TRI);
  assert(CCUse >= 0 && "BRC must use CC");
  Branch->RemoveOperand(CCUse);
  Branch->RemoveOperand(2);
  Branch->RemoveOperand(1);
  Branch->RemoveOperand(0);

  // Rebuild Branch as a fused compare and branch.
  Branch->setDesc(TII->get(FusedOpcode));
  MachineInstrBuilder(*Branch->getParent()->getParent(), Branch)
    .addOperand(Compare->getOperand(0))
    .addOperand(Compare->getOperand(1))
    .addOperand(CCMask)
    .addOperand(Target)
    .addReg(SystemZ::CC, RegState::ImplicitDefine);

  // Clear any intervening kills of SrcReg and SrcReg2.
  MBBI = Compare;
  for (++MBBI; MBBI != MBBE; ++MBBI) {
    MBBI->clearRegisterKills(SrcReg, TRI);
    if (SrcReg2)
      MBBI->clearRegisterKills(SrcReg2, TRI);
  }
  FusedComparisons += 1;
  return true;
}

// Process all comparison instructions in MBB.  Return true if something
// changed.
bool SystemZElimCompare::processBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  // Walk backwards through the block looking for comparisons, recording
  // all CC users as we go.  The subroutines can delete Compare and
  // instructions before it.
  bool CompleteCCUsers = !isCCLiveOut(MBB);
  SmallVector<MachineInstr *, 4> CCUsers;
  MachineBasicBlock::iterator MBBI = MBB.end();
  while (MBBI != MBB.begin()) {
    MachineInstr *MI = --MBBI;
    if (CompleteCCUsers &&
        MI->isCompare() &&
        (optimizeCompareZero(MI, CCUsers) ||
         fuseCompareAndBranch(MI, CCUsers))) {
      ++MBBI;
      MI->removeFromParent();
      Changed = true;
      CCUsers.clear();
      CompleteCCUsers = true;
      continue;
    }

    Reference CCRefs(getRegReferences(MI, SystemZ::CC));
    if (CCRefs.Def) {
      CCUsers.clear();
      CompleteCCUsers = !CCRefs.IndirectDef;
    }
    if (CompleteCCUsers && CCRefs.Use)
      CCUsers.push_back(MI);
  }
  return Changed;
}

bool SystemZElimCompare::runOnMachineFunction(MachineFunction &F) {
  TII = static_cast<const SystemZInstrInfo *>(F.getSubtarget().getInstrInfo());
  TRI = &TII->getRegisterInfo();

  bool Changed = false;
  for (auto &MBB : F)
    Changed |= processBlock(MBB);

  return Changed;
}
