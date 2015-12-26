//===-- WebAssemblyAsmBackend.cpp - WebAssembly Assembler Backend ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements the WebAssemblyAsmBackend class.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
class WebAssemblyAsmBackend final : public MCAsmBackend {
  bool Is64Bit;

public:
  explicit WebAssemblyAsmBackend(bool Is64Bit)
      : MCAsmBackend(), Is64Bit(Is64Bit) {}
  ~WebAssemblyAsmBackend() override {}

  void applyFixup(const MCFixup &Fixup, char *Data, unsigned DataSize,
                  uint64_t Value, bool IsPCRel) const override;

  MCObjectWriter *createObjectWriter(raw_pwrite_stream &OS) const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    return false;
  }

  unsigned getNumFixupKinds() const override {
    // We currently just use the generic fixups in MCFixup.h and don't have any
    // target-specific fixups.
    return 0;
  }

  bool mayNeedRelaxation(const MCInst &Inst) const override { return false; }

  void relaxInstruction(const MCInst &Inst, MCInst &Res) const override {}

  bool writeNopData(uint64_t Count, MCObjectWriter *OW) const override;
};

bool WebAssemblyAsmBackend::writeNopData(uint64_t Count,
                                         MCObjectWriter *OW) const {
  if (Count == 0)
    return true;

  // FIXME: Do something.
  return false;
}

void WebAssemblyAsmBackend::applyFixup(const MCFixup &Fixup, char *Data,
                                       unsigned DataSize, uint64_t Value,
                                       bool IsPCRel) const {
  const MCFixupKindInfo &Info = getFixupKindInfo(Fixup.getKind());
  unsigned NumBytes = RoundUpToAlignment(Info.TargetSize, 8);
  if (!Value)
    return; // Doesn't change encoding.

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= DataSize && "Invalid fixup offset!");

  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  for (unsigned i = 0; i != NumBytes; ++i)
    Data[Offset + i] |= uint8_t((Value >> (i * 8)) & 0xff);
}

MCObjectWriter *
WebAssemblyAsmBackend::createObjectWriter(raw_pwrite_stream &OS) const {
  return createWebAssemblyELFObjectWriter(OS, Is64Bit, 0);
}
} // end anonymous namespace

MCAsmBackend *llvm::createWebAssemblyAsmBackend(const Target &T,
                                                const MCRegisterInfo &MRI,
                                                const Triple &TT,
                                                StringRef CPU) {
  return new WebAssemblyAsmBackend(TT.isArch64Bit());
}
