//===-- EVMELFObjectWriter.cpp - EVM ELF Writer ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

using namespace llvm;

namespace {

class EVMELFObjectWriter : public MCELFObjectTargetWriter {
public:
  EVMELFObjectWriter(uint8_t OSABI);
  ~EVMELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

} // end anonymous namespace

EVMELFObjectWriter::EVMELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(/*Is64Bit*/ true, OSABI, 0,
                              /*HasRelocationAddend*/ false) {}

unsigned EVMELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {
  llvm_unreachable("unimplemented.");
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createEVMELFObjectWriter(uint8_t OSABI) {
  return llvm::make_unique<EVMELFObjectWriter>(OSABI);
}
