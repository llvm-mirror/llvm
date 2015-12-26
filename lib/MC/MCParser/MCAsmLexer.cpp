//===-- MCAsmLexer.cpp - Abstract Asm Lexer Interface ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

MCAsmLexer::MCAsmLexer() : TokStart(nullptr), SkipSpace(true) {
  CurTok.emplace_back(AsmToken::Error, StringRef());
}

MCAsmLexer::~MCAsmLexer() {
}

SMLoc MCAsmLexer::getLoc() const {
  return SMLoc::getFromPointer(TokStart);
}

SMLoc AsmToken::getLoc() const {
  return SMLoc::getFromPointer(Str.data());
}

SMLoc AsmToken::getEndLoc() const {
  return SMLoc::getFromPointer(Str.data() + Str.size());
}

SMRange AsmToken::getLocRange() const {
  return SMRange(getLoc(), getEndLoc());
}
