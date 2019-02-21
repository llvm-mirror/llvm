//===-- EVMAsmParser.cpp - Parse EVM assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMAsmBackend.h"
#include "MCTargetDesc/EVMMCExpr.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "MCTargetDesc/EVMTargetStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"

#include <limits>

using namespace llvm;

// Include the auto-generated portion of the compress emitter.
#define GEN_COMPRESS_INSTR
#include "EVMGenCompressInstEmitter.inc"

namespace {
struct EVMOperand;

class EVMAsmParser : public MCTargetAsmParser {


  EVMTargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<EVMTargetStreamer &>(TS);
  }

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;

  // Helper to actually emit an instruction to the MCStreamer. Also, when
  // possible, compression of the instruction is performed.
  void emitToStreamer(MCStreamer &S, const MCInst &Inst);

  // Helper to emit a combination of LUI, ADDI(W), and SLLI instructions that
  // synthesize the desired immedate value into the destination register.
  void emitLoadImm(unsigned DestReg, int64_t Value, MCStreamer &Out);

  // Helper to emit pseudo instruction "lla" used in PC-rel addressing.
  void emitLoadLocalAddress(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

  /// Helper for processing MC instructions that have been successfully matched
  /// by MatchAndEmitInstruction. Modifications to the emitted instructions,
  /// like the expansion of pseudo instructions (e.g., "li"), can be performed
  /// in this method.
  bool processInstruction(MCInst &Inst, SMLoc IDLoc, MCStreamer &Out);

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "EVMGenAsmMatcher.inc"

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);

  bool parseDirectiveOption();

  void setFeatureBits(uint64_t Feature, StringRef FeatureString) {
  }

public:

  static bool classifySymbolRef(const MCExpr *Expr,
                                EVMMCExpr::VariantKind &Kind,
                                int64_t &Addend);

  EVMAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

} // end anonymous namespace.

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "EVMGenAsmMatcher.inc"


bool EVMAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  llvm_unreachable("Unknown match type detected!");
}

bool EVMAsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {

  return Error(StartLoc, "invalid register name");
}


bool EVMAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  return false;
}

bool EVMAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  return false;
}

void EVMAsmParser::emitToStreamer(MCStreamer &S, const MCInst &Inst) {
}



bool EVMAsmParser::processInstruction(MCInst &Inst, SMLoc IDLoc,
                                        MCStreamer &Out) {
  return false;
}

extern "C" void LLVMInitializeEVMAsmParser() {
  RegisterMCAsmParser<EVMAsmParser> Y(getTheEVMTarget());
}
