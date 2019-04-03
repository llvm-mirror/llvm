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
/// 3. For each def, there is at monst 1 use.
///
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

  // first, find all the virtual register defs.
  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      for (const MachineOperand &MO : MI.explicit_uses()) {

        // only cares about registers.
        if ( !MO.isReg() || MO.isImplicit() ) continue;

        unsigned r = MO.getReg();
        if (TargetRegisterInfo::isPhysicalRegister(r)) continue;
        unsigned reg = TRI.virtReg2Index(r);

        if (Reg2Mem.find(reg) != Reg2Mem.end()) {
          // we've visited this register.
          continue;
        }

        // take a look at its def
        MachineInstr *Def = MRI.getVRegDef(MO.getReg());
        LLVM_DEBUG({
            dbgs() << "Find def for vreg: " << reg << ": ";
            if (Def != NULL) Def->dump();
            else dbgs() << "NULL\n";
        });

        LLVM_DEBUG({dbgs() << "Find vreg index def: " << reg <<
                           ", assigning location: " << index << "\n"; });

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


  for (MachineBasicBlock & MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;

      for (MachineOperand &MO : MI.explicit_uses()) {
        if (!MO.isReg()) continue;

        unsigned reg = MO.getReg();
        // FIXME: should not have non-virtual registers here.
        if (!TRI.isVirtualRegister(reg)) {
          LLVM_DEBUG({ dbgs() << "skipping non-virt reg MO: "; MO.dump(); });
          //llvm_unreachable("should not have non-vitural registers.");
          continue;
        }
        unsigned regindex = TRI.virtReg2Index(MO.getReg());

        if (Reg2Mem.find(regindex) == Reg2Mem.end()) {
          LLVM_DEBUG({ dbgs() << "skipping reg index in use: " << regindex << "\n"; });
          continue;
        }
        unsigned memindex = Reg2Mem[regindex];

        LLVM_DEBUG({ dbgs() << "found regindex use: " << regindex << ","; });

        // insert before instruction:
        // r1 = MLOAD_r 0x200
        // r2 = SUB_r r1, (index * 32)
        // new_vreg = MLOAD_r r2
        unsigned NewReg1 = MRI.createVirtualRegister(&EVM::GPRRegClass);
        unsigned NewReg2 = MRI.createVirtualRegister(&EVM::GPRRegClass);
        unsigned NewVReg = MRI.createVirtualRegister(&EVM::GPRRegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
               .addImm(0x200);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::SUB_r), NewReg2)
               .addReg(NewReg1).addImm(memindex * 32);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::MLOAD_r), NewVReg)
               .addReg(NewReg2);
        MO.setReg(NewVReg);

        LLVM_DEBUG({ dbgs() << " replacing use with: " << TRI.virtReg2Index(NewVReg)
                    << ", load from: " << memindex << ".\n"; });

        Changed = true;
      }

      for (MachineOperand &MO : MI.defs()) {
        unsigned regindex = TRI.virtReg2Index(MO.getReg());

        if (Reg2Mem.find(regindex) == Reg2Mem.end()) {
          LLVM_DEBUG({ dbgs() << "skipping reg index in def: " << regindex << "\n"; });
          continue;
        }

        unsigned memindex = Reg2Mem[regindex];

        LLVM_DEBUG({ dbgs() << "found regindex def: " << regindex; });
        // 1. insert after instruction:
        // r1 = MLOAD_r 0x200
        // r2 = SUB_r r1, (index * 32)
        // MSTORE_r vreg, r2
        // 2. vreg should be dead from now on.
        auto InsertPt = I;
        unsigned NewReg1 = MRI.createVirtualRegister(&EVM::GPRRegClass);
        unsigned NewReg2 = MRI.createVirtualRegister(&EVM::GPRRegClass);
        BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
               .addImm(0x200);
        BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::SUB_r), NewReg2)
               .addReg(NewReg1).addImm(memindex * 32);
        BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::MSTORE_r))
               .addReg(MO.getReg()).addReg(NewReg2);

        LLVM_DEBUG({ dbgs() << ", storing def to location: " << TRI.virtReg2Index(NewReg2)
                            << ", store to: " << memindex << ".\n"; });

        Changed = true;
      }
    }
  }

  // at the beginning of the function, increment memory allocator pointer.
  {
    MachineBasicBlock &EntryMBB = MF.front();
    MachineInstr &MI = EntryMBB.front();
    // insert into the front of BB.0:
    // r1 = (MLOAD_r LOC_mem)
    // r2 = ADD_r INC_imm, r1
    // MSTORE_r LOC_mem r2
    unsigned NewReg1 = MRI.createVirtualRegister(&EVM::GPRRegClass);
    unsigned NewReg2 = MRI.createVirtualRegister(&EVM::GPRRegClass);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::MLOAD_r), NewReg1)
      .addImm(0x200);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::ADD_r), NewReg2)
      .addImm(increment_size * 32).addReg(NewReg1);
    BuildMI(EntryMBB, MI, MI.getDebugLoc(), TII.get(EVM::MSTORE_r))
      .addImm(0x200).addReg(NewReg2);
    LLVM_DEBUG({ dbgs() << "Increment AP at entry: " << increment_size << ".\n"; });
  }


  // at the end of the function, decrement memory allocator pointer.
  // r1 = (MLOAD_r LOC_mem)
  // r2 = SUB_r r1, INC_imm
  // MSTORE_r LOC_mem r2
  {
    MachineBasicBlock &ExitMBB = MF.back();
    MachineInstr &MI = ExitMBB.back();
    unsigned NewReg3 = MRI.createVirtualRegister(&EVM::GPRRegClass);
    unsigned NewReg4 = MRI.createVirtualRegister(&EVM::GPRRegClass);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::MLOAD_r), NewReg3).addImm(0x200);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::SUB_r), NewReg4)
      .addReg(NewReg3).addImm(increment_size * 32);
    BuildMI(ExitMBB, MI, ExitMBB.findPrevDebugLoc(MF.back().end()),
        TII.get(EVM::MSTORE_r)).addImm(0x200).addReg(NewReg4);
    LLVM_DEBUG({ dbgs() << "decrement AP at exit: " << increment_size << ".\n"; });
  }

#ifndef NDEBUG
  // Assert that no use of vregs are across basic block.
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isLabel())
        continue;
      for (const MachineOperand &MO : MI.defs()) {
        // find all uses.
        // TODO

      }
    }
  }
#endif

  return Changed;
}
