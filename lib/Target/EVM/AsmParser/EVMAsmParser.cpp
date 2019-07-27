//===---- EVMAsmParser.cpp - Parse EVM assembly to MCInst instructions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMRegisterInfo.h"
#include "MCTargetDesc/EVMMCELFStreamer.h"
#include "MCTargetDesc/EVMMCExpr.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "TargetInfo/EVMTargetInfo.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"

#include <sstream>

#define DEBUG_TYPE "evm-asm-parser"

using namespace llvm;

namespace {
/// Parses EVM assembly from a stream.
class EVMAsmParser : public MCTargetAsmParser {
  const MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  const MCRegisterInfo *MRI;
  const std::string GENERATE_STUBS = "gs";

#define GET_ASSEMBLER_HEADER
#include "EVMGenAsmMatcher.inc"

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

public:
  EVMAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
               const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), STI(STI), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    MRI = getContext().getRegisterInfo();

    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

  MCAsmParser &getParser() const { return Parser; }
  MCAsmLexer &getLexer() const { return Parser.getLexer(); }
};

/// An parsed EVM assembly operand.
class EVMOperand : public MCParsedAsmOperand {
  typedef MCParsedAsmOperand Base;
  enum KindTy { k_Immediate, k_Register, k_Token, k_Memri } Kind;

public:
  EVMOperand(StringRef Tok, SMLoc const &S)
      : Base(), Kind(k_Token), Tok(Tok), Start(S), End(S) {}
  EVMOperand(unsigned Reg, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(k_Register), RegImm({Reg, nullptr}), Start(S), End(E) {}
  EVMOperand(MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(k_Immediate), RegImm({0, Imm}), Start(S), End(E) {}
  EVMOperand(unsigned Reg, MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(k_Memri), RegImm({Reg, Imm}), Start(S), End(E) {}

  struct RegisterImmediate {
    unsigned Reg;
    MCExpr const *Imm;
  };
  union {
    StringRef Tok;
    RegisterImmediate RegImm;
  };

  SMLoc Start, End;

public:
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Register && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediate when possible
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Immediate && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  /// Adds the contained reg+imm operand to an instruction.
  void addMemriOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Memri && "Unexpected operand kind");
    assert(N == 2 && "Invalid number of operands");

    Inst.addOperand(MCOperand::createReg(getReg()));
    addExpr(Inst, getImm());
  }

  void addImmCom8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // The operand is actually a imm8, but we have its bitwise
    // negation in the assembly source, so twiddle it here.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(~(uint8_t)CE->getValue()));
  }

  bool isImmCom8() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    int64_t Value = CE->getValue();
    return isUInt<8>(Value);
  }

  bool isReg() const { return Kind == k_Register; }
  bool isImm() const { return Kind == k_Immediate; }
  bool isToken() const { return Kind == k_Token; }
  bool isMem() const { return Kind == k_Memri; }
  bool isMemri() const { return Kind == k_Memri; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return Tok;
  }

  unsigned getReg() const {
    assert((Kind == k_Register || Kind == k_Memri) && "Invalid access!");

    return RegImm.Reg;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate || Kind == k_Memri) && "Invalid access!");
    return RegImm.Imm;
  }

  static std::unique_ptr<EVMOperand> CreateToken(StringRef Str, SMLoc S) {
    return make_unique<EVMOperand>(Str, S);
  }

  static std::unique_ptr<EVMOperand> CreateReg(unsigned RegNum, SMLoc S,
                                               SMLoc E) {
    return make_unique<EVMOperand>(RegNum, S, E);
  }

  static std::unique_ptr<EVMOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                               SMLoc E) {
    return make_unique<EVMOperand>(Val, S, E);
  }

  static std::unique_ptr<EVMOperand>
  CreateMemri(unsigned RegNum, const MCExpr *Val, SMLoc S, SMLoc E) {
    return make_unique<EVMOperand>(RegNum, Val, S, E);
  }

  void makeToken(StringRef Token) {
    Kind = k_Token;
    Tok = Token;
  }

  void makeReg(unsigned RegNo) {
    Kind = k_Register;
    RegImm = {RegNo, nullptr};
  }

  void makeImm(MCExpr const *Ex) {
    Kind = k_Immediate;
    RegImm = {0, Ex};
  }

  void makeMemri(unsigned RegNo, MCExpr const *Imm) {
    Kind = k_Memri;
    RegImm = {RegNo, Imm};
  }

  SMLoc getStartLoc() const { return Start; }
  SMLoc getEndLoc() const { return End; }

  virtual void print(raw_ostream &O) const {
    switch (Kind) {
    case k_Token:
      O << "Token: \"" << getToken() << "\"";
      break;
    case k_Register:
      O << "Register: " << getReg();
      break;
    case k_Immediate:
      O << "Immediate: \"" << *getImm() << "\"";
      break;
    case k_Memri: {
      // only manually print the size for non-negative values,
      // as the sign is inserted automatically.
      O << "Memri: \"" << getReg() << '+' << *getImm() << "\"";
      break;
    }
    }
    O << "\n";
  }
};

} // end anonymous namespace.

// Auto-generated Match Functions

