//===-- ARMInstrInfo.cpp - ARM Instruction Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMInstrInfo.h"
#include "ARM.h"
#include "ARMConstantPoolValue.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMTargetMachine.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
using namespace llvm;

ARMInstrInfo::ARMInstrInfo(const ARMSubtarget &STI)
    : ARMBaseInstrInfo(STI), RI() {}

/// getNoopForMachoTarget - Return the noop instruction to use for a noop.
void ARMInstrInfo::getNoopForMachoTarget(MCInst &NopInst) const {
  if (hasNOP()) {
    NopInst.setOpcode(ARM::HINT);
    NopInst.addOperand(MCOperand::createImm(0));
    NopInst.addOperand(MCOperand::createImm(ARMCC::AL));
    NopInst.addOperand(MCOperand::createReg(0));
  } else {
    NopInst.setOpcode(ARM::MOVr);
    NopInst.addOperand(MCOperand::createReg(ARM::R0));
    NopInst.addOperand(MCOperand::createReg(ARM::R0));
    NopInst.addOperand(MCOperand::createImm(ARMCC::AL));
    NopInst.addOperand(MCOperand::createReg(0));
    NopInst.addOperand(MCOperand::createReg(0));
  }
}

unsigned ARMInstrInfo::getUnindexedOpcode(unsigned Opc) const {
  switch (Opc) {
  default:
    break;
  case ARM::LDR_PRE_IMM:
  case ARM::LDR_PRE_REG:
  case ARM::LDR_POST_IMM:
  case ARM::LDR_POST_REG:
    return ARM::LDRi12;
  case ARM::LDRH_PRE:
  case ARM::LDRH_POST:
    return ARM::LDRH;
  case ARM::LDRB_PRE_IMM:
  case ARM::LDRB_PRE_REG:
  case ARM::LDRB_POST_IMM:
  case ARM::LDRB_POST_REG:
    return ARM::LDRBi12;
  case ARM::LDRSH_PRE:
  case ARM::LDRSH_POST:
    return ARM::LDRSH;
  case ARM::LDRSB_PRE:
  case ARM::LDRSB_POST:
    return ARM::LDRSB;
  case ARM::STR_PRE_IMM:
  case ARM::STR_PRE_REG:
  case ARM::STR_POST_IMM:
  case ARM::STR_POST_REG:
    return ARM::STRi12;
  case ARM::STRH_PRE:
  case ARM::STRH_POST:
    return ARM::STRH;
  case ARM::STRB_PRE_IMM:
  case ARM::STRB_PRE_REG:
  case ARM::STRB_POST_IMM:
  case ARM::STRB_POST_REG:
    return ARM::STRBi12;
  }

  return 0;
}

void ARMInstrInfo::expandLoadStackGuard(MachineBasicBlock::iterator MI,
                                        Reloc::Model RM) const {
  MachineFunction &MF = *MI->getParent()->getParent();
  const ARMSubtarget &Subtarget = MF.getSubtarget<ARMSubtarget>();

  if (!Subtarget.useMovt(MF)) {
    if (RM == Reloc::PIC_)
      expandLoadStackGuardBase(MI, ARM::LDRLIT_ga_pcrel, ARM::LDRi12, RM);
    else
      expandLoadStackGuardBase(MI, ARM::LDRLIT_ga_abs, ARM::LDRi12, RM);
    return;
  }

  if (RM != Reloc::PIC_) {
    expandLoadStackGuardBase(MI, ARM::MOVi32imm, ARM::LDRi12, RM);
    return;
  }

  const GlobalValue *GV =
      cast<GlobalValue>((*MI->memoperands_begin())->getValue());

  if (!Subtarget.GVIsIndirectSymbol(GV, RM)) {
    expandLoadStackGuardBase(MI, ARM::MOV_ga_pcrel, ARM::LDRi12, RM);
    return;
  }

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned Reg = MI->getOperand(0).getReg();
  MachineInstrBuilder MIB;

  MIB = BuildMI(MBB, MI, DL, get(ARM::MOV_ga_pcrel_ldr), Reg)
            .addGlobalAddress(GV, 0, ARMII::MO_NONLAZY);
  unsigned Flag = MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant;
  MachineMemOperand *MMO = MBB.getParent()->getMachineMemOperand(
      MachinePointerInfo::getGOT(*MBB.getParent()), Flag, 4, 4);
  MIB.addMemOperand(MMO);
  MIB = BuildMI(MBB, MI, DL, get(ARM::LDRi12), Reg);
  MIB.addReg(Reg, RegState::Kill).addImm(0);
  MIB.setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
  AddDefaultPred(MIB);
}
