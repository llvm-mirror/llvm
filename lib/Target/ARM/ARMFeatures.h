//===-- ARMFeatures.h - Checks for ARM instruction features -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the code shared between ARM CodeGen and ARM MC
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMFEATURES_H
#define LLVM_LIB_TARGET_ARM_ARMFEATURES_H

#include "MCTargetDesc/ARMMCTargetDesc.h"

namespace llvm {

template<typename InstrType> // could be MachineInstr or MCInst
bool IsCPSRDead(InstrType *Instr);

template<typename InstrType> // could be MachineInstr or MCInst
inline bool isV8EligibleForIT(InstrType *Instr) {
  switch (Instr->getOpcode()) {
  default:
    return false;
  case ARM::tADC:
  case ARM::tADDi3:
  case ARM::tADDi8:
  case ARM::tADDrr:
  case ARM::tAND:
  case ARM::tASRri:
  case ARM::tASRrr:
  case ARM::tBIC:
  case ARM::tEOR:
  case ARM::tLSLri:
  case ARM::tLSLrr:
  case ARM::tLSRri:
  case ARM::tLSRrr:
  case ARM::tMOVi8:
  case ARM::tMUL:
  case ARM::tMVN:
  case ARM::tORR:
  case ARM::tROR:
  case ARM::tRSB:
  case ARM::tSBC:
  case ARM::tSUBi3:
  case ARM::tSUBi8:
  case ARM::tSUBrr:
    // Outside of an IT block, these set CPSR.
    return IsCPSRDead(Instr);
  case ARM::tADDrSPi:
  case ARM::tCMNz:
  case ARM::tCMPi8:
  case ARM::tCMPr:
  case ARM::tLDRBi:
  case ARM::tLDRBr:
  case ARM::tLDRHi:
  case ARM::tLDRHr:
  case ARM::tLDRSB:
  case ARM::tLDRSH:
  case ARM::tLDRi:
  case ARM::tLDRr:
  case ARM::tLDRspi:
  case ARM::tSTRBi:
  case ARM::tSTRBr:
  case ARM::tSTRHi:
  case ARM::tSTRHr:
  case ARM::tSTRi:
  case ARM::tSTRr:
  case ARM::tSTRspi:
  case ARM::tTST:
    return true;
// there are some "conditionally deprecated" opcodes
  case ARM::tADDspr:
  case ARM::tBLXr:
    return Instr->getOperand(2).getReg() != ARM::PC;
  // ADD PC, SP and BLX PC were always unpredictable,
  // now on top of it they're deprecated
  case ARM::tADDrSP:
  case ARM::tBX:
    return Instr->getOperand(0).getReg() != ARM::PC;
  case ARM::tADDhirr:
    return Instr->getOperand(0).getReg() != ARM::PC &&
           Instr->getOperand(2).getReg() != ARM::PC;
  case ARM::tCMPhir:
  case ARM::tMOVr:
    return Instr->getOperand(0).getReg() != ARM::PC &&
           Instr->getOperand(1).getReg() != ARM::PC;
  }
}

}

#endif
