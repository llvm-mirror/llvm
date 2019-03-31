//===-- EVMReplacePhysRegs.cpp - Replace phys regs with virt regs -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that replaces physical registers with
/// virtual registers.
///
/// LLVM expects certain physical registers, such as a stack pointer. However,
/// EVM doesn't actually have such physical registers. This pass is run
/// once LLVM no longer needs these registers, and replaces them with virtual
/// registers, so they can participate in register stackifying and coloring in
/// the normal way.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-replace-phys-regs"

/// Copied from WebAssembly's backend.
namespace {
class EVMReplacePhysRegs final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  EVMReplacePhysRegs() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "EVM Replace Physical Registers";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char EVMReplacePhysRegs::ID = 0;
INITIALIZE_PASS(EVMReplacePhysRegs, DEBUG_TYPE,
                "Replace physical registers with virtual registers", false,
                false)

FunctionPass *llvm::createEVMReplacePhysRegs() {
  return new EVMReplacePhysRegs();
}

bool EVMReplacePhysRegs::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Replace Physical Registers **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  assert(!mustPreserveAnalysisID(LiveIntervalsID) &&
         "LiveIntervals shouldn't be active yet!");
  // We don't preserve SSA or liveness.
  MRI.leaveSSA();
  MRI.invalidateLiveness();

  for (unsigned PReg = EVM::NoRegister + 1;
       PReg < EVM::NUM_TARGET_REGS; ++PReg) {

    // Replace explicit uses of the physical register with a virtual register.
    unsigned VReg = EVM::NoRegister;
    for (auto I = MRI.reg_begin(PReg), E = MRI.reg_end(); I != E;) {
      MachineOperand &MO = *I++;
      if (!MO.isImplicit()) {
        if (VReg == EVM::NoRegister)
          VReg = MRI.createVirtualRegister(&EVM::GPRRegClass);
        MO.setReg(VReg);
        if (MO.getParent()->isDebugValue())
          MO.setIsDebug();
        Changed = true;
      }
    }
  }

  return Changed;
}
