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
#include "MCTargetDesc/EVMMCExpr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

#define DEBUG_TYPE "evm-asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "EVMGenAsmWriter.inc"

// Include the auto-generated portion of the compress emitter.
#define GEN_UNCOMPRESS_INSTR
#include "EVMGenCompressInstEmitter.inc"

static cl::opt<bool>
    NoAliases("evm-no-aliases",
              cl::desc("Disable the emission of assembler pseudo instructions"),
              cl::init(false), cl::Hidden);

void EVMInstPrinter::printInst(const MCInst *MI, raw_ostream &O,
                                 StringRef Annot, const MCSubtargetInfo &STI) {
  bool Res = false;
  const MCInst *NewMI = MI;

  printInstruction(MI, STI, O);
  printAnnotation(O, Annot);
}

void EVMInstPrinter::printRegName(raw_ostream &O, unsigned RegNo) const {
  llvm_unreachable("unimplemented.");
}

void EVMInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    const MCSubtargetInfo &STI, raw_ostream &O,
                                    const char *Modifier) {
  llvm_unreachable("unimpleted");
}

