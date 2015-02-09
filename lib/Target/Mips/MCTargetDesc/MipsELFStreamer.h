//===-------- MipsELFStreamer.h - ELF Object Output -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a custom MCELFStreamer which allows us to insert some hooks before
// emitting data into an actual object file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSELFSTREAMER_H
#define LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSELFSTREAMER_H

#include "MipsOptionRecord.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCELFStreamer.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCSubtargetInfo;

class MipsELFStreamer : public MCELFStreamer {
  SmallVector<std::unique_ptr<MipsOptionRecord>, 8> MipsOptionRecords;
  MipsRegInfoRecord *RegInfoRecord;
  SmallVector<MCSymbol*, 4> Labels;


public:
  MipsELFStreamer(MCContext &Context, MCAsmBackend &MAB, raw_ostream &OS,
                  MCCodeEmitter *Emitter, const MCSubtargetInfo &STI)
      : MCELFStreamer(Context, MAB, OS, Emitter) {

    RegInfoRecord = new MipsRegInfoRecord(this, Context);
    MipsOptionRecords.push_back(
        std::unique_ptr<MipsRegInfoRecord>(RegInfoRecord));
  }

  /// Overriding this function allows us to add arbitrary behaviour before the
  /// \p Inst is actually emitted. For example, we can inspect the operands and
  /// gather sufficient information that allows us to reason about the register
  /// usage for the translation unit.
  void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;

  /// Overriding this function allows us to record all labels that should be
  /// marked as microMIPS. Based on this data marking is done in
  /// EmitInstruction.
  void EmitLabel(MCSymbol *Symbol) override;

  /// Overriding this function allows us to dismiss all labels that are
  /// candidates for marking as microMIPS when .section directive is processed.
  void SwitchSection(const MCSection *Section,
                     const MCExpr *Subsection = nullptr) override;

  /// Overriding this function allows us to dismiss all labels that are
  /// candidates for marking as microMIPS when .word directive is emitted.
  void EmitValueImpl(const MCExpr *Value, unsigned Size,
                     const SMLoc &Loc) override;

  /// Emits all the option records stored up until the point it's called.
  void EmitMipsOptionRecords();
};

MCELFStreamer *createMipsELFStreamer(MCContext &Context, MCAsmBackend &MAB,
                                     raw_ostream &OS, MCCodeEmitter *Emitter,
                                     const MCSubtargetInfo &STI, bool RelaxAll);
} // namespace llvm.
#endif
