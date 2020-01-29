//===- EVMStackAllocAnalysis ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the AMGPU address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMStackAllocAnalysis.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "evm-stackalloc"

// Register this pass...
char EVMStackAlloc::ID = 0;

INITIALIZE_PASS(EVMStackAlloc, "evm-stackalloc",
                "Stack Allocation Analysis", false, true)


unsigned EVMStackStatus::getStackDepth() const {
  return stackElements.size();
}

unsigned EVMStackStatus::get(unsigned depth) const {
  return stackElements.rbegin()[depth];
}

void EVMStackStatus::swap(unsigned depth) {
    if (depth == 0) {
      return;
    }
    assert(stackElements.size() >= 2);
    LLVM_DEBUG({
      unsigned first = stackElements.rbegin()[0];
      unsigned second = stackElements.rbegin()[depth];
      unsigned fst_idx = Register::virtReg2Index(first);
      unsigned snd_idx = Register::virtReg2Index(second);
      dbgs() << "    >>> SWAP" << depth << ": (%" << fst_idx << "<-> %" << snd_idx
             << ")\n";
    });
    std::iter_swap(stackElements.rbegin(), stackElements.rbegin() + depth);
}

void EVMStackStatus::dup(unsigned depth) {
  unsigned elem = *(stackElements.rbegin() + depth);

  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(elem);
    dbgs() << "    >>> DUP" << depth << "(%" << idx << ")\n";
  });

  stackElements.push_back(elem);
}

unsigned EVMStackStatus::pop() {
  unsigned reg = stackElements.back();
  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "    >>> POP (%" << idx << ")\n";
  });
  stackElements.pop_back();
  return reg;
}

void EVMStackStatus::push(unsigned reg) {
  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "    >>> PUSH %" << idx << "\n";
  });
  stackElements.push_back(reg);
}


void EVMStackStatus::dump() const {
  LLVM_DEBUG({
    dbgs() << "    Stack : (";
    unsigned counter = 0;
    for (auto i = stackElements.rbegin(), e = stackElements.rend(); i != e; ++i) {
      unsigned idx = Register::virtReg2Index(*i);
      dbgs() << "%" << idx << ", ";
      counter ++;
    }
    dbgs() << ")\n";
  });
}

unsigned EVMStackStatus::findRegDepth(unsigned reg) const {
  unsigned curHeight = getStackDepth();

  for (unsigned d = 0; d < curHeight; ++d) {
    unsigned stackReg = get(d);
    if (stackReg == reg) {
      /*
      LLVM_DEBUG({
        dbgs() << "    Found %" << Register::virtReg2Index(reg)
               << " at depth: " << d << "\n";
      });
      */
      return d;
    }
  }
  llvm_unreachable("Cannot find register on stack");
}


unsigned EdgeSets::getEdgeSetIndex(Edge edge) const {
  // Linear search. Can improve it.
  for (std::pair<unsigned, Edge> edgePair : edgeIndex) {
    if (edgePair.second == edge) {
      auto result = edgeIndex2EdgeSet.find(edgePair.first);
      assert(result != edgeIndex2EdgeSet.end() && "Cannot find edge set!");
      return result->second;
    }
  }
  llvm_unreachable("Cannot find edge index.");
}

unsigned EdgeSets::getEdgeIndex(Edge edge) const {
  // Linear search. Can improve it.
  for (std::pair<unsigned, Edge> edgePair : edgeIndex) {
    if (edgePair.second == edge) {
      return edgePair.first;
    }
  }
  llvm_unreachable("Cannot find edge index.");
}


void EdgeSets::collectEdges(MachineFunction *MF) {
  std::set<MachineBasicBlock*> visited;

  // Artifically create a {NULL, EntryMBB} Edge,
  // and {ExitMBB, NULL} Edge
  edgeIndex[0] = Edge(NULL, &MF->front());
  edgeIndex[1] = Edge(&MF->back(), NULL);

  for (MachineBasicBlock &MBB : *MF) {
    // Skip if we have visited this MBB:
    if (visited.find(&MBB) != visited.end()) {
      continue;
    }

    visited.insert(&MBB);
    for (auto *NextMBB : MBB.successors()) {
      unsigned key = edgeIndex.size();
      assert(edgeIndex.find(key) == edgeIndex.end() &&
             "Trying to emplace edge indexes.");
      edgeIndex[edgeIndex.size()] = Edge(&MBB, NextMBB);
    }
  }
}

void EdgeSets::computeEdgeSets(MachineFunction *MF) {
  // First, assign each edge with a number:
  collectEdges(MF);

  // Insert a default edge set: NULL->Entry:

  // Then, assign a new edge set for each of the edges
  for (std::pair<unsigned, Edge> index : edgeIndex) {
    edgeIndex2EdgeSet.insert({index.first, index.first});
  }

  // Criteria: Two MBBs share a same edge set if:
  // 1. they have a common child.
  // 2. they have a common parent.  
  for (MachineBasicBlock &MBB : *MF) {
    // we have more than one predecessors
    if (std::distance(MBB.pred_begin(), MBB.pred_end()) > 1) {
      // all predecessors belong to an edge set
      MachineBasicBlock *first = *MBB.pred_begin();

      MachineBasicBlock::pred_iterator iter = ++MBB.pred_begin();
      while (iter != MBB.pred_end()) {
        mergeEdgeSets({first, &MBB}, {*iter, &MBB});
      }
    }
    if (std::distance(MBB.succ_begin(), MBB.succ_end()) > 1) {
      // all sucessors belong to an edge set
      MachineBasicBlock *first = *MBB.succ_begin();

      MachineBasicBlock::pred_iterator iter = ++MBB.succ_begin();
      while (iter != MBB.succ_end()) {
        mergeEdgeSets({&MBB, first}, {&MBB, *iter++});
      }
    }
  }
}

void EdgeSets::mergeEdgeSets(Edge edge1, Edge edge2) {
  unsigned edgeSet1 = getEdgeSetIndex(edge1);
  unsigned edgeSet2 = getEdgeSetIndex(edge2);

  // return if they are already in the same edge set.
  if (edgeSet1 == edgeSet2) {
    return;
  }

  // change edgeSet2 to edgeSet1
  for (std::pair<unsigned, unsigned> index2Set : edgeIndex2EdgeSet) {
    if (index2Set.second == edgeSet2) {
      edgeIndex2EdgeSet[index2Set.first] = edgeSet1;
    }
  }
  return;
}

EdgeSets::Edge EdgeSets::getEdge(unsigned edgeId) const {
  auto result = edgeIndex.find(edgeId);
  assert(result != edgeIndex.end());
  return result->second;
}

void EdgeSets::printEdge(Edge edge) const {
  LLVM_DEBUG({
    if (edge.first == NULL) {
      dbgs() << "    EntryMBB"
             << " -> MBB" << edge.second->getNumber() << "\n";
    } else if (edge.second == NULL) {
      dbgs() << "    MBB" << edge.first->getNumber() << " -> ExitMBB\n";
    } else {
      dbgs() << "    MBB" << edge.first->getNumber() << " -> MBB"
             << edge.second->getNumber() << "\n";
    }
  });
}

void EdgeSets::dump() const {
  LLVM_DEBUG({
    dbgs() << "  Computed edge set: (size of edgesets: " << 0 << ")\n";
    for (std::pair<unsigned, unsigned> it : edgeIndex2EdgeSet) {
      // edge set Index : edge index
      unsigned edgeId = it.first;
      unsigned esIndex = it.second;
      // find the Edges
      Edge edge = getEdge(edgeId);
      dbgs() << "    " << esIndex << " : ";
      printEdge(edge);
    }
    dbgs() << "-------------------------------------------------\n";
  });
}

void EVMStackAlloc::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LiveIntervals>();
  //AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void EVMStackAlloc::initialize() {
  edgeSets.reset();
  regAssignments.clear();
  currentStackStatus.reset();
  edgeset2assignment.clear();
}

void EVMStackAlloc::allocateRegistersToStack(MachineFunction &F) {
  // assert(MRI->isSSA() && "Must be run on SSA.");
  // clean up previous assignments.
  initialize();

  // compute edge sets
  edgeSets.computeEdgeSets(&F);
  edgeSets.dump();

  // analyze each BB
  for (MachineBasicBlock &MBB : F) {
    analyzeBasicBlock(&MBB);
  }

  // exit check: stack should be empty
}

static unsigned getDefRegister(const MachineInstr &MI) {
    unsigned numDefs = MI.getDesc().getNumDefs();
    assert(numDefs <= 1);
    const MachineOperand &def = *MI.defs().begin();
    assert(def.isReg() && def.isDef());
    return def.getReg();
}

bool EVMStackAlloc::defIsLocal(const MachineInstr &MI) const {
  unsigned defReg = getDefRegister(MI);

  // examine live range to see if it only covers a single MBB:
  const LiveInterval &LI = LIS->getInterval(defReg);
  // if it has multiple VNs, ignore it.
  if (LI.segments.size() > 1) {
    return false;
  }

  // if it goes across multiple MBBs, ignore it.
  const MachineBasicBlock* MBB = MI.getParent();
  SlotIndex MBBBegin = LIS->getMBBStartIdx(MBB);
  SlotIndex MBBEnd = LIS->getMBBEndIdx(MBB);

  return LI.isLocal(MBBBegin, MBBEnd);
}
void EVMStackAlloc::consolidateXRegionForEdgeSet(unsigned edgetSetIndex) {
  // TODO
  llvm_unreachable("unimplemented");
}

void EVMStackAlloc::beginOfBlockUpdates(MachineBasicBlock *MBB) {
  stack.clear();

  // find edgeset
  if (MBB->pred_empty()) {
    return;
  }
  MachineBasicBlock* Pred = *MBB->pred_begin();
  EdgeSets::Edge edge = {Pred, MBB};
  unsigned setIndex = edgeSets.getEdgeSetIndex(edge);

  assert(edgeset2assignment.find(setIndex) != edgeset2assignment.end());
  ActiveStack xStack = edgeset2assignment[setIndex].first;
  MemorySlots memslots = edgeset2assignment[setIndex].second;

  // initialize the stack using x stack.
  for (unsigned i = 0; i < xStack.size(); ++i) {
    unsigned reg = xStack[i];
    stack.push(reg);
    // also need to update X Stack
    StackAssignment SA = regAssignments.lookup(reg);
    if (SA.region == X_STACK) {
      stack.pushElementToXRegion();
    }
  }
  
  // also initialize the memory slots;
  memoryAssignment = memslots;

  // Now pop those dead x registers.
  MachineInstr &MI = *MBB->begin();
  for (unsigned i = 0; i < xStack.size(); ++i) {
    unsigned xreg = xStack[i];
    if (regIsDeadAtBeginningOfMBB(xreg, MBB)) {
      LLVM_DEBUG({
        dbgs() << "  %" << Register::virtReg2Index(xreg)
               << " is dead, popping:\n";
      });
      SwapRegToTop(xreg, MI);
      insertPopBefore(MI);
      stack.popElementFromXRegion();
    }
  }

  // Now free up memory slots if its occupant is out of range
  SlotIndex MBBBeginSlot = LIS->getMBBStartIdx(MBB);
  for (size_t i = 0; i < memoryAssignment.size(); ++i) {
    unsigned reg = memoryAssignment[i];
    if (reg == 0) {
      continue;
    }
    const LiveInterval &LI = LIS->getInterval(reg);
    if (!LI.liveAt(MBBBeginSlot)) {
      LLVM_DEBUG({
        dbgs() << "    Memslot: free up %" << Register::virtReg2Index(reg)
               << "\n";
      });
      deallocateMemorySlot(reg);
      currentStackStatus.X.erase(reg);
    }
  }
}

bool EVMStackAlloc::regIsDeadAtBeginningOfMBB(
    unsigned reg, const MachineBasicBlock *MBB) const {
  // First, check if MBB has use of reg.
  SlotIndex BBStartSlot = LIS->getMBBStartIdx(MBB);
  SlotIndex BBEndSlot = LIS->getMBBEndIdx(MBB);

  if (rangeContainsRegUses(reg, BBStartSlot, BBEndSlot)) {
    return false;
  }
  if (sucessorsContainRegUses(reg, MBB)) {
    return false;
  }
  return true;
}

void EVMStackAlloc::dumpMemoryStatus() const {
  LLVM_DEBUG({
    dbgs() << "  Memory status: ";
    for (size_t i = 0; i < memoryAssignment.size(); ++i) {
      unsigned reg = memoryAssignment[i];
      dbgs() << "(" << i << ", ";
      if (reg == 0) {
        dbgs() << "empty";
      } else {
        dbgs() << "%" << Register::virtReg2Index(reg);
      }
      dbgs() << "), ";
    }
    dbgs() << "\n";
  });
}

void EVMStackAlloc::endOfBlockUpdates(MachineBasicBlock *MBB) {
  // make sure the reg is in X region.
  assert(stack.getStackDepth() == stack.getSizeOfXRegion() &&
         "L Region elements are still on the stack at end of MBB.");

  // make sure all the X registers are live at thend of MBB.
  LLVM_DEBUG({
    for (size_t i = 0; i < stack.getStackDepth(); ++i) {
      unsigned reg = stack.get(i);
      assert(sucessorsContainRegUses(reg, MBB));
    }
    // print out memory assignments
    dumpMemoryStatus();
  });


  std::vector<unsigned> xStack(stack.getStackElements());
  for (MachineBasicBlock *NextMBB : MBB->successors()) {
    unsigned edgeSetIndex = edgeSets.getEdgeSetIndex({MBB, NextMBB});

    /*
    if (edgeset2assignment.find(edgeSetIndex) != edgeset2assignment.end()) {
      // We've already have an x stack assignment previously. so now we will
      // need to arrange them so they are the same
      
      ActiveStack &another = edgeset2assignment[edgeSetIndex].first;
      MemorySlots memslots = edgeset2assignment[edgeSetIndex].second;
      assert(another == xStack && "Two X Stack arrangements are different!");
      assert(memslots == memoryAssignment && "Two memory arrangements are different!");
      //consolidateXRegionForEdgeSet(edgetSetIndex);
    }
    */

    // TODO: merge edgeset
    EdgeSetAssignment EA = {stack.getStackElements(), memoryAssignment};
    edgeset2assignment.erase(edgeSetIndex);
    edgeset2assignment[edgeSetIndex] = EA;
    break;
  }
}

void EVMStackAlloc::analyzeBasicBlock(MachineBasicBlock *MBB) {
  LLVM_DEBUG({ dbgs() << "  Analyzing MBB" << MBB->getNumber() << ":\n"; });

  MachineInstr &BeginMI = *MBB->begin();

  // this will alter MBB, so we record begin MI instruction
  beginOfBlockUpdates(MBB);

  LLVM_DEBUG({
    dbgs() << "    X Stack dump: (size: " << stack.getStackDepth() << ") \n";
    stack.dump();
  });
  dumpMemoryStatus();
  
  for (MachineBasicBlock::iterator I(BeginMI), E = MBB->end(); I != E;) {
    MachineInstr &MI = *I++;

    LLVM_DEBUG({ dbgs() << "\n  Instr: "; MI.dump();});

    // First consume, then create
    handleUses(MI);
    handleDef(MI);

    cleanUpDeadRegisters(MI);

    stack.dump();
  }

  endOfBlockUpdates(MBB);
  LLVM_DEBUG({
    dbgs() << "    --------\n";
  });
}

void EVMStackAlloc::cleanUpDeadRegisters(MachineInstr &MI) {
  if (MI.getNumExplicitOperands() - MI.getNumExplicitDefs() <= 2) {
    return;
  }

  std::vector<MOPUseType> useTypes;
  unsigned uses = calculateUseRegs(MI, useTypes);
  assert(uses > 2 && useTypes.size() > 2);

  MachineBasicBlock::iterator insertPoint(MI);
  insertPoint++;
  for (MOPUseType mut : useTypes) {
    unsigned depth = stack.findRegDepth(mut.second.reg);
    if (depth != 0) {
      insertSwapBefore(depth, *insertPoint);
    }
    insertPopBefore(*insertPoint);
    }
} 

StackAssignment EVMStackAlloc::getStackAssignment(unsigned reg) const {
  assert(regAssignments.find(reg) != regAssignments.end() &&
         "Cannot find stack assignment for register.");
  return regAssignments.lookup(reg);
}

// Test whether Reg, as defined at Def, has exactly one use. This is a
// generalization of MachineRegisterInfo::hasOneUse that uses LiveIntervals
// to handle complex cases.
static bool hasOneUse(unsigned Reg, const MachineInstr *Def, MachineRegisterInfo *MRI,
                      LiveIntervals *LIS) {
  return MRI->hasOneUse(Reg);
  /*
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MRI->hasOneUse(Reg))
    return true;

  bool HasOne = false;
  const LiveInterval &LI = LIS->getInterval(Reg);
  const VNInfo *DefVNI =
      LI.getVNInfoAt(LIS->getInstructionIndex(*Def).getRegSlot());
  assert(DefVNI);
  for (auto &I : MRI->use_nodbg_operands(Reg)) {
    const auto &Result = LI.Query(LIS->getInstructionIndex(*I.getParent()));
    if (Result.valueIn() == DefVNI) {
      if (!Result.isKill())
        return false;
      if (HasOne)
        return false;
      HasOne = true;
    }
  }
  return HasOne;
  */
}

bool EVMStackAlloc::rangeContainsRegUses(unsigned reg,
                                         SlotIndex &beginSlot,
                                         SlotIndex &endSlot) const {
  for (auto &Use : MRI->use_nodbg_operands(reg)) {
    MachineInstr * UseMI = Use.getParent();
    SlotIndex UseSlot = LIS->getInstructionIndex(*UseMI).getRegSlot();

    // check if the same BB has subsequent uses.
    if (SlotIndex::isEarlierInstr(beginSlot, UseSlot) &&
        SlotIndex::isEarlierInstr(UseSlot, endSlot)) {
      return true;
    }
  }
  return false;
}

bool EVMStackAlloc::sucessorsContainRegUses(unsigned reg,
                                            const MachineBasicBlock *MBB) const {
  for (auto &Use : MRI->use_nodbg_operands(reg)) {
    MachineInstr * UseMI = Use.getParent();
    SlotIndex UseSlot = LIS->getInstructionIndex(*UseMI).getRegSlot();
    for (auto SuccMBB : MBB->successors()) {
      SlotIndex SuccBegin = LIS->getMBBStartIdx(SuccMBB);
      if (SlotIndex::isEarlierInstr(SuccBegin, UseSlot)) {
        return true;
      }
    }
  }
  return false;
}

// return true of the register use in the machine instruction is the last use.
bool EVMStackAlloc::regIsLastUse(const MachineOperand &MOP) const {
  assert(MOP.isReg());
  unsigned reg = MOP.getReg();

  if (hasOneUse(reg, MOP.getParent(), MRI, LIS))
    return true;
  
  // iterate over all uses
  const MachineInstr *MI = MOP.getParent();
  const MachineBasicBlock *MBB = MI->getParent();
  SlotIndex MISlot = LIS->getInstructionIndex(*MI).getRegSlot();
  SlotIndex BBEndSlot = LIS->getMBBEndIdx(MBB);

  if (rangeContainsRegUses(reg, MISlot, BBEndSlot)) {
    return false;
  }

  if (sucessorsContainRegUses(reg, MBB)) {
    return false;
  }

  return true;
}

// return the number of uses of the register.
unsigned EVMStackAlloc::getRegNumUses(unsigned reg) const {
  return std::distance(MRI->use_nodbg_begin(reg), MRI->use_nodbg_end());
}

void EVMStackAlloc::handleDef(MachineInstr &MI) {
  if (MI.getNumDefs() == 0) {
    return;
  }

  unsigned defReg = getDefRegister(MI);

  // case: multiple defs:
  // if there are multiple defines, then it goes to memory
  if (MRI->hasOneDef(defReg)) {
    // if the register has no use, then we do not allocate it
    if (MRI->use_nodbg_empty(defReg)) {
      regAssignments.insert(std::pair<unsigned, StackAssignment>(
          defReg, {NO_ALLOCATION, 0}));
      LLVM_DEBUG(dbgs() << "    Allocating %"
                        << Register::virtReg2Index(defReg)
                        << " to NO_ALLOCATION.\n");
      insertPopAfter(MI);
      return;
    }

    // LOCAL case
    if (defIsLocal(MI)) {
      // record assignment
      regAssignments.insert(
          std::pair<unsigned, StackAssignment>(defReg, {L_STACK, 0}));
      LLVM_DEBUG(dbgs() << "    Allocating %"
                        << Register::virtReg2Index(defReg)
                        << " to LOCAL STACK.\n");
      // update stack status
      currentStackStatus.L.insert(defReg);
      stack.push(defReg);
      return;
    }

    // If all uses are in a same edge set, send it to Transfer Stack
    // This could greatly benefit from a stack machine specific optimization.
    if (liveIntervalWithinSameEdgeSet(defReg)) {
      // it is a def register, so we only care about out-going edges.
      regAssignments.insert(
          std::pair<unsigned, StackAssignment>(defReg, {X_STACK, 0}));

      LLVM_DEBUG(dbgs() << "    Allocating %"
                        << Register::virtReg2Index(defReg)
                        << " to X STACK.\n");

      // TODO: allocate X regions to the following code
      MachineBasicBlock *ThisMBB = const_cast<MachineInstr &>(MI).getParent();

      // find the outgoing edge set.
      assert(!ThisMBB->succ_empty() &&
             "Cannot allocate XRegion to empty edgeset.");
      unsigned edgesetIndex =
          edgeSets.getEdgeSetIndex({ThisMBB, *(ThisMBB->succ_begin())});
      allocateXRegion(edgesetIndex, defReg);

      stack.push(defReg);
      return;
    }
  }

  // Everything else goes to memory
  currentStackStatus.M.insert(defReg);
  unsigned slot = allocateMemorySlot(defReg);
  regAssignments.insert(
      std::pair<unsigned, StackAssignment>(defReg, {NONSTACK, slot}));

  insertStoreToMemoryAfter(defReg, MI, slot);
  LLVM_DEBUG({
    dbgs() << "    Allocating %" << Register::virtReg2Index(defReg)
           << " to memslot: " << slot << "\n";
  });
  return;
}

// We only look at uses.
bool EVMStackAlloc::liveIntervalWithinSameEdgeSet(unsigned defReg) {
  std::set<unsigned> edgeSetIndices;
  for (MachineOperand &use : MRI->use_operands(defReg)){
    MachineBasicBlock* MBB = use.getParent()->getParent();

    // Look for predecessor edges
    for (MachineBasicBlock* Pred : MBB->predecessors()) {
      EdgeSets::Edge edge = {Pred, MBB};
      unsigned setIndex = edgeSets.getEdgeSetIndex(edge);
      edgeSetIndices.insert(setIndex);
    }

    // Look for successor edges
    for (MachineBasicBlock* Succ : MBB->successors()) {
      EdgeSets::Edge edge = {MBB, Succ};
      unsigned setIndex = edgeSets.getEdgeSetIndex(edge);
      edgeSetIndices.insert(setIndex);
    }
  }

  assert(!edgeSetIndices.empty() && "Edge set cannot be empty.");

  if (edgeSetIndices.size() == 1) {
    return true;
  } else {
    return false;
  }
}

unsigned EVMStackAlloc::calculateUseRegs(MachineInstr &MI, std::vector<MOPUseType> &useTypes) {
  unsigned index = 0;
  for (const MachineOperand &MOP : MI.explicit_uses()) {
    if (!MOP.isReg()) {
      LLVM_DEBUG({
        if (MI.getOpcode() == EVM::PUSH32_r) {
          assert(MOP.isImm() || MOP.isCImm() || MOP.isGlobal());
          assert(MI.getNumExplicitOperands() - MI.getNumExplicitDefs() == 1);
        } else if (MI.getOpcode() == EVM::JUMP_r ||
                   MI.getOpcode() == EVM::JUMPI_r) {
          assert(MOP.isSymbol() || MOP.isMBB());
        }
      });
      ++index;
      continue;
    }

    unsigned useReg = MOP.getReg();
    // get stack assignment
    assert(regAssignments.find(useReg) != regAssignments.end());
    StackAssignment SA = regAssignments.lookup(useReg);
    assert(SA.region != NO_ALLOCATION && "Cannot see unused register use.");

    bool isMemUse = false;
    bool isLastUse = false;
    if (regIsLastUse(MOP)) {
      isLastUse = true;
    }

    RegUseType RUT;
    if (SA.region == NONSTACK) {
      // we need to handle memory types differently
      isMemUse = true;
      RUT = {isLastUse, isMemUse, SA.slot, useReg};
    } else {
      unsigned depth = stack.findRegDepth(useReg);
      RUT = {isLastUse, isMemUse, depth, useReg};
    }

    useTypes.push_back({index, RUT});
    ++index;
  }
  return index;
}

void EVMStackAlloc::handleUses(MachineInstr &MI) {

  // Collect reg use info
  std::vector<MOPUseType> useTypes;
  calculateUseRegs(MI, useTypes);

  LLVM_DEBUG({
    for (MOPUseType &MUT : useTypes) {
      dbgs() << "      Uses: ";
      dbgs() << " %" << Register::virtReg2Index(MUT.second.reg)
             << " index: " << MUT.first
             << ", last use: " << MUT.second.isLastUse << ", "
             << "isMemUse: " << MUT.second.isMemUse << ", "
             << "depth: " << MUT.second.stackDepth << "\n";
    }
  });

  // Handle uses (back to front), insert DUPs, SWAPs if needed
  unsigned numUses = useTypes.size();
  switch (numUses) {
    case 0: {
      break;
    }
    case 1: {
      // handle unary operands
      assert(useTypes.size() == 1);
      handleUnaryOpcode(useTypes[0], MI);
      break;
    }
    case 2: {
      // handle binary operands
      assert(useTypes.size() == 2);
      handleBinaryOpcode(useTypes[0], useTypes[1], MI);
      break;
    }
    default: {
      // handle default operands
      assert(useTypes.size() > 2);
      handleArbitraryOpcode(useTypes, MI);
      LLVM_DEBUG(dbgs() << "    Stack status at call:"; stack.dump());
      break;
    }
  }

  for (unsigned i = 0; i < numUses; ++i) {
    unsigned reg = stack.pop();
    // TODO: check the reg is conformant to use
    assert(reg == useTypes[i].second.reg);
  }

}

void EVMStackAlloc::insertLoadFromMemoryBefore(unsigned reg, MachineInstr &MI,
                                               unsigned memSlot) {
  MachineInstrBuilder getlocal = 
  BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(EVM::pGETLOCAL_r), reg)
      .addImm(memSlot);
  LIS->InsertMachineInstrInMaps(*getlocal);
  stack.push(reg);

  LLVM_DEBUG(dbgs() << "    >>> %" << Register::virtReg2Index(reg) << " <= GETLOCAL("
                    << memSlot << ")\n");
}

void EVMStackAlloc::insertDupBefore(unsigned index, MachineInstr &MI) {
  assert(index > 0);
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder dup = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::DUP_r)).addImm(index);
  MBB->insert(MachineBasicBlock::iterator(MI), dup);
  LIS->InsertMachineInstrInMaps(*dup);
  stack.dup(index - 1);
}

void EVMStackAlloc::insertStoreToMemoryAfter(unsigned reg, MachineInstr &MI,
                                             unsigned memSlot) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();

  MachineInstrBuilder putlocal =
      BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::pPUTLOCAL_r))
          .addReg(reg)
          .addImm(memSlot);
  MBB->insertAfter(MachineBasicBlock::iterator(MI), putlocal);
  LIS->InsertMachineInstrInMaps(*putlocal);

  // TODO: insert this new put local to LiveIntervals
  LLVM_DEBUG(dbgs() << "    >>> PUTLOCAL(" << memSlot << ") <= %"
                    << Register::virtReg2Index(reg) << "\n");
}

void EVMStackAlloc::insertPopAfter(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder pop = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::POP_r));
  MBB->insertAfter(MachineBasicBlock::iterator(MI), pop);
  LIS->InsertMachineInstrInMaps(*pop);
  stack.pop();
}

void EVMStackAlloc::insertPopBefore(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineInstrBuilder pop =
      BuildMI(*MBB->getParent(), MI.getDebugLoc(), TII->get(EVM::POP_r));
  MBB->insert(MachineBasicBlock::iterator(MI), pop);
  LIS->InsertMachineInstrInMaps(*pop);
  stack.pop();
}


void EVMStackAlloc::insertSwapBefore(unsigned index, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineInstrBuilder swap =
      BuildMI(*MBB->getParent(), MI.getDebugLoc(), TII->get(EVM::SWAP_r))
          .addImm(index);
  MBB->insert(MachineBasicBlock::iterator(MI), swap);
  LIS->InsertMachineInstrInMaps(*swap);
  stack.swap(index);
}

bool EVMStackAlloc::handleSingleUse(MachineInstr &MI, const MachineOperand &MOP,
                                    std::vector<RegUseType> &usetypes) {
  if (!MOP.isReg()) {
    return false;
  }

  unsigned useReg = MOP.getReg();

  // get stack assignment
  assert(regAssignments.find(useReg) != regAssignments.end());
  StackAssignment SA = regAssignments.lookup(useReg);
  // we also do not care if we has determined we do not allocate it.
  if (SA.region == NO_ALLOCATION) {
    return false;
  }

  // update stack status if it is the last use.
  if (regIsLastUse(MOP)) {
    switch (SA.region) {
      case NONSTACK: {
        insertLoadFromMemoryBefore(useReg, MI, SA.slot);
        break;
      }
      case X_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertSwapBefore(depth, MI);
        currentStackStatus.X.erase(useReg);
        break;
      }
      case L_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertSwapBefore(depth, MI);
        break;
      }
      default: {
        llvm_unreachable("Impossible case");
        break;
      }
    }
  } else { // It is not the last use of a register.

    //assert(hasUsesAfterInSameBB(useReg, MI)/* || successorsHaveUses(useReg, MI)*/);
    switch (SA.region) {
      case NONSTACK: {
        // release memory slot
        insertLoadFromMemoryBefore(useReg, MI, SA.slot);
        stack.push(useReg);
        break;
      }
      case X_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertDupBefore(depth, MI);
        break;
      }
      case L_STACK: {
        unsigned depth = stack.findRegDepth(useReg);
        insertDupBefore(depth, MI);
        break;
      }
      default: {
        llvm_unreachable("Impossible case");
        break;
      }
    }
  }
  return true;
}

// return the allocated slot index of a memory
unsigned EVMStackAlloc::allocateMemorySlot(unsigned reg) {
  assert(reg != 0 && "Incoming registers cannot be zero.");
  // first, find if there is an empty slot:
  unsigned slot = 0;
  for (unsigned i = 0; i < memoryAssignment.size(); ++i) {
    // here we let 0 represent an empty slot
    if (memoryAssignment[i] == 0) {
      memoryAssignment[i] = reg;
      slot = i;
      break;
    }
  }

  if (slot == 0) {
    memoryAssignment.push_back(reg);
    MFI->updateMemoryFrameSize(memoryAssignment.size());
    return (memoryAssignment.size() - 1);
  } else {
    return slot;
  }

}

void EVMStackAlloc::deallocateMemorySlot(unsigned reg) {
  for (unsigned i = 0; i < memoryAssignment.size(); ++i) {
    if (reg == memoryAssignment[i]) {
      memoryAssignment[i] = 0;
      LLVM_DEBUG(dbgs() << "    deallocate %" << Register::virtReg2Index(reg)
                        << " at memslot: " << i << "\n");
      while (memoryAssignment.size() > 0 &&
             memoryAssignment.back() == 0) {
        memoryAssignment.pop_back();
      }
      return;
    }
  }
  LLVM_DEBUG(dbgs() << "    Unfound register %" << Register::virtReg2Index(reg)
                    << "\n");
  llvm_unreachable("Cannot find allocated memory slot");
}

unsigned EVMStackAlloc::allocateXRegion(unsigned setIndex, unsigned reg) {
  // Each entry of an edge set should contain the same
  // X region layout
  EdgeSetAssignment ESA = edgeset2assignment[setIndex];
  std::vector<unsigned> &x_region = ESA.first;

  assert(std::find(x_region.begin(), x_region.end(), reg) == x_region.end() &&
         "Inserting duplicate element in X region.");

  x_region.push_back(reg);
  stack.pushElementToXRegion();
  return x_region.size();
}

bool EVMStackAlloc::hasUsesAfterInSameBB(unsigned reg, const MachineInstr &MI) const {
  // if this is the only use, then for sure it is the last use in MBB.
  assert(!MRI->use_nodbg_empty(reg) && "Empty use registers should not have a use.");
  if (MRI->hasOneUse(reg)) {
    return false;
  }

  SlotIndex FstUse = LIS->getInstructionIndex(MI);

  // iterate over uses and see if any use exists in the same BB.
  for (MachineRegisterInfo::use_nodbg_iterator 
       Use = MRI->use_nodbg_begin(reg),
       E = MRI->use_nodbg_end();
       Use != E; ++Use) {
    
    SlotIndex SI = LIS->getInstructionIndex(*Use->getParent());

    SlotIndex EndOfMBBSI = LIS->getMBBEndIdx(Use->getParent()->getParent());

    // Check if SI lies in between FstUSE and EndOfMBBSI
    if (SlotIndex::isEarlierInstr(FstUse, SI) &&
        SlotIndex::isEarlierInstr(SI, EndOfMBBSI)) {
      return true;
    }
  }

  // we cannot find a use after it in BB
  return false;
}

unsigned EVMStackAlloc::getCurrentStackDepth() const {
  return currentStackStatus.L.size() + currentStackStatus.X.size();
}

void EVMStackAlloc::pruneStackDepth() {
  unsigned stackDepth = getCurrentStackDepth();
  if (stackDepth < MAXIMUM_STACK_DEPTH) {
    return;
  }
  LLVM_DEBUG(dbgs() << "Stack Depth exceeds maximum, start pruning.");
  llvm_unreachable("unimplemented.");

  // First look at transfer stackœ:
  unsigned spillingCandidate = 0;
  std::set<unsigned> *vecRegs;
  if (currentStackStatus.X.size() != 0) {
    vecRegs = &currentStackStatus.X;
  } else {
    vecRegs = &currentStackStatus.L;
  }
  spillingCandidate = findSpillingCandidate(*vecRegs);
  unsigned slot = allocateMemorySlot(spillingCandidate);

  regAssignments.insert(std::pair<unsigned, StackAssignment>(spillingCandidate,
                                                             {NONSTACK, slot}));
}

unsigned EVMStackAlloc::findSpillingCandidate(std::set<unsigned> &vecRegs) const {
  llvm_unreachable("unimplemented");
}

void EVMStackAlloc::getXStackRegion(unsigned edgeSetIndex,
                                    std::vector<unsigned> xRegion) const {
  // order is important
  assert(edgeset2assignment.find(edgeSetIndex) != edgeset2assignment.end() &&
         "Cannot find edgeset index!");
  llvm_unreachable("not implemented");
  //std::copy(edgeset2assignment.begin(), edgeset2assignment.end(), xRegion);
  return;
}

bool EVMStackAlloc::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo(); 
  LIS = &getAnalysis<LiveIntervals>();
  MRI = &MF.getRegInfo();
  MFI = MF.getInfo<EVMMachineFunctionInfo>();
  allocateRegistersToStack(MF);
  return true;
}

bool EVMStackAlloc::successorsHaveUses(unsigned reg, const MachineInstr &MI) const {
  /*
  const MachineBasicBlock* MBB = MI.getParent();

  // It is possible that we some NextMBB will not have uses. In this case we
  // probably will need to pop it at the end of liveInterval.

  for (const MachineBasicBlock* NextMBB : MBB->successors()) {
    llvm_unreachable("unimplemented");
  }
  */

  return true;
}

void EVMStackAlloc::loadOperandFromMemoryIfNeeded(RegUseType op, MachineInstr &MI) {
  if (!op.isMemUse) {
    return;
  }

  StackAssignment SA = getStackAssignment(op.reg);
  assert(SA.region == NONSTACK);
  insertLoadFromMemoryBefore(op.reg, MI, SA.slot);
}

void EVMStackAlloc::handleOperandLiveness(RegUseType useType, MachineOperand &MOP) {
  assert(MOP.isReg() && useType.reg == MOP.getReg());

  unsigned reg = useType.reg;

  StackAssignment SA = getStackAssignment(reg);

  if (regIsLastUse(MOP)) {
    // when it goes out of liveness range, free resources if it is on X region.
    if (SA.region == X_STACK) {
      currentStackStatus.X.erase(reg);
      stack.popElementFromXRegion();
    }
  }

}

void EVMStackAlloc::handleUnaryOpcode(EVMStackAlloc::MOPUseType op, MachineInstr &MI) {
  assert(op.first == 0);

  unsigned reg = op.second.reg;
  MachineOperand &MOP = MI.getOperand(op.first + MI.getNumDefs());
  StackAssignment SA = getStackAssignment(reg);

  handleOperandLiveness(op.second, MOP);

  // if there is only one operand, simply bring it to the top.
  if (SA.region == NONSTACK) {
    insertLoadFromMemoryBefore(reg, MI, SA.slot);
  } else {
    unsigned depth = stack.findRegDepth(reg);
    if (regIsLastUse(MOP)) {
      if (depth != 0) {
        insertSwapBefore(depth, MI);
      }
    } else {
      insertDupBefore(depth + 1, MI);
    }
  }
}

void EVMStackAlloc::handleBinaryOpcode(EVMStackAlloc::MOPUseType op1, MOPUseType op2,
                                       MachineInstr &MI) {
  assert(op1.first == 0 && op2.first == 1);

  MachineOperand &MOP1 = MI.getOperand(op1.first + MI.getNumDefs());
  MachineOperand &MOP2 = MI.getOperand(op2.first + MI.getNumDefs());
  handleOperandLiveness(op1.second, MOP1);
  handleOperandLiveness(op2.second, MOP2);

  unsigned reg1 = op1.second.reg;
  unsigned reg2 = op2.second.reg;
  StackAssignment SA1 = getStackAssignment(reg1);
  StackAssignment SA2 = getStackAssignment(reg2);

  if (SA1.region == NONSTACK && SA2.region == NONSTACK) {
    LLVM_DEBUG({
      dbgs() << "    Case 1 (reg1, reg2 on memory)\n";
    });
    insertLoadFromMemoryBefore(reg2, MI, SA2.slot);
    insertLoadFromMemoryBefore(reg1, MI, SA1.slot);
  }

  if (SA1.region == NONSTACK && SA2.region != NONSTACK) {
    assert(SA2.region != NO_ALLOCATION);
    LLVM_DEBUG({
      dbgs() << "    Case 2 (reg2 on stack)\n";
    });

    if (regIsLastUse(MOP2)) {
      SwapRegToTop(reg2, MI);
    } else {
      DupRegToTop(reg2, MI);
    }
    insertLoadFromMemoryBefore(reg1, MI, SA1.slot);
  }

  // r1 on stack, r2 not on stack
  if (SA1.region != NONSTACK && SA2.region == NONSTACK) {
    assert(SA1.region != NO_ALLOCATION);
    LLVM_DEBUG({
      dbgs() << "    Case 3 (reg1 on stack)\n";
    });

    if (regIsLastUse(MOP1)) {
      // depth == 0:
      // r1, xx, xx --load-> r2, r1, xx --> swap1 --> r1, r2, xx
      // depth != 0:
      // xx, r1, xx -swap-> r1, xx, xx -load-> r2, r1, xx -swap1-> r1, r2, xx

      SwapRegToTop(reg1, MI);
      insertLoadFromMemoryBefore(reg2, MI, SA2.slot);
      SwapRegToTop(reg1, MI);
    } else {
      //r1 is not the first use:
      // xx --> r2, xx --> r1, r2, xx
      insertLoadFromMemoryBefore(reg2, MI, SA2.slot);
      DupRegToTop(reg1, MI);
    }
  }

  if (SA1.region != NONSTACK && SA2.region != NONSTACK) {
    assert(SA1.region != NO_ALLOCATION);
    assert(SA2.region != NO_ALLOCATION);

    LLVM_DEBUG({
      dbgs() << "    Case 4 (reg1, reg2 on stack) ";
    });

    unsigned depth1 = stack.findRegDepth(reg1);
    unsigned depth2 = stack.findRegDepth(reg2);

    bool lastUse1 = regIsLastUse(MOP1);
    bool lastUse2 = regIsLastUse(MOP2);

    if (lastUse1 && lastUse2) {
      // all last uses
      LLVM_DEBUG({ dbgs() << "(last use: 1, 2), "; });
      if (depth1 == 0 && depth2 == 1) {
        // do nothing
        LLVM_DEBUG({ dbgs() << " (Do nothing)\n"; });
      } else if (depth1 == 0 && depth2 != 1) {
        LLVM_DEBUG({ dbgs() << " (reg2 not in place)\n"; });
        // r2 is not in place:
        // r1,X,r2 -S1-> X,r1,r2 -S(depth2)-> r2,r1,X -S1-> r1,r2,X
        assert(depth2 >= 2);
        insertSwapBefore(1, MI);
        SwapRegToTop(reg2, MI);
        SwapRegToTop(reg1, MI);
      } else if (depth1 != 0 && depth2 == 1) {
        LLVM_DEBUG({ dbgs() << " (reg1 not in place)\n"; });
        SwapRegToTop(reg1, MI);
      } else if (depth1 != 0 && depth2 != 1) {
        if (depth1 == 1 && depth2 == 0) {
          LLVM_DEBUG({ dbgs() << " (reg1, reg2 reversed)\n"; });
          insertSwapBefore(1, MI);
        } else {
          LLVM_DEBUG({ dbgs() << " (neither reg1, reg2 in place)\n"; });
          SwapRegToTop(reg2, MI);
          insertSwapBefore(1, MI);
          SwapRegToTop(reg1, MI);
        }
      }
    }
    
    //  SWAP for reg1, DUP for reg2
    if (lastUse1 && !lastUse2) {
      LLVM_DEBUG({ dbgs() << "(last use: 1), "; });
      if (depth1 == 0 && depth2 == 1) {
        LLVM_DEBUG({ dbgs() << "(1, 2 in place)\n"; });
        // keep reg2 while consume depth1
        DupRegToTop(reg2, MI);
        SwapRegToTop(reg1, MI);
      } else if (depth1 == 0 && depth2 != 1) {
        LLVM_DEBUG({ dbgs() << "(1 in place)\n"; });
        // r2 is not in place
        DupRegToTop(reg2, MI);
        SwapRegToTop(reg1, MI);
      } else if (depth1 != 0 && depth2 == 1) {
        LLVM_DEBUG({ dbgs() << "(2 in place)\n"; });
        SwapRegToTop(reg1, MI);
      } else if (depth1 != 0 && depth2 != 1) {
        LLVM_DEBUG({ dbgs() << "(no in place)\n"; });
        DupRegToTop(reg2, MI);
        insertSwapBefore(1, MI);
        SwapRegToTop(reg1, MI);
      }
    }

    // DUP for reg1, SWAP for reg2
    if (!lastUse1 && lastUse2) {
      LLVM_DEBUG({ dbgs() << "(last use: 2), "; });
      if (depth1 == 0 && depth2 == 1) {
        LLVM_DEBUG({ dbgs() << "(1, 2 swapped)\n"; });
      } else if (depth1 == 0 && depth2 != 1) {
        // r2 is not in place
        // r1, x, r2 -S-> r2, r1, x -D-> r1, r2, r1, x
        LLVM_DEBUG({ dbgs() << "(1 in place)\n"; });
      } else if (depth1 != 0 && depth2 == 1) {
        LLVM_DEBUG({ dbgs() << "(2 in place)\n"; });
        // r1 is not in place
        // xx, r2, r1 -S-> r2, xx, r1 -D-> r1,r2, xx, r1
      } else if (depth1 != 0 && depth2 != 1) {
        LLVM_DEBUG({ dbgs() << "(no in place)\n"; });
        // now reg2 is at top
      }
      SwapRegToTop(reg2, MI);
      DupRegToTop(reg1, MI);
    }

    if (!lastUse1 && !lastUse2) {
      LLVM_DEBUG({ dbgs() << "(last use: no)\n"; });
      DupRegToTop(reg2, MI);
      DupRegToTop(reg1, MI);
    }
  }
}

void EVMStackAlloc::SwapRegToTop(unsigned reg, MachineInstr &MI) {
  unsigned depth = stack.findRegDepth(reg);
  if (depth != 0) {
    insertSwapBefore(depth, MI);
  }
}

void EVMStackAlloc::DupRegToTop(unsigned reg, MachineInstr &MI) {
  unsigned depth = stack.findRegDepth(reg);
  insertDupBefore(depth + 1, MI);
}

void EVMStackAlloc::handleArbitraryOpcode(std::vector<MOPUseType> useTypes,
                                          MachineInstr &MI) {
  // This is very tricky. 
  // We should do the following:
  // 1. DUP every operands on to stack top.
  // 2. record dead registers.
  // 3. execute Instruction.
  // 4. SWAP each dead registers on top and pop it.

  for (MOPUseType &useType : reverse(useTypes)) {
    RegUseType rut = useType.second;
    unsigned reg = rut.reg;
    StackAssignment SA = getStackAssignment(reg);

    if (rut.isMemUse) {
      assert(SA.region == NONSTACK);
      insertLoadFromMemoryBefore(reg, MI, SA.slot);
    } else {
      assert(SA.region != NONSTACK);
      unsigned depth = stack.findRegDepth(reg);
      insertDupBefore(depth + 1, MI);
    }
  }

}

FunctionPass *llvm::createEVMStackAllocPass() {
  return new EVMStackAlloc();
}