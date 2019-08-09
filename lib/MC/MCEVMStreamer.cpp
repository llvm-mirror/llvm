//===- lib/MC/MCEVMStreamer.cpp - EVM Object Output ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits EVM .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCEVMStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCEVMStreamer::~MCEVMStreamer() = default; // anchor.

void MCEVMStreamer::mergeFragment(MCDataFragment *DF, MCDataFragment *EF) {
  flushPendingLabels(DF, DF->getContents().size());

  for (unsigned I = 0, E = EF->getFixups().size(); I != E; ++I) {
    EF->getFixups()[I].setOffset(EF->getFixups()[I].getOffset() +
                                 DF->getContents().size());
    DF->getFixups().push_back(EF->getFixups()[I]);
  }
  if (DF->getSubtargetInfo() == nullptr && EF->getSubtargetInfo())
    DF->setHasInstructions(*EF->getSubtargetInfo());
  DF->getContents().append(EF->getContents().begin(), EF->getContents().end());
}

void MCEVMStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);

  // Do any generic stuff we need to do.
  llvm_unreachable("invalid assembler flag!");
}

void MCEVMStreamer::ChangeSection(MCSection *Section,
                                  const MCExpr *Subsection) {
  llvm_unreachable("unimplemented");
}

void MCEVMStreamer::EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {
  getAssembler().registerSymbol(*Symbol);
  const MCExpr *Value = MCSymbolRefExpr::create(
      Symbol, MCSymbolRefExpr::VK_WEAKREF, getContext());
  Alias->setVariableValue(Value);
}

bool MCEVMStreamer::EmitSymbolAttribute(MCSymbol *S, MCSymbolAttr Attribute) {

  return true;
}

void MCEVMStreamer::EmitCommonSymbol(MCSymbol *S, uint64_t Size,
                                     unsigned ByteAlignment) {
  llvm_unreachable("Common symbols are not yet implemented for EVM");
}

void MCEVMStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  llvm_unreachable("unimplemented");
}

void MCEVMStreamer::EmitLocalCommonSymbol(MCSymbol *S, uint64_t Size,
                                          unsigned ByteAlignment) {
  llvm_unreachable("Local common symbols are not yet implemented for EVM");
}

void MCEVMStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size,
                                  SMLoc Loc) {
  MCObjectStreamer::EmitValueImpl(Value, Size, Loc);
}

void MCEVMStreamer::EmitValueToAlignment(unsigned ByteAlignment, int64_t Value,
                                         unsigned ValueSize,
                                         unsigned MaxBytesToEmit) {
  MCObjectStreamer::EmitValueToAlignment(ByteAlignment, Value, ValueSize,
                                         MaxBytesToEmit);
}

void MCEVMStreamer::EmitIdent(StringRef IdentString) {
  // TODO(sbc): Add the ident section once we support mergable strings
  // sections in the object format
}

void MCEVMStreamer::EmitInstToFragment(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  this->MCObjectStreamer::EmitInstToFragment(Inst, STI);
}

void MCEVMStreamer::EmitInstToData(const MCInst &Inst,
                                   const MCSubtargetInfo &STI) {
  MCAssembler &Assembler = getAssembler();
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  raw_svector_ostream VecOS(Code);
  Assembler.getEmitter().encodeInstruction(Inst, VecOS, Fixups, STI);

  // Append the encoded instruction to the current data fragment (or create a
  // new such fragment if the current fragment is not a data fragment).
  MCDataFragment *DF = getOrCreateDataFragment();

  // Add the fixups and data.
  for (unsigned I = 0, E = Fixups.size(); I != E; ++I) {
    Fixups[I].setOffset(Fixups[I].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[I]);
  }
  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCEVMStreamer::FinishImpl() {
  EmitFrames(nullptr);

  this->MCObjectStreamer::FinishImpl();
}

MCStreamer *llvm::createEVMStreamer(MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&CE,
                                    bool RelaxAll) {
  MCEVMStreamer *S =
      new MCEVMStreamer(Context, std::move(MAB), std::move(OW), std::move(CE));
  if (RelaxAll)
    S->getAssembler().setRelaxAll(true);
  return S;
}

void MCEVMStreamer::EmitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("Generic EVM doesn't support this directive");
}

void MCEVMStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("EVM doesn't support this directive");
}

void MCEVMStreamer::EmitZerofill(MCSection *Section, MCSymbol *Symbol,
                                 uint64_t Size, unsigned ByteAlignment,
                                 SMLoc Loc) {
  llvm_unreachable("EVM doesn't support this directive");
}

void MCEVMStreamer::EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, unsigned ByteAlignment) {
  llvm_unreachable("EVM doesn't support this directive");
}
