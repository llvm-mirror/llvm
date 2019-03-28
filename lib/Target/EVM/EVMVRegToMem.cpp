//===-- EVMVRegToMem.cpp - Move virtual regisers to memory location -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that moves virtual register access to memory
/// locations.
/// After this pass, there should not be any virtual registers that are:
/// 1. created in a basic block i, and 
/// 2. used in basic block j, where i != j

/// It removes all the cross-basic block dependencies of vregs and converts
/// them to loads and stores of the memory. (like reversing mem2reg).
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-move-vregs"

/// Copied from WebAssembly's backend.
namespace {
class EVMVRegToMem final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMVRegToMem() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM Move VRegisters to Memory";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMVRegToMem::ID = 0;
INITIALIZE_PASS(EVMVRegToMem, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMVRegToMem() {
  return new EVMVRegToMem();
}

bool EVMVRegToMem::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Virtual Registers to Memory Locations **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  const auto &TRI = *MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  bool Changed = false;

  DenseMap<unsigned, unsigned> Reg2Mem;
  unsigned index = 1;

  // first, find all the virtual registers.
  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      for (const MachineOperand &MO : MI.explicit_operands()) {
        // only cares about registers.
        if ( !MO.isReg() || !MO.isDef() ) continue;
        unsigned reg = MO.getReg();

        LLVM_DEBUG({dbgs() << "Find vreg: " << reg <<
                           ", location: " << index << "\n"; });

        Reg2Mem.insert(std::make_pair(reg, index));
        index ++;
      }
    }
  }

  unsigned increment_size = Reg2Mem.size();

  LLVM_DEBUG({
      dbgs() << "Total Number of virtual register definintion: "
             << increment_size << '\n';
  });

  // at the beginning of the function, increment memory allocator pointer.
  {
    MachineBasicBlock &EntryMBB = MF.front();
    MachineInstr &MI = EntryMBB.front();
    // insert into the front of BB.0:
    // r1 = (MLOAD_r LOC_mem)
    // r2 = ADD_r INC_imm, r1
    // MSTORE_r LOC_mem r2
    const TargetRegisterClass *RC = MRI.getRegClass(MVT::i256);
    unsigned NewReg1 = MRI.createVirtualRegister(RC);
    unsigned NewReg2 = MRI.createVirtualRegister(RC);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
      .addImm(0x200);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::ADD_r), NewReg2)
      .addImm(increment_size * 32).addReg(NewReg1);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::MSTORE_r))
      .addImm(0x200).addReg(NewReg2);
  }


  // at the end of the function, decrement memory allocator pointer.
  // r1 = (MLOAD_r LOC_mem)
  // r2 = SUB_r r1, INC_imm
  // MSTORE_r LOC_mem r2
  const TargetRegisterClass *RC = MRI.getRegClass(MVT::i256);
  {
    MachineBasicBlock &ExitMBB = MF.back();
    MachineInstr &MI = ExitMBB.back();
    unsigned NewReg3 = MRI.createVirtualRegister(RC);
    unsigned NewReg4 = MRI.createVirtualRegister(RC);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::MLOAD_r), NewReg3).addImm(0x200);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::SUB_r), NewReg4)
      .addReg(NewReg3).addImm(increment_size * 32);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::MSTORE_r)).addImm(0x200).addReg(NewReg4);
  }


  // second: replace those instructions with acess to memory.
  for (MachineFunction::iterator MBB = MF.begin(), BB_E = MF.end(); MBB != BB_E; ++MBB) {
    for (MachineBasicBlock::iterator MI = MBB->begin(), E = MBB->end(); MI != E; ++MI) {
      for (MachineInstr::mop_iterator MO = MI->explicit_operands().begin(),
                                    MI_E = MI->explicit_operands().end();
                                     MO != MI_E; ++MO) {
        if ( !MO->isReg() ) continue;

        unsigned reg = MO->getReg();
        assert(Reg2Mem.find(reg) != Reg2Mem.end());
        unsigned index = Reg2Mem[reg];

        Changed = true;

        if (MO->isDef()) {
          // insert after instruction:
          // r1 = MLOAD_r 0x200
          // r2 = SUB_r r1, (index * 32)
          // MSTORE_r vreg, (index * 32)
          auto InsertPt = std::next(MI);
          unsigned NewReg1 = MRI.createVirtualRegister(RC);
          unsigned NewReg2 = MRI.createVirtualRegister(RC);
          BuildMI(*MBB, InsertPt, MI->getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
                 .addImm(0x200);
          BuildMI(*MBB, InsertPt, MI->getDebugLoc(), TII.get(EVM::SUB_r), NewReg2)
                 .addReg(NewReg1).addImm(index * 32);
          BuildMI(*MBB, InsertPt, MI->getDebugLoc(), TII.get(EVM::MSTORE_r))
                 .addReg(reg).addImm(index * 32);

        } else {
          assert(MO->isUse());
          // insert before instruction:
          // r1 = MLOAD_r 0x200
          // r2 = SUB_r r1, (index * 32)
          // new_vreg = MLOAD_r (index *32)
          unsigned NewReg1 = MRI.createVirtualRegister(RC);
          unsigned NewReg2 = MRI.createVirtualRegister(RC);
          BuildMI(*MBB, MI, MI->getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
                 .addImm(0x200);
          BuildMI(*MBB, MI, MI->getDebugLoc(), TII.get(EVM::SUB_r), NewReg2)
                 .addReg(NewReg1).addImm(index * 32);
          BuildMI(*MBB, MI, MI->getDebugLoc(), TII.get(EVM::SUB_r), NewReg2)
                 .addReg(NewReg1).addImm(index * 32);
          MO->setReg(NewReg2);
        }
      }
    }
  }

  return Changed;
}
