//===-- EVMObjectWriter.cpp -  EVM Writer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file handles EVM-specific object emission, converting LLVM's
/// internal fixups into the appropriate relocations.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/BinaryFormat/EVM.h" // TODO: change the content of EVM.h
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
//#include "llvm/MC/MCSectionEVM.h"
//#include "llvm/MC/MCSymbolEVM.h" // TODO
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCEVMObjectWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class EVMObjectWriter final : public MCEVMObjectTargetWriter {
public:
  explicit EVMObjectWriter();

private:
  unsigned getRelocType(const MCValue &Target,
                        const MCFixup &Fixup) const override;
};
} // end anonymous namespace

EVMObjectWriter::EVMObjectWriter() : MCEVMObjectTargetWriter() {}

static const MCSection *getFixupSection(const MCExpr *Expr) {
  llvm_unreachable("Fixup Section implementation unfinished");
  if (auto SyExp = dyn_cast<MCSymbolRefExpr>(Expr)) {
    if (SyExp->getSymbol().isInSection())
      return &SyExp->getSymbol().getSection();
  }

  return nullptr;
}

unsigned EVMObjectWriter::getRelocType(const MCValue &Target,
    const MCFixup &Fixup) const {
  llvm_unreachable("getRelocType implementation unfinished");
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createEVMObjectWriter() {
  return std::make_unique<EVMObjectWriter>();
}
