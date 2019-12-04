//===-- EVMMCInstLower.cpp - Convert EVM MachineInstr to an MCInst ------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower EVM MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMMCInstLower.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define GET_INSTRINFO_ENUM 1
#define GET_INSTRMAP_INFO 1
#include "EVMGenInstrInfo.inc"

bool llvm::LowerEVMMachineOperandToMCOperand(const MachineOperand &MO,
                                             MCOperand &MCOp,
                                             const AsmPrinter &AP) {
  llvm_unreachable("unimplemented.");
}

void llvm::LowerEVMMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                        const AsmPrinter &AP) {
  llvm_unreachable("unimplemented.");
}

void EVMMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);

    MCOperand MCOp;
    switch (MO.getType()) {
      default:
        {
          MI->print(errs());
          llvm_unreachable("unknown operand type");
        }
      case MachineOperand::MO_BlockAddress:
        {
          MCOp = MCOperand::createExpr(
              MCSymbolRefExpr::create(
                Printer.GetBlockAddressSymbol(MO.getBlockAddress()),
                Ctx));
          break;
        }
      case MachineOperand::MO_MachineBasicBlock:
        {
          MCOp = MCOperand::createExpr(
              MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
          break;
        }
      case MachineOperand::MO_Register:
        {
          MCOp = MCOperand::createReg(MO.getReg());
          break;
        }
      // 256-bit types
      case MachineOperand::MO_CImmediate:
        {
          MCOp = MCOperand::createCImm(MO.getCImm());
          break;
        }
      case MachineOperand::MO_Immediate:
        {
          MCOp = MCOperand::createImm(MO.getImm());
          break;
        }
      case MachineOperand::MO_FPImmediate:
        {
          MI->print(errs());
          llvm_unreachable("unknown operand type");
          break;
        }
      case MachineOperand::MO_GlobalAddress:
        {
          MCOp = MCOperand::createExpr(
              MCSymbolRefExpr::create(
                Printer.getSymbol(MO.getGlobal()),
                Ctx));
          break;
        }
      case MachineOperand::MO_ExternalSymbol:
        {
          MCOp = MCOperand::createExpr(
              MCSymbolRefExpr::create(
                Printer.GetExternalSymbolSymbol(MO.getSymbolName()),
                Ctx));
          break;
        }
    }
    OutMI.addOperand(MCOp);
  }
}



