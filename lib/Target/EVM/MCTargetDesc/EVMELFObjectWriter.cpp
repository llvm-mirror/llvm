//===-- EVMELFObjectWriter.cpp - EVM ELF Writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMFixupKinds.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class EVMELFObjectWriter : public MCELFObjectTargetWriter {
public:
  EVMELFObjectWriter(uint8_t OSABI, bool Is64Bit);

  ~EVMELFObjectWriter() override;

  // Return true if the given relocation must be with a symbol rather than
  // section plus offset.
  bool needsRelocateWithSymbol(const MCSymbol &Sym,
                               unsigned Type) const override {
    // TODO: this is very conservative, update once EVM psABI requirements
    //       are clarified.
    return true;
  }

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};
}

EVMELFObjectWriter::EVMELFObjectWriter(uint8_t OSABI, bool Is64Bit)
    : MCELFObjectTargetWriter(Is64Bit, OSABI, ELF::EM_EVM,
                              /*HasRelocationAddend*/ true) {}

EVMELFObjectWriter::~EVMELFObjectWriter() {}

unsigned EVMELFObjectWriter::getRelocType(MCContext &Ctx,
                                            const MCValue &Target,
                                            const MCFixup &Fixup,
                                            bool IsPCRel) const {
  // Determine the type of the relocation
  switch ((unsigned)Fixup.getKind()) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_Data_4:
    return ELF::R_EVM_32;
  case FK_Data_8:
    return ELF::R_EVM_64;
  case FK_Data_Add_1:
    return ELF::R_EVM_ADD8;
  case FK_Data_Add_2:
    return ELF::R_EVM_ADD16;
  case FK_Data_Add_4:
    return ELF::R_EVM_ADD32;
  case FK_Data_Add_8:
    return ELF::R_EVM_ADD64;
  case FK_Data_Sub_1:
    return ELF::R_EVM_SUB8;
  case FK_Data_Sub_2:
    return ELF::R_EVM_SUB16;
  case FK_Data_Sub_4:
    return ELF::R_EVM_SUB32;
  case FK_Data_Sub_8:
    return ELF::R_EVM_SUB64;
  case EVM::fixup_evm_hi20:
    return ELF::R_EVM_HI20;
  case EVM::fixup_evm_lo12_i:
    return ELF::R_EVM_LO12_I;
  case EVM::fixup_evm_lo12_s:
    return ELF::R_EVM_LO12_S;
  case EVM::fixup_evm_pcrel_hi20:
    return ELF::R_EVM_PCREL_HI20;
  case EVM::fixup_evm_pcrel_lo12_i:
    return ELF::R_EVM_PCREL_LO12_I;
  case EVM::fixup_evm_pcrel_lo12_s:
    return ELF::R_EVM_PCREL_LO12_S;
  case EVM::fixup_evm_jal:
    return ELF::R_EVM_JAL;
  case EVM::fixup_evm_branch:
    return ELF::R_EVM_BRANCH;
  case EVM::fixup_evm_rvc_jump:
    return ELF::R_EVM_RVC_JUMP;
  case EVM::fixup_evm_rvc_branch:
    return ELF::R_EVM_RVC_BRANCH;
  case EVM::fixup_evm_call:
    return ELF::R_EVM_CALL;
  case EVM::fixup_evm_relax:
    return ELF::R_EVM_RELAX;
  case EVM::fixup_evm_align:
    return ELF::R_EVM_ALIGN;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createEVMELFObjectWriter(uint8_t OSABI, bool Is64Bit) {
  return llvm::make_unique<EVMELFObjectWriter>(OSABI, Is64Bit);
}
