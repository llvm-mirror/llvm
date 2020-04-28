//===-- EVMArgumentMove.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file moves pSTACKARG instructions after ScheduleDAG scheduling.
///
/// This is copied from WebAssembly backend.
/// Another way to do it might be that  we create glues to bind stack arguments
/// to the beginning (EntryToken). But it is just a thought.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
using namespace llvm;

#define DEBUG_TYPE "evm-argument-move"

namespace {
class EVMArgumentMove final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMArgumentMove() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "EVM Argument Move"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  static bool isStackArg(const MachineInstr &MI);
private:
  void arrangeStackArgs(MachineFunction &MF) const;
  void insertRetAddrArg(MachineFunction &MF) const;
  unsigned computeStackArgOffset(MachineFunction &MF) const;
  void sortStackArgs(MachineFunction &MF) const;

  const EVMInstrInfo *TII;
  EVMMachineFunctionInfo *MFI;
  const Function *F;
  MachineRegisterInfo *MRI;
  const EVMSubtarget *Subtarget;
};
} // end anonymous namespace

char EVMArgumentMove::ID = 0;
INITIALIZE_PASS(EVMArgumentMove, DEBUG_TYPE,
                "Move function arguments for EVM", false, false)

FunctionPass *llvm::createEVMArgumentMove() {
  return new EVMArgumentMove();
}

bool EVMArgumentMove::isStackArg(const MachineInstr &MI) {
  return (MI.getOpcode() == EVM::pSTACKARG_r);
}

void EVMArgumentMove::insertRetAddrArg(MachineFunction& MF) const {
  unsigned returnAddrReg = 0;
  // we now insert a special return address stack argument to the beginning of
  // the function:
  if (!EVMSubtarget::isMainFunction(*F)) {
    // return address is the last stackarg:
    unsigned retAddrStackArgNum = MFI->getNumStackArgs();
    unsigned destReg = MRI->createVirtualRegister(&EVM::GPRRegClass);
    MachineBasicBlock &EntryMBB = MF.front();
    auto bmi =
        BuildMI(EntryMBB, EntryMBB.front(), EntryMBB.front().getDebugLoc(),
                TII->get(EVM::pSTACKARG_r), destReg)
            .addImm(retAddrStackArgNum);
    returnAddrReg = destReg;

    LLVM_DEBUG({
      dbgs() << "Creating return address STACKARG: ";
      bmi->dump();
    });
  }

  // we have found the return address, add the return address to instruction
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
      if (MI.getOpcode() == EVM::pRETURNSUB_r) {
        auto mibuilder = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                TII->get(EVM::pRETURNSUB_r));

        // TODO: this might change
        if (!EVMSubtarget::isMainFunction(*F)) {
            mibuilder.addReg(returnAddrReg);
        }

        mibuilder.addReg(MI.getOperand(0).getReg());
        MI.eraseFromParent();
      }
      if (MI.getOpcode() == EVM::pRETURNSUBVOID_r) {
        auto mibuilder = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                TII->get(EVM::pRETURNSUBVOID_r));

        if (!EVMSubtarget::isMainFunction(*F)) {
            mibuilder.addReg(returnAddrReg);
        }

        MI.eraseFromParent();
      }
    }
  }
}

// This is used to calculate if we need to allocate additional variables for
// return address on the stack.
unsigned EVMArgumentMove::computeStackArgOffset(MachineFunction &MF) const {
  bool hasSubroutine = MF.getSubtarget<EVMSubtarget>().hasSubroutine();
  if (EVMSubtarget::isMainFunction(*F) || hasSubroutine) {
    return 0;
  } else {
    // we plus one so that the return address is included
    return 1;
  }
}

void EVMArgumentMove::arrangeStackArgs(MachineFunction& MF) const {
  unsigned stackArgOffset = computeStackArgOffset(MF);
  BitVector stackargs(MFI->getNumStackArgs() + stackArgOffset, false);

  MachineBasicBlock &EntryMBB = MF.front();
  for (MachineInstr &MI : EntryMBB) {
    if (!EVMArgumentMove::isStackArg(MI)) {
      continue;
    }

    MachineOperand &MO = MI.getOperand(1);
    unsigned index = MO.getImm();
    stackargs.set(index);
  }

  assert(stackargs.size() == MFI->getNumStackArgs() + stackArgOffset);

  // the stack arrangement is like this:
  // (top) 1st argument, 2nd argument, 3rd argument, ..., return address (if any)
  // (bottom) Iterate over stack args, excluding the index zero one (return
  // address slot)

  // if an arg is deleted (by isel), create the instruction, and insert it
  for (unsigned i = 0; i < MFI->getNumStackArgs(); ++i) {
    if (!stackargs[i]) {
      MachineBasicBlock::iterator insertPt = EntryMBB.begin();

      unsigned destReg = MRI->createVirtualRegister(&EVM::GPRRegClass);
      BuildMI(EntryMBB, insertPt, insertPt->getDebugLoc(),
              TII->get(EVM::pSTACKARG_r), destReg)
          .addImm(i);

      LLVM_DEBUG({
        dbgs() << "Inserting Removed stackarg index: " << i << "\n";
      });
    }
  }

  if (!Subtarget->hasSubroutine()) {
    insertRetAddrArg(MF);
  }
}

void EVMArgumentMove::sortStackArgs(MachineFunction &MF) const {
  MachineBasicBlock &EntryMBB = MF.front();

  std::vector<unsigned> unusedArgs;
  DenseMap<unsigned, MachineInstr*> index2mi;
  MachineBasicBlock::iterator InsertPt = EntryMBB.end();

  // fills index2mi table so we can sort it later.
  for (MachineInstr &MI : EntryMBB) {
    if (EVMArgumentMove::isStackArg(MI)) {
      unsigned index = MI.getOperand(1).getImm();
      LLVM_DEBUG({
        dbgs() << "Inserting index2mi: " << index << ", ";
        MI.dump();
      });
      index2mi.insert(std::pair<unsigned, MachineInstr *>(index, &MI));
      unsigned reg = MI.getOperand(0).getReg();

      // record unused stack arguments so we can pop it later.
      bool IsDead = MRI->use_empty(reg);
      if (IsDead) {
        LLVM_DEBUG({
          dbgs() << "Stack argument: " << Register::virtReg2Index(reg)
                 << " is unused.\n";
        });
        unusedArgs.push_back(reg);
      }
    }
  }

  unsigned numArgs = index2mi.size();
  LLVM_DEBUG({ dbgs() << "nums in index2mi: " << numArgs << "\n"; });

  // Look for the first NonArg instruction.
  for (MachineInstr &MI : EntryMBB) {
    if (!EVMArgumentMove::isStackArg(MI)) {
      LLVM_DEBUG({
        dbgs() << "Insert point is: "; MI.dump();
      });
      InsertPt = MI;
      break;
    }
  }

  // arrange stackargs to top of MBB
  for (unsigned i = numArgs; i > 0; --i) {
    unsigned index = i - 1;
    assert(index2mi.find(index) != index2mi.end());
      LLVM_DEBUG({
        dbgs() << "rearranging index2mi: " << i << ", ";
        index2mi[index]->dump();
      });
    EntryMBB.insert(InsertPt, index2mi[index]->removeFromParent());
  }

  // TODO: this is buggy
  // insert pops afterwards
  for (std::vector<unsigned>::iterator rit = unusedArgs.begin();
       rit != unusedArgs.end(); ++rit) {
    LLVM_DEBUG({
      dbgs() << "Inserting POP instruction to remove reg "
             << Register::virtReg2Index(*rit) << "\n";
    });
    BuildMI(EntryMBB, InsertPt, InsertPt->getDebugLoc(),
            TII->get(EVM::POP_r)).addReg(*rit);
  }

  // Now move any argument instructions later in the block
  // to before our first NonArg instruction.
  for (MachineInstr &MI : llvm::make_range(InsertPt, EntryMBB.end())) {
    if (EVMArgumentMove::isStackArg(MI)) {
      llvm_unreachable("there shouldn't be another stack args unmoved.");
    }
  }
}

bool EVMArgumentMove::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Argument Move **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  Subtarget = &MF.getSubtarget<EVMSubtarget>();
  TII = Subtarget->getInstrInfo();
  MFI = MF.getInfo<EVMMachineFunctionInfo>();
  F = &MF.getFunction();
  MRI = &MF.getRegInfo();

  // fill in deleted stack args
  arrangeStackArgs(MF);

  // sort them in order
  sortStackArgs(MF);

  return true;
}
