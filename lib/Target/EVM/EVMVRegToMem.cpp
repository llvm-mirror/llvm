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
  EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
  unsigned fiSize = MFI.getFrameIndexSize();
  
  const EVMSubtarget &ST = MF.getSubtarget<EVMSubtarget>();
  const auto &TII = *MF.getSubtarget<EVMSubtarget>().getInstrInfo();
  const auto &TRI = *MF.getSubtarget<EVMSubtarget>().getRegisterInfo();
  bool Changed = false;

  DenseMap<unsigned, unsigned> Reg2Mem;
  unsigned index = 0;

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

      unsigned opc = MI.getOpcode();
      // skip the stack arguments.
      if (opc == EVM::pSTACKARG_r) {
        continue;
      }

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
        unsigned memindex = Reg2Mem[regindex] + fiSize;

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
        unsigned regindex = TRI.virtReg2Index(MO.getReg());

        if (Reg2Mem.find(regindex) == Reg2Mem.end()) {
          LLVM_DEBUG({ dbgs() << "skipping reg index in def: " << regindex << "\n"; });
          continue;
        }

        unsigned memindex = Reg2Mem[regindex] + fiSize;

        auto &InsertPt = I;
        BuildMI(MBB, InsertPt, MI.getDebugLoc(), TII.get(EVM::pPUTLOCAL_r))
        .addReg(MO.getReg())
        .addImm(memindex);

        Changed = true;
      }
    }
  }

  // Patch the frame pointer in the prologue and epilogue.
  if (!Reg2Mem.empty()) {
    int fiSize = -1;
    // iterate over instructions, find pADJFP(UP/DOWN)_r,
    // patch the value --- we can't know the value beforehand.
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        unsigned opc = MI.getOpcode();
        if (opc == EVM::pADJFPUP_r ||
            opc == EVM::pADJFPDOWN_r) {
          unsigned s = MI.getOperand(0).getImm(); 
          
          // simple check
          if (fiSize != -1) {
            assert(s == fiSize &&
                   "FrameSize not the same in a same function");
          } else {
            fiSize = s;
          }

          //add the local variable spilling size
          unsigned newSize = s + Reg2Mem.size();
          MI.getOperand(0).setImm(newSize);
        }

      }
    }
  }

  return Changed;
}
