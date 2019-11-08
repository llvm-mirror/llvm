//===-- EVMAsmBackend.cpp - EVM Assembler Backend -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/EndianStream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

class EVMAsmBackend : public MCAsmBackend {
public:
  EVMAsmBackend(support::endianness Endian) : MCAsmBackend(Endian) {}
  ~EVMAsmBackend() override = default;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    return false;
  }

  unsigned getNumFixupKinds() const override { return 1; }

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override {
    return false;
  }

  void relaxInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                        MCInst &Res) const override {}

  bool writeNopData(raw_ostream &OS, uint64_t Count) const override;

private:
  // Writes the smart contract initialization code. Reference:
  // https://blog.openzeppelin.com/deconstructing-a-solidity-contract-part-ii-creation-vs-runtime-6b9d60ecb44c/
  void appendHeader(raw_ostream &OS, uint64_t Count) const;
};

} // end anonymous namespace

bool EVMAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count) const {
  return true;
}

void EVMAsmBackend::appendHeader(raw_ostream &OS, uint64_t Count) const {

}

void EVMAsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                               const MCValue &Target,
                               MutableArrayRef<char> Data, uint64_t Value,
                               bool IsResolved,
                               const MCSubtargetInfo *STI) const {
  assert(Fixup.getKind() == FK_SecRel_2);
  assert(Value <= 0xFFFF);

  // The offset points to the beginning of the instruction, so we have to
  // + 1
  support::endian::write<uint16_t>(&Data[Fixup.getOffset()],
                                   static_cast<uint16_t>(Value), Endian);
}

std::unique_ptr<MCObjectTargetWriter>
EVMAsmBackend::createObjectTargetWriter() const {
  //return createEVMELFObjectWriter(0);
  return createEVMBinaryObjectWriter();
}

MCAsmBackend *llvm::createEVMAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &) {
  return new EVMAsmBackend(support::big);
}

