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
