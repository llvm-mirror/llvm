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
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  StringRef getPassName() const override {
    return "EVM Move VRegisters to Memory";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool initializeMemSlots(MachineFunction &MF);
  DenseMap<unsigned, unsigned> Reg2Local;
  unsigned getLocalId(unsigned Reg);

  unsigned CurLocal;
};
} // end anonymous namespace

char EVMVRegToMem::ID = 0;
INITIALIZE_PASS(EVMVRegToMem, DEBUG_TYPE,
                "Replace virtual registers accesses with memory locations",
                false, false)

FunctionPass *llvm::createEVMVRegToMem() {
  return new EVMVRegToMem();
}

/// Return a local id number for the given register, assigning it a new one
/// if it doesn't yet have one.
unsigned EVMVRegToMem::getLocalId(unsigned Reg) {
  auto P = Reg2Local.insert(std::make_pair(Reg, CurLocal));
  if (P.second)
    ++CurLocal;
  return P.first->second;
}

/// insert STACKARG and frameslots into the Reg2Mem.
/// The frame is formatted as follows:
/// | Frame Slots | STACKARGs | variables |
bool EVMVRegToMem::initializeMemSlots(MachineFunction &MF) {
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();

  bool Changed = false;
  
  // We have to set up the frame index and the incoming arguments.
  unsigned stackSlots = MF.getFrameInfo().getNumObjects();

  // initialize the slot allocator
  CurLocal = stackSlots;

  LLVM_DEBUG({
    dbgs() << "Pre-alloc stack slots: " << stackSlots <<"\n";
  });
  
  for (MachineBasicBlock::iterator I = MF.begin()->begin(),
                                   E = MF.begin()->end();
       I != E;) {
    MachineInstr &MI = *I++;
    if (MI.getOpcode() != EVM::pSTACKARG_r)
      break;
    unsigned Reg = MI.getOperand(0).getReg();

    // skip stackified instructions
    if (MFI.isVRegStackified(Reg)) {
      continue;
    }

    // assign a slot to the STACKARG
    unsigned slot = getLocalId(Reg);
    unsigned idx = MI.getOperand(1).getImm();
    LLVM_DEBUG({
      dbgs() << "Allocating stack arg " << idx << " to slot: " << slot << "\n";
    });
    //MI.eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool EVMVRegToMem::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Virtual Registers to Memory Locations **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
  
  const EVMSubtarget &ST = MF.getSubtarget<EVMSubtarget>();
  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  const auto &TRI = *MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  bool Changed = false;

  // this records the registers we should ignore
  SmallVector<unsigned, 16> IgnoredRegs;

  DenseMap<unsigned, unsigned> Reg2Mem;
  unsigned index = 0;

  // We have to set up the frame index and the incoming arguments.
  unsigned stackSlots = MF.getFrameInfo().getNumObjects();
  
  LLVM_DEBUG({ dbgs() << "== Start initializing memory slot map.\n"; });
  Changed |= initializeMemSlots(MF);

  LLVM_DEBUG({ dbgs() << "== Start scanning registers.\n"; });

  // Precompute the set of registers that are unused, so that we can insert
  // drops to their defs.
  BitVector UseEmpty(MRI.getNumVirtRegs());
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I)
    UseEmpty[I] = MRI.use_empty(TargetRegisterInfo::index2VirtReg(I));
  
  // Visit each instruction in the function.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;
  
      if (MI.isDebugInstr() || MI.isLabel())
        continue;


      // Insert local.sets for any defs that aren't stackified yet. Currently
      // we handle at most one def.
      assert(MI.getDesc().getNumDefs() <= 1);
      if (MI.getDesc().getNumDefs() == 1) {
        unsigned OldReg = MI.getOperand(0).getReg();

        if (MFI.isVRegStackified(OldReg)) {
          llvm_unreachable("unimplemented");
        }

        const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
        unsigned NewReg = MRI.createVirtualRegister(RC);
        auto InsertPt = std::next(MI.getIterator());

        if (UseEmpty[TargetRegisterInfo::virtReg2Index(OldReg)]) {
          MachineInstr *Drop =
              BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::POP))
                  .addReg(NewReg);
          // After the drop instruction, this reg operand will not be used
          Drop->getOperand(0).setIsKill();
        } else {
          unsigned LocalId = getLocalId(OldReg);
          BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::pPUTLOCAL_r))
              .addImm(LocalId)
              .addReg(NewReg);
        }

        MI.getOperand(0).setReg(NewReg);
        // This register operand of the original instruction is now being used
        // by the inserted drop or local.set instruction, so make it not dead
        // yet.
        MI.getOperand(0).setIsDead(false);
        MFI.stackifyVReg(NewReg);
        Changed = true;
      }
       
      MachineInstr *InsertPt = &MI;
      for (MachineOperand &MO : reverse(MI.explicit_uses())) {
        if (!MO.isReg())
          continue;

        unsigned OldReg = MO.getReg();

        unsigned LocalId = getLocalId(OldReg);
        const TargetRegisterClass *RC = MRI.getRegClass(OldReg);
        unsigned NewReg = MRI.createVirtualRegister(RC);
        InsertPt = BuildMI(MBB, InsertPt, MI.getDebugLoc(),
                           TII.get(EVM::pGETLOCAL_r), NewReg)
                       .addImm(LocalId);
        MO.setReg(NewReg);
        MFI.stackifyVReg(NewReg);
        Changed = true;
      }

      // Coalesce and eliminate COPY instructions.
      if (MI.getOpcode() == EVM::pMOVE_r) {
        MRI.replaceRegWith(MI.getOperand(1).getReg(),
                           MI.getOperand(0).getReg());
        MI.eraseFromParent();
        Changed = true;
      }

    }
  }

  return Changed;

  // first, find all the virtual register defs.
  for (const MachineBasicBlock & MBB : MF) {
    for (const MachineInstr & MI : MBB) {
      for (const MachineOperand &MO : MI.explicit_uses()) {

        // only cares about registers.
        if ( !MO.isReg() || MO.isImplicit() ) continue;

        unsigned r = MO.getReg();

        // if the Register is stackified, ignore it
        if (MFI.isVRegStackified(r)) {
          llvm_unreachable("unimplemented");
        }

        // up till now, all physical registers are gone.
        assert(!TargetRegisterInfo::isPhysicalRegister(r));
        unsigned reg = TRI.virtReg2Index(r);

        if (std::find(IgnoredRegs.begin(), IgnoredRegs.end(), r) !=
            IgnoredRegs.end()) {
          // ignore registers that should stay on the stack
          LLVM_DEBUG({
            dbgs() << "Ignore allocating space for register: " << reg << "\n";
          });
          continue;
        }

        if (Reg2Mem.find(reg) != Reg2Mem.end()) {
          // we've visited this register.
          continue;
        }

        // take a look at its def
        MachineInstr *Def = MRI.getVRegDef(r);
        assert(Def && "Unimplemented case for no definition");

        unsigned opc = Def->getOpcode();
        assert (opc != EVM::pSTACKARG_r);
        assert (opc != EVM::pGETLOCAL_r);

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

  // the increment size is the actual number that the framepointer
  // should adjust. we need to improve it to involve register liveness
  // analysis to reduce it.
  unsigned increment_size = Reg2Mem.size();

  LLVM_DEBUG({
      dbgs() << "Total Number of virtual register slots: "
             << increment_size << '\n';
  });
  // increment_size is the number we will patch at the prologue and epilogue.

  for (MachineBasicBlock & MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &MI = *I++;

      for (MachineOperand &MO : MI.explicit_uses()) {
        if (!MO.isReg()) continue;
        unsigned reg = MO.getReg();
        unsigned regindex = TRI.virtReg2Index(MO.getReg());

        // ignore ...
        if(std::find(IgnoredRegs.begin(), IgnoredRegs.end(), reg)
               != IgnoredRegs.end()) {
          continue;
        }

        assert(Reg2Mem.find(regindex) != Reg2Mem.end());
        unsigned memindex = Reg2Mem[regindex];

        LLVM_DEBUG({ dbgs() << "found regindex use: " << regindex << ","; });

        // insert before instruction:
        unsigned NewVReg = MRI.createVirtualRegister(&EVM::GPRRegClass);

        // when instantiating the pGETLOCAL, make sure it is generated
        // before the frame adjustment.
        if (MI.getOpcode() == EVM::pRETURNSUB_r) {
          MachineBasicBlock::iterator PI = MachineBasicBlock::iterator(MI);
          --PI;
          assert(PI->getOpcode() == EVM::pADJFPDOWN_r &&
                 "ADJFPDOWN_r should follow pRETURNSUB_r");
          MachineInstr &PrevInstr = *PI;
          BuildMI(MBB, PrevInstr, PrevInstr.getDebugLoc(),
                  TII.get(EVM::pGETLOCAL_r), NewVReg)
                 .addImm(memindex);
        } else {
          BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(EVM::pGETLOCAL_r), NewVReg)
                 .addImm(memindex);
        }

        MO.setReg(NewVReg);

        LLVM_DEBUG({ dbgs() << " replacing use with: " << TRI.virtReg2Index(NewVReg)
                    << ", load from: " << memindex << ".\n"; });

        Changed = true;
      }

      for (MachineOperand &MO : MI.defs()) {
        assert(MO.isReg());

        unsigned reg = MO.getReg();
        unsigned regindex = TRI.virtReg2Index(reg);

        // if we should ignore this ...
        if(std::find(IgnoredRegs.begin(), IgnoredRegs.end(), reg)
               != IgnoredRegs.end()) {
          continue;
        }
        assert(Reg2Mem.find(regindex) != Reg2Mem.end());

        unsigned memindex = Reg2Mem[regindex];

        auto &InsertPt = I;
        BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::pPUTLOCAL_r))
        .addReg(reg)
        .addImm(memindex);

        Changed = true;
      }
    }
  }

  // Patch the frame pointer in the prologue and epilogue.
  if (!Reg2Mem.empty()) {
    // iterate over instructions, find pADJFP(UP/DOWN)_r,
    // patch the value --- we can't know the value beforehand.
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        unsigned opc = MI.getOpcode();
        if (opc == EVM::pADJFPUP_r ||
            opc == EVM::pADJFPDOWN_r) {
          MI.getOperand(0).setImm(Reg2Mem.size());
        }

      }
    }
  }

  return Changed;
}
