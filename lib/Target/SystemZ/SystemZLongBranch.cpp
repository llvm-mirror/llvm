//===-- SystemZLongBranch.cpp - Branch lengthening for SystemZ ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass makes sure that all branches are in range.  There are several ways
// in which this could be done.  One aggressive approach is to assume that all
// branches are in range and successively replace those that turn out not
// to be in range with a longer form (branch relaxation).  A simple
// implementation is to continually walk through the function relaxing
// branches until no more changes are needed and a fixed point is reached.
// However, in the pathological worst case, this implementation is
// quadratic in the number of blocks; relaxing branch N can make branch N-1
// go out of range, which in turn can make branch N-2 go out of range,
// and so on.
//
// An alternative approach is to assume that all branches must be
// converted to their long forms, then reinstate the short forms of
// branches that, even under this pessimistic assumption, turn out to be
// in range (branch shortening).  This too can be implemented as a function
// walk that is repeated until a fixed point is reached.  In general,
// the result of shortening is not as good as that of relaxation, and
// shortening is also quadratic in the worst case; shortening branch N
// can bring branch N-1 in range of the short form, which in turn can do
// the same for branch N-2, and so on.  The main advantage of shortening
// is that each walk through the function produces valid code, so it is
// possible to stop at any point after the first walk.  The quadraticness
// could therefore be handled with a maximum pass count, although the
// question then becomes: what maximum count should be used?
//
// On SystemZ, long branches are only needed for functions bigger than 64k,
// which are relatively rare to begin with, and the long branch sequences
// are actually relatively cheap.  It therefore doesn't seem worth spending
// much compilation time on the problem.  Instead, the approach we take is:
//
// (1) Work out the address that each block would have if no branches
//     need relaxing.  Exit the pass early if all branches are in range
//     according to this assumption.
//
// (2) Work out the address that each block would have if all branches
//     need relaxing.
//
// (3) Walk through the block calculating the final address of each instruction
//     and relaxing those that need to be relaxed.  For backward branches,
//     this check uses the final address of the target block, as calculated
//     earlier in the walk.  For forward branches, this check uses the
//     address of the target block that was calculated in (2).  Both checks
//     give a conservatively-correct range.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "systemz-long-branch"

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

STATISTIC(LongBranches, "Number of long branches.");

namespace {
  typedef MachineBasicBlock::iterator Iter;

  // Represents positional information about a basic block.
  struct MBBInfo {
    // The address that we currently assume the block has.
    uint64_t Address;

    // The size of the block in bytes, excluding terminators.
    // This value never changes.
    uint64_t Size;

    // The minimum alignment of the block, as a log2 value.
    // This value never changes.
    unsigned Alignment;

    // The number of terminators in this block.  This value never changes.
    unsigned NumTerminators;

    MBBInfo()
      : Address(0), Size(0), Alignment(0), NumTerminators(0) {} 
  };

  // Represents the state of a block terminator.
  struct TerminatorInfo {
    // If this terminator is a relaxable branch, this points to the branch
    // instruction, otherwise it is null.
    MachineInstr *Branch;

    // The address that we currently assume the terminator has.
    uint64_t Address;

    // The current size of the terminator in bytes.
    uint64_t Size;

    // If Branch is nonnull, this is the number of the target block,
    // otherwise it is unused.
    unsigned TargetBlock;

    // If Branch is nonnull, this is the length of the longest relaxed form,
    // otherwise it is zero.
    unsigned ExtraRelaxSize;

    TerminatorInfo() : Branch(0), Size(0), TargetBlock(0), ExtraRelaxSize(0) {}
  };

  // Used to keep track of the current position while iterating over the blocks.
  struct BlockPosition {
    // The address that we assume this position has.
    uint64_t Address;

    // The number of low bits in Address that are known to be the same
    // as the runtime address.
    unsigned KnownBits;

    BlockPosition(unsigned InitialAlignment)
      : Address(0), KnownBits(InitialAlignment) {}
  };

  class SystemZLongBranch : public MachineFunctionPass {
  public:
    static char ID;
    SystemZLongBranch(const SystemZTargetMachine &tm)
      : MachineFunctionPass(ID), TII(0) {}

    virtual const char *getPassName() const {
      return "SystemZ Long Branch";
    }

    bool runOnMachineFunction(MachineFunction &F);

  private:
    void skipNonTerminators(BlockPosition &Position, MBBInfo &Block);
    void skipTerminator(BlockPosition &Position, TerminatorInfo &Terminator,
                        bool AssumeRelaxed);
    TerminatorInfo describeTerminator(MachineInstr *MI);
    uint64_t initMBBInfo();
    bool mustRelaxBranch(const TerminatorInfo &Terminator, uint64_t Address);
    bool mustRelaxABranch();
    void setWorstCaseAddresses();
    void splitCompareBranch(MachineInstr *MI, unsigned CompareOpcode);
    void relaxBranch(TerminatorInfo &Terminator);
    void relaxBranches();

    const SystemZInstrInfo *TII;
    MachineFunction *MF;
    SmallVector<MBBInfo, 16> MBBs;
    SmallVector<TerminatorInfo, 16> Terminators;
  };

  char SystemZLongBranch::ID = 0;

  const uint64_t MaxBackwardRange = 0x10000;
  const uint64_t MaxForwardRange = 0xfffe;
} // end of anonymous namespace

FunctionPass *llvm::createSystemZLongBranchPass(SystemZTargetMachine &TM) {
  return new SystemZLongBranch(TM);
}

// Position describes the state immediately before Block.  Update Block
// accordingly and move Position to the end of the block's non-terminator
// instructions.
void SystemZLongBranch::skipNonTerminators(BlockPosition &Position,
                                           MBBInfo &Block) {
  if (Block.Alignment > Position.KnownBits) {
    // When calculating the address of Block, we need to conservatively
    // assume that Block had the worst possible misalignment.
    Position.Address += ((uint64_t(1) << Block.Alignment) -
                         (uint64_t(1) << Position.KnownBits));
    Position.KnownBits = Block.Alignment;
  }

  // Align the addresses.
  uint64_t AlignMask = (uint64_t(1) << Block.Alignment) - 1;
  Position.Address = (Position.Address + AlignMask) & ~AlignMask;

  // Record the block's position.
  Block.Address = Position.Address;

  // Move past the non-terminators in the block.
  Position.Address += Block.Size;
}

// Position describes the state immediately before Terminator.
// Update Terminator accordingly and move Position past it.
// Assume that Terminator will be relaxed if AssumeRelaxed.
void SystemZLongBranch::skipTerminator(BlockPosition &Position,
                                       TerminatorInfo &Terminator,
                                       bool AssumeRelaxed) {
  Terminator.Address = Position.Address;
  Position.Address += Terminator.Size;
  if (AssumeRelaxed)
    Position.Address += Terminator.ExtraRelaxSize;
}

// Return a description of terminator instruction MI.
TerminatorInfo SystemZLongBranch::describeTerminator(MachineInstr *MI) {
  TerminatorInfo Terminator;
  Terminator.Size = TII->getInstSizeInBytes(MI);
  if (MI->isConditionalBranch() || MI->isUnconditionalBranch()) {
    switch (MI->getOpcode()) {
    case SystemZ::J:
      // Relaxes to JG, which is 2 bytes longer.
      Terminator.ExtraRelaxSize = 2;
      break;
    case SystemZ::BRC:
      // Relaxes to BRCL, which is 2 bytes longer.
      Terminator.ExtraRelaxSize = 2;
      break;
    case SystemZ::CRJ:
      // Relaxes to a CR/BRCL sequence, which is 2 bytes longer.
      Terminator.ExtraRelaxSize = 2;
      break;
    case SystemZ::CGRJ:
      // Relaxes to a CGR/BRCL sequence, which is 4 bytes longer.
      Terminator.ExtraRelaxSize = 4;
      break;
    case SystemZ::CIJ:
    case SystemZ::CGIJ:
      // Relaxes to a C(G)HI/BRCL sequence, which is 4 bytes longer.
      Terminator.ExtraRelaxSize = 4;
      break;
    default:
      llvm_unreachable("Unrecognized branch instruction");
    }
    Terminator.Branch = MI;
    Terminator.TargetBlock =
      TII->getBranchInfo(MI).Target->getMBB()->getNumber();
  }
  return Terminator;
}

// Fill MBBs and Terminators, setting the addresses on the assumption
// that no branches need relaxation.  Return the size of the function under
// this assumption.
uint64_t SystemZLongBranch::initMBBInfo() {
  MF->RenumberBlocks();
  unsigned NumBlocks = MF->size();

  MBBs.clear();
  MBBs.resize(NumBlocks);

  Terminators.clear();
  Terminators.reserve(NumBlocks);

  BlockPosition Position(MF->getAlignment());
  for (unsigned I = 0; I < NumBlocks; ++I) {
    MachineBasicBlock *MBB = MF->getBlockNumbered(I);
    MBBInfo &Block = MBBs[I];

    // Record the alignment, for quick access.
    Block.Alignment = MBB->getAlignment();

    // Calculate the size of the fixed part of the block.
    MachineBasicBlock::iterator MI = MBB->begin();
    MachineBasicBlock::iterator End = MBB->end();
    while (MI != End && !MI->isTerminator()) {
      Block.Size += TII->getInstSizeInBytes(MI);
      ++MI;
    }
    skipNonTerminators(Position, Block);

    // Add the terminators.
    while (MI != End) {
      if (!MI->isDebugValue()) {
        assert(MI->isTerminator() && "Terminator followed by non-terminator");
        Terminators.push_back(describeTerminator(MI));
        skipTerminator(Position, Terminators.back(), false);
        ++Block.NumTerminators;
      }
      ++MI;
    }
  }

  return Position.Address;
}

// Return true if, under current assumptions, Terminator would need to be
// relaxed if it were placed at address Address.
bool SystemZLongBranch::mustRelaxBranch(const TerminatorInfo &Terminator,
                                        uint64_t Address) {
  if (!Terminator.Branch)
    return false;

  const MBBInfo &Target = MBBs[Terminator.TargetBlock];
  if (Address >= Target.Address) {
    if (Address - Target.Address <= MaxBackwardRange)
      return false;
  } else {
    if (Target.Address - Address <= MaxForwardRange)
      return false;
  }

  return true;
}

// Return true if, under current assumptions, any terminator needs
// to be relaxed.
bool SystemZLongBranch::mustRelaxABranch() {
  for (SmallVectorImpl<TerminatorInfo>::iterator TI = Terminators.begin(),
         TE = Terminators.end(); TI != TE; ++TI)
    if (mustRelaxBranch(*TI, TI->Address))
      return true;
  return false;
}

// Set the address of each block on the assumption that all branches
// must be long.
void SystemZLongBranch::setWorstCaseAddresses() {
  SmallVector<TerminatorInfo, 16>::iterator TI = Terminators.begin();
  BlockPosition Position(MF->getAlignment());
  for (SmallVectorImpl<MBBInfo>::iterator BI = MBBs.begin(), BE = MBBs.end();
       BI != BE; ++BI) {
    skipNonTerminators(Position, *BI);
    for (unsigned BTI = 0, BTE = BI->NumTerminators; BTI != BTE; ++BTI) {
      skipTerminator(Position, *TI, true);
      ++TI;
    }
  }
}

// Split MI into the comparison given by CompareOpcode followed
// a BRCL on the result.
void SystemZLongBranch::splitCompareBranch(MachineInstr *MI,
                                           unsigned CompareOpcode) {
  MachineBasicBlock *MBB = MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  BuildMI(*MBB, MI, DL, TII->get(CompareOpcode))
    .addOperand(MI->getOperand(0))
    .addOperand(MI->getOperand(1));
  MachineInstr *BRCL = BuildMI(*MBB, MI, DL, TII->get(SystemZ::BRCL))
    .addOperand(MI->getOperand(2))
    .addOperand(MI->getOperand(3));
  // The implicit use of CC is a killing use.
  BRCL->getOperand(2).setIsKill();
  MI->eraseFromParent();
}

// Relax the branch described by Terminator.
void SystemZLongBranch::relaxBranch(TerminatorInfo &Terminator) {
  MachineInstr *Branch = Terminator.Branch;
  switch (Branch->getOpcode()) {
  case SystemZ::J:
    Branch->setDesc(TII->get(SystemZ::JG));
    break;
  case SystemZ::BRC:
    Branch->setDesc(TII->get(SystemZ::BRCL));
    break;
  case SystemZ::CRJ:
    splitCompareBranch(Branch, SystemZ::CR);
    break;
  case SystemZ::CGRJ:
    splitCompareBranch(Branch, SystemZ::CGR);
    break;
  case SystemZ::CIJ:
    splitCompareBranch(Branch, SystemZ::CHI);
    break;
  case SystemZ::CGIJ:
    splitCompareBranch(Branch, SystemZ::CGHI);
    break;
  default:
    llvm_unreachable("Unrecognized branch");
  }

  Terminator.Size += Terminator.ExtraRelaxSize;
  Terminator.ExtraRelaxSize = 0;
  Terminator.Branch = 0;

  ++LongBranches;
}

// Run a shortening pass and relax any branches that need to be relaxed.
void SystemZLongBranch::relaxBranches() {
  SmallVector<TerminatorInfo, 16>::iterator TI = Terminators.begin();
  BlockPosition Position(MF->getAlignment());
  for (SmallVectorImpl<MBBInfo>::iterator BI = MBBs.begin(), BE = MBBs.end();
       BI != BE; ++BI) {
    skipNonTerminators(Position, *BI);
    for (unsigned BTI = 0, BTE = BI->NumTerminators; BTI != BTE; ++BTI) {
      assert(Position.Address <= TI->Address &&
             "Addresses shouldn't go forwards");
      if (mustRelaxBranch(*TI, Position.Address))
        relaxBranch(*TI);
      skipTerminator(Position, *TI, false);
      ++TI;
    }
  }
}

bool SystemZLongBranch::runOnMachineFunction(MachineFunction &F) {
  TII = static_cast<const SystemZInstrInfo *>(F.getTarget().getInstrInfo());
  MF = &F;
  uint64_t Size = initMBBInfo();
  if (Size <= MaxForwardRange || !mustRelaxABranch())
    return false;

  setWorstCaseAddresses();
  relaxBranches();
  return true;
}
