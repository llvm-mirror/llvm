//===-- EVMInstPrinter.cpp - Convert EVM MCInst to asm syntax ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an EVM MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "EVMInstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

#include "llvm/IR/Constants.h"
using namespace llvm;

#define DEBUG_TYPE "evm-asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "EVMGenAsmWriter.inc"

static cl::opt<bool>
    NoAliases("evm-no-aliases",
              cl::desc("Disable the emission of assembler pseudo instructions"),
              cl::init(false), cl::Hidden);

static void printExpr(const MCExpr *Expr, raw_ostream &O) {
  O << *Expr;
}

void EVMInstPrinter::printInst(const MCInst *MI, raw_ostream &O,
                                 StringRef Annot, const MCSubtargetInfo &STI) {
  switch (MI->getOpcode()) {
    default: {
      printInstruction(MI, STI, O);
      break;
    }
  }

  printAnnotation(O, Annot);
}

void EVMInstPrinter::printRegName(raw_ostream &OS, unsigned RegNo) const {
  OS << StringRef(getRegisterName(RegNo)).lower();
}

void EVMInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                  const MCSubtargetInfo &STI, raw_ostream &O,
                                  const char *Modifier) {
  assert((Modifier == 0 || Modifier[0] == 0) && "No modifiers supported");

  if (MI->getNumOperands() == 0) return;

  const MCOperand &Op = MI->getOperand(OpNo);

  if (Op.isReg()) {
    llvm_unreachable("Should not have registers");
    return;
  } else if (Op.isImm()) {
    O << Op.getImm();
    return;
  } else if (Op.isExpr()) {
    printExpr(Op.getExpr(), O);
    return;
  } else if (Op.isCImm()) {
    O << Op.getCImm()->getValue();
    return;
  }
  llvm_unreachable("unimplemented.");
}

void EVMInstPrinter::printBrTargetOperand(const MCInst *MI, unsigned OpNo,
                                          const MCSubtargetInfo &STI,
                                          raw_ostream&OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    llvm_unreachable("unimplemented imm type");
  } else if (Op.isCImm()) {
    llvm_unreachable("unimplemented cimm type");
  } else if (Op.isExpr()) {
    llvm_unreachable("unimplemented expr type");
  } else {
    OS << Op;
  }
}
