//===-- Mips16FrameLowering.cpp - Mips16 Frame Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips16 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "Mips16FrameLowering.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips16InstrInfo.h"
#include "MipsInstrInfo.h"
#include "MipsRegisterInfo.h"
#include "MipsSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

Mips16FrameLowering::Mips16FrameLowering(const MipsSubtarget &STI)
    : MipsFrameLowering(STI, STI.stackAlignment()) {}

void Mips16FrameLowering::emitPrologue(MachineFunction &MF) const {
  MachineBasicBlock &MBB = MF.front();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const Mips16InstrInfo &TII =
      *static_cast<const Mips16InstrInfo *>(STI.getInstrInfo());
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  uint64_t StackSize = MFI->getStackSize();

  // No need to allocate space on the stack.
  if (StackSize == 0 && !MFI->adjustsStack()) return;

  MachineModuleInfo &MMI = MF.getMMI();
  const MCRegisterInfo *MRI = MMI.getContext().getRegisterInfo();
  MachineLocation DstML, SrcML;

  // Adjust stack.
  TII.makeFrame(Mips::SP, StackSize, MBB, MBBI);

  // emit ".cfi_def_cfa_offset StackSize"
  unsigned CFIIndex = MMI.addFrameInst(
      MCCFIInstruction::createDefCfaOffset(nullptr, -StackSize));
  BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);

  const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();

  if (CSI.size()) {
    const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();

    for (std::vector<CalleeSavedInfo>::const_iterator I = CSI.begin(),
         E = CSI.end(); I != E; ++I) {
      int64_t Offset = MFI->getObjectOffset(I->getFrameIdx());
      unsigned Reg = I->getReg();
      unsigned DReg = MRI->getDwarfRegNum(Reg, true);
      unsigned CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DReg, Offset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }
  if (hasFP(MF))
    BuildMI(MBB, MBBI, dl, TII.get(Mips::MoveR3216), Mips::S0)
      .addReg(Mips::SP).setMIFlag(MachineInstr::FrameSetup);

}

void Mips16FrameLowering::emitEpilogue(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const Mips16InstrInfo &TII =
      *static_cast<const Mips16InstrInfo *>(STI.getInstrInfo());
  DebugLoc dl = MBBI->getDebugLoc();
  uint64_t StackSize = MFI->getStackSize();

  if (!StackSize)
    return;

  if (hasFP(MF))
    BuildMI(MBB, MBBI, dl, TII.get(Mips::Move32R16), Mips::SP)
      .addReg(Mips::S0);

  // Adjust stack.
  // assumes stacksize multiple of 8
  TII.restoreFrame(Mips::SP, StackSize, MBB, MBBI);
}

bool Mips16FrameLowering::
spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MI,
                          const std::vector<CalleeSavedInfo> &CSI,
                          const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *EntryBlock = MF->begin();

  //
  // Registers RA, S0,S1 are the callee saved registers and they
  // will be saved with the "save" instruction
  // during emitPrologue
  //
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    // Add the callee-saved register as live-in. Do not add if the register is
    // RA and return address is taken, because it has already been added in
    // method MipsTargetLowering::LowerRETURNADDR.
    // It's killed at the spill, unless the register is RA and return address
    // is taken.
    unsigned Reg = CSI[i].getReg();
    bool IsRAAndRetAddrIsTaken = (Reg == Mips::RA)
      && MF->getFrameInfo()->isReturnAddressTaken();
    if (!IsRAAndRetAddrIsTaken)
      EntryBlock->addLiveIn(Reg);
  }

  return true;
}

bool Mips16FrameLowering::restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI,
                                       const std::vector<CalleeSavedInfo> &CSI,
                                       const TargetRegisterInfo *TRI) const {
  //
  // Registers RA,S0,S1 are the callee saved registers and they will be restored
  // with the restore instruction during emitEpilogue.
  // We need to override this virtual function, otherwise llvm will try and
  // restore the registers on it's on from the stack.
  //

  return true;
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions
void Mips16FrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = I->getOperand(0).getImm();

    if (I->getOpcode() == Mips::ADJCALLSTACKDOWN)
      Amount = -Amount;

    const Mips16InstrInfo &TII =
        *static_cast<const Mips16InstrInfo *>(STI.getInstrInfo());

    TII.adjustStackPtr(Mips::SP, Amount, MBB, I);
  }

  MBB.erase(I);
}

bool
Mips16FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  // Reserve call frame if the size of the maximum call frame fits into 15-bit
  // immediate field and there are no variable sized objects on the stack.
  return isInt<15>(MFI->getMaxCallFrameSize()) && !MFI->hasVarSizedObjects();
}

void Mips16FrameLowering::
processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                     RegScavenger *RS) const {
  const Mips16InstrInfo &TII =
      *static_cast<const Mips16InstrInfo *>(STI.getInstrInfo());
  const MipsRegisterInfo &RI = TII.getRegisterInfo();
  const BitVector Reserved = RI.getReservedRegs(MF);
  bool SaveS2 = Reserved[Mips::S2];
  if (SaveS2)
    MF.getRegInfo().setPhysRegUsed(Mips::S2);
  if (hasFP(MF))
    MF.getRegInfo().setPhysRegUsed(Mips::S0);
}

const MipsFrameLowering *
llvm::createMips16FrameLowering(const MipsSubtarget &ST) {
  return new Mips16FrameLowering(ST);
}
