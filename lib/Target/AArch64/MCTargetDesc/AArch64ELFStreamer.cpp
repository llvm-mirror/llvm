//===- lib/MC/AArch64ELFStreamer.cpp - ELF Object Output for AArch64 ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits AArch64 ELF .o object files. Different
// from generic ELF streamer in emitting mapping symbols ($x and $d) to delimit
// regions of data and code.
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetStreamer.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class AArch64ELFStreamer;

class AArch64TargetAsmStreamer : public AArch64TargetStreamer {
  formatted_raw_ostream &OS;

  void emitInst(uint32_t Inst) override;

public:
  AArch64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
};

AArch64TargetAsmStreamer::AArch64TargetAsmStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS)
  : AArch64TargetStreamer(S), OS(OS) {}

void AArch64TargetAsmStreamer::emitInst(uint32_t Inst) {
  OS << "\t.inst\t0x" << Twine::utohexstr(Inst) << "\n";
}

class AArch64TargetELFStreamer : public AArch64TargetStreamer {
private:
  AArch64ELFStreamer &getStreamer();

  void emitInst(uint32_t Inst) override;

public:
  AArch64TargetELFStreamer(MCStreamer &S) : AArch64TargetStreamer(S) {}
};

/// Extend the generic ELFStreamer class so that it can emit mapping symbols at
/// the appropriate points in the object files. These symbols are defined in the
/// AArch64 ELF ABI:
///    infocenter.arm.com/help/topic/com.arm.doc.ihi0056a/IHI0056A_aaelf64.pdf
///
/// In brief: $x or $d should be emitted at the start of each contiguous region
/// of A64 code or data in a section. In practice, this emission does not rely
/// on explicit assembler directives but on inherent properties of the
/// directives doing the emission (e.g. ".byte" is data, "add x0, x0, x0" an
/// instruction).
///
/// As a result this system is orthogonal to the DataRegion infrastructure used
/// by MachO. Beware!
class AArch64ELFStreamer : public MCELFStreamer {
public:
  friend class AArch64TargetELFStreamer;

  AArch64ELFStreamer(MCContext &Context, MCAsmBackend &TAB,
                     raw_pwrite_stream &OS, MCCodeEmitter *Emitter)
      : MCELFStreamer(Context, TAB, OS, Emitter), MappingSymbolCounter(0),
        LastEMS(EMS_None) {}

  void ChangeSection(MCSection *Section, const MCExpr *Subsection) override {
    // We have to keep track of the mapping symbol state of any sections we
    // use. Each one should start off as EMS_None, which is provided as the
    // default constructor by DenseMap::lookup.
    LastMappingSymbols[getPreviousSection().first] = LastEMS;
    LastEMS = LastMappingSymbols.lookup(Section);

    MCELFStreamer::ChangeSection(Section, Subsection);
  }

  /// This function is the one used to emit instruction data into the ELF
  /// streamer. We override it to add the appropriate mapping symbol if
  /// necessary.
  void EmitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    EmitA64MappingSymbol();
    MCELFStreamer::EmitInstruction(Inst, STI);
  }

  void emitInst(uint32_t Inst) {
    EmitA64MappingSymbol();
    MCELFStreamer::EmitIntValue(Inst, 4);
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void EmitBytes(StringRef Data) override {
    EmitDataMappingSymbol();
    MCELFStreamer::EmitBytes(Data);
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void EmitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    EmitDataMappingSymbol();
    MCELFStreamer::EmitValueImpl(Value, Size, Loc);
  }

private:
  enum ElfMappingSymbol {
    EMS_None,
    EMS_A64,
    EMS_Data
  };

  void EmitDataMappingSymbol() {
    if (LastEMS == EMS_Data)
      return;
    EmitMappingSymbol("$d");
    LastEMS = EMS_Data;
  }

  void EmitA64MappingSymbol() {
    if (LastEMS == EMS_A64)
      return;
    EmitMappingSymbol("$x");
    LastEMS = EMS_A64;
  }

  void EmitMappingSymbol(StringRef Name) {
    auto *Symbol = cast<MCSymbolELF>(getContext().getOrCreateSymbol(
        Name + "." + Twine(MappingSymbolCounter++)));
    EmitLabel(Symbol);
    Symbol->setType(ELF::STT_NOTYPE);
    Symbol->setBinding(ELF::STB_LOCAL);
    Symbol->setExternal(false);
  }

  int64_t MappingSymbolCounter;

  DenseMap<const MCSection *, ElfMappingSymbol> LastMappingSymbols;
  ElfMappingSymbol LastEMS;
};
} // end anonymous namespace

AArch64ELFStreamer &AArch64TargetELFStreamer::getStreamer() {
  return static_cast<AArch64ELFStreamer &>(Streamer);
}

void AArch64TargetELFStreamer::emitInst(uint32_t Inst) {
  getStreamer().emitInst(Inst);
}

namespace llvm {
MCTargetStreamer *createAArch64AsmTargetStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint,
                                                 bool isVerboseAsm) {
  return new AArch64TargetAsmStreamer(S, OS);
}

MCELFStreamer *createAArch64ELFStreamer(MCContext &Context, MCAsmBackend &TAB,
                                        raw_pwrite_stream &OS,
                                        MCCodeEmitter *Emitter, bool RelaxAll) {
  AArch64ELFStreamer *S = new AArch64ELFStreamer(Context, TAB, OS, Emitter);
  if (RelaxAll)
    S->getAssembler().setRelaxAll(true);
  return S;
}

MCTargetStreamer *
createAArch64ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new AArch64TargetELFStreamer(S);
  return nullptr;
}
}
