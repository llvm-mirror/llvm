//===-- X86MachineFuctionInfo.cpp - X86 machine function info -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"

using namespace llvm;

void X86MachineFunctionInfo::anchor() { }

void X86MachineFunctionInfo::setRestoreBasePointer(const MachineFunction *MF) {
  if (!RestoreBasePointerOffset) {
    const X86RegisterInfo *RegInfo = static_cast<const X86RegisterInfo *>(
      MF->getSubtarget().getRegisterInfo());
    unsigned SlotSize = RegInfo->getSlotSize();
    for (const MCPhysReg *CSR =
      RegInfo->X86RegisterInfo::getCalleeSavedRegs(MF);
      unsigned Reg = *CSR;
       ++CSR)
    {
      if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
        RestoreBasePointerOffset -= SlotSize;
    }
  }
}

