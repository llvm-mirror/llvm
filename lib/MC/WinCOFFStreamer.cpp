//===-- llvm/MC/WinCOFFStreamer.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains an implementation of a Win32 COFF object file streamer.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "WinCOFFStreamer"

#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/Support/COFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class WinCOFFStreamer : public MCObjectStreamer {
public:
  MCSymbol const *CurSymbol;

  WinCOFFStreamer(MCContext &Context,
                  MCAsmBackend &MAB,
                  MCCodeEmitter &CE,
                  raw_ostream &OS);

  void AddCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                       unsigned ByteAlignment, bool External);

  // MCStreamer interface

  virtual void InitSections();
  virtual void InitToTextSection();
  virtual void EmitLabel(MCSymbol *Symbol);
  virtual void EmitDebugLabel(MCSymbol *Symbol);
  virtual void EmitAssemblerFlag(MCAssemblerFlag Flag);
  virtual void EmitThumbFunc(MCSymbol *Func);
  virtual void EmitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute);
  virtual void EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue);
  virtual void BeginCOFFSymbolDef(MCSymbol const *Symbol);
  virtual void EmitCOFFSymbolStorageClass(int StorageClass);
  virtual void EmitCOFFSymbolType(int Type);
  virtual void EndCOFFSymbolDef();
  virtual void EmitCOFFSecRel32(MCSymbol const *Symbol);
  virtual void EmitELFSize(MCSymbol *Symbol, const MCExpr *Value);
  virtual void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                unsigned ByteAlignment);
  virtual void EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     unsigned ByteAlignment);
  virtual void EmitZerofill(const MCSection *Section, MCSymbol *Symbol,
                            uint64_t Size,unsigned ByteAlignment);
  virtual void EmitTBSSSymbol(const MCSection *Section, MCSymbol *Symbol,
                              uint64_t Size, unsigned ByteAlignment);
  virtual void EmitFileDirective(StringRef Filename);
  virtual void EmitWin64EHHandlerData();
  virtual void FinishImpl();

  static bool classof(const MCStreamer *S) {
    return S->getKind() == SK_WinCOFFStreamer;
  }

private:
  virtual void EmitInstToData(const MCInst &Inst) {
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

  void SetSection(StringRef Section,
                  unsigned Characteristics,
                  SectionKind Kind) {
    SwitchSection(getContext().getCOFFSection(Section, Characteristics, Kind));
  }

  void SetSectionText() {
    SetSection(".text",
               COFF::IMAGE_SCN_CNT_CODE
             | COFF::IMAGE_SCN_MEM_EXECUTE
             | COFF::IMAGE_SCN_MEM_READ,
               SectionKind::getText());
    EmitCodeAlignment(4, 0);
  }

  void SetSectionData() {
    SetSection(".data",
               COFF::IMAGE_SCN_CNT_INITIALIZED_DATA
             | COFF::IMAGE_SCN_MEM_READ
             | COFF::IMAGE_SCN_MEM_WRITE,
               SectionKind::getDataRel());
    EmitCodeAlignment(4, 0);
  }

  void SetSectionBSS() {
    SetSection(".bss",
               COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA
             | COFF::IMAGE_SCN_MEM_READ
             | COFF::IMAGE_SCN_MEM_WRITE,
               SectionKind::getBSS());
    EmitCodeAlignment(4, 0);
  }
};
} // end anonymous namespace.

WinCOFFStreamer::WinCOFFStreamer(MCContext &Context, MCAsmBackend &MAB,
                                 MCCodeEmitter &CE, raw_ostream &OS)
    : MCObjectStreamer(SK_WinCOFFStreamer, Context, MAB, OS, &CE),
      CurSymbol(NULL) {}

void WinCOFFStreamer::AddCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                      unsigned ByteAlignment, bool External) {
  assert(!Symbol->isInSection() && "Symbol must not already have a section!");

  std::string SectionName(".bss$linkonce");
  SectionName.append(Symbol->getName().begin(), Symbol->getName().end());

  MCSymbolData &SymbolData = getAssembler().getOrCreateSymbolData(*Symbol);

  unsigned Characteristics =
    COFF::IMAGE_SCN_LNK_COMDAT |
    COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
    COFF::IMAGE_SCN_MEM_READ |
    COFF::IMAGE_SCN_MEM_WRITE;

  int Selection = COFF::IMAGE_COMDAT_SELECT_LARGEST;

  const MCSection *Section = MCStreamer::getContext().getCOFFSection(
    SectionName, Characteristics, SectionKind::getBSS(), Selection);

  MCSectionData &SectionData = getAssembler().getOrCreateSectionData(*Section);

  if (SectionData.getAlignment() < ByteAlignment)
    SectionData.setAlignment(ByteAlignment);

  SymbolData.setExternal(External);

  Symbol->setSection(*Section);

  if (ByteAlignment != 1)
      new MCAlignFragment(ByteAlignment, 0, 0, ByteAlignment, &SectionData);

  SymbolData.setFragment(new MCFillFragment(0, 0, Size, &SectionData));
}

// MCStreamer interface

void WinCOFFStreamer::InitToTextSection() {
  SetSectionText();
}

void WinCOFFStreamer::InitSections() {
  SetSectionText();
  SetSectionData();
  SetSectionBSS();
  SetSectionText();
}

void WinCOFFStreamer::EmitLabel(MCSymbol *Symbol) {
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");
  MCObjectStreamer::EmitLabel(Symbol);
}

void WinCOFFStreamer::EmitDebugLabel(MCSymbol *Symbol) {
  EmitLabel(Symbol);
}
void WinCOFFStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::EmitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::EmitSymbolAttribute(MCSymbol *Symbol,
                                          MCSymbolAttr Attribute) {
  assert(Symbol && "Symbol must be non-null!");
  assert((Symbol->isInSection()
         ? Symbol->getSection().getVariant() == MCSection::SV_COFF
         : true) && "Got non COFF section in the COFF backend!");
  switch (Attribute) {
  case MCSA_WeakReference:
  case MCSA_Weak: {
      MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);
      SD.modifyFlags(COFF::SF_WeakExternal, COFF::SF_WeakExternal);
      SD.setExternal(true);
    }
    break;

  case MCSA_Global:
    getAssembler().getOrCreateSymbolData(*Symbol).setExternal(true);
    break;

  default:
    llvm_unreachable("unsupported attribute");
  }
}

void WinCOFFStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::BeginCOFFSymbolDef(MCSymbol const *Symbol) {
  assert((Symbol->isInSection()
         ? Symbol->getSection().getVariant() == MCSection::SV_COFF
         : true) && "Got non COFF section in the COFF backend!");
  assert(CurSymbol == NULL && "EndCOFFSymbolDef must be called between calls "
                              "to BeginCOFFSymbolDef!");
  CurSymbol = Symbol;
}

void WinCOFFStreamer::EmitCOFFSymbolStorageClass(int StorageClass) {
  assert(CurSymbol != NULL && "BeginCOFFSymbolDef must be called first!");
  assert((StorageClass & ~0xFF) == 0 && "StorageClass must only have data in "
                                        "the first byte!");

  getAssembler().getOrCreateSymbolData(*CurSymbol).modifyFlags(
    StorageClass << COFF::SF_ClassShift,
    COFF::SF_ClassMask);
}

void WinCOFFStreamer::EmitCOFFSymbolType(int Type) {
  assert(CurSymbol != NULL && "BeginCOFFSymbolDef must be called first!");
  assert((Type & ~0xFFFF) == 0 && "Type must only have data in the first 2 "
                                  "bytes");

  getAssembler().getOrCreateSymbolData(*CurSymbol).modifyFlags(
    Type << COFF::SF_TypeShift,
    COFF::SF_TypeMask);
}

void WinCOFFStreamer::EndCOFFSymbolDef() {
  assert(CurSymbol != NULL && "BeginCOFFSymbolDef must be called first!");
  CurSymbol = NULL;
}

void WinCOFFStreamer::EmitCOFFSecRel32(MCSymbol const *Symbol)
{
  MCDataFragment *DF = getOrCreateDataFragment();

  DF->getFixups().push_back(
      MCFixup::Create(DF->getContents().size(),
                      MCSymbolRefExpr::Create (Symbol, getContext ()),
                      FK_SecRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

void WinCOFFStreamer::EmitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       unsigned ByteAlignment) {
  assert((Symbol->isInSection()
         ? Symbol->getSection().getVariant() == MCSection::SV_COFF
         : true) && "Got non COFF section in the COFF backend!");
  AddCommonSymbol(Symbol, Size, ByteAlignment, true);
}

void WinCOFFStreamer::EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                            unsigned ByteAlignment) {
  assert((Symbol->isInSection()
         ? Symbol->getSection().getVariant() == MCSection::SV_COFF
         : true) && "Got non COFF section in the COFF backend!");
  AddCommonSymbol(Symbol, Size, ByteAlignment, false);
}

void WinCOFFStreamer::EmitZerofill(const MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size,unsigned ByteAlignment) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::EmitTBSSSymbol(const MCSection *Section, MCSymbol *Symbol,
                                     uint64_t Size, unsigned ByteAlignment) {
  llvm_unreachable("not implemented");
}

void WinCOFFStreamer::EmitFileDirective(StringRef Filename) {
  // Ignore for now, linkers don't care, and proper debug
  // info will be a much large effort.
}

void WinCOFFStreamer::EmitWin64EHHandlerData() {
  MCStreamer::EmitWin64EHHandlerData();

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  MCWin64EHUnwindEmitter::EmitUnwindInfo(*this, getCurrentW64UnwindInfo());
}

void WinCOFFStreamer::FinishImpl() {
  EmitW64Tables();
  MCObjectStreamer::FinishImpl();
}

namespace llvm
{
  MCStreamer *createWinCOFFStreamer(MCContext &Context,
                                    MCAsmBackend &MAB,
                                    MCCodeEmitter &CE,
                                    raw_ostream &OS,
                                    bool RelaxAll) {
    WinCOFFStreamer *S = new WinCOFFStreamer(Context, MAB, CE, OS);
    S->getAssembler().setRelaxAll(RelaxAll);
    return S;
  }
}
