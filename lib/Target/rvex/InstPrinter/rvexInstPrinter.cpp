//===-- rvexInstPrinter.cpp - Convert rvex MCInst to assembly syntax ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints an rvex MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "asm-printer"
#include "rvexInstPrinter.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#include "rvexGenAsmWriter.inc"

#include "rvexInstrInfo.h"

#include "llvm/Support/CommandLine.h"
extern cl::opt<bool> DisableOutputNops;

void rvexInstPrinter::printRegName(raw_ostream &OS, unsigned RegNo) const {
//- getRegisterName(RegNo) defined in rvexGenAsmWriter.inc which came from 
//   rvex.td indicate.
  OS << '$' << StringRef(getRegisterName(RegNo)).lower();
}

void rvexInstPrinter::printInst(const MCInst *MI, raw_ostream &O,
                                StringRef Annot) {
//- printInstruction(MI, O) defined in rvexGenAsmWriter.inc which came from 
//   rvex.td indicate.
  // Check if nop instruction should be printed or an empty packet be printed
  if ((MI->getOpcode() != rvex::NOP) || (DisableOutputNops==false))
  {
    O << "\tc0 ";
    printInstruction(MI, O);
    printAnnotation(O, Annot);    
  }

}

static void printExpr(const MCExpr *Expr, raw_ostream &OS) {
  int Offset = 0;
  const MCSymbolRefExpr *SRE;

  if (const MCBinaryExpr *BE = dyn_cast<MCBinaryExpr>(Expr)) {
    SRE = dyn_cast<MCSymbolRefExpr>(BE->getLHS());
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(BE->getRHS());
    assert(SRE && CE && "Binary expression must be sym+const.");
    Offset = CE->getValue();
  }
  else if (!(SRE = dyn_cast<MCSymbolRefExpr>(Expr)))
    assert(false && "Unexpected MCExpr type.");

  MCSymbolRefExpr::VariantKind Kind = SRE->getKind();

  switch (Kind) {
  default:                                 llvm_unreachable("Invalid kind!");
  case MCSymbolRefExpr::VK_None:           break;
// rvex_GPREL is for llc -march=rvex -relocation-model=static
  case MCSymbolRefExpr::VK_Cpu0_GPREL:     OS << "%gp_rel("; break;
  case MCSymbolRefExpr::VK_Cpu0_GOT_CALL:  OS << "%call24("; break;
  case MCSymbolRefExpr::VK_Cpu0_GOT16:     OS << "%got(";    break;
  case MCSymbolRefExpr::VK_Cpu0_GOT:       OS << "%got(";    break;
  case MCSymbolRefExpr::VK_Cpu0_ABS_HI:    OS << "%hi(";     break;
  case MCSymbolRefExpr::VK_Cpu0_ABS_LO:    OS << "%lo(";     break;
  }

  OS << SRE->getSymbol();

  if (Offset) {
    if (Offset > 0)
      OS << '+';
    OS << Offset;
  }

  if ((Kind == MCSymbolRefExpr::VK_Cpu0_GPOFF_HI) ||
      (Kind == MCSymbolRefExpr::VK_Cpu0_GPOFF_LO))
    OS << ")))";
  else if (Kind != MCSymbolRefExpr::VK_None)
    OS << ')';
}

void rvexInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }

  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }

  assert(Op.isExpr() && "unknown operand kind in printOperand");
  printExpr(Op.getExpr(), O);
}

void rvexInstPrinter::printUnsignedImm(const MCInst *MI, int opNum,
                                       raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(opNum);
  if (MO.isImm())
    O << (unsigned short int)MO.getImm();
  else
    printOperand(MI, opNum, O);
}

void rvexInstPrinter::
printMemOperand(const MCInst *MI, int opNum, raw_ostream &O) {
  // Load/Store memory operands -- imm($reg)
  // If PIC target the target is loaded as the
  // pattern ld $t9,%call24($gp)
  printOperand(MI, opNum+1, O);
  O << "[";
  printOperand(MI, opNum, O);
  O << "]";
}

void rvexInstPrinter::
printMemOperand_glob(const MCInst *MI, int opNum, raw_ostream &O) {
  // Load/Store memory operands -- imm($reg)
  // If PIC target the target is loaded as the
  // pattern ld $t9,%call24($gp)
  printOperand(MI, opNum+1, O);
  O << "[";
  //printOperand(MI, opNum, O);
  O << "$r0.0]";
}

void rvexInstPrinter::
printMemOperandEA(const MCInst *MI, int opNum, raw_ostream &O) {
  // when using stack locations for not load/store instructions
  // print the same way as all normal 3 operand instructions.
  printOperand(MI, opNum, O);
  O << ", ";
  printOperand(MI, opNum+1, O);
  return;
}

