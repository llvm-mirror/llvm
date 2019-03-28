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
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Defines llvm::EVM::getStackOpcode to convert register instructions to
// stack instructions
#define GET_INSTRINFO_ENUM 1
#define GET_INSTRMAP_INFO 1
#include "EVMGenInstrInfo.inc"


static MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym,
                                    const AsmPrinter &AP) {
  llvm_unreachable("unimplemented.");
}

bool llvm::LowerEVMMachineOperandToMCOperand(const MachineOperand &MO,
                                             MCOperand &MCOp,
                                             const AsmPrinter &AP) {
  llvm_unreachable("unimplemented.");
}

void llvm::LowerEVMMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                        const AsmPrinter &AP) {
  llvm_unreachable("unimplemented.");
}

static void removeRegisterOperands(const MachineInstr *MI, MCInst &OutMI) {
  // Copied from WASM backend.
  // Remove all uses of stackified registers to bring the instruction format
  // into its final stack form used thruout MC, and transition opcodes to
  // their _S variant.
  // TODO: handle inline asm as WASM backend stated.
  if (MI->isDebugInstr() || MI->isLabel() || MI->isInlineAsm())
    return;

  // Transform to _S instruction.
  auto RegOpcode = OutMI.getOpcode();
  auto StackOpcode = 0;//llvm::EVM::getStackOpcode(RegOpcode);
  assert(StackOpcode != -1 && "Failed to stackify instruction");
  OutMI.setOpcode(StackOpcode);

  // Remove register operands.
  for (auto I = OutMI.getNumOperands(); I; --I) {
    auto &MO = OutMI.getOperand(I - 1);
    if (MO.isReg()) {
      OutMI.erase(&MO);
    }
  }
}

void EVMMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  const MCInstrDesc &Desc = MI->getDesc();
  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);

    MCOperand MCOp;
    switch (MO.getType()) {
      default:
        {
          MI->print(errs());
          llvm_unreachable("unknown operand type");
        }
      case MachineOperand::MO_MachineBasicBlock:
        {
          MI->print(errs());
          llvm_unreachable("MachineBasicBlock operand should have been rewritten");
        }
      case MachineOperand::MO_Register:
        {
          MI->print(errs());
          llvm_unreachable("unimplemented.");
        }
      case MachineOperand::MO_Immediate:
        {
          MCOp = MCOperand::createImm(MO.getImm());
          break;
        }
      case MachineOperand::MO_FPImmediate:
        {

          break;
        }
    }
    OutMI.addOperand(MCOp);
  }

  removeRegisterOperands(MI, OutMI);
}



