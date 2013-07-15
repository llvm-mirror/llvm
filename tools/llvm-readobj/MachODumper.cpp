//===-- MachODump.cpp - Object file dumping utility for llvm --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MachO-specific dumper for llvm-readobj.
//
//===----------------------------------------------------------------------===//

#include "llvm-readobj.h"
#include "Error.h"
#include "ObjDumper.h"
#include "StreamWriter.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Casting.h"

using namespace llvm;
using namespace object;

namespace {

class MachODumper : public ObjDumper {
public:
  MachODumper(const MachOObjectFile *Obj, StreamWriter& Writer)
    : ObjDumper(Writer)
    , Obj(Obj) { }

  virtual void printFileHeaders() LLVM_OVERRIDE;
  virtual void printSections() LLVM_OVERRIDE;
  virtual void printRelocations() LLVM_OVERRIDE;
  virtual void printSymbols() LLVM_OVERRIDE;
  virtual void printDynamicSymbols() LLVM_OVERRIDE;
  virtual void printUnwindInfo() LLVM_OVERRIDE;

private:
  void printSymbol(symbol_iterator SymI);

  void printRelocation(section_iterator SecI, relocation_iterator RelI);

  void printRelocation(const MachOObjectFile *Obj,
                       section_iterator SecI, relocation_iterator RelI);

  void printSections(const MachOObjectFile *Obj);

  const MachOObjectFile *Obj;
};

} // namespace


namespace llvm {

error_code createMachODumper(const object::ObjectFile *Obj,
                             StreamWriter& Writer,
                             OwningPtr<ObjDumper> &Result) {
  const MachOObjectFile *MachOObj = dyn_cast<MachOObjectFile>(Obj);
  if (!MachOObj)
    return readobj_error::unsupported_obj_file_format;

  Result.reset(new MachODumper(MachOObj, Writer));
  return readobj_error::success;
}

} // namespace llvm


static const EnumEntry<unsigned> MachOSectionTypes[] = {
  { "Regular"                        , 0x00 },
  { "ZeroFill"                       , 0x01 },
  { "CStringLiterals"                , 0x02 },
  { "4ByteLiterals"                  , 0x03 },
  { "8ByteLiterals"                  , 0x04 },
  { "LiteralPointers"                , 0x05 },
  { "NonLazySymbolPointers"          , 0x06 },
  { "LazySymbolPointers"             , 0x07 },
  { "SymbolStubs"                    , 0x08 },
  { "ModInitFuncs"                   , 0x09 },
  { "ModTermFuncs"                   , 0x0A },
  { "Coalesced"                      , 0x0B },
  { "GBZeroFill"                     , 0x0C },
  { "Interposing"                    , 0x0D },
  { "16ByteLiterals"                 , 0x0E },
  { "DTraceDOF"                      , 0x0F },
  { "LazyDylibSymbolPoints"          , 0x10 },
  { "ThreadLocalRegular"             , 0x11 },
  { "ThreadLocalZerofill"            , 0x12 },
  { "ThreadLocalVariables"           , 0x13 },
  { "ThreadLocalVariablePointers"    , 0x14 },
  { "ThreadLocalInitFunctionPointers", 0x15 }
};

static const EnumEntry<unsigned> MachOSectionAttributes[] = {
  { "LocReloc"         , 1 <<  0 /*S_ATTR_LOC_RELOC          */ },
  { "ExtReloc"         , 1 <<  1 /*S_ATTR_EXT_RELOC          */ },
  { "SomeInstructions" , 1 <<  2 /*S_ATTR_SOME_INSTRUCTIONS  */ },
  { "Debug"            , 1 << 17 /*S_ATTR_DEBUG              */ },
  { "SelfModifyingCode", 1 << 18 /*S_ATTR_SELF_MODIFYING_CODE*/ },
  { "LiveSupport"      , 1 << 19 /*S_ATTR_LIVE_SUPPORT       */ },
  { "NoDeadStrip"      , 1 << 20 /*S_ATTR_NO_DEAD_STRIP      */ },
  { "StripStaticSyms"  , 1 << 21 /*S_ATTR_STRIP_STATIC_SYMS  */ },
  { "NoTOC"            , 1 << 22 /*S_ATTR_NO_TOC             */ },
  { "PureInstructions" , 1 << 23 /*S_ATTR_PURE_INSTRUCTIONS  */ },
};

static const EnumEntry<unsigned> MachOSymbolRefTypes[] = {
  { "UndefinedNonLazy",                     0 },
  { "ReferenceFlagUndefinedLazy",           1 },
  { "ReferenceFlagDefined",                 2 },
  { "ReferenceFlagPrivateDefined",          3 },
  { "ReferenceFlagPrivateUndefinedNonLazy", 4 },
  { "ReferenceFlagPrivateUndefinedLazy",    5 }
};

static const EnumEntry<unsigned> MachOSymbolFlags[] = {
  { "ReferencedDynamically", 0x10 },
  { "NoDeadStrip",           0x20 },
  { "WeakRef",               0x40 },
  { "WeakDef",               0x80 }
};

static const EnumEntry<unsigned> MachOSymbolTypes[] = {
  { "Undef",           0x0 },
  { "External",        0x1 },
  { "Abs",             0x2 },
  { "Indirect",        0xA },
  { "PreboundUndef",   0xC },
  { "Section",         0xE },
  { "PrivateExternal", 0x10 }
};

namespace {
  enum {
    N_STAB = 0xE0
  };

  struct MachOSection {
    ArrayRef<char> Name;
    ArrayRef<char> SegmentName;
    uint64_t Address;
    uint64_t Size;
    uint32_t Offset;
    uint32_t Alignment;
    uint32_t RelocationTableOffset;
    uint32_t NumRelocationTableEntries;
    uint32_t Flags;
    uint32_t Reserved1;
    uint32_t Reserved2;
  };

  struct MachOSymbol {
    uint32_t StringIndex;
    uint8_t Type;
    uint8_t SectionIndex;
    uint16_t Flags;
    uint64_t Value;
  };
}

static void getSection(const MachOObjectFile *Obj,
                       DataRefImpl Sec,
                       MachOSection &Section) {
  if (!Obj->is64Bit()) {
    macho::Section Sect = Obj->getSection(Sec);
    Section.Address     = Sect.Address;
    Section.Size        = Sect.Size;
    Section.Offset      = Sect.Offset;
    Section.Alignment   = Sect.Align;
    Section.RelocationTableOffset = Sect.RelocationTableOffset;
    Section.NumRelocationTableEntries = Sect.NumRelocationTableEntries;
    Section.Flags       = Sect.Flags;
    Section.Reserved1   = Sect.Reserved1;
    Section.Reserved2   = Sect.Reserved2;
    return;
  }
  macho::Section64 Sect = Obj->getSection64(Sec);
  Section.Address     = Sect.Address;
  Section.Size        = Sect.Size;
  Section.Offset      = Sect.Offset;
  Section.Alignment   = Sect.Align;
  Section.RelocationTableOffset = Sect.RelocationTableOffset;
  Section.NumRelocationTableEntries = Sect.NumRelocationTableEntries;
  Section.Flags       = Sect.Flags;
  Section.Reserved1   = Sect.Reserved1;
  Section.Reserved2   = Sect.Reserved2;
}


static void getSymbol(const MachOObjectFile *Obj,
                      DataRefImpl DRI,
                      MachOSymbol &Symbol) {
  if (!Obj->is64Bit()) {
    macho::SymbolTableEntry Entry = Obj->getSymbolTableEntry(DRI);
    Symbol.StringIndex  = Entry.StringIndex;
    Symbol.Type         = Entry.Type;
    Symbol.SectionIndex = Entry.SectionIndex;
    Symbol.Flags        = Entry.Flags;
    Symbol.Value        = Entry.Value;
    return;
  }
  macho::Symbol64TableEntry Entry = Obj->getSymbol64TableEntry(DRI);
  Symbol.StringIndex  = Entry.StringIndex;
  Symbol.Type         = Entry.Type;
  Symbol.SectionIndex = Entry.SectionIndex;
  Symbol.Flags        = Entry.Flags;
  Symbol.Value        = Entry.Value;
}

void MachODumper::printFileHeaders() {
  W.startLine() << "FileHeaders not implemented.\n";
}

void MachODumper::printSections() {
  return printSections(Obj);
}

void MachODumper::printSections(const MachOObjectFile *Obj) {
  ListScope Group(W, "Sections");

  int SectionIndex = -1;
  error_code EC;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    if (error(EC)) break;

    ++SectionIndex;

    MachOSection Section;
    getSection(Obj, SecI->getRawDataRefImpl(), Section);
    DataRefImpl DR = SecI->getRawDataRefImpl();

    StringRef Name;
    if (error(SecI->getName(Name)))
        Name = "";

    ArrayRef<char> RawName = Obj->getSectionRawName(DR);
    StringRef SegmentName = Obj->getSectionFinalSegmentName(DR);
    ArrayRef<char> RawSegmentName = Obj->getSectionRawFinalSegmentName(DR);

    DictScope SectionD(W, "Section");
    W.printNumber("Index", SectionIndex);
    W.printBinary("Name", Name, RawName);
    W.printBinary("Segment", SegmentName, RawSegmentName);
    W.printHex   ("Address", Section.Address);
    W.printHex   ("Size", Section.Size);
    W.printNumber("Offset", Section.Offset);
    W.printNumber("Alignment", Section.Alignment);
    W.printHex   ("RelocationOffset", Section.RelocationTableOffset);
    W.printNumber("RelocationCount", Section.NumRelocationTableEntries);
    W.printEnum  ("Type", Section.Flags & 0xFF,
                  makeArrayRef(MachOSectionAttributes));
    W.printFlags ("Attributes", Section.Flags >> 8,
                  makeArrayRef(MachOSectionAttributes));
    W.printHex   ("Reserved1", Section.Reserved1);
    W.printHex   ("Reserved2", Section.Reserved2);

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (relocation_iterator RelI = SecI->begin_relocations(),
                               RelE = SecI->end_relocations();
                               RelI != RelE; RelI.increment(EC)) {
        if (error(EC)) break;

        printRelocation(SecI, RelI);
      }
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      for (symbol_iterator SymI = Obj->begin_symbols(),
                           SymE = Obj->end_symbols();
                           SymI != SymE; SymI.increment(EC)) {
        if (error(EC)) break;

        bool Contained = false;
        if (SecI->containsSymbol(*SymI, Contained) || !Contained)
          continue;

        printSymbol(SymI);
      }
    }

    if (opts::SectionData) {
      StringRef Data;
      if (error(SecI->getContents(Data))) break;

      W.printBinaryBlock("SectionData", Data);
    }
  }
}

void MachODumper::printRelocations() {
  ListScope D(W, "Relocations");

  error_code EC;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    if (error(EC)) break;

    StringRef Name;
    if (error(SecI->getName(Name)))
      continue;

    bool PrintedGroup = false;
    for (relocation_iterator RelI = SecI->begin_relocations(),
                             RelE = SecI->end_relocations();
                             RelI != RelE; RelI.increment(EC)) {
      if (error(EC)) break;

      if (!PrintedGroup) {
        W.startLine() << "Section " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(SecI, RelI);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void MachODumper::printRelocation(section_iterator SecI,
                                  relocation_iterator RelI) {
  return printRelocation(Obj, SecI, RelI);
}

void MachODumper::printRelocation(const MachOObjectFile *Obj,
                                  section_iterator SecI,
                                  relocation_iterator RelI) {
  uint64_t Offset;
  SmallString<32> RelocName;
  StringRef SymbolName;
  if (error(RelI->getOffset(Offset))) return;
  if (error(RelI->getTypeName(RelocName))) return;
  symbol_iterator Symbol = RelI->getSymbol();
  if (Symbol != Obj->end_symbols() &&
      error(Symbol->getName(SymbolName)))
    return;

  DataRefImpl DR = RelI->getRawDataRefImpl();
  macho::RelocationEntry RE = Obj->getRelocation(DR);
  bool IsScattered = Obj->isRelocationScattered(RE);

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Offset", Offset);
    W.printNumber("PCRel", Obj->getAnyRelocationPCRel(RE));
    W.printNumber("Length", Obj->getAnyRelocationLength(RE));
    if (IsScattered)
      W.printString("Extern", StringRef("N/A"));
    else
      W.printNumber("Extern", Obj->getPlainRelocationExternal(RE));
    W.printNumber("Type", RelocName, Obj->getAnyRelocationType(RE));
    W.printString("Symbol", SymbolName.size() > 0 ? SymbolName : "-");
    W.printNumber("Scattered", IsScattered);
  } else {
    raw_ostream& OS = W.startLine();
    OS << W.hex(Offset)
       << " " << Obj->getAnyRelocationPCRel(RE)
       << " " << Obj->getAnyRelocationLength(RE);
    if (IsScattered)
      OS << " n/a";
    else
      OS << " " << Obj->getPlainRelocationExternal(RE);
    OS << " " << RelocName
       << " " << IsScattered
       << " " << (SymbolName.size() > 0 ? SymbolName : "-")
       << "\n";
  }
}

void MachODumper::printSymbols() {
  ListScope Group(W, "Symbols");

  error_code EC;
  for (symbol_iterator SymI = Obj->begin_symbols(),
                       SymE = Obj->end_symbols();
                       SymI != SymE; SymI.increment(EC)) {
    if (error(EC)) break;

    printSymbol(SymI);
  }
}

void MachODumper::printDynamicSymbols() {
  ListScope Group(W, "DynamicSymbols");
}

void MachODumper::printSymbol(symbol_iterator SymI) {
  error_code EC;

  StringRef SymbolName;
  if (SymI->getName(SymbolName))
    SymbolName = "";

  MachOSymbol Symbol;
  getSymbol(Obj, SymI->getRawDataRefImpl(), Symbol);

  StringRef SectionName = "";
  section_iterator SecI(Obj->end_sections());
  if (!error(SymI->getSection(SecI)) &&
      SecI != Obj->end_sections())
      error(SecI->getName(SectionName));

  DictScope D(W, "Symbol");
  W.printNumber("Name", SymbolName, Symbol.StringIndex);
  if (Symbol.Type & N_STAB) {
    W.printHex ("Type", "SymDebugTable", Symbol.Type);
  } else {
    W.printEnum("Type", Symbol.Type, makeArrayRef(MachOSymbolTypes));
  }
  W.printHex   ("Section", SectionName, Symbol.SectionIndex);
  W.printEnum  ("RefType", static_cast<uint16_t>(Symbol.Flags & 0xF),
                  makeArrayRef(MachOSymbolRefTypes));
  W.printFlags ("Flags", static_cast<uint16_t>(Symbol.Flags & ~0xF),
                  makeArrayRef(MachOSymbolFlags));
  W.printHex   ("Value", Symbol.Value);
}

void MachODumper::printUnwindInfo() {
  W.startLine() << "UnwindInfo not implemented.\n";
}
