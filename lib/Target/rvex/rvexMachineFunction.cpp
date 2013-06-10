//===-- rvexMachineFunctionInfo.cpp - Private data used for rvex ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "rvexMachineFunction.h"
#include "rvexInstrInfo.h"
#include "rvexSubtarget.h"
#include "MCTargetDesc/rvexBaseInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

bool rvexFixGlobalBaseReg = true;

bool rvexFunctionInfo::globalBaseRegFixed() const {
  return rvexFixGlobalBaseReg;
}

bool rvexFunctionInfo::globalBaseRegSet() const {
  return GlobalBaseReg;
}

unsigned rvexFunctionInfo::getGlobalBaseReg() {
  // Return if it has already been initialized.
  if (GlobalBaseReg)
    return GlobalBaseReg;

  //rvex HACK
  if (rvexFixGlobalBaseReg) // $gp is the global base register.
    return GlobalBaseReg = rvex::R0;

  const TargetRegisterClass *RC;
  RC = (const TargetRegisterClass*)&rvex::CPURegsRegClass;

  return GlobalBaseReg = MF.getRegInfo().createVirtualRegister(RC);
}

void rvexFunctionInfo::anchor() { }
