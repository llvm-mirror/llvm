//===- EVMObjAsmParser.cpp - Wasm Assembly Parser -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// --
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/MachineValueType.h"

using namespace llvm;

namespace {

class EVMObjAsmParser : public MCAsmParserExtension {
  MCAsmParser *Parser = nullptr;
  MCAsmLexer *Lexer = nullptr;

  template<bool (EVMObjAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<EVMObjAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

public:
  EVMObjAsmParser() { BracketExpressionsSupported = true; }

  void Initialize(MCAsmParser &P) override {
    Parser = &P;
    Lexer = &Parser->getLexer();
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(*Parser);
  }

  bool error(const StringRef &Msg, const AsmToken &Tok) {
    return Parser->Error(Tok.getLoc(), Msg + Tok.getString());
  }

  bool isNext(AsmToken::TokenKind Kind) {
    auto Ok = Lexer->is(Kind);
    if (Ok)
      Lex();
    return Ok;
  }

  bool expect(AsmToken::TokenKind Kind, const char *KindName) {
    if (!isNext(Kind))
      return error(std::string("Expected ") + KindName + ", instead got: ",
                   Lexer->getTok());
    return false;
  }

  bool parseSectionDirectiveText(StringRef, SMLoc) {
    return false;
  }

  bool parseSectionFlags(StringRef FlagStr, bool &Passive) {
    return false;
  }

  bool parseSectionDirective(StringRef, SMLoc) {
    return false;
  }

  // TODO: This function is almost the same as ELFAsmParser::ParseDirectiveSize
  // so maybe could be shared somehow.
  bool parseDirectiveSize(StringRef, SMLoc) {
    return false;
  }

  bool parseDirectiveType(StringRef, SMLoc) {
    return false;
  }

  bool ParseDirectiveIdent(StringRef, SMLoc) {
    return false;
  }

  bool ParseDirectiveSymbolAttribute(StringRef Directive, SMLoc) {
    return false;
  }
};

} // end anonymous namespace

namespace llvm {

MCAsmParserExtension *createEVMAsmParser() {
  return new EVMObjAsmParser;
}

} // end namespace llvm
