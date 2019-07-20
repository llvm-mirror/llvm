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
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"

using namespace llvm;

#define DEBUG_TYPE "evm-stackification"

namespace {

class StackStatus {
public:
  StackStatus() {}

  void swap(unsigned depth);
  void dup(unsigned depth);
  void push(unsigned reg);
  void pop();
  unsigned getStackDepth();
  unsigned get(unsigned depth);
  void dump() const;

private:
  // stack arrangements.
  // TODO: think of a way to do it.
  std::vector<unsigned> stackElements;

  // TODO: use DUPs instead of DUP1 + SWAPx for uses.
  DenseMap<unsigned, unsigned> remainingUses;
};

unsigned StackStatus::getStackDepth() {
  return stackElements.size();
}

unsigned StackStatus::get(unsigned depth) {
  return stackElements.rbegin()[depth];
}

void StackStatus::swap(unsigned depth) {
    assert(depth != 0);
    assert(stackElements.size() >= 2);
    LLVM_DEBUG({
      unsigned first = stackElements.rbegin()[0];
      unsigned second = stackElements.rbegin()[depth];

      std::string f = TargetRegisterInfo::isPhysicalRegister(first)
          ? "%fp" : "%" + std::to_string(TargetRegisterInfo::virtReg2Index(first));

      std::string s = TargetRegisterInfo::isPhysicalRegister(second)
          ? "%fp" : "%" + std::to_string(TargetRegisterInfo::virtReg2Index(second));

      dbgs() << "  Swapping " << f << " and " << s << "\n";
    });
    std::iter_swap(stackElements.rbegin(), stackElements.rbegin() + depth);
}

void StackStatus::dup(unsigned depth) {
  assert(depth != 0);
  unsigned elem = *(stackElements.rbegin() + depth);

  LLVM_DEBUG({
    if (TargetRegisterInfo::isPhysicalRegister(elem)) {
      dbgs() << "  Duplicating %fp.\n";
    } else {
      unsigned idx = TargetRegisterInfo::virtReg2Index(elem);
      dbgs() << "  Duplicating %" << idx << " at depth " << depth << "\n";
    }
  });

  stackElements.push_back(elem);
}

void StackStatus::pop() {
  LLVM_DEBUG({
    unsigned reg = stackElements.back();
    if (TargetRegisterInfo::isPhysicalRegister(reg)) {
      dbgs() << "  Popping %fp to top of stack.\n";
    } else {
      unsigned idx = TargetRegisterInfo::virtReg2Index(reg);
      dbgs() << "  Popping %" << idx << " from stack.\n";
    }
  });
  stackElements.pop_back();
}

void StackStatus::push(unsigned reg) {
  LLVM_DEBUG({
    if (TargetRegisterInfo::isPhysicalRegister(reg)) {
      dbgs() << "  Pushing %fp to top of stack.\n";
    } else {
      unsigned idx = TargetRegisterInfo::virtReg2Index(reg);
      dbgs() << "  Pushing %" << idx << " to top of stack.\n";
    }
  });
  stackElements.push_back(reg);
}


void StackStatus::dump() const {
  LLVM_DEBUG({
    dbgs() << "  Dumping stackstatus: \n    ";
    for (auto i = stackElements.begin(), e = stackElements.end(); i != e; ++i) {
      unsigned idx = TargetRegisterInfo::virtReg2Index(*i);
      dbgs() << "%" << idx << ", ";
    }
  });
}

class EVMStackification final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMStackification() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Stackification"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervals>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachineDominanceFrontier>();
    AU.addPreserved<MachineDominatorTree>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool isSingleDefSingleUse(unsigned RegNo) const;
  MachineInstr *getVRegDef(unsigned Reg, const MachineInstr *Insert) const;

  void handleUses(StackStatus &ss, MachineInstr &MI);
  void handleDef(StackStatus &ss, MachineInstr &MI);
  bool canStackifyReg(unsigned reg, MachineInstr &MI) const;
  unsigned findNumOfUses(unsigned reg) const;

  void insertPopAfter(MachineInstr &MI);
  void insertDupAfter(unsigned index, MachineInstr &MI);
  void insertSwapBefore(unsigned index, MachineInstr &MI);

  void insertLoadFromMemoryBefore(unsigned reg, MachineInstr& MI);
  void insertStoreToMemoryAfter(unsigned reg, MachineInstr &MI);
  void moveOperandsToStackTop(StackStatus& ss, MachineInstr &MI);

  DenseMap<unsigned, unsigned> reg2index;

  const EVMInstrInfo* TII;
  MachineRegisterInfo *MRI;
  LiveIntervals *LIS;
  EVMMachineFunctionInfo *MFI;
};
} // end anonymous namespace

char EVMStackification::ID = 0;
INITIALIZE_PASS(EVMStackification, DEBUG_TYPE,
      "Converting register-based instructions into stack instructions",
      false, false)

FunctionPass *llvm::createEVMStackification() {
  return new EVMStackification();
}

bool EVMStackification::isSingleDefSingleUse(unsigned RegNo) const {
  return (MRI->hasOneUse(RegNo) && MRI->hasOneDef(RegNo));
}

// Identify the definition for this register at this point. This is a
// generalization of MachineRegisterInfo::getUniqueVRegDef that uses
// LiveIntervals to handle complex cases.
MachineInstr *EVMStackification::getVRegDef(unsigned Reg,
                                            const MachineInstr *Insert) const {
  // Most registers are in SSA form here so we try a quick MRI query first.
  if (MachineInstr *Def = MRI->getUniqueVRegDef(Reg))
    return Def;

  auto &LIS = *this->LIS;

  // MRI doesn't know what the Def is. Try asking LIS.
  if (const VNInfo *ValNo = LIS.getInterval(Reg).getVNInfoBefore(
          LIS.getInstructionIndex(*Insert)))
    return LIS.getInstructionFromIndex(ValNo->def);

  return nullptr;
}

/***
// The following are the criteria for deciding whether to stackify the register
// or not:
// 1. This reg has only one def
// 2. is not physical register  ($fp in this case)
// 3. the uses of the reg is in the same basicblock
*/
bool EVMStackification::canStackifyReg(unsigned reg, MachineInstr& MI) const {
  if (TargetRegisterInfo::isPhysicalRegister(reg)) {
    return false;
  }

  const LiveInterval &LI = LIS->getInterval(reg);
  
  // if it has multiple VNs, ignore it.
  if (LI.segments.size() > 1) {
    return false;
  }

  // if it goes across multiple MBBs, ignore it.
  MachineBasicBlock* MBB = MI.getParent();
  SlotIndex MBBBegin = LIS->getMBBStartIdx(MBB);
  SlotIndex MBBEnd = LIS->getMBBEndIdx(MBB);

  if(!LI.isLocal(MBBBegin, MBBEnd)) {
    return false;
  }

  return true;
}

unsigned EVMStackification::findNumOfUses(unsigned reg) const {
  auto useOperands = MRI->reg_nodbg_operands(reg);
  unsigned numUses = std::distance(useOperands.begin(), useOperands.end());
  return numUses;
}

void EVMStackification::insertPopAfter(MachineInstr& MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder pop = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::POP_r));
  MBB->insertAfter(MachineBasicBlock::iterator(MI), pop);
}

void EVMStackification::insertDupAfter(unsigned index, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineInstrBuilder dup = BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::DUP_r)).addImm(index);
  MBB->insertAfter(MachineBasicBlock::iterator(MI), dup);
}

static bool findRegDepthOnStack(StackStatus &ss, unsigned reg, unsigned *depth) {
  unsigned curHeight = ss.getStackDepth();

  for (unsigned d = 0; d < curHeight; ++d) {
    unsigned stackReg = ss.get(d);
    if (stackReg == reg) {
      *depth = d;
      LLVM_DEBUG({
        unsigned idx = TargetRegisterInfo::virtReg2Index(reg);
        dbgs() << "Found %" << idx << " at depth: " << *depth << "\n";
      });
      return true;
    }
  }

  return false;
}

void EVMStackification::insertSwapBefore(unsigned index, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  BuildMI(*MBB, MI, MI.getDebugLoc(), TII->get(EVM::SWAP_r)).addImm(index);
}

void EVMStackification::insertLoadFromMemoryBefore(unsigned reg, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();

  assert(!TargetRegisterInfo::isPhysicalRegister(reg) &&
         "$fp has already been eliminated.");

  // deal with physical register.
  unsigned index = MFI->get_memory_index(reg);
  BuildMI(*MBB, MI, MI.getDebugLoc(), TII->get(EVM::pGETLOCAL_r), reg)
      .addImm(index);
}

void EVMStackification::insertStoreToMemoryAfter(unsigned reg, MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction &MF = *MBB->getParent();

  unsigned index = MFI->allocate_memory_index(reg);
  MachineInstrBuilder putlocal =
      BuildMI(MF, MI.getDebugLoc(), TII->get(EVM::pPUTLOCAL_r)).addReg(reg).addImm(index);
  MBB->insertAfter(MachineBasicBlock::iterator(MI), putlocal);
}

/// organize the stack to prepare for the instruction.
void EVMStackification::moveOperandsToStackTop(StackStatus& ss, MachineInstr &MI) {

  for (const MachineOperand &MO : MI.explicit_uses()) {
    if (!MO.isReg() || MO.isImplicit()) {
      return;
    }

    unsigned reg = MO.getReg();
    if (!MFI->isVRegStackified(reg)) {
      insertLoadFromMemoryBefore(reg, MI);
      ss.push(reg);
    } else {
      // stackified case:
      unsigned depthFromTop = 0;
      bool result = findRegDepthOnStack(ss, reg, &depthFromTop);
      assert(result);

      if (depthFromTop != 0) {
        insertSwapBefore(depthFromTop, MI);
        ss.swap(depthFromTop);
      }
    }
  }

  return;
}

void EVMStackification::handleUses(StackStatus &ss, MachineInstr& MI) {
  // TODO: do not support more than 2 uses in an MI. We need scheduler to help
  // us make sure that the registers are on stack top.
 
  const auto &uses = MI.explicit_uses();
  unsigned numUsesInMI = std::distance(uses.begin(), uses.end());

  // Case 1: only 1 use
  if (numUsesInMI == 1) {
    MachineOperand& MO = *MI.explicit_uses().begin(); 
    if (!MO.isReg() || MO.isImplicit()) {
      return;
    }
    unsigned reg = MO.getReg();

    // handle vreg unstackfied case
    if (!MFI->isVRegStackified(reg)) {
      LLVM_DEBUG(dbgs() << "  Operand is not stackified.\n";);
      insertLoadFromMemoryBefore(reg, MI);
      ss.push(reg);
    } else {

      // handle vreg stackified case
      unsigned depthFromTop = 0;
      bool result = findRegDepthOnStack(ss, reg, &depthFromTop);
      assert(result);
      LLVM_DEBUG(dbgs() << "  Operand is on the stack at depth: "
                        << depthFromTop << "\n";);

      // check if it is on top of the stack.
      if (depthFromTop != 0) {
        // TODO: insert swap
        insertSwapBefore(depthFromTop, MI);
        ss.swap(depthFromTop);
      }
    }

    ss.pop();
    return;
  }

  if (numUsesInMI == 2) {
    MachineOperand& MO1 = *MI.explicit_uses().begin(); 
    MachineOperand& MO2 = *(MI.explicit_uses().begin() + 1); 

    assert(!MO1.isImplicit() && MO1.isReg() &&
           !MO2.isImplicit() && MO2.isReg());

    unsigned firstReg  = MO1.getReg();
    unsigned secondReg = MO2.getReg();

    bool firstStackified = MFI->isVRegStackified(firstReg);
    bool secondStackified = MFI->isVRegStackified(secondReg);

    // case 1: both regs are not stackified:
    if (!firstStackified && !secondStackified) {
      insertLoadFromMemoryBefore(firstReg, MI);
      ss.push(firstReg);

      insertLoadFromMemoryBefore(secondReg, MI);
      ss.push(secondReg);

      ss.pop();
      ss.pop();
      return;
    }

    // case 2: the first reg is not stackified:
    if (!firstStackified && secondStackified) {
      unsigned depthFromTop = 0;
      bool result = findRegDepthOnStack(ss, secondReg, &depthFromTop);
      assert(result);
      
      if (depthFromTop != 0) {
        insertSwapBefore(depthFromTop, MI);
        ss.swap(depthFromTop);
      }

      insertLoadFromMemoryBefore(firstReg, MI);
      ss.push(firstReg);

      insertSwapBefore(1, MI);
      ss.swap(1);

      ss.pop();
      ss.pop();
      return;
    }

    // case 3: the second reg is not stackfieid:
    if (firstStackified && !secondStackified) {
      unsigned depthFromTop = 0;
      bool result = findRegDepthOnStack(ss, firstReg, &depthFromTop);
      assert(result);

      if (depthFromTop != 0) {
        insertSwapBefore(depthFromTop, MI);
        ss.swap(depthFromTop);
      }

      insertLoadFromMemoryBefore(secondReg, MI);
      ss.push(secondReg);

      ss.pop();
      ss.pop();
      return;
    }

    // case 4: both reg is stackified:
    unsigned firstDepthFromTop = 0;
    unsigned secondDepthFromTop = 0;
    bool result = findRegDepthOnStack(ss, firstReg, &firstDepthFromTop);
    assert(result);

    result = findRegDepthOnStack(ss, secondReg, &secondDepthFromTop);
    assert(result);

    // idea case, we don't need to do anything
    if (firstDepthFromTop == 1 && secondDepthFromTop == 0) {
      // do nothing
    } else
    
    // first in position, second not in.
    if (firstDepthFromTop == 1 && secondDepthFromTop != 0) {
      // TODO: do if it is commutatble, optimization

      // move the second operand to top, so a swap
      insertSwapBefore(secondDepthFromTop, MI);
      ss.swap(secondDepthFromTop);
    } else 

    // second in posotion, first is not.
    if (firstDepthFromTop != 1 && secondDepthFromTop == 0) {
      insertSwapBefore(secondDepthFromTop, MI);
      ss.swap(secondDepthFromTop);

      insertSwapBefore(1, MI);
      ss.swap(1);
    } else

    // first and second are reversed
    if (firstDepthFromTop == 0 && secondDepthFromTop == 1) {
      insertSwapBefore(secondDepthFromTop, MI);
      ss.swap(secondDepthFromTop);
    } else

    // special case: 
    if (firstDepthFromTop == 0 && secondDepthFromTop > 1) {
      // move the first operand to the correct position.
      insertSwapBefore(1, MI);
      ss.swap(1);
      
      // then move the second operand on to the top
      insertSwapBefore(secondDepthFromTop, MI);
      ss.swap(secondDepthFromTop);
    } else
    
    // all other situations.
    if (firstDepthFromTop != 1 && secondDepthFromTop != 0) {
      // either registers are not in place.

      result = findRegDepthOnStack(ss, secondReg, &secondDepthFromTop);
      assert(result);
      assert(secondDepthFromTop != 0);

      insertSwapBefore(secondDepthFromTop, MI);
      ss.swap(secondDepthFromTop);
    } else {
      llvm_unreachable("missing cases for handling.");
    }

    ss.pop();
    ss.pop();
    return;
  }

  moveOperandsToStackTop(ss, MI);
}

void EVMStackification::handleDef(StackStatus &ss, MachineInstr& MI) {
  // Look up LiveInterval info.
  // Insert DUP if it is necessary.
  unsigned numDefs = MI.getDesc().getNumDefs();
  assert(numDefs <= 1 && "more than one defs");

  // skip if there is no definition.
  if (numDefs == 0) {
    return;
  }

  MachineOperand& def = *MI.defs().begin();
  assert(def.isReg() && def.isDef());
  unsigned defReg = def.getReg();

  // insert pop if it does not have def.
  bool IsDead = MRI->use_empty(defReg);
  if (IsDead) {
    LLVM_DEBUG({ dbgs() << "  Def's use is empty, insert POP after.\n"; });
    insertPopAfter(MI);
    return;
  }

  if (!canStackifyReg(defReg, MI)) {
    LLVM_DEBUG({
      if (TargetRegisterInfo::isPhysicalRegister(defReg)) {
        dbgs() << "  Skip stackifying %fp\n";
      } else {
        unsigned ridx = TargetRegisterInfo::virtReg2Index(defReg);
        dbgs() << "  Cannot stackify %" << ridx << "\n";
      }
    });

    insertStoreToMemoryAfter(defReg, MI);
    return;
  }


  // stackify the register
  assert(!MFI->isVRegStackified(defReg));
  MFI->stackifyVReg(defReg);

  LLVM_DEBUG({
    unsigned ridx = TargetRegisterInfo::virtReg2Index(defReg);
    dbgs() << "  Reg %" << ridx << " is stackified.\n";
  });

  ss.push(defReg);

  unsigned numUses = std::distance(MRI->use_begin(defReg), MRI->use_end());
  for (unsigned i = 1; i < numUses; ++i) {
    insertDupAfter(1, MI);
    ss.dup(1);
  }

}

bool EVMStackification::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Stackification **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  this->MRI = &MF.getRegInfo();
  this->MFI = MF.getInfo<EVMMachineFunctionInfo>();

  dbgs() << "dumping liveness\n";
  this->LIS = &getAnalysis<LiveIntervals>();

  for (unsigned I = 0, E = MRI->getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);

    const LiveInterval &LI = LIS->getInterval(Reg);
    LI.dump();
  }

  bool Changed = false;
  TII = MF.getSubtarget<EVMSubtarget>().getInstrInfo();

  for (MachineBasicBlock & MBB : MF) {
    // at the beginning of each block iteration, the stack status should be zero.
    // since at this momment we do not stackify register that expand across BBs.
    StackStatus ss;

    // The scheduler has already set the sequence for us. We just need to
    // iterate over by order.
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
         I != E;) {
      MachineInstr &MI = *I++;

      LLVM_DEBUG({
        dbgs() << "Stackifying instr: ";
        MI.dump();
      });

      // If the Use is stackified:
      // insert SWAP if necessary
      handleUses(ss, MI);

      // If the Def is able to be stackified:
      // 1. mark VregStackified
      // 2. insert DUP if necessary
      handleDef(ss, MI);
    }

    // at the end of the basicblock, there should be no elements on the stack.
    assert(ss.getStackDepth() == 0);
  }

  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);


  return Changed;
}
