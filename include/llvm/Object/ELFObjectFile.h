//===- ELFObjectFile.h - ELF object file implementation ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ELFObjectFile template class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELFOBJECTFILE_H
#define LLVM_OBJECT_ELFOBJECTFILE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace llvm {
namespace object {

class ELFObjectFileBase : public ObjectFile {
protected:
  ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source);

public:
  virtual std::error_code getRelocationAddend(DataRefImpl Rel,
                                              int64_t &Res) const = 0;
  virtual std::pair<symbol_iterator, symbol_iterator>
  getELFDynamicSymbolIterators() const = 0;

  virtual std::error_code getSymbolVersion(SymbolRef Symb, StringRef &Version,
                                           bool &IsDefault) const = 0;

  virtual uint64_t getSectionFlags(SectionRef Sec) const = 0;
  virtual uint32_t getSectionType(SectionRef Sec) const = 0;

  static inline bool classof(const Binary *v) { return v->isELF(); }
};

template <class ELFT> class ELFObjectFile : public ELFObjectFileBase {
public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  typedef typename ELFFile<ELFT>::uintX_t uintX_t;

  typedef typename ELFFile<ELFT>::Elf_Sym Elf_Sym;
  typedef typename ELFFile<ELFT>::Elf_Shdr Elf_Shdr;
  typedef typename ELFFile<ELFT>::Elf_Ehdr Elf_Ehdr;
  typedef typename ELFFile<ELFT>::Elf_Rel Elf_Rel;
  typedef typename ELFFile<ELFT>::Elf_Rela Elf_Rela;
  typedef typename ELFFile<ELFT>::Elf_Dyn Elf_Dyn;

  typedef typename ELFFile<ELFT>::Elf_Sym_Iter Elf_Sym_Iter;
  typedef typename ELFFile<ELFT>::Elf_Shdr_Iter Elf_Shdr_Iter;
  typedef typename ELFFile<ELFT>::Elf_Dyn_Iter Elf_Dyn_Iter;

protected:
  ELFFile<ELFT> EF;

  void moveSymbolNext(DataRefImpl &Symb) const override;
  std::error_code getSymbolName(DataRefImpl Symb,
                                StringRef &Res) const override;
  std::error_code getSymbolAddress(DataRefImpl Symb,
                                   uint64_t &Res) const override;
  std::error_code getSymbolAlignment(DataRefImpl Symb,
                                     uint32_t &Res) const override;
  std::error_code getSymbolSize(DataRefImpl Symb, uint64_t &Res) const override;
  uint32_t getSymbolFlags(DataRefImpl Symb) const override;
  std::error_code getSymbolOther(DataRefImpl Symb, uint8_t &Res) const override;
  std::error_code getSymbolType(DataRefImpl Symb,
                                SymbolRef::Type &Res) const override;
  std::error_code getSymbolSection(DataRefImpl Symb,
                                   section_iterator &Res) const override;

  void moveSectionNext(DataRefImpl &Sec) const override;
  std::error_code getSectionName(DataRefImpl Sec,
                                 StringRef &Res) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  std::error_code getSectionContents(DataRefImpl Sec,
                                     StringRef &Res) const override;
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override;
  bool isSectionVirtual(DataRefImpl Sec) const override;
  bool sectionContainsSymbol(DataRefImpl Sec, DataRefImpl Symb) const override;
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;
  section_iterator getRelocatedSection(DataRefImpl Sec) const override;

  void moveRelocationNext(DataRefImpl &Rel) const override;
  std::error_code getRelocationAddress(DataRefImpl Rel,
                                       uint64_t &Res) const override;
  std::error_code getRelocationOffset(DataRefImpl Rel,
                                      uint64_t &Res) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  std::error_code getRelocationType(DataRefImpl Rel,
                                    uint64_t &Res) const override;
  std::error_code
  getRelocationTypeName(DataRefImpl Rel,
                        SmallVectorImpl<char> &Result) const override;
  std::error_code
  getRelocationValueString(DataRefImpl Rel,
                           SmallVectorImpl<char> &Result) const override;

  uint64_t getROffset(DataRefImpl Rel) const;
  StringRef getRelocationTypeName(uint32_t Type) const;

  /// \brief Get the relocation section that contains \a Rel.
  const Elf_Shdr *getRelSection(DataRefImpl Rel) const {
    return EF.getSection(Rel.d.a);
  }

  const Elf_Rel *getRel(DataRefImpl Rel) const;
  const Elf_Rela *getRela(DataRefImpl Rela) const;

  Elf_Sym_Iter toELFSymIter(DataRefImpl Symb) const {
    bool IsDynamic = Symb.p & 1;
    if (IsDynamic)
      return Elf_Sym_Iter(
          EF.begin_dynamic_symbols().getEntSize(),
          reinterpret_cast<const char *>(Symb.p & ~uintptr_t(1)), IsDynamic);
    return Elf_Sym_Iter(EF.begin_symbols().getEntSize(),
                        reinterpret_cast<const char *>(Symb.p), IsDynamic);
  }

  DataRefImpl toDRI(Elf_Sym_Iter Symb) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Symb.get()) |
      static_cast<uintptr_t>(Symb.isDynamic());
    return DRI;
  }

  Elf_Shdr_Iter toELFShdrIter(DataRefImpl Sec) const {
    return Elf_Shdr_Iter(EF.getHeader()->e_shentsize,
                         reinterpret_cast<const char *>(Sec.p));
  }

  DataRefImpl toDRI(Elf_Shdr_Iter Sec) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Sec.get());
    return DRI;
  }

  DataRefImpl toDRI(const Elf_Shdr *Sec) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Sec);
    return DRI;
  }

  Elf_Dyn_Iter toELFDynIter(DataRefImpl Dyn) const {
    return Elf_Dyn_Iter(EF.begin_dynamic_table().getEntSize(),
                        reinterpret_cast<const char *>(Dyn.p));
  }

  DataRefImpl toDRI(Elf_Dyn_Iter Dyn) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Dyn.get());
    return DRI;
  }

  bool isExportedToOtherDSO(const Elf_Sym *ESym) const {
    unsigned char Binding = ESym->getBinding();
    unsigned char Visibility = ESym->getVisibility();

    // A symbol is exported if its binding is either GLOBAL or WEAK, and its
    // visibility is either DEFAULT or PROTECTED. All other symbols are not
    // exported.
    if ((Binding == ELF::STB_GLOBAL || Binding == ELF::STB_WEAK) &&
        (Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_PROTECTED))
      return true;

    return false;
  }

  // This flag is used for classof, to distinguish ELFObjectFile from
  // its subclass. If more subclasses will be created, this flag will
  // have to become an enum.
  bool isDyldELFObject;

public:
  ELFObjectFile(MemoryBufferRef Object, std::error_code &EC);

  const Elf_Sym *getSymbol(DataRefImpl Symb) const;

  basic_symbol_iterator symbol_begin_impl() const override;
  basic_symbol_iterator symbol_end_impl() const override;

  symbol_iterator dynamic_symbol_begin() const;
  symbol_iterator dynamic_symbol_end() const;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  std::error_code getRelocationAddend(DataRefImpl Rel,
                                      int64_t &Res) const override;
  std::error_code getSymbolVersion(SymbolRef Symb, StringRef &Version,
                                   bool &IsDefault) const override;

  uint64_t getSectionFlags(SectionRef Sec) const override;
  uint32_t getSectionType(SectionRef Sec) const override;

  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  unsigned getArch() const override;
  StringRef getLoadName() const;

  std::error_code getPlatformFlags(unsigned &Result) const override {
    Result = EF.getHeader()->e_flags;
    return object_error::success;
  }

  const ELFFile<ELFT> *getELFFile() const { return &EF; }

  bool isDyldType() const { return isDyldELFObject; }
  static inline bool classof(const Binary *v) {
    return v->getType() == getELFType(ELFT::TargetEndianness == support::little,
                                      ELFT::Is64Bits);
  }

  std::pair<symbol_iterator, symbol_iterator>
  getELFDynamicSymbolIterators() const override;

  bool isRelocatableObject() const override;
};

// Use an alignment of 2 for the typedefs since that is the worst case for
// ELF files in archives.
typedef ELFObjectFile<ELFType<support::little, 2, false> > ELF32LEObjectFile;
typedef ELFObjectFile<ELFType<support::little, 2, true> > ELF64LEObjectFile;
typedef ELFObjectFile<ELFType<support::big, 2, false> > ELF32BEObjectFile;
typedef ELFObjectFile<ELFType<support::big, 2, true> > ELF64BEObjectFile;

template <class ELFT>
void ELFObjectFile<ELFT>::moveSymbolNext(DataRefImpl &Symb) const {
  Symb = toDRI(++toELFSymIter(Symb));
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolName(DataRefImpl Symb,
                                                   StringRef &Result) const {
  ErrorOr<StringRef> Name = EF.getSymbolName(toELFSymIter(Symb));
  if (!Name)
    return Name.getError();
  Result = *Name;
  return object_error::success;
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolVersion(SymbolRef SymRef,
                                                      StringRef &Version,
                                                      bool &IsDefault) const {
  DataRefImpl Symb = SymRef.getRawDataRefImpl();
  const Elf_Sym *symb = getSymbol(Symb);
  ErrorOr<StringRef> Ver =
      EF.getSymbolVersion(EF.getSection(Symb.d.b), symb, IsDefault);
  if (!Ver)
    return Ver.getError();
  Version = *Ver;
  return object_error::success;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionFlags(SectionRef Sec) const {
  DataRefImpl DRI = Sec.getRawDataRefImpl();
  return toELFShdrIter(DRI)->sh_flags;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSectionType(SectionRef Sec) const {
  DataRefImpl DRI = Sec.getRawDataRefImpl();
  return toELFShdrIter(DRI)->sh_type;
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolAddress(DataRefImpl Symb,
                                                      uint64_t &Result) const {
  const Elf_Sym *ESym = getSymbol(Symb);
  switch (EF.getSymbolTableIndex(ESym)) {
  case ELF::SHN_COMMON:
  case ELF::SHN_UNDEF:
    Result = UnknownAddressOrSize;
    return object_error::success;
  case ELF::SHN_ABS:
    Result = ESym->st_value;
    return object_error::success;
  default:
    break;
  }

  const Elf_Ehdr *Header = EF.getHeader();
  Result = ESym->st_value;

  // Clear the ARM/Thumb or microMIPS indicator flag.
  if ((Header->e_machine == ELF::EM_ARM || Header->e_machine == ELF::EM_MIPS) &&
      ESym->getType() == ELF::STT_FUNC)
    Result &= ~1;

  if (Header->e_type == ELF::ET_REL)
    Result += EF.getSection(ESym)->sh_addr;

  return object_error::success;
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolAlignment(DataRefImpl Symb,
                                                        uint32_t &Res) const {
  Elf_Sym_Iter Sym = toELFSymIter(Symb);
  if (Sym->st_shndx == ELF::SHN_COMMON)
    Res = Sym->st_value;
  else
    Res = 0;
  return object_error::success;
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolSize(DataRefImpl Symb,
                                                   uint64_t &Result) const {
  Result = toELFSymIter(Symb)->st_size;
  return object_error::success;
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSymbolOther(DataRefImpl Symb,
                                                    uint8_t &Result) const {
  Result = toELFSymIter(Symb)->st_other;
  return object_error::success;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getSymbolType(DataRefImpl Symb,
                                   SymbolRef::Type &Result) const {
  const Elf_Sym *ESym = getSymbol(Symb);

  switch (ESym->getType()) {
  case ELF::STT_NOTYPE:
    Result = SymbolRef::ST_Unknown;
    break;
  case ELF::STT_SECTION:
    Result = SymbolRef::ST_Debug;
    break;
  case ELF::STT_FILE:
    Result = SymbolRef::ST_File;
    break;
  case ELF::STT_FUNC:
    Result = SymbolRef::ST_Function;
    break;
  case ELF::STT_OBJECT:
  case ELF::STT_COMMON:
  case ELF::STT_TLS:
    Result = SymbolRef::ST_Data;
    break;
  default:
    Result = SymbolRef::ST_Other;
    break;
  }
  return object_error::success;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSymbolFlags(DataRefImpl Symb) const {
  Elf_Sym_Iter EIter = toELFSymIter(Symb);
  const Elf_Sym *ESym = &*EIter;

  uint32_t Result = SymbolRef::SF_None;

  if (ESym->getBinding() != ELF::STB_LOCAL)
    Result |= SymbolRef::SF_Global;

  if (ESym->getBinding() == ELF::STB_WEAK)
    Result |= SymbolRef::SF_Weak;

  if (ESym->st_shndx == ELF::SHN_ABS)
    Result |= SymbolRef::SF_Absolute;

  if (ESym->getType() == ELF::STT_FILE || ESym->getType() == ELF::STT_SECTION ||
      EIter == EF.begin_symbols() || EIter == EF.begin_dynamic_symbols())
    Result |= SymbolRef::SF_FormatSpecific;

  if (EF.getSymbolTableIndex(ESym) == ELF::SHN_UNDEF)
    Result |= SymbolRef::SF_Undefined;

  if (ESym->getType() == ELF::STT_COMMON ||
      EF.getSymbolTableIndex(ESym) == ELF::SHN_COMMON)
    Result |= SymbolRef::SF_Common;

  if (isExportedToOtherDSO(ESym))
    Result |= SymbolRef::SF_Exported;

  return Result;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getSymbolSection(DataRefImpl Symb,
                                      section_iterator &Res) const {
  const Elf_Sym *ESym = getSymbol(Symb);
  const Elf_Shdr *ESec = EF.getSection(ESym);
  if (!ESec)
    Res = section_end();
  else {
    DataRefImpl Sec;
    Sec.p = reinterpret_cast<intptr_t>(ESec);
    Res = section_iterator(SectionRef(Sec, this));
  }
  return object_error::success;
}

template <class ELFT>
void ELFObjectFile<ELFT>::moveSectionNext(DataRefImpl &Sec) const {
  Sec = toDRI(++toELFShdrIter(Sec));
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSectionName(DataRefImpl Sec,
                                                    StringRef &Result) const {
  ErrorOr<StringRef> Name = EF.getSectionName(&*toELFShdrIter(Sec));
  if (!Name)
    return Name.getError();
  Result = *Name;
  return object_error::success;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAddress(DataRefImpl Sec) const {
  return toELFShdrIter(Sec)->sh_addr;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionSize(DataRefImpl Sec) const {
  return toELFShdrIter(Sec)->sh_size;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getSectionContents(DataRefImpl Sec,
                                        StringRef &Result) const {
  Elf_Shdr_Iter EShdr = toELFShdrIter(Sec);
  Result = StringRef((const char *)base() + EShdr->sh_offset, EShdr->sh_size);
  return object_error::success;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAlignment(DataRefImpl Sec) const {
  return toELFShdrIter(Sec)->sh_addralign;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionText(DataRefImpl Sec) const {
  return toELFShdrIter(Sec)->sh_flags & ELF::SHF_EXECINSTR;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionData(DataRefImpl Sec) const {
  Elf_Shdr_Iter EShdr = toELFShdrIter(Sec);
  return EShdr->sh_flags & (ELF::SHF_ALLOC | ELF::SHF_WRITE) &&
         EShdr->sh_type == ELF::SHT_PROGBITS;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionBSS(DataRefImpl Sec) const {
  Elf_Shdr_Iter EShdr = toELFShdrIter(Sec);
  return EShdr->sh_flags & (ELF::SHF_ALLOC | ELF::SHF_WRITE) &&
         EShdr->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionVirtual(DataRefImpl Sec) const {
  return toELFShdrIter(Sec)->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::sectionContainsSymbol(DataRefImpl Sec,
                                                DataRefImpl Symb) const {
  Elf_Sym_Iter ESym = toELFSymIter(Symb);

  uintX_t Index = ESym->st_shndx;
  bool Reserved = Index >= ELF::SHN_LORESERVE && Index <= ELF::SHN_HIRESERVE;

  return !Reserved && (&*toELFShdrIter(Sec) == EF.getSection(ESym->st_shndx));
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_begin(DataRefImpl Sec) const {
  DataRefImpl RelData;
  uintptr_t SHT = reinterpret_cast<uintptr_t>(EF.begin_sections().get());
  RelData.d.a = (Sec.p - SHT) / EF.getHeader()->e_shentsize;
  RelData.d.b = 0;
  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_end(DataRefImpl Sec) const {
  DataRefImpl RelData;
  uintptr_t SHT = reinterpret_cast<uintptr_t>(EF.begin_sections().get());
  const Elf_Shdr *S = reinterpret_cast<const Elf_Shdr *>(Sec.p);
  RelData.d.a = (Sec.p - SHT) / EF.getHeader()->e_shentsize;
  if (S->sh_type != ELF::SHT_RELA && S->sh_type != ELF::SHT_REL)
    RelData.d.b = 0;
  else
    RelData.d.b = S->sh_size / S->sh_entsize;

  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
section_iterator
ELFObjectFile<ELFT>::getRelocatedSection(DataRefImpl Sec) const {
  if (EF.getHeader()->e_type != ELF::ET_REL)
    return section_end();

  Elf_Shdr_Iter EShdr = toELFShdrIter(Sec);
  uintX_t Type = EShdr->sh_type;
  if (Type != ELF::SHT_REL && Type != ELF::SHT_RELA)
    return section_end();

  const Elf_Shdr *R = EF.getSection(EShdr->sh_info);
  return section_iterator(SectionRef(toDRI(R), this));
}

// Relocations
template <class ELFT>
void ELFObjectFile<ELFT>::moveRelocationNext(DataRefImpl &Rel) const {
  ++Rel.d.b;
}

template <class ELFT>
symbol_iterator
ELFObjectFile<ELFT>::getRelocationSymbol(DataRefImpl Rel) const {
  uint32_t symbolIdx;
  const Elf_Shdr *sec = getRelSection(Rel);
  switch (sec->sh_type) {
  default:
    report_fatal_error("Invalid section type in Rel!");
  case ELF::SHT_REL: {
    symbolIdx = getRel(Rel)->getSymbol(EF.isMips64EL());
    break;
  }
  case ELF::SHT_RELA: {
    symbolIdx = getRela(Rel)->getSymbol(EF.isMips64EL());
    break;
  }
  }
  if (!symbolIdx)
    return symbol_end();

  const Elf_Shdr *SymSec = EF.getSection(sec->sh_link);

  DataRefImpl SymbolData;
  switch (SymSec->sh_type) {
  default:
    report_fatal_error("Invalid symbol table section type!");
  case ELF::SHT_SYMTAB:
    SymbolData = toDRI(EF.begin_symbols() + symbolIdx);
    break;
  case ELF::SHT_DYNSYM:
    SymbolData = toDRI(EF.begin_dynamic_symbols() + symbolIdx);
    break;
  }

  return symbol_iterator(SymbolRef(SymbolData, this));
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getRelocationAddress(DataRefImpl Rel,
                                          uint64_t &Result) const {
  uint64_t ROffset = getROffset(Rel);
  const Elf_Ehdr *Header = EF.getHeader();

  if (Header->e_type == ELF::ET_REL) {
    const Elf_Shdr *RelocationSec = getRelSection(Rel);
    const Elf_Shdr *RelocatedSec = EF.getSection(RelocationSec->sh_info);
    Result = ROffset + RelocatedSec->sh_addr;
  } else {
    Result = ROffset;
  }

  return object_error::success;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getRelocationOffset(DataRefImpl Rel,
                                         uint64_t &Result) const {
  assert(EF.getHeader()->e_type == ELF::ET_REL &&
         "Only relocatable object files have relocation offsets");
  Result = getROffset(Rel);
  return object_error::success;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getROffset(DataRefImpl Rel) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  switch (sec->sh_type) {
  default:
    report_fatal_error("Invalid section type in Rel!");
  case ELF::SHT_REL:
    return getRel(Rel)->r_offset;
  case ELF::SHT_RELA:
    return getRela(Rel)->r_offset;
  }
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getRelocationType(DataRefImpl Rel,
                                                       uint64_t &Result) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  switch (sec->sh_type) {
  default:
    report_fatal_error("Invalid section type in Rel!");
  case ELF::SHT_REL: {
    Result = getRel(Rel)->getType(EF.isMips64EL());
    break;
  }
  case ELF::SHT_RELA: {
    Result = getRela(Rel)->getType(EF.isMips64EL());
    break;
  }
  }
  return object_error::success;
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getRelocationTypeName(uint32_t Type) const {
  return getELFRelocationTypeName(EF.getHeader()->e_machine, Type);
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  uint32_t type;
  switch (sec->sh_type) {
  default:
    return object_error::parse_failed;
  case ELF::SHT_REL: {
    type = getRel(Rel)->getType(EF.isMips64EL());
    break;
  }
  case ELF::SHT_RELA: {
    type = getRela(Rel)->getType(EF.isMips64EL());
    break;
  }
  }

  EF.getRelocationTypeName(type, Result);
  return object_error::success;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getRelocationAddend(DataRefImpl Rel,
                                         int64_t &Result) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  switch (sec->sh_type) {
  default:
    report_fatal_error("Invalid section type in Rel!");
  case ELF::SHT_REL: {
    Result = 0;
    return object_error::success;
  }
  case ELF::SHT_RELA: {
    Result = getRela(Rel)->r_addend;
    return object_error::success;
  }
  }
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getRelocationValueString(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  uint8_t type;
  StringRef res;
  int64_t addend = 0;
  uint16_t symbol_index = 0;
  switch (sec->sh_type) {
  default:
    return object_error::parse_failed;
  case ELF::SHT_REL: {
    type = getRel(Rel)->getType(EF.isMips64EL());
    symbol_index = getRel(Rel)->getSymbol(EF.isMips64EL());
    // TODO: Read implicit addend from section data.
    break;
  }
  case ELF::SHT_RELA: {
    type = getRela(Rel)->getType(EF.isMips64EL());
    symbol_index = getRela(Rel)->getSymbol(EF.isMips64EL());
    addend = getRela(Rel)->r_addend;
    break;
  }
  }
  const Elf_Sym *symb =
      EF.template getEntry<Elf_Sym>(sec->sh_link, symbol_index);
  ErrorOr<StringRef> SymName =
      EF.getSymbolName(EF.getSection(sec->sh_link), symb);
  if (!SymName)
    return SymName.getError();
  switch (EF.getHeader()->e_machine) {
  case ELF::EM_X86_64:
    switch (type) {
    case ELF::R_X86_64_PC8:
    case ELF::R_X86_64_PC16:
    case ELF::R_X86_64_PC32: {
      std::string fmtbuf;
      raw_string_ostream fmt(fmtbuf);
      fmt << *SymName << (addend < 0 ? "" : "+") << addend << "-P";
      fmt.flush();
      Result.append(fmtbuf.begin(), fmtbuf.end());
    } break;
    case ELF::R_X86_64_8:
    case ELF::R_X86_64_16:
    case ELF::R_X86_64_32:
    case ELF::R_X86_64_32S:
    case ELF::R_X86_64_64: {
      std::string fmtbuf;
      raw_string_ostream fmt(fmtbuf);
      fmt << *SymName << (addend < 0 ? "" : "+") << addend;
      fmt.flush();
      Result.append(fmtbuf.begin(), fmtbuf.end());
    } break;
    default:
      res = "Unknown";
    }
    break;
  case ELF::EM_AARCH64: {
    std::string fmtbuf;
    raw_string_ostream fmt(fmtbuf);
    fmt << *SymName;
    if (addend != 0)
      fmt << (addend < 0 ? "" : "+") << addend;
    fmt.flush();
    Result.append(fmtbuf.begin(), fmtbuf.end());
    break;
  }
  case ELF::EM_386:
  case ELF::EM_ARM:
  case ELF::EM_HEXAGON:
  case ELF::EM_MIPS:
    res = *SymName;
    break;
  default:
    res = "Unknown";
  }
  if (Result.empty())
    Result.append(res.begin(), res.end());
  return object_error::success;
}

template <class ELFT>
const typename ELFFile<ELFT>::Elf_Sym *
ELFObjectFile<ELFT>::getSymbol(DataRefImpl Symb) const {
  return &*toELFSymIter(Symb);
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rel *
ELFObjectFile<ELFT>::getRel(DataRefImpl Rel) const {
  return EF.template getEntry<Elf_Rel>(Rel.d.a, Rel.d.b);
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rela *
ELFObjectFile<ELFT>::getRela(DataRefImpl Rela) const {
  return EF.template getEntry<Elf_Rela>(Rela.d.a, Rela.d.b);
}

template <class ELFT>
ELFObjectFile<ELFT>::ELFObjectFile(MemoryBufferRef Object, std::error_code &EC)
    : ELFObjectFileBase(
          getELFType(static_cast<endianness>(ELFT::TargetEndianness) ==
                         support::little,
                     ELFT::Is64Bits),
          Object),
      EF(Data.getBuffer(), EC) {}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_begin_impl() const {
  return basic_symbol_iterator(SymbolRef(toDRI(EF.begin_symbols()), this));
}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_end_impl() const {
  return basic_symbol_iterator(SymbolRef(toDRI(EF.end_symbols()), this));
}

template <class ELFT>
symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_begin() const {
  return symbol_iterator(SymbolRef(toDRI(EF.begin_dynamic_symbols()), this));
}

template <class ELFT>
symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_end() const {
  return symbol_iterator(SymbolRef(toDRI(EF.end_dynamic_symbols()), this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_begin() const {
  return section_iterator(SectionRef(toDRI(EF.begin_sections()), this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_end() const {
  return section_iterator(SectionRef(toDRI(EF.end_sections()), this));
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getLoadName() const {
  Elf_Dyn_Iter DI = EF.begin_dynamic_table();
  Elf_Dyn_Iter DE = EF.end_dynamic_table();

  while (DI != DE && DI->getTag() != ELF::DT_SONAME)
    ++DI;

  if (DI != DE)
    return EF.getDynamicString(DI->getVal());
  return "";
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getBytesInAddress() const {
  return ELFT::Is64Bits ? 8 : 4;
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getFileFormatName() const {
  bool IsLittleEndian = ELFT::TargetEndianness == support::little;
  switch (EF.getHeader()->e_ident[ELF::EI_CLASS]) {
  case ELF::ELFCLASS32:
    switch (EF.getHeader()->e_machine) {
    case ELF::EM_386:
      return "ELF32-i386";
    case ELF::EM_X86_64:
      return "ELF32-x86-64";
    case ELF::EM_ARM:
      return (IsLittleEndian ? "ELF32-arm-little" : "ELF32-arm-big");
    case ELF::EM_HEXAGON:
      return "ELF32-hexagon";
    case ELF::EM_MIPS:
      return "ELF32-mips";
    case ELF::EM_PPC:
      return "ELF32-ppc";
    case ELF::EM_SPARC:
    case ELF::EM_SPARC32PLUS:
      return "ELF32-sparc";
    default:
      return "ELF32-unknown";
    }
  case ELF::ELFCLASS64:
    switch (EF.getHeader()->e_machine) {
    case ELF::EM_386:
      return "ELF64-i386";
    case ELF::EM_X86_64:
      return "ELF64-x86-64";
    case ELF::EM_AARCH64:
      return (IsLittleEndian ? "ELF64-aarch64-little" : "ELF64-aarch64-big");
    case ELF::EM_PPC64:
      return "ELF64-ppc64";
    case ELF::EM_S390:
      return "ELF64-s390";
    case ELF::EM_SPARCV9:
      return "ELF64-sparc";
    case ELF::EM_MIPS:
      return "ELF64-mips";
    default:
      return "ELF64-unknown";
    }
  default:
    // FIXME: Proper error handling.
    report_fatal_error("Invalid ELFCLASS!");
  }
}

template <class ELFT>
unsigned ELFObjectFile<ELFT>::getArch() const {
  bool IsLittleEndian = ELFT::TargetEndianness == support::little;
  switch (EF.getHeader()->e_machine) {
  case ELF::EM_386:
    return Triple::x86;
  case ELF::EM_X86_64:
    return Triple::x86_64;
  case ELF::EM_AARCH64:
    return Triple::aarch64;
  case ELF::EM_ARM:
    return Triple::arm;
  case ELF::EM_HEXAGON:
    return Triple::hexagon;
  case ELF::EM_MIPS:
    switch (EF.getHeader()->e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return IsLittleEndian ? Triple::mipsel : Triple::mips;
    case ELF::ELFCLASS64:
      return IsLittleEndian ? Triple::mips64el : Triple::mips64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }
  case ELF::EM_PPC:
    return Triple::ppc;
  case ELF::EM_PPC64:
    return IsLittleEndian ? Triple::ppc64le : Triple::ppc64;
  case ELF::EM_S390:
    return Triple::systemz;

  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
    return Triple::sparc;
  case ELF::EM_SPARCV9:
    return Triple::sparcv9;

  default:
    return Triple::UnknownArch;
  }
}

template <class ELFT>
std::pair<symbol_iterator, symbol_iterator>
ELFObjectFile<ELFT>::getELFDynamicSymbolIterators() const {
  return std::make_pair(dynamic_symbol_begin(), dynamic_symbol_end());
}

template <class ELFT> bool ELFObjectFile<ELFT>::isRelocatableObject() const {
  return EF.getHeader()->e_type == ELF::ET_REL;
}

inline std::error_code getELFRelocationAddend(const RelocationRef R,
                                              int64_t &Addend) {
  const ObjectFile *Obj = R.getObjectFile();
  DataRefImpl DRI = R.getRawDataRefImpl();
  return cast<ELFObjectFileBase>(Obj)->getRelocationAddend(DRI, Addend);
}

inline std::pair<symbol_iterator, symbol_iterator>
getELFDynamicSymbolIterators(const SymbolicFile *Obj) {
  return cast<ELFObjectFileBase>(Obj)->getELFDynamicSymbolIterators();
}

inline std::error_code GetELFSymbolVersion(const ObjectFile *Obj,
                                           const SymbolRef &Sym,
                                           StringRef &Version,
                                           bool &IsDefault) {
  return cast<ELFObjectFileBase>(Obj)
      ->getSymbolVersion(Sym, Version, IsDefault);
}
}
}

#endif
