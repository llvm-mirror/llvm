//===- lib/MC/MCELFStreamer.cpp - ELF Object Output -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits ELF .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELF.h"
#include "llvm/MC/MCELFSymbolFlags.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCELFStreamer::~MCELFStreamer() {
}

void MCELFStreamer::InitSections(bool NoExecStack) {
  // This emulates the same behavior of GNU as. This makes it easier
  // to compare the output as the major sections are in the same order.
  MCContext &Ctx = getContext();
  SwitchSection(Ctx.getObjectFileInfo()->getTextSection());
  EmitCodeAlignment(4);

  SwitchSection(Ctx.getObjectFileInfo()->getDataSection());
  EmitCodeAlignment(4);

  SwitchSection(Ctx.getObjectFileInfo()->getBSSSection());
  EmitCodeAlignment(4);

  SwitchSection(Ctx.getObjectFileInfo()->getTextSection());

  if (NoExecStack)
    SwitchSection(Ctx.getAsmInfo()->getNonexecutableStackSection(Ctx));
}

void MCELFStreamer::EmitLabel(MCSymbol *Symbol) {
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");

  MCObjectStreamer::EmitLabel(Symbol);

  const MCSectionELF &Section =
    static_cast<const MCSectionELF&>(Symbol->getSection());
  MCSymbolData &SD = getAssembler().getSymbolData(*Symbol);
  if (Section.getFlags() & ELF::SHF_TLS)
    MCELF::SetType(SD, ELF::STT_TLS);
}

void MCELFStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);
  // Do any generic stuff we need to do.
  switch (Flag) {
  case MCAF_SyntaxUnified: return; // no-op here.
  case MCAF_Code16: return; // Change parsing mode; no-op here.
  case MCAF_Code32: return; // Change parsing mode; no-op here.
  case MCAF_Code64: return; // Change parsing mode; no-op here.
  case MCAF_SubsectionsViaSymbols:
    getAssembler().setSubsectionsViaSymbols(true);
    return;
  }

  llvm_unreachable("invalid assembler flag!");
}

void MCELFStreamer::ChangeSection(const MCSection *Section,
                                  const MCExpr *Subsection) {
  MCSectionData *CurSection = getCurrentSectionData();
  if (CurSection && CurSection->isBundleLocked())
    report_fatal_error("Unterminated .bundle_lock when changing a section");

  MCAssembler &Asm = getAssembler();
  auto *SectionELF = static_cast<const MCSectionELF *>(Section);
  const MCSymbol *Grp = SectionELF->getGroup();
  if (Grp)
    Asm.getOrCreateSymbolData(*Grp);

  this->MCObjectStreamer::ChangeSection(Section, Subsection);
  MCSymbol *SectionSymbol = getContext().getOrCreateSectionSymbol(*SectionELF);
  if (SectionSymbol->isUndefined()) {
    EmitLabel(SectionSymbol);
    MCELF::SetType(Asm.getSymbolData(*SectionSymbol), ELF::STT_SECTION);
  }
}

void MCELFStreamer::EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {
  getAssembler().getOrCreateSymbolData(*Symbol);
  const MCExpr *Value = MCSymbolRefExpr::Create(
      Symbol, MCSymbolRefExpr::VK_WEAKREF, getContext());
  Alias->setVariableValue(Value);
}

// When GNU as encounters more than one .type declaration for an object it seems
// to use a mechanism similar to the one below to decide which type is actually
// used in the object file.  The greater of T1 and T2 is selected based on the
// following ordering:
//  STT_NOTYPE < STT_OBJECT < STT_FUNC < STT_GNU_IFUNC < STT_TLS < anything else
// If neither T1 < T2 nor T2 < T1 according to this ordering, use T2 (the user
// provided type).
static unsigned CombineSymbolTypes(unsigned T1, unsigned T2) {
  unsigned TypeOrdering[] = {ELF::STT_NOTYPE, ELF::STT_OBJECT, ELF::STT_FUNC,
                             ELF::STT_GNU_IFUNC, ELF::STT_TLS};
  for (unsigned i = 0; i != array_lengthof(TypeOrdering); ++i) {
    if (T1 == TypeOrdering[i])
      return T2;
    if (T2 == TypeOrdering[i])
      return T1;
  }

  return T2;
}

bool MCELFStreamer::EmitSymbolAttribute(MCSymbol *Symbol,
                                        MCSymbolAttr Attribute) {
  // Indirect symbols are handled differently, to match how 'as' handles
  // them. This makes writing matching .o files easier.
  if (Attribute == MCSA_IndirectSymbol) {
    // Note that we intentionally cannot use the symbol data here; this is
    // important for matching the string table that 'as' generates.
    IndirectSymbolData ISD;
    ISD.Symbol = Symbol;
    ISD.SectionData = getCurrentSectionData();
    getAssembler().getIndirectSymbols().push_back(ISD);
    return true;
  }

  // Adding a symbol attribute always introduces the symbol, note that an
  // important side effect of calling getOrCreateSymbolData here is to register
  // the symbol with the assembler.
  MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);

  // The implementation of symbol attributes is designed to match 'as', but it
  // leaves much to desired. It doesn't really make sense to arbitrarily add and
  // remove flags, but 'as' allows this (in particular, see .desc).
  //
  // In the future it might be worth trying to make these operations more well
  // defined.
  switch (Attribute) {
  case MCSA_LazyReference:
  case MCSA_Reference:
  case MCSA_SymbolResolver:
  case MCSA_PrivateExtern:
  case MCSA_WeakDefinition:
  case MCSA_WeakDefAutoPrivate:
  case MCSA_Invalid:
  case MCSA_IndirectSymbol:
    return false;

  case MCSA_NoDeadStrip:
    // Ignore for now.
    break;

  case MCSA_ELF_TypeGnuUniqueObject:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD), ELF::STT_OBJECT));
    MCELF::SetBinding(SD, ELF::STB_GNU_UNIQUE);
    SD.setExternal(true);
    BindingExplicitlySet.insert(Symbol);
    break;

  case MCSA_Global:
    MCELF::SetBinding(SD, ELF::STB_GLOBAL);
    SD.setExternal(true);
    BindingExplicitlySet.insert(Symbol);
    break;

  case MCSA_WeakReference:
  case MCSA_Weak:
    MCELF::SetBinding(SD, ELF::STB_WEAK);
    SD.setExternal(true);
    BindingExplicitlySet.insert(Symbol);
    break;

  case MCSA_Local:
    MCELF::SetBinding(SD, ELF::STB_LOCAL);
    SD.setExternal(false);
    BindingExplicitlySet.insert(Symbol);
    break;

  case MCSA_ELF_TypeFunction:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_FUNC));
    break;

  case MCSA_ELF_TypeIndFunction:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_GNU_IFUNC));
    break;

  case MCSA_ELF_TypeObject:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_OBJECT));
    break;

  case MCSA_ELF_TypeTLS:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_TLS));
    break;

  case MCSA_ELF_TypeCommon:
    // TODO: Emit these as a common symbol.
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_OBJECT));
    break;

  case MCSA_ELF_TypeNoType:
    MCELF::SetType(SD, CombineSymbolTypes(MCELF::GetType(SD),
                                          ELF::STT_NOTYPE));
    break;

  case MCSA_Protected:
    MCELF::SetVisibility(SD, ELF::STV_PROTECTED);
    break;

  case MCSA_Hidden:
    MCELF::SetVisibility(SD, ELF::STV_HIDDEN);
    break;

  case MCSA_Internal:
    MCELF::SetVisibility(SD, ELF::STV_INTERNAL);
    break;
  }

  return true;
}

void MCELFStreamer::EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       unsigned ByteAlignment) {
  MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);

  if (!BindingExplicitlySet.count(Symbol)) {
    MCELF::SetBinding(SD, ELF::STB_GLOBAL);
    SD.setExternal(true);
  }

  MCELF::SetType(SD, ELF::STT_OBJECT);

  if (MCELF::GetBinding(SD) == ELF_STB_Local) {
    const MCSection *Section = getAssembler().getContext().getELFSection(
        ".bss", ELF::SHT_NOBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);

    AssignSection(Symbol, Section);

    struct LocalCommon L = {&SD, Size, ByteAlignment};
    LocalCommons.push_back(L);
  } else {
    SD.setCommon(Size, ByteAlignment);
  }

  SD.setSize(MCConstantExpr::Create(Size, getContext()));
}

void MCELFStreamer::EmitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);
  SD.setSize(Value);
}

void MCELFStreamer::EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                          unsigned ByteAlignment) {
  // FIXME: Should this be caught and done earlier?
  MCSymbolData &SD = getAssembler().getOrCreateSymbolData(*Symbol);
  MCELF::SetBinding(SD, ELF::STB_LOCAL);
  SD.setExternal(false);
  BindingExplicitlySet.insert(Symbol);
  EmitCommonSymbol(Symbol, Size, ByteAlignment);
}

void MCELFStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size,
                                  const SMLoc &Loc) {
  if (getCurrentSectionData()->isBundleLocked())
    report_fatal_error("Emitting values inside a locked bundle is forbidden");
  fixSymbolsInTLSFixups(Value);
  MCObjectStreamer::EmitValueImpl(Value, Size, Loc);
}

void MCELFStreamer::EmitValueToAlignment(unsigned ByteAlignment,
                                         int64_t Value,
                                         unsigned ValueSize,
                                         unsigned MaxBytesToEmit) {
  if (getCurrentSectionData()->isBundleLocked())
    report_fatal_error("Emitting values inside a locked bundle is forbidden");
  MCObjectStreamer::EmitValueToAlignment(ByteAlignment, Value,
                                         ValueSize, MaxBytesToEmit);
}

// Add a symbol for the file name of this module. They start after the
// null symbol and don't count as normal symbol, i.e. a non-STT_FILE symbol
// with the same name may appear.
void MCELFStreamer::EmitFileDirective(StringRef Filename) {
  getAssembler().addFileName(Filename);
}

void MCELFStreamer::EmitIdent(StringRef IdentString) {
  const MCSection *Comment = getAssembler().getContext().getELFSection(
      ".comment", ELF::SHT_PROGBITS, ELF::SHF_MERGE | ELF::SHF_STRINGS, 1, "");
  PushSection();
  SwitchSection(Comment);
  if (!SeenIdent) {
    EmitIntValue(0, 1);
    SeenIdent = true;
  }
  EmitBytes(IdentString);
  EmitIntValue(0, 1);
  PopSection();
}

void MCELFStreamer::fixSymbolsInTLSFixups(const MCExpr *expr) {
  switch (expr->getKind()) {
  case MCExpr::Target:
    cast<MCTargetExpr>(expr)->fixELFSymbolsInTLSFixups(getAssembler());
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *be = cast<MCBinaryExpr>(expr);
    fixSymbolsInTLSFixups(be->getLHS());
    fixSymbolsInTLSFixups(be->getRHS());
    break;
  }

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &symRef = *cast<MCSymbolRefExpr>(expr);
    switch (symRef.getKind()) {
    default:
      return;
    case MCSymbolRefExpr::VK_GOTTPOFF:
    case MCSymbolRefExpr::VK_INDNTPOFF:
    case MCSymbolRefExpr::VK_NTPOFF:
    case MCSymbolRefExpr::VK_GOTNTPOFF:
    case MCSymbolRefExpr::VK_TLSGD:
    case MCSymbolRefExpr::VK_TLSLD:
    case MCSymbolRefExpr::VK_TLSLDM:
    case MCSymbolRefExpr::VK_TPOFF:
    case MCSymbolRefExpr::VK_DTPOFF:
    case MCSymbolRefExpr::VK_Mips_TLSGD:
    case MCSymbolRefExpr::VK_Mips_GOTTPREL:
    case MCSymbolRefExpr::VK_Mips_TPREL_HI:
    case MCSymbolRefExpr::VK_Mips_TPREL_LO:
    case MCSymbolRefExpr::VK_PPC_DTPMOD:
    case MCSymbolRefExpr::VK_PPC_TPREL:
    case MCSymbolRefExpr::VK_PPC_TPREL_LO:
    case MCSymbolRefExpr::VK_PPC_TPREL_HI:
    case MCSymbolRefExpr::VK_PPC_TPREL_HA:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHER:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHERA:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHEST:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHESTA:
    case MCSymbolRefExpr::VK_PPC_DTPREL:
    case MCSymbolRefExpr::VK_PPC_DTPREL_LO:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HI:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHER:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHERA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHEST:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHESTA:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_HA:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_HA:
    case MCSymbolRefExpr::VK_PPC_TLS:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_HA:
    case MCSymbolRefExpr::VK_PPC_TLSGD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_HA:
    case MCSymbolRefExpr::VK_PPC_TLSLD:
      break;
    }
    MCSymbolData &SD = getAssembler().getOrCreateSymbolData(symRef.getSymbol());
    MCELF::SetType(SD, ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixSymbolsInTLSFixups(cast<MCUnaryExpr>(expr)->getSubExpr());
    break;
  }
}

void MCELFStreamer::EmitInstToFragment(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  this->MCObjectStreamer::EmitInstToFragment(Inst, STI);
  MCRelaxableFragment &F = *cast<MCRelaxableFragment>(getCurrentFragment());

  for (unsigned i = 0, e = F.getFixups().size(); i != e; ++i)
    fixSymbolsInTLSFixups(F.getFixups()[i].getValue());
}

void MCELFStreamer::EmitInstToData(const MCInst &Inst,
                                   const MCSubtargetInfo &STI) {
  MCAssembler &Assembler = getAssembler();
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  raw_svector_ostream VecOS(Code);
  Assembler.getEmitter().EncodeInstruction(Inst, VecOS, Fixups, STI);
  VecOS.flush();

  for (unsigned i = 0, e = Fixups.size(); i != e; ++i)
    fixSymbolsInTLSFixups(Fixups[i].getValue());

  // There are several possibilities here:
  //
  // If bundling is disabled, append the encoded instruction to the current data
  // fragment (or create a new such fragment if the current fragment is not a
  // data fragment).
  //
  // If bundling is enabled:
  // - If we're not in a bundle-locked group, emit the instruction into a
  //   fragment of its own. If there are no fixups registered for the
  //   instruction, emit a MCCompactEncodedInstFragment. Otherwise, emit a
  //   MCDataFragment.
  // - If we're in a bundle-locked group, append the instruction to the current
  //   data fragment because we want all the instructions in a group to get into
  //   the same fragment. Be careful not to do that for the first instruction in
  //   the group, though.
  MCDataFragment *DF;

  if (Assembler.isBundlingEnabled()) {
    MCSectionData *SD = getCurrentSectionData();
    if (SD->isBundleLocked() && !SD->isBundleGroupBeforeFirstInst())
      // If we are bundle-locked, we re-use the current fragment.
      // The bundle-locking directive ensures this is a new data fragment.
      DF = cast<MCDataFragment>(getCurrentFragment());
    else if (!SD->isBundleLocked() && Fixups.size() == 0) {
      // Optimize memory usage by emitting the instruction to a
      // MCCompactEncodedInstFragment when not in a bundle-locked group and
      // there are no fixups registered.
      MCCompactEncodedInstFragment *CEIF = new MCCompactEncodedInstFragment();
      insert(CEIF);
      CEIF->getContents().append(Code.begin(), Code.end());
      return;
    } else {
      DF = new MCDataFragment();
      insert(DF);
    }
    if (SD->getBundleLockState() == MCSectionData::BundleLockedAlignToEnd) {
      // If this fragment is for a group marked "align_to_end", set a flag
      // in the fragment. This can happen after the fragment has already been
      // created if there are nested bundle_align groups and an inner one
      // is the one marked align_to_end.
      DF->setAlignToBundleEnd(true);
    }

    // We're now emitting an instruction in a bundle group, so this flag has
    // to be turned off.
    SD->setBundleGroupBeforeFirstInst(false);
  } else {
    DF = getOrCreateDataFragment();
  }

  // Add the fixups and data.
  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    Fixups[i].setOffset(Fixups[i].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[i]);
  }
  DF->setHasInstructions(true);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCELFStreamer::EmitBundleAlignMode(unsigned AlignPow2) {
  assert(AlignPow2 <= 30 && "Invalid bundle alignment");
  MCAssembler &Assembler = getAssembler();
  if (AlignPow2 > 0 && (Assembler.getBundleAlignSize() == 0 ||
                        Assembler.getBundleAlignSize() == 1U << AlignPow2))
    Assembler.setBundleAlignSize(1U << AlignPow2);
  else
    report_fatal_error(".bundle_align_mode cannot be changed once set");
}

void MCELFStreamer::EmitBundleLock(bool AlignToEnd) {
  MCSectionData *SD = getCurrentSectionData();

  // Sanity checks
  //
  if (!getAssembler().isBundlingEnabled())
    report_fatal_error(".bundle_lock forbidden when bundling is disabled");

  if (!SD->isBundleLocked())
    SD->setBundleGroupBeforeFirstInst(true);

  SD->setBundleLockState(AlignToEnd ? MCSectionData::BundleLockedAlignToEnd :
                                      MCSectionData::BundleLocked);
}

void MCELFStreamer::EmitBundleUnlock() {
  MCSectionData *SD = getCurrentSectionData();

  // Sanity checks
  if (!getAssembler().isBundlingEnabled())
    report_fatal_error(".bundle_unlock forbidden when bundling is disabled");
  else if (!SD->isBundleLocked())
    report_fatal_error(".bundle_unlock without matching lock");
  else if (SD->isBundleGroupBeforeFirstInst())
    report_fatal_error("Empty bundle-locked group is forbidden");

  SD->setBundleLockState(MCSectionData::NotBundleLocked);
}

void MCELFStreamer::Flush() {
  for (std::vector<LocalCommon>::const_iterator i = LocalCommons.begin(),
                                                e = LocalCommons.end();
       i != e; ++i) {
    MCSymbolData *SD = i->SD;
    uint64_t Size = i->Size;
    unsigned ByteAlignment = i->ByteAlignment;
    const MCSymbol &Symbol = SD->getSymbol();
    const MCSection &Section = Symbol.getSection();

    MCSectionData &SectData = getAssembler().getOrCreateSectionData(Section);
    new MCAlignFragment(ByteAlignment, 0, 1, ByteAlignment, &SectData);

    MCFragment *F = new MCFillFragment(0, 0, Size, &SectData);
    SD->setFragment(F);

    // Update the maximum alignment of the section if necessary.
    if (ByteAlignment > SectData.getAlignment())
      SectData.setAlignment(ByteAlignment);
  }

  LocalCommons.clear();
}

void MCELFStreamer::FinishImpl() {
  EmitFrames(nullptr);

  Flush();

  this->MCObjectStreamer::FinishImpl();
}

MCStreamer *llvm::createELFStreamer(MCContext &Context, MCAsmBackend &MAB,
                                    raw_ostream &OS, MCCodeEmitter *CE,
                                    bool RelaxAll) {
  MCELFStreamer *S = new MCELFStreamer(Context, MAB, OS, CE);
  if (RelaxAll)
    S->getAssembler().setRelaxAll(true);
  return S;
}

void MCELFStreamer::EmitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("Generic ELF doesn't support this directive");
}

void MCELFStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::BeginCOFFSymbolDef(const MCSymbol *Symbol) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitCOFFSymbolStorageClass(int StorageClass) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitCOFFSymbolType(int Type) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EndCOFFSymbolDef() {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitZerofill(const MCSection *Section, MCSymbol *Symbol,
                                 uint64_t Size, unsigned ByteAlignment) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitTBSSSymbol(const MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, unsigned ByteAlignment) {
  llvm_unreachable("ELF doesn't support this directive");
}
