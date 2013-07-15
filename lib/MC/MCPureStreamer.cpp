//===- lib/MC/MCPureStreamer.cpp - MC "Pure" Object Output ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class MCPureStreamer : public MCObjectStreamer {
private:
  virtual void EmitInstToFragment(const MCInst &Inst);
  virtual void EmitInstToData(const MCInst &Inst);

public:
  MCPureStreamer(MCContext &Context, MCAsmBackend &TAB, raw_ostream &OS,
                 MCCodeEmitter *Emitter)
      : MCObjectStreamer(SK_PureStreamer, Context, TAB, OS, Emitter) {}

  /// @name MCStreamer Interface
  /// @{

  virtual void InitSections();
  virtual void InitToTextSection();
  virtual void EmitLabel(MCSymbol *Symbol);
  virtual void EmitDebugLabel(MCSymbol *Symbol);
  virtual void EmitZerofill(const MCSection *Section, MCSymbol *Symbol = 0,
                            uint64_t Size = 0, unsigned ByteAlignment = 0);
  virtual void EmitBytes(StringRef Data);
  virtual void EmitValueToAlignment(unsigned ByteAlignment, int64_t Value = 0,
                                    unsigned ValueSize = 1,
                                    unsigned MaxBytesToEmit = 0);
  virtual void EmitCodeAlignment(unsigned ByteAlignment,
                                 unsigned MaxBytesToEmit = 0);
  virtual bool EmitValueToOffset(const MCExpr *Offset,
                                 unsigned char Value = 0);
  virtual void FinishImpl();


  virtual void EmitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitAssemblerFlag(MCAssemblerFlag Flag) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitTBSSSymbol(const MCSection *Section, MCSymbol *Symbol,
                              uint64_t Size, unsigned ByteAlignment = 0) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                unsigned ByteAlignment) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitThumbFunc(MCSymbol *Func) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void BeginCOFFSymbolDef(const MCSymbol *Symbol) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitCOFFSymbolStorageClass(int StorageClass) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitCOFFSymbolType(int Type) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EndCOFFSymbolDef() {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     unsigned ByteAlignment) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual void EmitFileDirective(StringRef Filename) {
    report_fatal_error("unsupported directive in pure streamer");
  }
  virtual bool EmitDwarfFileDirective(unsigned FileNo, StringRef Directory,
                                      StringRef Filename, unsigned CUID = 0) {
    report_fatal_error("unsupported directive in pure streamer");
  }

  /// @}

  static bool classof(const MCStreamer *S) {
    return S->getKind() == SK_PureStreamer;
  }
};

} // end anonymous namespace.

void MCPureStreamer::InitSections() {
  InitToTextSection();
}

void MCPureStreamer::InitToTextSection() {
  SwitchSection(getContext().getObjectFileInfo()->getTextSection());
}

void MCPureStreamer::EmitLabel(MCSymbol *Symbol) {
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");
  assert(!Symbol->isVariable() && "Cannot emit a variable symbol!");
  assert(getCurrentSection().first && "Cannot emit before setting section!");

  Symbol->setSection(*getCurrentSection().first);

  MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);

  // We have to create a new fragment if this is an atom defining symbol,
  // fragments cannot span atoms.
  if (getAssembler().isSymbolLinkerVisible(SD.getSymbol()))
    insert(new MCDataFragment());

  // FIXME: This is wasteful, we don't necessarily need to create a data
  // fragment. Instead, we should mark the symbol as pointing into the data
  // fragment if it exists, otherwise we should just queue the label and set its
  // fragment pointer when we emit the next fragment.
  MCDataFragment *F = getOrCreateDataFragment();
  assert(!SD.getFragment() && "Unexpected fragment on symbol data!");
  SD.setFragment(F);
  SD.setOffset(F->getContents().size());
}


void MCPureStreamer::EmitDebugLabel(MCSymbol *Symbol) {
  EmitLabel(Symbol);
}

void MCPureStreamer::EmitZerofill(const MCSection *Section, MCSymbol *Symbol,
                                  uint64_t Size, unsigned ByteAlignment) {
  report_fatal_error("not yet implemented in pure streamer");
}

void MCPureStreamer::EmitBytes(StringRef Data) {
  // TODO: This is exactly the same as WinCOFFStreamer. Consider merging into
  // MCObjectStreamer.
  getOrCreateDataFragment()->getContents().append(Data.begin(), Data.end());
}

void MCPureStreamer::EmitValueToAlignment(unsigned ByteAlignment,
                                          int64_t Value, unsigned ValueSize,
                                          unsigned MaxBytesToEmit) {
  // TODO: This is exactly the same as WinCOFFStreamer. Consider merging into
  // MCObjectStreamer.
  if (MaxBytesToEmit == 0)
    MaxBytesToEmit = ByteAlignment;
  insert(new MCAlignFragment(ByteAlignment, Value, ValueSize, MaxBytesToEmit));

  // Update the maximum alignment on the current section if necessary.
  if (ByteAlignment > getCurrentSectionData()->getAlignment())
    getCurrentSectionData()->setAlignment(ByteAlignment);
}

void MCPureStreamer::EmitCodeAlignment(unsigned ByteAlignment,
                                       unsigned MaxBytesToEmit) {
  // TODO: This is exactly the same as WinCOFFStreamer. Consider merging into
  // MCObjectStreamer.
  if (MaxBytesToEmit == 0)
    MaxBytesToEmit = ByteAlignment;
  MCAlignFragment *F = new MCAlignFragment(ByteAlignment, 0, 1, MaxBytesToEmit);
  insert(F);
  F->setEmitNops(true);

  // Update the maximum alignment on the current section if necessary.
  if (ByteAlignment > getCurrentSectionData()->getAlignment())
    getCurrentSectionData()->setAlignment(ByteAlignment);
}

bool MCPureStreamer::EmitValueToOffset(const MCExpr *Offset,
                                       unsigned char Value) {
  insert(new MCOrgFragment(*Offset, Value));
  return false;
}

void MCPureStreamer::EmitInstToFragment(const MCInst &Inst) {
  MCRelaxableFragment *IF = new MCRelaxableFragment(Inst);
  insert(IF);

  // Add the fixups and data.
  //
  // FIXME: Revisit this design decision when relaxation is done, we may be
  // able to get away with not storing any extra data in the MCInst.
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  raw_svector_ostream VecOS(Code);
  getAssembler().getEmitter().EncodeInstruction(Inst, VecOS, Fixups);
  VecOS.flush();

  IF->getContents() = Code;
  IF->getFixups() = Fixups;
}

void MCPureStreamer::EmitInstToData(const MCInst &Inst) {
  MCDataFragment *DF = getOrCreateDataFragment();

  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  raw_svector_ostream VecOS(Code);
  getAssembler().getEmitter().EncodeInstruction(Inst, VecOS, Fixups);
  VecOS.flush();

  // Add the fixups and data.
  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    Fixups[i].setOffset(Fixups[i].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[i]);
  }
  DF->getContents().append(Code.begin(), Code.end());
}

void MCPureStreamer::FinishImpl() {
  // FIXME: Handle DWARF tables?

  this->MCObjectStreamer::FinishImpl();
}

MCStreamer *llvm::createPureStreamer(MCContext &Context, MCAsmBackend &MAB,
                                     raw_ostream &OS, MCCodeEmitter *CE) {
  return new MCPureStreamer(Context, MAB, OS, CE);
}
