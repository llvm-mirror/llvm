//===-- ShrinkWrap.cpp - Compute safe point for prolog/epilog insertion ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass looks for safe point where the prologue and epilogue can be
// inserted.
// The safe point for the prologue (resp. epilogue) is called Save
// (resp. Restore).
// A point is safe for prologue (resp. epilogue) if and only if
// it 1) dominates (resp. post-dominates) all the frame related operations and
// between 2) two executions of the Save (resp. Restore) point there is an
// execution of the Restore (resp. Save) point.
//
// For instance, the following points are safe:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Restore -> Save -> Restore ...
// And the following points are not:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
// }
// for (int i = 0; i < 10; ++i) {
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Save -> ... -> Restore -> Restore.
//
// This pass also ensures that the safe points are 3) cheaper than the regular
// entry and exits blocks.
//
// Property #1 is ensured via the use of MachineDominatorTree and
// MachinePostDominatorTree.
// Property #2 is ensured via property #1 and MachineLoopInfo, i.e., both
// points must be in the same loop.
// Property #3 is ensured via the MachineBlockFrequencyInfo.
//
// If this pass found points matching all these properties, then
// MachineFrameInfo is updated with this information.
//===----------------------------------------------------------------------===//
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
// To check for profitability.
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
// For property #1 for Save.
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
// To record the result of the analysis.
#include "llvm/CodeGen/MachineFrameInfo.h"
// For property #2.
#include "llvm/CodeGen/MachineLoopInfo.h"
// For property #1 for Restore.
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/Passes.h"
// To know about callee-saved.
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"
// To query the target about frame lowering.
#include "llvm/Target/TargetFrameLowering.h"
// To know about frame setup operation.
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
// To access TargetInstrInfo.
#include "llvm/Target/TargetSubtargetInfo.h"

#define DEBUG_TYPE "shrink-wrap"

using namespace llvm;

STATISTIC(NumFunc, "Number of functions");
STATISTIC(NumCandidates, "Number of shrink-wrapping candidates");
STATISTIC(NumCandidatesDropped,
          "Number of shrink-wrapping candidates dropped because of frequency");

static cl::opt<cl::boolOrDefault>
    EnableShrinkWrapOpt("enable-shrink-wrap", cl::Hidden,
                        cl::desc("enable the shrink-wrapping pass"));

namespace {
/// \brief Class to determine where the safe point to insert the
/// prologue and epilogue are.
/// Unlike the paper from Fred C. Chow, PLDI'88, that introduces the
/// shrink-wrapping term for prologue/epilogue placement, this pass
/// does not rely on expensive data-flow analysis. Instead we use the
/// dominance properties and loop information to decide which point
/// are safe for such insertion.
class ShrinkWrap : public MachineFunctionPass {
  /// Hold callee-saved information.
  RegisterClassInfo RCI;
  MachineDominatorTree *MDT;
  MachinePostDominatorTree *MPDT;
  /// Current safe point found for the prologue.
  /// The prologue will be inserted before the first instruction
  /// in this basic block.
  MachineBasicBlock *Save;
  /// Current safe point found for the epilogue.
  /// The epilogue will be inserted before the first terminator instruction
  /// in this basic block.
  MachineBasicBlock *Restore;
  /// Hold the information of the basic block frequency.
  /// Use to check the profitability of the new points.
  MachineBlockFrequencyInfo *MBFI;
  /// Hold the loop information. Used to determine if Save and Restore
  /// are in the same loop.
  MachineLoopInfo *MLI;
  /// Frequency of the Entry block.
  uint64_t EntryFreq;
  /// Current opcode for frame setup.
  unsigned FrameSetupOpcode;
  /// Current opcode for frame destroy.
  unsigned FrameDestroyOpcode;
  /// Entry block.
  const MachineBasicBlock *Entry;
  typedef SmallSetVector<unsigned, 16> SetOfRegs;
  /// Registers that need to be saved for the current function.
  mutable SetOfRegs CurrentCSRs;
  /// Current MachineFunction.
  MachineFunction *MachineFunc;

  /// \brief Check if \p MI uses or defines a callee-saved register or
  /// a frame index. If this is the case, this means \p MI must happen
  /// after Save and before Restore.
  bool useOrDefCSROrFI(const MachineInstr &MI, RegScavenger *RS) const;

  const SetOfRegs &getCurrentCSRs(RegScavenger *RS) const {
    if (CurrentCSRs.empty()) {
      BitVector SavedRegs;
      const TargetFrameLowering *TFI =
          MachineFunc->getSubtarget().getFrameLowering();

      TFI->determineCalleeSaves(*MachineFunc, SavedRegs, RS);

      for (int Reg = SavedRegs.find_first(); Reg != -1;
           Reg = SavedRegs.find_next(Reg))
        CurrentCSRs.insert((unsigned)Reg);
    }
    return CurrentCSRs;
  }

  /// \brief Update the Save and Restore points such that \p MBB is in
  /// the region that is dominated by Save and post-dominated by Restore
  /// and Save and Restore still match the safe point definition.
  /// Such point may not exist and Save and/or Restore may be null after
  /// this call.
  void updateSaveRestorePoints(MachineBasicBlock &MBB, RegScavenger *RS);

  /// \brief Initialize the pass for \p MF.
  void init(MachineFunction &MF) {
    RCI.runOnMachineFunction(MF);
    MDT = &getAnalysis<MachineDominatorTree>();
    MPDT = &getAnalysis<MachinePostDominatorTree>();
    Save = nullptr;
    Restore = nullptr;
    MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
    MLI = &getAnalysis<MachineLoopInfo>();
    EntryFreq = MBFI->getEntryFreq();
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    FrameSetupOpcode = TII.getCallFrameSetupOpcode();
    FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();
    Entry = &MF.front();
    CurrentCSRs.clear();
    MachineFunc = &MF;

    ++NumFunc;
  }

  /// Check whether or not Save and Restore points are still interesting for
  /// shrink-wrapping.
  bool ArePointsInteresting() const { return Save != Entry && Save && Restore; }

  /// \brief Check if shrink wrapping is enabled for this target and function.
  static bool isShrinkWrapEnabled(const MachineFunction &MF);

public:
  static char ID;

  ShrinkWrap() : MachineFunctionPass(ID) {
    initializeShrinkWrapPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const char *getPassName() const override {
    return "Shrink Wrapping analysis";
  }

  /// \brief Perform the shrink-wrapping analysis and update
  /// the MachineFrameInfo attached to \p MF with the results.
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // End anonymous namespace.

char ShrinkWrap::ID = 0;
char &llvm::ShrinkWrapID = ShrinkWrap::ID;

INITIALIZE_PASS_BEGIN(ShrinkWrap, "shrink-wrap", "Shrink Wrap Pass", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(ShrinkWrap, "shrink-wrap", "Shrink Wrap Pass", false, false)

bool ShrinkWrap::useOrDefCSROrFI(const MachineInstr &MI,
                                 RegScavenger *RS) const {
  if (MI.getOpcode() == FrameSetupOpcode ||
      MI.getOpcode() == FrameDestroyOpcode) {
    DEBUG(dbgs() << "Frame instruction: " << MI << '\n');
    return true;
  }
  for (const MachineOperand &MO : MI.operands()) {
    bool UseOrDefCSR = false;
    if (MO.isReg()) {
      unsigned PhysReg = MO.getReg();
      if (!PhysReg)
        continue;
      assert(TargetRegisterInfo::isPhysicalRegister(PhysReg) &&
             "Unallocated register?!");
      UseOrDefCSR = RCI.getLastCalleeSavedAlias(PhysReg);
    } else if (MO.isRegMask()) {
      // Check if this regmask clobbers any of the CSRs.
      for (unsigned Reg : getCurrentCSRs(RS)) {
        if (MO.clobbersPhysReg(Reg)) {
          UseOrDefCSR = true;
          break;
        }
      }
    }
    if (UseOrDefCSR || MO.isFI()) {
      DEBUG(dbgs() << "Use or define CSR(" << UseOrDefCSR << ") or FI("
                   << MO.isFI() << "): " << MI << '\n');
      return true;
    }
  }
  return false;
}

/// \brief Helper function to find the immediate (post) dominator.
template <typename ListOfBBs, typename DominanceAnalysis>
MachineBasicBlock *FindIDom(MachineBasicBlock &Block, ListOfBBs BBs,
                            DominanceAnalysis &Dom) {
  MachineBasicBlock *IDom = &Block;
  for (MachineBasicBlock *BB : BBs) {
    IDom = Dom.findNearestCommonDominator(IDom, BB);
    if (!IDom)
      break;
  }
  return IDom;
}

void ShrinkWrap::updateSaveRestorePoints(MachineBasicBlock &MBB,
                                         RegScavenger *RS) {
  // Get rid of the easy cases first.
  if (!Save)
    Save = &MBB;
  else
    Save = MDT->findNearestCommonDominator(Save, &MBB);

  if (!Save) {
    DEBUG(dbgs() << "Found a block that is not reachable from Entry\n");
    return;
  }

  if (!Restore)
    Restore = &MBB;
  else
    Restore = MPDT->findNearestCommonDominator(Restore, &MBB);

  // Make sure we would be able to insert the restore code before the
  // terminator.
  if (Restore == &MBB) {
    for (const MachineInstr &Terminator : MBB.terminators()) {
      if (!useOrDefCSROrFI(Terminator, RS))
        continue;
      // One of the terminator needs to happen before the restore point.
      if (MBB.succ_empty()) {
        Restore = nullptr;
        break;
      }
      // Look for a restore point that post-dominates all the successors.
      // The immediate post-dominator is what we are looking for.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      break;
    }
  }

  if (!Restore) {
    DEBUG(dbgs() << "Restore point needs to be spanned on several blocks\n");
    return;
  }

  // Make sure Save and Restore are suitable for shrink-wrapping:
  // 1. all path from Save needs to lead to Restore before exiting.
  // 2. all path to Restore needs to go through Save from Entry.
  // We achieve that by making sure that:
  // A. Save dominates Restore.
  // B. Restore post-dominates Save.
  // C. Save and Restore are in the same loop.
  bool SaveDominatesRestore = false;
  bool RestorePostDominatesSave = false;
  while (Save && Restore &&
         (!(SaveDominatesRestore = MDT->dominates(Save, Restore)) ||
          !(RestorePostDominatesSave = MPDT->dominates(Restore, Save)) ||
          // Post-dominance is not enough in loops to ensure that all uses/defs
          // are after the prologue and before the epilogue at runtime.
          // E.g.,
          // while(1) {
          //  Save
          //  Restore
          //   if (...)
          //     break;
          //  use/def CSRs
          // }
          // All the uses/defs of CSRs are dominated by Save and post-dominated
          // by Restore. However, the CSRs uses are still reachable after
          // Restore and before Save are executed.
          //
          // For now, just push the restore/save points outside of loops.
          // FIXME: Refine the criteria to still find interesting cases
          // for loops.
          MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
    // Fix (A).
    if (!SaveDominatesRestore) {
      Save = MDT->findNearestCommonDominator(Save, Restore);
      continue;
    }
    // Fix (B).
    if (!RestorePostDominatesSave)
      Restore = MPDT->findNearestCommonDominator(Restore, Save);

    // Fix (C).
    if (Save && Restore &&
        (MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
      if (MLI->getLoopDepth(Save) > MLI->getLoopDepth(Restore)) {
        // Push Save outside of this loop if immediate dominator is different
        // from save block. If immediate dominator is not different, bail out.
        MachineBasicBlock *IDom = FindIDom<>(*Save, Save->predecessors(), *MDT);
        if (IDom != Save)
          Save = IDom;
        else {
          Save = nullptr;
          break;
        }
      } else {
        // If the loop does not exit, there is no point in looking
        // for a post-dominator outside the loop.
        SmallVector<MachineBasicBlock*, 4> ExitBlocks;
        MLI->getLoopFor(Restore)->getExitingBlocks(ExitBlocks);
        // Push Restore outside of this loop.
        // Look for the immediate post-dominator of the loop exits.
        MachineBasicBlock *IPdom = Restore;
        for (MachineBasicBlock *LoopExitBB: ExitBlocks) {
          IPdom = FindIDom<>(*IPdom, LoopExitBB->successors(), *MPDT);
          if (!IPdom)
            break;
        }
        // If the immediate post-dominator is not in a less nested loop,
        // then we are stuck in a program with an infinite loop.
        // In that case, we will not find a safe point, hence, bail out.
        if (IPdom && MLI->getLoopDepth(IPdom) < MLI->getLoopDepth(Restore))
          Restore = IPdom;
        else {
          Restore = nullptr;
          break;
        }
      }
    }
  }
}

bool ShrinkWrap::runOnMachineFunction(MachineFunction &MF) {
  if (MF.empty() || !isShrinkWrapEnabled(MF))
    return false;

  DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  init(MF);

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  std::unique_ptr<RegScavenger> RS(
      TRI->requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr);

  for (MachineBasicBlock &MBB : MF) {
    DEBUG(dbgs() << "Look into: " << MBB.getNumber() << ' ' << MBB.getName()
                 << '\n');

    if (MBB.isEHFuncletEntry()) {
      DEBUG(dbgs() << "EH Funclets are not supported yet.\n");
      return false;
    }

    for (const MachineInstr &MI : MBB) {
      if (!useOrDefCSROrFI(MI, RS.get()))
        continue;
      // Save (resp. restore) point must dominate (resp. post dominate)
      // MI. Look for the proper basic block for those.
      updateSaveRestorePoints(MBB, RS.get());
      // If we are at a point where we cannot improve the placement of
      // save/restore instructions, just give up.
      if (!ArePointsInteresting()) {
        DEBUG(dbgs() << "No Shrink wrap candidate found\n");
        return false;
      }
      // No need to look for other instructions, this basic block
      // will already be part of the handled region.
      break;
    }
  }
  if (!ArePointsInteresting()) {
    // If the points are not interesting at this point, then they must be null
    // because it means we did not encounter any frame/CSR related code.
    // Otherwise, we would have returned from the previous loop.
    assert(!Save && !Restore && "We miss a shrink-wrap opportunity?!");
    DEBUG(dbgs() << "Nothing to shrink-wrap\n");
    return false;
  }

  DEBUG(dbgs() << "\n ** Results **\nFrequency of the Entry: " << EntryFreq
               << '\n');

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  do {
    DEBUG(dbgs() << "Shrink wrap candidates (#, Name, Freq):\nSave: "
                 << Save->getNumber() << ' ' << Save->getName() << ' '
                 << MBFI->getBlockFreq(Save).getFrequency() << "\nRestore: "
                 << Restore->getNumber() << ' ' << Restore->getName() << ' '
                 << MBFI->getBlockFreq(Restore).getFrequency() << '\n');

    bool IsSaveCheap, TargetCanUseSaveAsPrologue = false;
    if (((IsSaveCheap = EntryFreq >= MBFI->getBlockFreq(Save).getFrequency()) &&
         EntryFreq >= MBFI->getBlockFreq(Restore).getFrequency()) &&
        ((TargetCanUseSaveAsPrologue = TFI->canUseAsPrologue(*Save)) &&
         TFI->canUseAsEpilogue(*Restore)))
      break;
    DEBUG(dbgs() << "New points are too expensive or invalid for the target\n");
    MachineBasicBlock *NewBB;
    if (!IsSaveCheap || !TargetCanUseSaveAsPrologue) {
      Save = FindIDom<>(*Save, Save->predecessors(), *MDT);
      if (!Save)
        break;
      NewBB = Save;
    } else {
      // Restore is expensive.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      if (!Restore)
        break;
      NewBB = Restore;
    }
    updateSaveRestorePoints(*NewBB, RS.get());
  } while (Save && Restore);

  if (!ArePointsInteresting()) {
    ++NumCandidatesDropped;
    return false;
  }

  DEBUG(dbgs() << "Final shrink wrap candidates:\nSave: " << Save->getNumber()
               << ' ' << Save->getName() << "\nRestore: "
               << Restore->getNumber() << ' ' << Restore->getName() << '\n');

  MachineFrameInfo *MFI = MF.getFrameInfo();
  MFI->setSavePoint(Save);
  MFI->setRestorePoint(Restore);
  ++NumCandidates;
  return false;
}

bool ShrinkWrap::isShrinkWrapEnabled(const MachineFunction &MF) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrapOpt) {
  case cl::BOU_UNSET:
    return TFI->enableShrinkWrapping(MF) &&
      // Windows with CFI has some limitations that make it impossible
      // to use shrink-wrapping.
      !MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
      // Sanitizers look at the value of the stack at the location
      // of the crash. Since a crash can happen anywhere, the
      // frame must be lowered before anything else happen for the
      // sanitizers to be able to get a correct stack frame.
      !(MF.getFunction()->hasFnAttribute(Attribute::SanitizeAddress) ||
        MF.getFunction()->hasFnAttribute(Attribute::SanitizeThread) ||
        MF.getFunction()->hasFnAttribute(Attribute::SanitizeMemory));
  // If EnableShrinkWrap is set, it takes precedence on whatever the
  // target sets. The rational is that we assume we want to test
  // something related to shrink-wrapping.
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}
