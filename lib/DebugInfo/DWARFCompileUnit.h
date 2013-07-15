//===-- DWARFCompileUnit.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFCOMPILEUNIT_H
#define LLVM_DEBUGINFO_DWARFCOMPILEUNIT_H

#include "DWARFDebugAbbrev.h"
#include "DWARFDebugInfoEntry.h"
#include "DWARFDebugRangeList.h"
#include "DWARFRelocMap.h"
#include <vector>

namespace llvm {

class DWARFDebugAbbrev;
class StringRef;
class raw_ostream;

class DWARFCompileUnit {
  const DWARFDebugAbbrev *Abbrev;
  StringRef InfoSection;
  StringRef AbbrevSection;
  StringRef RangeSection;
  StringRef StringSection;
  StringRef StringOffsetSection;
  StringRef AddrOffsetSection;
  const RelocAddrMap *RelocMap;
  bool isLittleEndian;

  uint32_t Offset;
  uint32_t Length;
  uint16_t Version;
  const DWARFAbbreviationDeclarationSet *Abbrevs;
  uint8_t AddrSize;
  uint64_t BaseAddr;
  // The compile unit debug information entry items.
  std::vector<DWARFDebugInfoEntryMinimal> DieArray;
public:

  DWARFCompileUnit(const DWARFDebugAbbrev *DA, StringRef IS, StringRef AS,
                   StringRef RS, StringRef SS, StringRef SOS, StringRef AOS,
                   const RelocAddrMap *M, bool LE) :
    Abbrev(DA), InfoSection(IS), AbbrevSection(AS),
    RangeSection(RS), StringSection(SS), StringOffsetSection(SOS),
    AddrOffsetSection(AOS), RelocMap(M), isLittleEndian(LE) {
    clear();
  }

  StringRef getStringSection() const { return StringSection; }
  StringRef getStringOffsetSection() const { return StringOffsetSection; }
  StringRef getAddrOffsetSection() const { return AddrOffsetSection; }
  const RelocAddrMap *getRelocMap() const { return RelocMap; }
  DataExtractor getDebugInfoExtractor() const;

  bool extract(DataExtractor debug_info, uint32_t* offset_ptr);
  uint32_t extract(uint32_t offset, DataExtractor debug_info_data,
                   const DWARFAbbreviationDeclarationSet *abbrevs);

  /// extractDIEsIfNeeded - Parses a compile unit and indexes its DIEs if it
  /// hasn't already been done. Returns the number of DIEs parsed at this call.
  size_t extractDIEsIfNeeded(bool CUDieOnly);
  /// extractRangeList - extracts the range list referenced by this compile
  /// unit from .debug_ranges section. Returns true on success.
  /// Requires that compile unit is already extracted.
  bool extractRangeList(uint32_t RangeListOffset,
                        DWARFDebugRangeList &RangeList) const;
  void clear();
  void dump(raw_ostream &OS);
  uint32_t getOffset() const { return Offset; }
  /// Size in bytes of the compile unit header.
  uint32_t getSize() const { return 11; }
  bool containsDIEOffset(uint32_t die_offset) const {
    return die_offset >= getFirstDIEOffset() &&
      die_offset < getNextCompileUnitOffset();
  }
  uint32_t getFirstDIEOffset() const { return Offset + getSize(); }
  uint32_t getNextCompileUnitOffset() const { return Offset + Length + 4; }
  /// Size in bytes of the .debug_info data associated with this compile unit.
  size_t getDebugInfoSize() const { return Length + 4 - getSize(); }
  uint32_t getLength() const { return Length; }
  uint16_t getVersion() const { return Version; }
  const DWARFAbbreviationDeclarationSet *getAbbreviations() const {
    return Abbrevs;
  }
  uint8_t getAddressByteSize() const { return AddrSize; }
  uint64_t getBaseAddress() const { return BaseAddr; }

  void setBaseAddress(uint64_t base_addr) {
    BaseAddr = base_addr;
  }

  const DWARFDebugInfoEntryMinimal *
  getCompileUnitDIE(bool extract_cu_die_only = true) {
    extractDIEsIfNeeded(extract_cu_die_only);
    return DieArray.empty() ? NULL : &DieArray[0];
  }

  const char *getCompilationDir();

  /// setDIERelations - We read in all of the DIE entries into our flat list
  /// of DIE entries and now we need to go back through all of them and set the
  /// parent, sibling and child pointers for quick DIE navigation.
  void setDIERelations();

  void buildAddressRangeTable(DWARFDebugAranges *debug_aranges,
                              bool clear_dies_if_already_not_parsed);

  /// getInlinedChainForAddress - fetches inlined chain for a given address.
  /// Returns empty chain if there is no subprogram containing address.
  DWARFDebugInfoEntryMinimal::InlinedChain getInlinedChainForAddress(
      uint64_t Address);

private:
  /// extractDIEsToVector - Appends all parsed DIEs to a vector.
  void extractDIEsToVector(bool AppendCUDie, bool AppendNonCUDIEs,
                           std::vector<DWARFDebugInfoEntryMinimal> &DIEs) const;
  /// clearDIEs - Clear parsed DIEs to keep memory usage low.
  void clearDIEs(bool KeepCUDie);
};

}

#endif
