//===-- EVMPrepareStackification.cpp - Argument instruction moving ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-prepare-stackification"

namespace {
class EVMPrepareStackification final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMPrepareStackification() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM prepare stackification";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervals>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMPrepareStackification::ID = 0;
INITIALIZE_PASS(EVMPrepareStackification, DEBUG_TYPE,
                "Move live-ins and live-outs to memory location",
                false, false)

FunctionPass *llvm::createEVMPrepareStackification() {
  return new EVMPrepareStackification();
}

/*
static bool hasArgumentDef(unsigned Reg, const MachineRegisterInfo &MRI) {
  for (const auto &Def : MRI.def_instructions(Reg))
    if (Def.getOpcode() == EVM::pSTACKARG_r)
      return true;
  return false;
}
*/

bool EVMPrepareStackification::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Prepare Stackification **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();

  // We don't preserve SSA form.
  // MRI.leaveSSA();

  // BranchFolding and perhaps other passes don't preserve IMPLICIT_DEF
  // instructions. LiveIntervals requires that all paths to virtual register
  // uses provide a definition. Insert IMPLICIT_DEFs in the entry block to
  // conservatively satisfy this.
  //
  // TODO: This is fairly heavy-handed; find a better approach.
  /*
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = Register::index2VirtReg(I);

    // Skip unused registers.
    if (MRI.use_nodbg_empty(Reg))
      continue;

    // Skip registers that have an ARGUMENT definition.
    if (hasArgumentDef(Reg, MRI))
      continue;

    if (MRI.getVRegDef(Reg) == NULL) {
      BuildMI(Entry, Entry.begin(), DebugLoc(), TII.get(EVM::IMPLICIT_DEF),
              Reg);
      Changed = true;
    }
  }

  // Move ARGUMENT_* instructions to the top of the entry block, so that their
  // liveness reflects the fact that these really are live-in values.
  for (auto MII = Entry.begin(), MIE = Entry.end(); MII != MIE;) {
    MachineInstr &MI = *MII++;
    if (MI.getOpcode() == EVM::pSTACKARG_r) {
      MI.removeFromParent();
      Entry.insert(Entry.begin(), &MI);
    }
  }

  // Ok, we're now ready to run the LiveIntervals analysis again.
  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);
  */

  auto &LIS = getAnalysis<LiveIntervals>();
  LLVM_DEBUG({
    dbgs() << "Before:\n\n";
    for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
      unsigned Reg = Register::index2VirtReg(I);

      const LiveInterval &LI = LIS.getInterval(Reg);
      LI.dump();
    }
  });

  // Split multiple-VN LiveIntervals into multiple LiveIntervals.
  SmallVector<LiveInterval *, 4> SplitLIs;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(Reg))
      continue;

    LIS.splitSeparateComponents(LIS.getInterval(Reg), SplitLIs);
    SplitLIs.clear();
  }

  LLVM_DEBUG({
    dbgs() << "after:\n";
    for (unsigned I = 0, E = MRI.getNumVirtRegs(); I < E; ++I) {
      unsigned Reg = Register::index2VirtReg(I);
      const LiveInterval &LI = LIS.getInterval(Reg);
      LI.dump();
    }
  });

  return true;
}
