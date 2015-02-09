//===-- AMDILCFGStructurizer.cpp - CFG Structurizer -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//==-----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "R600InstrInfo.h"
#include "AMDGPUSubtarget.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "structcfg"

#define DEFAULT_VEC_SLOTS 8

// TODO: move-begin.

//===----------------------------------------------------------------------===//
//
// Statistics for CFGStructurizer.
//
//===----------------------------------------------------------------------===//

STATISTIC(numSerialPatternMatch,    "CFGStructurizer number of serial pattern "
    "matched");
STATISTIC(numIfPatternMatch,        "CFGStructurizer number of if pattern "
    "matched");
STATISTIC(numLoopcontPatternMatch,  "CFGStructurizer number of loop-continue "
    "pattern matched");
STATISTIC(numClonedBlock,           "CFGStructurizer cloned blocks");
STATISTIC(numClonedInstr,           "CFGStructurizer cloned instructions");

namespace llvm {
  void initializeAMDGPUCFGStructurizerPass(PassRegistry&);
}

//===----------------------------------------------------------------------===//
//
// Miscellaneous utility for CFGStructurizer.
//
//===----------------------------------------------------------------------===//
namespace {
#define SHOWNEWINSTR(i) \
  DEBUG(dbgs() << "New instr: " << *i << "\n");

#define SHOWNEWBLK(b, msg) \
DEBUG( \
  dbgs() << msg << "BB" << b->getNumber() << "size " << b->size(); \
  dbgs() << "\n"; \
);

#define SHOWBLK_DETAIL(b, msg) \
DEBUG( \
  if (b) { \
  dbgs() << msg << "BB" << b->getNumber() << "size " << b->size(); \
  b->print(dbgs()); \
  dbgs() << "\n"; \
  } \
);

#define INVALIDSCCNUM -1

template<class NodeT>
void ReverseVector(SmallVectorImpl<NodeT *> &Src) {
  size_t sz = Src.size();
  for (size_t i = 0; i < sz/2; ++i) {
    NodeT *t = Src[i];
    Src[i] = Src[sz - i - 1];
    Src[sz - i - 1] = t;
  }
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//
// supporting data structure for CFGStructurizer
//
//===----------------------------------------------------------------------===//


namespace {

class BlockInformation {
public:
  bool IsRetired;
  int  SccNum;
  BlockInformation() : IsRetired(false), SccNum(INVALIDSCCNUM) {}
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//
// CFGStructurizer
//
//===----------------------------------------------------------------------===//

namespace {
class AMDGPUCFGStructurizer : public MachineFunctionPass {
public:
  typedef SmallVector<MachineBasicBlock *, 32> MBBVector;
  typedef std::map<MachineBasicBlock *, BlockInformation *> MBBInfoMap;
  typedef std::map<MachineLoop *, MachineBasicBlock *> LoopLandInfoMap;

  enum PathToKind {
    Not_SinglePath = 0,
    SinglePath_InPath = 1,
    SinglePath_NotInPath = 2
  };

  static char ID;

  AMDGPUCFGStructurizer() :
      MachineFunctionPass(ID), TII(nullptr), TRI(nullptr) {
    initializeAMDGPUCFGStructurizerPass(*PassRegistry::getPassRegistry());
  }

   const char *getPassName() const override {
    return "AMDGPU Control Flow Graph structurizer Pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<MachineFunctionAnalysis>();
    AU.addRequired<MachineFunctionAnalysis>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
  }

  /// Perform the CFG structurization
  bool run();

  /// Perform the CFG preparation
  /// This step will remove every unconditionnal/dead jump instructions and make
  /// sure all loops have an exit block
  bool prepare();

  bool runOnMachineFunction(MachineFunction &MF) override {
    TII = static_cast<const R600InstrInfo *>(MF.getSubtarget().getInstrInfo());
    TRI = &TII->getRegisterInfo();
    DEBUG(MF.dump(););
    OrderedBlks.clear();
    FuncRep = &MF;
    MLI = &getAnalysis<MachineLoopInfo>();
    DEBUG(dbgs() << "LoopInfo:\n"; PrintLoopinfo(*MLI););
    MDT = &getAnalysis<MachineDominatorTree>();
    DEBUG(MDT->print(dbgs(), (const llvm::Module*)nullptr););
    PDT = &getAnalysis<MachinePostDominatorTree>();
    DEBUG(PDT->print(dbgs()););
    prepare();
    run();
    DEBUG(MF.dump(););
    return true;
  }

protected:
  MachineDominatorTree *MDT;
  MachinePostDominatorTree *PDT;
  MachineLoopInfo *MLI;
  const R600InstrInfo *TII;
  const AMDGPURegisterInfo *TRI;

  // PRINT FUNCTIONS
  /// Print the ordered Blocks.
  void printOrderedBlocks() const {
    size_t i = 0;
    for (MBBVector::const_iterator iterBlk = OrderedBlks.begin(),
        iterBlkEnd = OrderedBlks.end(); iterBlk != iterBlkEnd; ++iterBlk, ++i) {
      dbgs() << "BB" << (*iterBlk)->getNumber();
      dbgs() << "(" << getSCCNum(*iterBlk) << "," << (*iterBlk)->size() << ")";
      if (i != 0 && i % 10 == 0) {
        dbgs() << "\n";
      } else {
        dbgs() << " ";
      }
    }
  }
  static void PrintLoopinfo(const MachineLoopInfo &LoopInfo) {
    for (MachineLoop::iterator iter = LoopInfo.begin(),
         iterEnd = LoopInfo.end(); iter != iterEnd; ++iter) {
      (*iter)->print(dbgs(), 0);
    }
  }

  // UTILITY FUNCTIONS
  int getSCCNum(MachineBasicBlock *MBB) const;
  MachineBasicBlock *getLoopLandInfo(MachineLoop *LoopRep) const;
  bool hasBackEdge(MachineBasicBlock *MBB) const;
  static unsigned getLoopDepth(MachineLoop *LoopRep);
  bool isRetiredBlock(MachineBasicBlock *MBB) const;
  bool isActiveLoophead(MachineBasicBlock *MBB) const;
  PathToKind singlePathTo(MachineBasicBlock *SrcMBB, MachineBasicBlock *DstMBB,
      bool AllowSideEntry = true) const;
  int countActiveBlock(MBBVector::const_iterator It,
      MBBVector::const_iterator E) const;
  bool needMigrateBlock(MachineBasicBlock *MBB) const;

  // Utility Functions
  void reversePredicateSetter(MachineBasicBlock::iterator I);
  /// Compute the reversed DFS post order of Blocks
  void orderBlocks(MachineFunction *MF);

  // Function originally from CFGStructTraits
  void insertInstrEnd(MachineBasicBlock *MBB, int NewOpcode,
      DebugLoc DL = DebugLoc());
  MachineInstr *insertInstrBefore(MachineBasicBlock *MBB, int NewOpcode,
    DebugLoc DL = DebugLoc());
  MachineInstr *insertInstrBefore(MachineBasicBlock::iterator I, int NewOpcode);
  void insertCondBranchBefore(MachineBasicBlock::iterator I, int NewOpcode,
      DebugLoc DL);
  void insertCondBranchBefore(MachineBasicBlock *MBB,
      MachineBasicBlock::iterator I, int NewOpcode, int RegNum,
      DebugLoc DL);
  void insertCondBranchEnd(MachineBasicBlock *MBB, int NewOpcode, int RegNum);
  static int getBranchNzeroOpcode(int OldOpcode);
  static int getBranchZeroOpcode(int OldOpcode);
  static int getContinueNzeroOpcode(int OldOpcode);
  static int getContinueZeroOpcode(int OldOpcode);
  static MachineBasicBlock *getTrueBranch(MachineInstr *MI);
  static void setTrueBranch(MachineInstr *MI, MachineBasicBlock *MBB);
  static MachineBasicBlock *getFalseBranch(MachineBasicBlock *MBB,
      MachineInstr *MI);
  static bool isCondBranch(MachineInstr *MI);
  static bool isUncondBranch(MachineInstr *MI);
  static DebugLoc getLastDebugLocInBB(MachineBasicBlock *MBB);
  static MachineInstr *getNormalBlockBranchInstr(MachineBasicBlock *MBB);
  /// The correct naming for this is getPossibleLoopendBlockBranchInstr.
  ///
  /// BB with backward-edge could have move instructions after the branch
  /// instruction.  Such move instruction "belong to" the loop backward-edge.
  MachineInstr *getLoopendBlockBranchInstr(MachineBasicBlock *MBB);
  static MachineInstr *getReturnInstr(MachineBasicBlock *MBB);
  static MachineInstr *getContinueInstr(MachineBasicBlock *MBB);
  static bool isReturnBlock(MachineBasicBlock *MBB);
  static void cloneSuccessorList(MachineBasicBlock *DstMBB,
      MachineBasicBlock *SrcMBB) ;
  static MachineBasicBlock *clone(MachineBasicBlock *MBB);
  /// MachineBasicBlock::ReplaceUsesOfBlockWith doesn't serve the purpose
  /// because the AMDGPU instruction is not recognized as terminator fix this
  /// and retire this routine
  void replaceInstrUseOfBlockWith(MachineBasicBlock *SrcMBB,
      MachineBasicBlock *OldMBB, MachineBasicBlock *NewBlk);
  static void wrapup(MachineBasicBlock *MBB);


  int patternMatch(MachineBasicBlock *MBB);
  int patternMatchGroup(MachineBasicBlock *MBB);
  int serialPatternMatch(MachineBasicBlock *MBB);
  int ifPatternMatch(MachineBasicBlock *MBB);
  int loopendPatternMatch();
  int mergeLoop(MachineLoop *LoopRep);
  int loopcontPatternMatch(MachineLoop *LoopRep, MachineBasicBlock *LoopHeader);

  void handleLoopcontBlock(MachineBasicBlock *ContingMBB,
      MachineLoop *ContingLoop, MachineBasicBlock *ContMBB,
      MachineLoop *ContLoop);
  /// return true iff src1Blk->succ_size() == 0 && src1Blk and src2Blk are in
  /// the same loop with LoopLandInfo without explicitly keeping track of
  /// loopContBlks and loopBreakBlks, this is a method to get the information.
  bool isSameloopDetachedContbreak(MachineBasicBlock *Src1MBB,
      MachineBasicBlock *Src2MBB);
  int handleJumpintoIf(MachineBasicBlock *HeadMBB,
      MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB);
  int handleJumpintoIfImp(MachineBasicBlock *HeadMBB,
      MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB);
  int improveSimpleJumpintoIf(MachineBasicBlock *HeadMBB,
      MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB,
      MachineBasicBlock **LandMBBPtr);
  void showImproveSimpleJumpintoIf(MachineBasicBlock *HeadMBB,
      MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB,
      MachineBasicBlock *LandMBB, bool Detail = false);
  int cloneOnSideEntryTo(MachineBasicBlock *PreMBB,
      MachineBasicBlock *SrcMBB, MachineBasicBlock *DstMBB);
  void mergeSerialBlock(MachineBasicBlock *DstMBB,
      MachineBasicBlock *SrcMBB);

  void mergeIfthenelseBlock(MachineInstr *BranchMI,
      MachineBasicBlock *MBB, MachineBasicBlock *TrueMBB,
      MachineBasicBlock *FalseMBB, MachineBasicBlock *LandMBB);
  void mergeLooplandBlock(MachineBasicBlock *DstMBB,
      MachineBasicBlock *LandMBB);
  void mergeLoopbreakBlock(MachineBasicBlock *ExitingMBB,
      MachineBasicBlock *LandMBB);
  void settleLoopcontBlock(MachineBasicBlock *ContingMBB,
      MachineBasicBlock *ContMBB);
  /// normalizeInfiniteLoopExit change
  ///   B1:
  ///        uncond_br LoopHeader
  ///
  /// to
  ///   B1:
  ///        cond_br 1 LoopHeader dummyExit
  /// and return the newly added dummy exit block
  MachineBasicBlock *normalizeInfiniteLoopExit(MachineLoop *LoopRep);
  void removeUnconditionalBranch(MachineBasicBlock *MBB);
  /// Remove duplicate branches instructions in a block.
  /// For instance
  /// B0:
  ///    cond_br X B1 B2
  ///    cond_br X B1 B2
  /// is transformed to
  /// B0:
  ///    cond_br X B1 B2
  void removeRedundantConditionalBranch(MachineBasicBlock *MBB);
  void addDummyExitBlock(SmallVectorImpl<MachineBasicBlock *> &RetMBB);
  void removeSuccessor(MachineBasicBlock *MBB);
  MachineBasicBlock *cloneBlockForPredecessor(MachineBasicBlock *MBB,
      MachineBasicBlock *PredMBB);
  void migrateInstruction(MachineBasicBlock *SrcMBB,
      MachineBasicBlock *DstMBB, MachineBasicBlock::iterator I);
  void recordSccnum(MachineBasicBlock *MBB, int SCCNum);
  void retireBlock(MachineBasicBlock *MBB);
  void setLoopLandBlock(MachineLoop *LoopRep, MachineBasicBlock *MBB = nullptr);

  MachineBasicBlock *findNearestCommonPostDom(std::set<MachineBasicBlock *>&);
  /// This is work around solution for findNearestCommonDominator not available
  /// to post dom a proper fix should go to Dominators.h.
  MachineBasicBlock *findNearestCommonPostDom(MachineBasicBlock *MBB1,
      MachineBasicBlock *MBB2);

private:
  MBBInfoMap BlockInfoMap;
  LoopLandInfoMap LLInfoMap;
  std::map<MachineLoop *, bool> Visited;
  MachineFunction *FuncRep;
  SmallVector<MachineBasicBlock *, DEFAULT_VEC_SLOTS> OrderedBlks;
};

int AMDGPUCFGStructurizer::getSCCNum(MachineBasicBlock *MBB) const {
  MBBInfoMap::const_iterator It = BlockInfoMap.find(MBB);
  if (It == BlockInfoMap.end())
    return INVALIDSCCNUM;
  return (*It).second->SccNum;
}

MachineBasicBlock *AMDGPUCFGStructurizer::getLoopLandInfo(MachineLoop *LoopRep)
    const {
  LoopLandInfoMap::const_iterator It = LLInfoMap.find(LoopRep);
  if (It == LLInfoMap.end())
    return nullptr;
  return (*It).second;
}

bool AMDGPUCFGStructurizer::hasBackEdge(MachineBasicBlock *MBB) const {
  MachineLoop *LoopRep = MLI->getLoopFor(MBB);
  if (!LoopRep)
    return false;
  MachineBasicBlock *LoopHeader = LoopRep->getHeader();
  return MBB->isSuccessor(LoopHeader);
}

unsigned AMDGPUCFGStructurizer::getLoopDepth(MachineLoop *LoopRep) {
  return LoopRep ? LoopRep->getLoopDepth() : 0;
}

bool AMDGPUCFGStructurizer::isRetiredBlock(MachineBasicBlock *MBB) const {
  MBBInfoMap::const_iterator It = BlockInfoMap.find(MBB);
  if (It == BlockInfoMap.end())
    return false;
  return (*It).second->IsRetired;
}

bool AMDGPUCFGStructurizer::isActiveLoophead(MachineBasicBlock *MBB) const {
  MachineLoop *LoopRep = MLI->getLoopFor(MBB);
  while (LoopRep && LoopRep->getHeader() == MBB) {
    MachineBasicBlock *LoopLand = getLoopLandInfo(LoopRep);
    if(!LoopLand)
      return true;
    if (!isRetiredBlock(LoopLand))
      return true;
    LoopRep = LoopRep->getParentLoop();
  }
  return false;
}
AMDGPUCFGStructurizer::PathToKind AMDGPUCFGStructurizer::singlePathTo(
    MachineBasicBlock *SrcMBB, MachineBasicBlock *DstMBB,
    bool AllowSideEntry) const {
  assert(DstMBB);
  if (SrcMBB == DstMBB)
    return SinglePath_InPath;
  while (SrcMBB && SrcMBB->succ_size() == 1) {
    SrcMBB = *SrcMBB->succ_begin();
    if (SrcMBB == DstMBB)
      return SinglePath_InPath;
    if (!AllowSideEntry && SrcMBB->pred_size() > 1)
      return Not_SinglePath;
  }
  if (SrcMBB && SrcMBB->succ_size()==0)
    return SinglePath_NotInPath;
  return Not_SinglePath;
}

int AMDGPUCFGStructurizer::countActiveBlock(MBBVector::const_iterator It,
    MBBVector::const_iterator E) const {
  int Count = 0;
  while (It != E) {
    if (!isRetiredBlock(*It))
      ++Count;
    ++It;
  }
  return Count;
}

bool AMDGPUCFGStructurizer::needMigrateBlock(MachineBasicBlock *MBB) const {
  unsigned BlockSizeThreshold = 30;
  unsigned CloneInstrThreshold = 100;
  bool MultiplePreds = MBB && (MBB->pred_size() > 1);

  if(!MultiplePreds)
    return false;
  unsigned BlkSize = MBB->size();
  return ((BlkSize > BlockSizeThreshold) &&
      (BlkSize * (MBB->pred_size() - 1) > CloneInstrThreshold));
}

void AMDGPUCFGStructurizer::reversePredicateSetter(
    MachineBasicBlock::iterator I) {
  while (I--) {
    if (I->getOpcode() == AMDGPU::PRED_X) {
      switch (static_cast<MachineInstr *>(I)->getOperand(2).getImm()) {
      case OPCODE_IS_ZERO_INT:
        static_cast<MachineInstr *>(I)->getOperand(2)
            .setImm(OPCODE_IS_NOT_ZERO_INT);
        return;
      case OPCODE_IS_NOT_ZERO_INT:
        static_cast<MachineInstr *>(I)->getOperand(2)
            .setImm(OPCODE_IS_ZERO_INT);
        return;
      case OPCODE_IS_ZERO:
        static_cast<MachineInstr *>(I)->getOperand(2)
            .setImm(OPCODE_IS_NOT_ZERO);
        return;
      case OPCODE_IS_NOT_ZERO:
        static_cast<MachineInstr *>(I)->getOperand(2)
            .setImm(OPCODE_IS_ZERO);
        return;
      default:
        llvm_unreachable("PRED_X Opcode invalid!");
      }
    }
  }
}

void AMDGPUCFGStructurizer::insertInstrEnd(MachineBasicBlock *MBB,
    int NewOpcode, DebugLoc DL) {
 MachineInstr *MI = MBB->getParent()
    ->CreateMachineInstr(TII->get(NewOpcode), DL);
  MBB->push_back(MI);
  //assume the instruction doesn't take any reg operand ...
  SHOWNEWINSTR(MI);
}

MachineInstr *AMDGPUCFGStructurizer::insertInstrBefore(MachineBasicBlock *MBB,
    int NewOpcode, DebugLoc DL) {
  MachineInstr *MI =
      MBB->getParent()->CreateMachineInstr(TII->get(NewOpcode), DL);
  if (MBB->begin() != MBB->end())
    MBB->insert(MBB->begin(), MI);
  else
    MBB->push_back(MI);
  SHOWNEWINSTR(MI);
  return MI;
}

MachineInstr *AMDGPUCFGStructurizer::insertInstrBefore(
    MachineBasicBlock::iterator I, int NewOpcode) {
  MachineInstr *OldMI = &(*I);
  MachineBasicBlock *MBB = OldMI->getParent();
  MachineInstr *NewMBB =
      MBB->getParent()->CreateMachineInstr(TII->get(NewOpcode), DebugLoc());
  MBB->insert(I, NewMBB);
  //assume the instruction doesn't take any reg operand ...
  SHOWNEWINSTR(NewMBB);
  return NewMBB;
}

void AMDGPUCFGStructurizer::insertCondBranchBefore(
    MachineBasicBlock::iterator I, int NewOpcode, DebugLoc DL) {
  MachineInstr *OldMI = &(*I);
  MachineBasicBlock *MBB = OldMI->getParent();
  MachineFunction *MF = MBB->getParent();
  MachineInstr *NewMI = MF->CreateMachineInstr(TII->get(NewOpcode), DL);
  MBB->insert(I, NewMI);
  MachineInstrBuilder MIB(*MF, NewMI);
  MIB.addReg(OldMI->getOperand(1).getReg(), false);
  SHOWNEWINSTR(NewMI);
  //erase later oldInstr->eraseFromParent();
}

void AMDGPUCFGStructurizer::insertCondBranchBefore(MachineBasicBlock *blk,
    MachineBasicBlock::iterator I, int NewOpcode, int RegNum,
    DebugLoc DL) {
  MachineFunction *MF = blk->getParent();
  MachineInstr *NewInstr = MF->CreateMachineInstr(TII->get(NewOpcode), DL);
  //insert before
  blk->insert(I, NewInstr);
  MachineInstrBuilder(*MF, NewInstr).addReg(RegNum, false);
  SHOWNEWINSTR(NewInstr);
}

void AMDGPUCFGStructurizer::insertCondBranchEnd(MachineBasicBlock *MBB,
    int NewOpcode, int RegNum) {
  MachineFunction *MF = MBB->getParent();
  MachineInstr *NewInstr =
    MF->CreateMachineInstr(TII->get(NewOpcode), DebugLoc());
  MBB->push_back(NewInstr);
  MachineInstrBuilder(*MF, NewInstr).addReg(RegNum, false);
  SHOWNEWINSTR(NewInstr);
}

int AMDGPUCFGStructurizer::getBranchNzeroOpcode(int OldOpcode) {
  switch(OldOpcode) {
  case AMDGPU::JUMP_COND:
  case AMDGPU::JUMP: return AMDGPU::IF_PREDICATE_SET;
  case AMDGPU::BRANCH_COND_i32:
  case AMDGPU::BRANCH_COND_f32: return AMDGPU::IF_LOGICALNZ_f32;
  default: llvm_unreachable("internal error");
  }
  return -1;
}

int AMDGPUCFGStructurizer::getBranchZeroOpcode(int OldOpcode) {
  switch(OldOpcode) {
  case AMDGPU::JUMP_COND:
  case AMDGPU::JUMP: return AMDGPU::IF_PREDICATE_SET;
  case AMDGPU::BRANCH_COND_i32:
  case AMDGPU::BRANCH_COND_f32: return AMDGPU::IF_LOGICALZ_f32;
  default: llvm_unreachable("internal error");
  }
  return -1;
}

int AMDGPUCFGStructurizer::getContinueNzeroOpcode(int OldOpcode) {
  switch(OldOpcode) {
  case AMDGPU::JUMP_COND:
  case AMDGPU::JUMP: return AMDGPU::CONTINUE_LOGICALNZ_i32;
  default: llvm_unreachable("internal error");
  };
  return -1;
}

int AMDGPUCFGStructurizer::getContinueZeroOpcode(int OldOpcode) {
  switch(OldOpcode) {
  case AMDGPU::JUMP_COND:
  case AMDGPU::JUMP: return AMDGPU::CONTINUE_LOGICALZ_i32;
  default: llvm_unreachable("internal error");
  }
  return -1;
}

MachineBasicBlock *AMDGPUCFGStructurizer::getTrueBranch(MachineInstr *MI) {
  return MI->getOperand(0).getMBB();
}

void AMDGPUCFGStructurizer::setTrueBranch(MachineInstr *MI,
    MachineBasicBlock *MBB) {
  MI->getOperand(0).setMBB(MBB);
}

MachineBasicBlock *
AMDGPUCFGStructurizer::getFalseBranch(MachineBasicBlock *MBB,
    MachineInstr *MI) {
  assert(MBB->succ_size() == 2);
  MachineBasicBlock *TrueBranch = getTrueBranch(MI);
  MachineBasicBlock::succ_iterator It = MBB->succ_begin();
  MachineBasicBlock::succ_iterator Next = It;
  ++Next;
  return (*It == TrueBranch) ? *Next : *It;
}

bool AMDGPUCFGStructurizer::isCondBranch(MachineInstr *MI) {
  switch (MI->getOpcode()) {
    case AMDGPU::JUMP_COND:
    case AMDGPU::BRANCH_COND_i32:
    case AMDGPU::BRANCH_COND_f32: return true;
  default:
    return false;
  }
  return false;
}

bool AMDGPUCFGStructurizer::isUncondBranch(MachineInstr *MI) {
  switch (MI->getOpcode()) {
  case AMDGPU::JUMP:
  case AMDGPU::BRANCH:
    return true;
  default:
    return false;
  }
  return false;
}

DebugLoc AMDGPUCFGStructurizer::getLastDebugLocInBB(MachineBasicBlock *MBB) {
  //get DebugLoc from the first MachineBasicBlock instruction with debug info
  DebugLoc DL;
  for (MachineBasicBlock::iterator It = MBB->begin(); It != MBB->end();
      ++It) {
    MachineInstr *instr = &(*It);
    if (instr->getDebugLoc().isUnknown() == false)
      DL = instr->getDebugLoc();
  }
  return DL;
}

MachineInstr *AMDGPUCFGStructurizer::getNormalBlockBranchInstr(
    MachineBasicBlock *MBB) {
  MachineBasicBlock::reverse_iterator It = MBB->rbegin();
  MachineInstr *MI = &*It;
  if (MI && (isCondBranch(MI) || isUncondBranch(MI)))
    return MI;
  return nullptr;
}

MachineInstr *AMDGPUCFGStructurizer::getLoopendBlockBranchInstr(
    MachineBasicBlock *MBB) {
  for (MachineBasicBlock::reverse_iterator It = MBB->rbegin(), E = MBB->rend();
      It != E; ++It) {
    // FIXME: Simplify
    MachineInstr *MI = &*It;
    if (MI) {
      if (isCondBranch(MI) || isUncondBranch(MI))
        return MI;
      else if (!TII->isMov(MI->getOpcode()))
        break;
    }
  }
  return nullptr;
}

MachineInstr *AMDGPUCFGStructurizer::getReturnInstr(MachineBasicBlock *MBB) {
  MachineBasicBlock::reverse_iterator It = MBB->rbegin();
  if (It != MBB->rend()) {
    MachineInstr *instr = &(*It);
    if (instr->getOpcode() == AMDGPU::RETURN)
      return instr;
  }
  return nullptr;
}

MachineInstr *AMDGPUCFGStructurizer::getContinueInstr(MachineBasicBlock *MBB) {
  MachineBasicBlock::reverse_iterator It = MBB->rbegin();
  if (It != MBB->rend()) {
    MachineInstr *MI = &(*It);
    if (MI->getOpcode() == AMDGPU::CONTINUE)
      return MI;
  }
  return nullptr;
}

bool AMDGPUCFGStructurizer::isReturnBlock(MachineBasicBlock *MBB) {
  MachineInstr *MI = getReturnInstr(MBB);
  bool IsReturn = (MBB->succ_size() == 0);
  if (MI)
    assert(IsReturn);
  else if (IsReturn)
    DEBUG(
      dbgs() << "BB" << MBB->getNumber()
             <<" is return block without RETURN instr\n";);
  return  IsReturn;
}

void AMDGPUCFGStructurizer::cloneSuccessorList(MachineBasicBlock *DstMBB,
    MachineBasicBlock *SrcMBB) {
  for (MachineBasicBlock::succ_iterator It = SrcMBB->succ_begin(),
       iterEnd = SrcMBB->succ_end(); It != iterEnd; ++It)
    DstMBB->addSuccessor(*It);  // *iter's predecessor is also taken care of
}

MachineBasicBlock *AMDGPUCFGStructurizer::clone(MachineBasicBlock *MBB) {
  MachineFunction *Func = MBB->getParent();
  MachineBasicBlock *NewMBB = Func->CreateMachineBasicBlock();
  Func->push_back(NewMBB);  //insert to function
  for (MachineBasicBlock::iterator It = MBB->begin(), E = MBB->end();
      It != E; ++It) {
    MachineInstr *MI = Func->CloneMachineInstr(It);
    NewMBB->push_back(MI);
  }
  return NewMBB;
}

void AMDGPUCFGStructurizer::replaceInstrUseOfBlockWith(
    MachineBasicBlock *SrcMBB, MachineBasicBlock *OldMBB,
    MachineBasicBlock *NewBlk) {
  MachineInstr *BranchMI = getLoopendBlockBranchInstr(SrcMBB);
  if (BranchMI && isCondBranch(BranchMI) &&
      getTrueBranch(BranchMI) == OldMBB)
    setTrueBranch(BranchMI, NewBlk);
}

void AMDGPUCFGStructurizer::wrapup(MachineBasicBlock *MBB) {
  assert((!MBB->getParent()->getJumpTableInfo()
          || MBB->getParent()->getJumpTableInfo()->isEmpty())
         && "found a jump table");

   //collect continue right before endloop
   SmallVector<MachineInstr *, DEFAULT_VEC_SLOTS> ContInstr;
   MachineBasicBlock::iterator Pre = MBB->begin();
   MachineBasicBlock::iterator E = MBB->end();
   MachineBasicBlock::iterator It = Pre;
   while (It != E) {
     if (Pre->getOpcode() == AMDGPU::CONTINUE
         && It->getOpcode() == AMDGPU::ENDLOOP)
       ContInstr.push_back(Pre);
     Pre = It;
     ++It;
   }

   //delete continue right before endloop
   for (unsigned i = 0; i < ContInstr.size(); ++i)
      ContInstr[i]->eraseFromParent();

   // TODO to fix up jump table so later phase won't be confused.  if
   // (jumpTableInfo->isEmpty() == false) { need to clean the jump table, but
   // there isn't such an interface yet.  alternatively, replace all the other
   // blocks in the jump table with the entryBlk //}

}


bool AMDGPUCFGStructurizer::prepare() {
  bool Changed = false;

  //FIXME: if not reducible flow graph, make it so ???

  DEBUG(dbgs() << "AMDGPUCFGStructurizer::prepare\n";);

  orderBlocks(FuncRep);

  SmallVector<MachineBasicBlock *, DEFAULT_VEC_SLOTS> RetBlks;

  // Add an ExitBlk to loop that don't have one
  for (MachineLoopInfo::iterator It = MLI->begin(),
       E = MLI->end(); It != E; ++It) {
    MachineLoop *LoopRep = (*It);
    MBBVector ExitingMBBs;
    LoopRep->getExitingBlocks(ExitingMBBs);

    if (ExitingMBBs.size() == 0) {
      MachineBasicBlock* DummyExitBlk = normalizeInfiniteLoopExit(LoopRep);
      if (DummyExitBlk)
        RetBlks.push_back(DummyExitBlk);
    }
  }

  // Remove unconditional branch instr.
  // Add dummy exit block iff there are multiple returns.
  for (SmallVectorImpl<MachineBasicBlock *>::const_iterator
       It = OrderedBlks.begin(), E = OrderedBlks.end(); It != E; ++It) {
    MachineBasicBlock *MBB = *It;
    removeUnconditionalBranch(MBB);
    removeRedundantConditionalBranch(MBB);
    if (isReturnBlock(MBB)) {
      RetBlks.push_back(MBB);
    }
    assert(MBB->succ_size() <= 2);
  }

  if (RetBlks.size() >= 2) {
    addDummyExitBlock(RetBlks);
    Changed = true;
  }

  return Changed;
}

bool AMDGPUCFGStructurizer::run() {

  //Assume reducible CFG...
  DEBUG(dbgs() << "AMDGPUCFGStructurizer::run\n");

#ifdef STRESSTEST
  //Use the worse block ordering to test the algorithm.
  ReverseVector(orderedBlks);
#endif

  DEBUG(dbgs() << "Ordered blocks:\n"; printOrderedBlocks(););
  int NumIter = 0;
  bool Finish = false;
  MachineBasicBlock *MBB;
  bool MakeProgress = false;
  int NumRemainedBlk = countActiveBlock(OrderedBlks.begin(),
                                        OrderedBlks.end());

  do {
    ++NumIter;
    DEBUG(
      dbgs() << "numIter = " << NumIter
             << ", numRemaintedBlk = " << NumRemainedBlk << "\n";
    );

    SmallVectorImpl<MachineBasicBlock *>::const_iterator It =
        OrderedBlks.begin();
    SmallVectorImpl<MachineBasicBlock *>::const_iterator E =
        OrderedBlks.end();

    SmallVectorImpl<MachineBasicBlock *>::const_iterator SccBeginIter =
        It;
    MachineBasicBlock *SccBeginMBB = nullptr;
    int SccNumBlk = 0;  // The number of active blocks, init to a
                        // maximum possible number.
    int SccNumIter;     // Number of iteration in this SCC.

    while (It != E) {
      MBB = *It;

      if (!SccBeginMBB) {
        SccBeginIter = It;
        SccBeginMBB = MBB;
        SccNumIter = 0;
        SccNumBlk = NumRemainedBlk; // Init to maximum possible number.
        DEBUG(
              dbgs() << "start processing SCC" << getSCCNum(SccBeginMBB);
              dbgs() << "\n";
        );
      }

      if (!isRetiredBlock(MBB))
        patternMatch(MBB);

      ++It;

      bool ContNextScc = true;
      if (It == E
          || getSCCNum(SccBeginMBB) != getSCCNum(*It)) {
        // Just finish one scc.
        ++SccNumIter;
        int sccRemainedNumBlk = countActiveBlock(SccBeginIter, It);
        if (sccRemainedNumBlk != 1 && sccRemainedNumBlk >= SccNumBlk) {
          DEBUG(
            dbgs() << "Can't reduce SCC " << getSCCNum(MBB)
                   << ", sccNumIter = " << SccNumIter;
            dbgs() << "doesn't make any progress\n";
          );
          ContNextScc = true;
        } else if (sccRemainedNumBlk != 1 && sccRemainedNumBlk < SccNumBlk) {
          SccNumBlk = sccRemainedNumBlk;
          It = SccBeginIter;
          ContNextScc = false;
          DEBUG(
            dbgs() << "repeat processing SCC" << getSCCNum(MBB)
                   << "sccNumIter = " << SccNumIter << '\n';
          );
        } else {
          // Finish the current scc.
          ContNextScc = true;
        }
      } else {
        // Continue on next component in the current scc.
        ContNextScc = false;
      }

      if (ContNextScc)
        SccBeginMBB = nullptr;
    } //while, "one iteration" over the function.

    MachineBasicBlock *EntryMBB =
        GraphTraits<MachineFunction *>::nodes_begin(FuncRep);
    if (EntryMBB->succ_size() == 0) {
      Finish = true;
      DEBUG(
        dbgs() << "Reduce to one block\n";
      );
    } else {
      int NewnumRemainedBlk
        = countActiveBlock(OrderedBlks.begin(), OrderedBlks.end());
      // consider cloned blocks ??
      if (NewnumRemainedBlk == 1 || NewnumRemainedBlk < NumRemainedBlk) {
        MakeProgress = true;
        NumRemainedBlk = NewnumRemainedBlk;
      } else {
        MakeProgress = false;
        DEBUG(
          dbgs() << "No progress\n";
        );
      }
    }
  } while (!Finish && MakeProgress);

  // Misc wrap up to maintain the consistency of the Function representation.
  wrapup(GraphTraits<MachineFunction *>::nodes_begin(FuncRep));

  // Detach retired Block, release memory.
  for (MBBInfoMap::iterator It = BlockInfoMap.begin(), E = BlockInfoMap.end();
      It != E; ++It) {
    if ((*It).second && (*It).second->IsRetired) {
      assert(((*It).first)->getNumber() != -1);
      DEBUG(
        dbgs() << "Erase BB" << ((*It).first)->getNumber() << "\n";
      );
      (*It).first->eraseFromParent();  //Remove from the parent Function.
    }
    delete (*It).second;
  }
  BlockInfoMap.clear();
  LLInfoMap.clear();

  if (!Finish) {
    DEBUG(FuncRep->viewCFG());
    llvm_unreachable("IRREDUCIBLE_CFG");
  }

  return true;
}



void AMDGPUCFGStructurizer::orderBlocks(MachineFunction *MF) {
  int SccNum = 0;
  MachineBasicBlock *MBB;
  for (scc_iterator<MachineFunction *> It = scc_begin(MF); !It.isAtEnd();
       ++It, ++SccNum) {
    const std::vector<MachineBasicBlock *> &SccNext = *It;
    for (std::vector<MachineBasicBlock *>::const_iterator
         blockIter = SccNext.begin(), blockEnd = SccNext.end();
         blockIter != blockEnd; ++blockIter) {
      MBB = *blockIter;
      OrderedBlks.push_back(MBB);
      recordSccnum(MBB, SccNum);
    }
  }

  //walk through all the block in func to check for unreachable
  typedef GraphTraits<MachineFunction *> GTM;
  MachineFunction::iterator It = GTM::nodes_begin(MF), E = GTM::nodes_end(MF);
  for (; It != E; ++It) {
    MachineBasicBlock *MBB = &(*It);
    SccNum = getSCCNum(MBB);
    if (SccNum == INVALIDSCCNUM)
      dbgs() << "unreachable block BB" << MBB->getNumber() << "\n";
  }
}

int AMDGPUCFGStructurizer::patternMatch(MachineBasicBlock *MBB) {
  int NumMatch = 0;
  int CurMatch;

  DEBUG(
        dbgs() << "Begin patternMatch BB" << MBB->getNumber() << "\n";
  );

  while ((CurMatch = patternMatchGroup(MBB)) > 0)
    NumMatch += CurMatch;

  DEBUG(
        dbgs() << "End patternMatch BB" << MBB->getNumber()
      << ", numMatch = " << NumMatch << "\n";
  );

  return NumMatch;
}

int AMDGPUCFGStructurizer::patternMatchGroup(MachineBasicBlock *MBB) {
  int NumMatch = 0;
  NumMatch += loopendPatternMatch();
  NumMatch += serialPatternMatch(MBB);
  NumMatch += ifPatternMatch(MBB);
  return NumMatch;
}


int AMDGPUCFGStructurizer::serialPatternMatch(MachineBasicBlock *MBB) {
  if (MBB->succ_size() != 1)
    return 0;

  MachineBasicBlock *childBlk = *MBB->succ_begin();
  if (childBlk->pred_size() != 1 || isActiveLoophead(childBlk))
    return 0;

  mergeSerialBlock(MBB, childBlk);
  ++numSerialPatternMatch;
  return 1;
}

int AMDGPUCFGStructurizer::ifPatternMatch(MachineBasicBlock *MBB) {
  //two edges
  if (MBB->succ_size() != 2)
    return 0;
  if (hasBackEdge(MBB))
    return 0;
  MachineInstr *BranchMI = getNormalBlockBranchInstr(MBB);
  if (!BranchMI)
    return 0;

  assert(isCondBranch(BranchMI));
  int NumMatch = 0;

  MachineBasicBlock *TrueMBB = getTrueBranch(BranchMI);
  NumMatch += serialPatternMatch(TrueMBB);
  NumMatch += ifPatternMatch(TrueMBB);
  MachineBasicBlock *FalseMBB = getFalseBranch(MBB, BranchMI);
  NumMatch += serialPatternMatch(FalseMBB);
  NumMatch += ifPatternMatch(FalseMBB);
  MachineBasicBlock *LandBlk;
  int Cloned = 0;

  assert (!TrueMBB->succ_empty() || !FalseMBB->succ_empty());
  // TODO: Simplify
  if (TrueMBB->succ_size() == 1 && FalseMBB->succ_size() == 1
    && *TrueMBB->succ_begin() == *FalseMBB->succ_begin()) {
    // Diamond pattern
    LandBlk = *TrueMBB->succ_begin();
  } else if (TrueMBB->succ_size() == 1 && *TrueMBB->succ_begin() == FalseMBB) {
    // Triangle pattern, false is empty
    LandBlk = FalseMBB;
    FalseMBB = nullptr;
  } else if (FalseMBB->succ_size() == 1
             && *FalseMBB->succ_begin() == TrueMBB) {
    // Triangle pattern, true is empty
    // We reverse the predicate to make a triangle, empty false pattern;
    std::swap(TrueMBB, FalseMBB);
    reversePredicateSetter(MBB->end());
    LandBlk = FalseMBB;
    FalseMBB = nullptr;
  } else if (FalseMBB->succ_size() == 1
             && isSameloopDetachedContbreak(TrueMBB, FalseMBB)) {
    LandBlk = *FalseMBB->succ_begin();
  } else if (TrueMBB->succ_size() == 1
    && isSameloopDetachedContbreak(FalseMBB, TrueMBB)) {
    LandBlk = *TrueMBB->succ_begin();
  } else {
    return NumMatch + handleJumpintoIf(MBB, TrueMBB, FalseMBB);
  }

  // improveSimpleJumpinfoIf can handle the case where landBlk == NULL but the
  // new BB created for landBlk==NULL may introduce new challenge to the
  // reduction process.
  if (LandBlk &&
      ((TrueMBB && TrueMBB->pred_size() > 1)
      || (FalseMBB && FalseMBB->pred_size() > 1))) {
     Cloned += improveSimpleJumpintoIf(MBB, TrueMBB, FalseMBB, &LandBlk);
  }

  if (TrueMBB && TrueMBB->pred_size() > 1) {
    TrueMBB = cloneBlockForPredecessor(TrueMBB, MBB);
    ++Cloned;
  }

  if (FalseMBB && FalseMBB->pred_size() > 1) {
    FalseMBB = cloneBlockForPredecessor(FalseMBB, MBB);
    ++Cloned;
  }

  mergeIfthenelseBlock(BranchMI, MBB, TrueMBB, FalseMBB, LandBlk);

  ++numIfPatternMatch;

  numClonedBlock += Cloned;

  return 1 + Cloned + NumMatch;
}

int AMDGPUCFGStructurizer::loopendPatternMatch() {
  std::vector<MachineLoop *> NestedLoops;
  for (MachineLoopInfo::iterator It = MLI->begin(), E = MLI->end(); It != E;
       ++It)
    for (MachineLoop *ML : depth_first(*It))
      NestedLoops.push_back(ML);

  if (NestedLoops.size() == 0)
    return 0;

  // Process nested loop outside->inside, so "continue" to a outside loop won't
  // be mistaken as "break" of the current loop.
  int Num = 0;
  for (std::vector<MachineLoop *>::reverse_iterator It = NestedLoops.rbegin(),
      E = NestedLoops.rend(); It != E; ++It) {
    MachineLoop *ExaminedLoop = *It;
    if (ExaminedLoop->getNumBlocks() == 0 || Visited[ExaminedLoop])
      continue;
    DEBUG(dbgs() << "Processing:\n"; ExaminedLoop->dump(););
    int NumBreak = mergeLoop(ExaminedLoop);
    if (NumBreak == -1)
      break;
    Num += NumBreak;
  }
  return Num;
}

int AMDGPUCFGStructurizer::mergeLoop(MachineLoop *LoopRep) {
  MachineBasicBlock *LoopHeader = LoopRep->getHeader();
  MBBVector ExitingMBBs;
  LoopRep->getExitingBlocks(ExitingMBBs);
  assert(!ExitingMBBs.empty() && "Infinite Loop not supported");
  DEBUG(dbgs() << "Loop has " << ExitingMBBs.size() << " exiting blocks\n";);
  // We assume a single ExitBlk
  MBBVector ExitBlks;
  LoopRep->getExitBlocks(ExitBlks);
  SmallPtrSet<MachineBasicBlock *, 2> ExitBlkSet;
  for (unsigned i = 0, e = ExitBlks.size(); i < e; ++i)
    ExitBlkSet.insert(ExitBlks[i]);
  assert(ExitBlkSet.size() == 1);
  MachineBasicBlock *ExitBlk = *ExitBlks.begin();
  assert(ExitBlk && "Loop has several exit block");
  MBBVector LatchBlks;
  typedef GraphTraits<Inverse<MachineBasicBlock*> > InvMBBTraits;
  InvMBBTraits::ChildIteratorType PI = InvMBBTraits::child_begin(LoopHeader),
      PE = InvMBBTraits::child_end(LoopHeader);
  for (; PI != PE; PI++) {
    if (LoopRep->contains(*PI))
      LatchBlks.push_back(*PI);
  }

  for (unsigned i = 0, e = ExitingMBBs.size(); i < e; ++i)
    mergeLoopbreakBlock(ExitingMBBs[i], ExitBlk);
  for (unsigned i = 0, e = LatchBlks.size(); i < e; ++i)
    settleLoopcontBlock(LatchBlks[i], LoopHeader);
  int Match = 0;
  do {
    Match = 0;
    Match += serialPatternMatch(LoopHeader);
    Match += ifPatternMatch(LoopHeader);
  } while (Match > 0);
  mergeLooplandBlock(LoopHeader, ExitBlk);
  MachineLoop *ParentLoop = LoopRep->getParentLoop();
  if (ParentLoop)
    MLI->changeLoopFor(LoopHeader, ParentLoop);
  else
    MLI->removeBlock(LoopHeader);
  Visited[LoopRep] = true;
  return 1;
}

int AMDGPUCFGStructurizer::loopcontPatternMatch(MachineLoop *LoopRep,
    MachineBasicBlock *LoopHeader) {
  int NumCont = 0;
  SmallVector<MachineBasicBlock *, DEFAULT_VEC_SLOTS> ContMBB;
  typedef GraphTraits<Inverse<MachineBasicBlock *> > GTIM;
  GTIM::ChildIteratorType It = GTIM::child_begin(LoopHeader),
      E = GTIM::child_end(LoopHeader);
  for (; It != E; ++It) {
    MachineBasicBlock *MBB = *It;
    if (LoopRep->contains(MBB)) {
      handleLoopcontBlock(MBB, MLI->getLoopFor(MBB),
                          LoopHeader, LoopRep);
      ContMBB.push_back(MBB);
      ++NumCont;
    }
  }

  for (SmallVectorImpl<MachineBasicBlock *>::iterator It = ContMBB.begin(),
      E = ContMBB.end(); It != E; ++It) {
    (*It)->removeSuccessor(LoopHeader);
  }

  numLoopcontPatternMatch += NumCont;

  return NumCont;
}


bool AMDGPUCFGStructurizer::isSameloopDetachedContbreak(
    MachineBasicBlock *Src1MBB, MachineBasicBlock *Src2MBB) {
  if (Src1MBB->succ_size() == 0) {
    MachineLoop *LoopRep = MLI->getLoopFor(Src1MBB);
    if (LoopRep&& LoopRep == MLI->getLoopFor(Src2MBB)) {
      MachineBasicBlock *&TheEntry = LLInfoMap[LoopRep];
      if (TheEntry) {
        DEBUG(
          dbgs() << "isLoopContBreakBlock yes src1 = BB"
                 << Src1MBB->getNumber()
                 << " src2 = BB" << Src2MBB->getNumber() << "\n";
        );
        return true;
      }
    }
  }
  return false;
}

int AMDGPUCFGStructurizer::handleJumpintoIf(MachineBasicBlock *HeadMBB,
    MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB) {
  int Num = handleJumpintoIfImp(HeadMBB, TrueMBB, FalseMBB);
  if (Num == 0) {
    DEBUG(
      dbgs() << "handleJumpintoIf swap trueBlk and FalseBlk" << "\n";
    );
    Num = handleJumpintoIfImp(HeadMBB, FalseMBB, TrueMBB);
  }
  return Num;
}

int AMDGPUCFGStructurizer::handleJumpintoIfImp(MachineBasicBlock *HeadMBB,
    MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB) {
  int Num = 0;
  MachineBasicBlock *DownBlk;

  //trueBlk could be the common post dominator
  DownBlk = TrueMBB;

  DEBUG(
    dbgs() << "handleJumpintoIfImp head = BB" << HeadMBB->getNumber()
           << " true = BB" << TrueMBB->getNumber()
           << ", numSucc=" << TrueMBB->succ_size()
           << " false = BB" << FalseMBB->getNumber() << "\n";
  );

  while (DownBlk) {
    DEBUG(
      dbgs() << "check down = BB" << DownBlk->getNumber();
    );

    if (singlePathTo(FalseMBB, DownBlk) == SinglePath_InPath) {
      DEBUG(
        dbgs() << " working\n";
      );

      Num += cloneOnSideEntryTo(HeadMBB, TrueMBB, DownBlk);
      Num += cloneOnSideEntryTo(HeadMBB, FalseMBB, DownBlk);

      numClonedBlock += Num;
      Num += serialPatternMatch(*HeadMBB->succ_begin());
      Num += serialPatternMatch(*std::next(HeadMBB->succ_begin()));
      Num += ifPatternMatch(HeadMBB);
      assert(Num > 0);

      break;
    }
    DEBUG(
      dbgs() << " not working\n";
    );
    DownBlk = (DownBlk->succ_size() == 1) ? (*DownBlk->succ_begin()) : nullptr;
  } // walk down the postDomTree

  return Num;
}

void AMDGPUCFGStructurizer::showImproveSimpleJumpintoIf(
    MachineBasicBlock *HeadMBB, MachineBasicBlock *TrueMBB,
    MachineBasicBlock *FalseMBB, MachineBasicBlock *LandMBB, bool Detail) {
  dbgs() << "head = BB" << HeadMBB->getNumber()
         << " size = " << HeadMBB->size();
  if (Detail) {
    dbgs() << "\n";
    HeadMBB->print(dbgs());
    dbgs() << "\n";
  }

  if (TrueMBB) {
    dbgs() << ", true = BB" << TrueMBB->getNumber() << " size = "
           << TrueMBB->size() << " numPred = " << TrueMBB->pred_size();
    if (Detail) {
      dbgs() << "\n";
      TrueMBB->print(dbgs());
      dbgs() << "\n";
    }
  }
  if (FalseMBB) {
    dbgs() << ", false = BB" << FalseMBB->getNumber() << " size = "
           << FalseMBB->size() << " numPred = " << FalseMBB->pred_size();
    if (Detail) {
      dbgs() << "\n";
      FalseMBB->print(dbgs());
      dbgs() << "\n";
    }
  }
  if (LandMBB) {
    dbgs() << ", land = BB" << LandMBB->getNumber() << " size = "
           << LandMBB->size() << " numPred = " << LandMBB->pred_size();
    if (Detail) {
      dbgs() << "\n";
      LandMBB->print(dbgs());
      dbgs() << "\n";
    }
  }

    dbgs() << "\n";
}

int AMDGPUCFGStructurizer::improveSimpleJumpintoIf(MachineBasicBlock *HeadMBB,
    MachineBasicBlock *TrueMBB, MachineBasicBlock *FalseMBB,
    MachineBasicBlock **LandMBBPtr) {
  bool MigrateTrue = false;
  bool MigrateFalse = false;

  MachineBasicBlock *LandBlk = *LandMBBPtr;

  assert((!TrueMBB || TrueMBB->succ_size() <= 1)
         && (!FalseMBB || FalseMBB->succ_size() <= 1));

  if (TrueMBB == FalseMBB)
    return 0;

  MigrateTrue = needMigrateBlock(TrueMBB);
  MigrateFalse = needMigrateBlock(FalseMBB);

  if (!MigrateTrue && !MigrateFalse)
    return 0;

  // If we need to migrate either trueBlk and falseBlk, migrate the rest that
  // have more than one predecessors.  without doing this, its predecessor
  // rather than headBlk will have undefined value in initReg.
  if (!MigrateTrue && TrueMBB && TrueMBB->pred_size() > 1)
    MigrateTrue = true;
  if (!MigrateFalse && FalseMBB && FalseMBB->pred_size() > 1)
    MigrateFalse = true;

  DEBUG(
    dbgs() << "before improveSimpleJumpintoIf: ";
    showImproveSimpleJumpintoIf(HeadMBB, TrueMBB, FalseMBB, LandBlk, 0);
  );

  // org: headBlk => if () {trueBlk} else {falseBlk} => landBlk
  //
  // new: headBlk => if () {initReg = 1; org trueBlk branch} else
  //      {initReg = 0; org falseBlk branch }
  //      => landBlk => if (initReg) {org trueBlk} else {org falseBlk}
  //      => org landBlk
  //      if landBlk->pred_size() > 2, put the about if-else inside
  //      if (initReg !=2) {...}
  //
  // add initReg = initVal to headBlk

  const TargetRegisterClass * I32RC = TRI->getCFGStructurizerRegClass(MVT::i32);
  if (!MigrateTrue || !MigrateFalse) {
    // XXX: We have an opportunity here to optimize the "branch into if" case
    // here.  Branch into if looks like this:
    //                        entry
    //                       /     |
    //           diamond_head       branch_from
    //             /      \           |
    // diamond_false        diamond_true
    //             \      /
    //               done
    //
    // The diamond_head block begins the "if" and the diamond_true block
    // is the block being "branched into".
    //
    // If MigrateTrue is true, then TrueBB is the block being "branched into"
    // and if MigrateFalse is true, then FalseBB is the block being
    // "branched into"
    // 
    // Here is the pseudo code for how I think the optimization should work:
    // 1. Insert MOV GPR0, 0 before the branch instruction in diamond_head.
    // 2. Insert MOV GPR0, 1 before the branch instruction in branch_from.
    // 3. Move the branch instruction from diamond_head into its own basic
    //    block (new_block).
    // 4. Add an unconditional branch from diamond_head to new_block
    // 5. Replace the branch instruction in branch_from with an unconditional
    //    branch to new_block.  If branch_from has multiple predecessors, then
    //    we need to replace the True/False block in the branch
    //    instruction instead of replacing it.
    // 6. Change the condition of the branch instruction in new_block from
    //    COND to (COND || GPR0)
    //
    // In order insert these MOV instruction, we will need to use the
    // RegisterScavenger.  Usually liveness stops being tracked during
    // the late machine optimization passes, however if we implement
    // bool TargetRegisterInfo::requiresRegisterScavenging(
    //                                                const MachineFunction &MF)
    // and have it return true, liveness will be tracked correctly 
    // by generic optimization passes.  We will also need to make sure that
    // all of our target-specific passes that run after regalloc and before
    // the CFGStructurizer track liveness and we will need to modify this pass
    // to correctly track liveness.
    //
    // After the above changes, the new CFG should look like this:
    //                        entry
    //                       /     |
    //           diamond_head       branch_from
    //                       \     /
    //                      new_block
    //                      /      |
    //         diamond_false        diamond_true
    //                      \      /
    //                        done
    //
    // Without this optimization, we are forced to duplicate the diamond_true
    // block and we will end up with a CFG like this:
    //
    //                        entry
    //                       /     |
    //           diamond_head       branch_from
    //             /      \                   |
    // diamond_false        diamond_true      diamond_true (duplicate)
    //             \      /                   |
    //               done --------------------|
    //
    // Duplicating diamond_true can be very costly especially if it has a
    // lot of instructions.
    return 0;
  }

  int NumNewBlk = 0;

  bool LandBlkHasOtherPred = (LandBlk->pred_size() > 2);

  //insert AMDGPU::ENDIF to avoid special case "input landBlk == NULL"
  MachineBasicBlock::iterator I = insertInstrBefore(LandBlk, AMDGPU::ENDIF);

  if (LandBlkHasOtherPred) {
    llvm_unreachable("Extra register needed to handle CFG");
    unsigned CmpResReg =
      HeadMBB->getParent()->getRegInfo().createVirtualRegister(I32RC);
    llvm_unreachable("Extra compare instruction needed to handle CFG");
    insertCondBranchBefore(LandBlk, I, AMDGPU::IF_PREDICATE_SET,
        CmpResReg, DebugLoc());
  }

  // XXX: We are running this after RA, so creating virtual registers will
  // cause an assertion failure in the PostRA scheduling pass.
  unsigned InitReg =
    HeadMBB->getParent()->getRegInfo().createVirtualRegister(I32RC);
  insertCondBranchBefore(LandBlk, I, AMDGPU::IF_PREDICATE_SET, InitReg,
      DebugLoc());

  if (MigrateTrue) {
    migrateInstruction(TrueMBB, LandBlk, I);
    // need to uncondionally insert the assignment to ensure a path from its
    // predecessor rather than headBlk has valid value in initReg if
    // (initVal != 1).
    llvm_unreachable("Extra register needed to handle CFG");
  }
  insertInstrBefore(I, AMDGPU::ELSE);

  if (MigrateFalse) {
    migrateInstruction(FalseMBB, LandBlk, I);
    // need to uncondionally insert the assignment to ensure a path from its
    // predecessor rather than headBlk has valid value in initReg if
    // (initVal != 0)
    llvm_unreachable("Extra register needed to handle CFG");
  }

  if (LandBlkHasOtherPred) {
    // add endif
    insertInstrBefore(I, AMDGPU::ENDIF);

    // put initReg = 2 to other predecessors of landBlk
    for (MachineBasicBlock::pred_iterator PI = LandBlk->pred_begin(),
         PE = LandBlk->pred_end(); PI != PE; ++PI) {
      MachineBasicBlock *MBB = *PI;
      if (MBB != TrueMBB && MBB != FalseMBB)
        llvm_unreachable("Extra register needed to handle CFG");
    }
  }
  DEBUG(
    dbgs() << "result from improveSimpleJumpintoIf: ";
    showImproveSimpleJumpintoIf(HeadMBB, TrueMBB, FalseMBB, LandBlk, 0);
  );

  // update landBlk
  *LandMBBPtr = LandBlk;

  return NumNewBlk;
}

void AMDGPUCFGStructurizer::handleLoopcontBlock(MachineBasicBlock *ContingMBB,
    MachineLoop *ContingLoop, MachineBasicBlock *ContMBB,
    MachineLoop *ContLoop) {
  DEBUG(dbgs() << "loopcontPattern cont = BB" << ContingMBB->getNumber()
               << " header = BB" << ContMBB->getNumber() << "\n";
        dbgs() << "Trying to continue loop-depth = "
               << getLoopDepth(ContLoop)
               << " from loop-depth = " << getLoopDepth(ContingLoop) << "\n";);
  settleLoopcontBlock(ContingMBB, ContMBB);
}

void AMDGPUCFGStructurizer::mergeSerialBlock(MachineBasicBlock *DstMBB,
    MachineBasicBlock *SrcMBB) {
  DEBUG(
    dbgs() << "serialPattern BB" << DstMBB->getNumber()
           << " <= BB" << SrcMBB->getNumber() << "\n";
  );
  DstMBB->splice(DstMBB->end(), SrcMBB, SrcMBB->begin(), SrcMBB->end());

  DstMBB->removeSuccessor(SrcMBB);
  cloneSuccessorList(DstMBB, SrcMBB);

  removeSuccessor(SrcMBB);
  MLI->removeBlock(SrcMBB);
  retireBlock(SrcMBB);
}

void AMDGPUCFGStructurizer::mergeIfthenelseBlock(MachineInstr *BranchMI,
    MachineBasicBlock *MBB, MachineBasicBlock *TrueMBB,
    MachineBasicBlock *FalseMBB, MachineBasicBlock *LandMBB) {
  assert (TrueMBB);
  DEBUG(
    dbgs() << "ifPattern BB" << MBB->getNumber();
    dbgs() << "{  ";
    if (TrueMBB) {
      dbgs() << "BB" << TrueMBB->getNumber();
    }
    dbgs() << "  } else ";
    dbgs() << "{  ";
    if (FalseMBB) {
      dbgs() << "BB" << FalseMBB->getNumber();
    }
    dbgs() << "  }\n ";
    dbgs() << "landBlock: ";
    if (!LandMBB) {
      dbgs() << "NULL";
    } else {
      dbgs() << "BB" << LandMBB->getNumber();
    }
    dbgs() << "\n";
  );

  int OldOpcode = BranchMI->getOpcode();
  DebugLoc BranchDL = BranchMI->getDebugLoc();

//    transform to
//    if cond
//       trueBlk
//    else
//       falseBlk
//    endif
//    landBlk

  MachineBasicBlock::iterator I = BranchMI;
  insertCondBranchBefore(I, getBranchNzeroOpcode(OldOpcode),
      BranchDL);

  if (TrueMBB) {
    MBB->splice(I, TrueMBB, TrueMBB->begin(), TrueMBB->end());
    MBB->removeSuccessor(TrueMBB);
    if (LandMBB && TrueMBB->succ_size()!=0)
      TrueMBB->removeSuccessor(LandMBB);
    retireBlock(TrueMBB);
    MLI->removeBlock(TrueMBB);
  }

  if (FalseMBB) {
    insertInstrBefore(I, AMDGPU::ELSE);
    MBB->splice(I, FalseMBB, FalseMBB->begin(),
                   FalseMBB->end());
    MBB->removeSuccessor(FalseMBB);
    if (LandMBB && FalseMBB->succ_size() != 0)
      FalseMBB->removeSuccessor(LandMBB);
    retireBlock(FalseMBB);
    MLI->removeBlock(FalseMBB);
  }
  insertInstrBefore(I, AMDGPU::ENDIF);

  BranchMI->eraseFromParent();

  if (LandMBB && TrueMBB && FalseMBB)
    MBB->addSuccessor(LandMBB);

}

void AMDGPUCFGStructurizer::mergeLooplandBlock(MachineBasicBlock *DstBlk,
    MachineBasicBlock *LandMBB) {
  DEBUG(dbgs() << "loopPattern header = BB" << DstBlk->getNumber()
               << " land = BB" << LandMBB->getNumber() << "\n";);

  insertInstrBefore(DstBlk, AMDGPU::WHILELOOP, DebugLoc());
  insertInstrEnd(DstBlk, AMDGPU::ENDLOOP, DebugLoc());
  DstBlk->addSuccessor(LandMBB);
  DstBlk->removeSuccessor(DstBlk);
}


void AMDGPUCFGStructurizer::mergeLoopbreakBlock(MachineBasicBlock *ExitingMBB,
    MachineBasicBlock *LandMBB) {
  DEBUG(dbgs() << "loopbreakPattern exiting = BB" << ExitingMBB->getNumber()
               << " land = BB" << LandMBB->getNumber() << "\n";);
  MachineInstr *BranchMI = getLoopendBlockBranchInstr(ExitingMBB);
  assert(BranchMI && isCondBranch(BranchMI));
  DebugLoc DL = BranchMI->getDebugLoc();
  MachineBasicBlock *TrueBranch = getTrueBranch(BranchMI);
  MachineBasicBlock::iterator I = BranchMI;
  if (TrueBranch != LandMBB)
    reversePredicateSetter(I);
  insertCondBranchBefore(ExitingMBB, I, AMDGPU::IF_PREDICATE_SET, AMDGPU::PREDICATE_BIT, DL);
  insertInstrBefore(I, AMDGPU::BREAK);
  insertInstrBefore(I, AMDGPU::ENDIF);
  //now branchInst can be erase safely
  BranchMI->eraseFromParent();
  //now take care of successors, retire blocks
  ExitingMBB->removeSuccessor(LandMBB);
}

void AMDGPUCFGStructurizer::settleLoopcontBlock(MachineBasicBlock *ContingMBB,
    MachineBasicBlock *ContMBB) {
  DEBUG(dbgs() << "settleLoopcontBlock conting = BB"
               << ContingMBB->getNumber()
               << ", cont = BB" << ContMBB->getNumber() << "\n";);

  MachineInstr *MI = getLoopendBlockBranchInstr(ContingMBB);
  if (MI) {
    assert(isCondBranch(MI));
    MachineBasicBlock::iterator I = MI;
    MachineBasicBlock *TrueBranch = getTrueBranch(MI);
    int OldOpcode = MI->getOpcode();
    DebugLoc DL = MI->getDebugLoc();

    bool UseContinueLogical = ((&*ContingMBB->rbegin()) == MI);

    if (UseContinueLogical == false) {
      int BranchOpcode =
          TrueBranch == ContMBB ? getBranchNzeroOpcode(OldOpcode) :
          getBranchZeroOpcode(OldOpcode);
      insertCondBranchBefore(I, BranchOpcode, DL);
      // insertEnd to ensure phi-moves, if exist, go before the continue-instr.
      insertInstrEnd(ContingMBB, AMDGPU::CONTINUE, DL);
      insertInstrEnd(ContingMBB, AMDGPU::ENDIF, DL);
    } else {
      int BranchOpcode =
          TrueBranch == ContMBB ? getContinueNzeroOpcode(OldOpcode) :
          getContinueZeroOpcode(OldOpcode);
      insertCondBranchBefore(I, BranchOpcode, DL);
    }

    MI->eraseFromParent();
  } else {
    // if we've arrived here then we've already erased the branch instruction
    // travel back up the basic block to see the last reference of our debug
    // location we've just inserted that reference here so it should be
    // representative insertEnd to ensure phi-moves, if exist, go before the
    // continue-instr.
    insertInstrEnd(ContingMBB, AMDGPU::CONTINUE,
        getLastDebugLocInBB(ContingMBB));
  }
}

int AMDGPUCFGStructurizer::cloneOnSideEntryTo(MachineBasicBlock *PreMBB,
    MachineBasicBlock *SrcMBB, MachineBasicBlock *DstMBB) {
  int Cloned = 0;
  assert(PreMBB->isSuccessor(SrcMBB));
  while (SrcMBB && SrcMBB != DstMBB) {
    assert(SrcMBB->succ_size() == 1);
    if (SrcMBB->pred_size() > 1) {
      SrcMBB = cloneBlockForPredecessor(SrcMBB, PreMBB);
      ++Cloned;
    }

    PreMBB = SrcMBB;
    SrcMBB = *SrcMBB->succ_begin();
  }

  return Cloned;
}

MachineBasicBlock *
AMDGPUCFGStructurizer::cloneBlockForPredecessor(MachineBasicBlock *MBB,
    MachineBasicBlock *PredMBB) {
  assert(PredMBB->isSuccessor(MBB) &&
         "succBlk is not a prececessor of curBlk");

  MachineBasicBlock *CloneMBB = clone(MBB);  //clone instructions
  replaceInstrUseOfBlockWith(PredMBB, MBB, CloneMBB);
  //srcBlk, oldBlk, newBlk

  PredMBB->removeSuccessor(MBB);
  PredMBB->addSuccessor(CloneMBB);

  // add all successor to cloneBlk
  cloneSuccessorList(CloneMBB, MBB);

  numClonedInstr += MBB->size();

  DEBUG(
    dbgs() << "Cloned block: " << "BB"
           << MBB->getNumber() << "size " << MBB->size() << "\n";
  );

  SHOWNEWBLK(CloneMBB, "result of Cloned block: ");

  return CloneMBB;
}

void AMDGPUCFGStructurizer::migrateInstruction(MachineBasicBlock *SrcMBB,
    MachineBasicBlock *DstMBB, MachineBasicBlock::iterator I) {
  MachineBasicBlock::iterator SpliceEnd;
  //look for the input branchinstr, not the AMDGPU branchinstr
  MachineInstr *BranchMI = getNormalBlockBranchInstr(SrcMBB);
  if (!BranchMI) {
    DEBUG(
      dbgs() << "migrateInstruction don't see branch instr\n" ;
    );
    SpliceEnd = SrcMBB->end();
  } else {
    DEBUG(
      dbgs() << "migrateInstruction see branch instr\n" ;
      BranchMI->dump();
    );
    SpliceEnd = BranchMI;
  }
  DEBUG(
    dbgs() << "migrateInstruction before splice dstSize = " << DstMBB->size()
      << "srcSize = " << SrcMBB->size() << "\n";
  );

  //splice insert before insertPos
  DstMBB->splice(I, SrcMBB, SrcMBB->begin(), SpliceEnd);

  DEBUG(
    dbgs() << "migrateInstruction after splice dstSize = " << DstMBB->size()
      << "srcSize = " << SrcMBB->size() << "\n";
  );
}

MachineBasicBlock *
AMDGPUCFGStructurizer::normalizeInfiniteLoopExit(MachineLoop* LoopRep) {
  MachineBasicBlock *LoopHeader = LoopRep->getHeader();
  MachineBasicBlock *LoopLatch = LoopRep->getLoopLatch();
  const TargetRegisterClass * I32RC = TRI->getCFGStructurizerRegClass(MVT::i32);

  if (!LoopHeader || !LoopLatch)
    return nullptr;
  MachineInstr *BranchMI = getLoopendBlockBranchInstr(LoopLatch);
  // Is LoopRep an infinite loop ?
  if (!BranchMI || !isUncondBranch(BranchMI))
    return nullptr;

  MachineBasicBlock *DummyExitBlk = FuncRep->CreateMachineBasicBlock();
  FuncRep->push_back(DummyExitBlk);  //insert to function
  SHOWNEWBLK(DummyExitBlk, "DummyExitBlock to normalize infiniteLoop: ");
  DEBUG(dbgs() << "Old branch instr: " << *BranchMI << "\n";);
  MachineBasicBlock::iterator I = BranchMI;
  unsigned ImmReg = FuncRep->getRegInfo().createVirtualRegister(I32RC);
  llvm_unreachable("Extra register needed to handle CFG");
  MachineInstr *NewMI = insertInstrBefore(I, AMDGPU::BRANCH_COND_i32);
  MachineInstrBuilder MIB(*FuncRep, NewMI);
  MIB.addMBB(LoopHeader);
  MIB.addReg(ImmReg, false);
  SHOWNEWINSTR(NewMI);
  BranchMI->eraseFromParent();
  LoopLatch->addSuccessor(DummyExitBlk);

  return DummyExitBlk;
}

void AMDGPUCFGStructurizer::removeUnconditionalBranch(MachineBasicBlock *MBB) {
  MachineInstr *BranchMI;

  // I saw two unconditional branch in one basic block in example
  // test_fc_do_while_or.c need to fix the upstream on this to remove the loop.
  while ((BranchMI = getLoopendBlockBranchInstr(MBB))
          && isUncondBranch(BranchMI)) {
    DEBUG(dbgs() << "Removing uncond branch instr"; BranchMI->dump(););
    BranchMI->eraseFromParent();
  }
}

void AMDGPUCFGStructurizer::removeRedundantConditionalBranch(
    MachineBasicBlock *MBB) {
  if (MBB->succ_size() != 2)
    return;
  MachineBasicBlock *MBB1 = *MBB->succ_begin();
  MachineBasicBlock *MBB2 = *std::next(MBB->succ_begin());
  if (MBB1 != MBB2)
    return;

  MachineInstr *BranchMI = getNormalBlockBranchInstr(MBB);
  assert(BranchMI && isCondBranch(BranchMI));
  DEBUG(dbgs() << "Removing unneeded cond branch instr"; BranchMI->dump(););
  BranchMI->eraseFromParent();
  SHOWNEWBLK(MBB1, "Removing redundant successor");
  MBB->removeSuccessor(MBB1);
}

void AMDGPUCFGStructurizer::addDummyExitBlock(
    SmallVectorImpl<MachineBasicBlock*> &RetMBB) {
  MachineBasicBlock *DummyExitBlk = FuncRep->CreateMachineBasicBlock();
  FuncRep->push_back(DummyExitBlk);  //insert to function
  insertInstrEnd(DummyExitBlk, AMDGPU::RETURN);

  for (SmallVectorImpl<MachineBasicBlock *>::iterator It = RetMBB.begin(),
       E = RetMBB.end(); It != E; ++It) {
    MachineBasicBlock *MBB = *It;
    MachineInstr *MI = getReturnInstr(MBB);
    if (MI)
      MI->eraseFromParent();
    MBB->addSuccessor(DummyExitBlk);
    DEBUG(
      dbgs() << "Add dummyExitBlock to BB" << MBB->getNumber()
             << " successors\n";
    );
  }
  SHOWNEWBLK(DummyExitBlk, "DummyExitBlock: ");
}

void AMDGPUCFGStructurizer::removeSuccessor(MachineBasicBlock *MBB) {
  while (MBB->succ_size())
    MBB->removeSuccessor(*MBB->succ_begin());
}

void AMDGPUCFGStructurizer::recordSccnum(MachineBasicBlock *MBB,
    int SccNum) {
  BlockInformation *&srcBlkInfo = BlockInfoMap[MBB];
  if (!srcBlkInfo)
    srcBlkInfo = new BlockInformation();
  srcBlkInfo->SccNum = SccNum;
}

void AMDGPUCFGStructurizer::retireBlock(MachineBasicBlock *MBB) {
  DEBUG(
        dbgs() << "Retiring BB" << MBB->getNumber() << "\n";
  );

  BlockInformation *&SrcBlkInfo = BlockInfoMap[MBB];

  if (!SrcBlkInfo)
    SrcBlkInfo = new BlockInformation();

  SrcBlkInfo->IsRetired = true;
  assert(MBB->succ_size() == 0 && MBB->pred_size() == 0
         && "can't retire block yet");
}

void AMDGPUCFGStructurizer::setLoopLandBlock(MachineLoop *loopRep,
    MachineBasicBlock *MBB) {
  MachineBasicBlock *&TheEntry = LLInfoMap[loopRep];
  if (!MBB) {
    MBB = FuncRep->CreateMachineBasicBlock();
    FuncRep->push_back(MBB);  //insert to function
    SHOWNEWBLK(MBB, "DummyLandingBlock for loop without break: ");
  }
  TheEntry = MBB;
  DEBUG(
    dbgs() << "setLoopLandBlock loop-header = BB"
           << loopRep->getHeader()->getNumber()
           << "  landing-block = BB" << MBB->getNumber() << "\n";
  );
}

MachineBasicBlock *
AMDGPUCFGStructurizer::findNearestCommonPostDom(MachineBasicBlock *MBB1,
    MachineBasicBlock *MBB2) {

  if (PDT->dominates(MBB1, MBB2))
    return MBB1;
  if (PDT->dominates(MBB2, MBB1))
    return MBB2;

  MachineDomTreeNode *Node1 = PDT->getNode(MBB1);
  MachineDomTreeNode *Node2 = PDT->getNode(MBB2);

  // Handle newly cloned node.
  if (!Node1 && MBB1->succ_size() == 1)
    return findNearestCommonPostDom(*MBB1->succ_begin(), MBB2);
  if (!Node2 && MBB2->succ_size() == 1)
    return findNearestCommonPostDom(MBB1, *MBB2->succ_begin());

  if (!Node1 || !Node2)
    return nullptr;

  Node1 = Node1->getIDom();
  while (Node1) {
    if (PDT->dominates(Node1, Node2))
      return Node1->getBlock();
    Node1 = Node1->getIDom();
  }

  return nullptr;
}

MachineBasicBlock *
AMDGPUCFGStructurizer::findNearestCommonPostDom(
    std::set<MachineBasicBlock *> &MBBs) {
  MachineBasicBlock *CommonDom;
  std::set<MachineBasicBlock *>::const_iterator It = MBBs.begin();
  std::set<MachineBasicBlock *>::const_iterator E = MBBs.end();
  for (CommonDom = *It; It != E && CommonDom; ++It) {
    MachineBasicBlock *MBB = *It;
    if (MBB != CommonDom)
      CommonDom = findNearestCommonPostDom(MBB, CommonDom);
  }

  DEBUG(
    dbgs() << "Common post dominator for exit blocks is ";
    if (CommonDom)
          dbgs() << "BB" << CommonDom->getNumber() << "\n";
    else
      dbgs() << "NULL\n";
  );

  return CommonDom;
}

char AMDGPUCFGStructurizer::ID = 0;

} // end anonymous namespace


INITIALIZE_PASS_BEGIN(AMDGPUCFGStructurizer, "amdgpustructurizer",
                      "AMDGPU CFG Structurizer", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(AMDGPUCFGStructurizer, "amdgpustructurizer",
                      "AMDGPU CFG Structurizer", false, false)

FunctionPass *llvm::createAMDGPUCFGStructurizerPass() {
  return new AMDGPUCFGStructurizer();
}
