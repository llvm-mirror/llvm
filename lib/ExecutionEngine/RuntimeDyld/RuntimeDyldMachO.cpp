//===-- RuntimeDyldMachO.cpp - Run-time dynamic linker for MC-JIT -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#include "RuntimeDyldMachO.h"
#include "Targets/RuntimeDyldMachOAArch64.h"
#include "Targets/RuntimeDyldMachOARM.h"
#include "Targets/RuntimeDyldMachOI386.h"
#include "Targets/RuntimeDyldMachOX86_64.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "dyld"

namespace {

class LoadedMachOObjectInfo : public RuntimeDyld::LoadedObjectInfo {
public:
  LoadedMachOObjectInfo(RuntimeDyldImpl &RTDyld, unsigned BeginIdx,
                        unsigned EndIdx)
    : RuntimeDyld::LoadedObjectInfo(RTDyld, BeginIdx, EndIdx) {}

  OwningBinary<ObjectFile>
  getObjectForDebug(const ObjectFile &Obj) const override {
    return OwningBinary<ObjectFile>();
  }
};

}

namespace llvm {

int64_t RuntimeDyldMachO::memcpyAddend(const RelocationEntry &RE) const {
  unsigned NumBytes = 1 << RE.Size;
  uint8_t *Src = Sections[RE.SectionID].Address + RE.Offset;

  return static_cast<int64_t>(readBytesUnaligned(Src, NumBytes));
}

RelocationValueRef RuntimeDyldMachO::getRelocationValueRef(
    const ObjectFile &BaseTObj, const relocation_iterator &RI,
    const RelocationEntry &RE, ObjSectionToIDMap &ObjSectionToID) {

  const MachOObjectFile &Obj =
      static_cast<const MachOObjectFile &>(BaseTObj);
  MachO::any_relocation_info RelInfo =
      Obj.getRelocation(RI->getRawDataRefImpl());
  RelocationValueRef Value;

  bool IsExternal = Obj.getPlainRelocationExternal(RelInfo);
  if (IsExternal) {
    symbol_iterator Symbol = RI->getSymbol();
    StringRef TargetName;
    Symbol->getName(TargetName);
    RTDyldSymbolTable::const_iterator SI =
      GlobalSymbolTable.find(TargetName.data());
    if (SI != GlobalSymbolTable.end()) {
      const auto &SymInfo = SI->second;
      Value.SectionID = SymInfo.getSectionID();
      Value.Offset = SymInfo.getOffset() + RE.Addend;
    } else {
      Value.SymbolName = TargetName.data();
      Value.Offset = RE.Addend;
    }
  } else {
    SectionRef Sec = Obj.getRelocationSection(RelInfo);
    bool IsCode = Sec.isText();
    Value.SectionID = findOrEmitSection(Obj, Sec, IsCode, ObjSectionToID);
    uint64_t Addr = Sec.getAddress();
    Value.Offset = RE.Addend - Addr;
  }

  return Value;
}

void RuntimeDyldMachO::makeValueAddendPCRel(RelocationValueRef &Value,
                                            const ObjectFile &BaseTObj,
                                            const relocation_iterator &RI,
                                            unsigned OffsetToNextPC) {
  const MachOObjectFile &Obj =
      static_cast<const MachOObjectFile &>(BaseTObj);
  MachO::any_relocation_info RelInfo =
      Obj.getRelocation(RI->getRawDataRefImpl());

  bool IsPCRel = Obj.getAnyRelocationPCRel(RelInfo);
  if (IsPCRel) {
    uint64_t RelocAddr = 0;
    RI->getAddress(RelocAddr);
    Value.Offset += RelocAddr + OffsetToNextPC;
  }
}

void RuntimeDyldMachO::dumpRelocationToResolve(const RelocationEntry &RE,
                                               uint64_t Value) const {
  const SectionEntry &Section = Sections[RE.SectionID];
  uint8_t *LocalAddress = Section.Address + RE.Offset;
  uint64_t FinalAddress = Section.LoadAddress + RE.Offset;

  dbgs() << "resolveRelocation Section: " << RE.SectionID
         << " LocalAddress: " << format("%p", LocalAddress)
         << " FinalAddress: " << format("0x%016" PRIx64, FinalAddress)
         << " Value: " << format("0x%016" PRIx64, Value) << " Addend: " << RE.Addend
         << " isPCRel: " << RE.IsPCRel << " MachoType: " << RE.RelType
         << " Size: " << (1 << RE.Size) << "\n";
}

section_iterator
RuntimeDyldMachO::getSectionByAddress(const MachOObjectFile &Obj,
                                      uint64_t Addr) {
  section_iterator SI = Obj.section_begin();
  section_iterator SE = Obj.section_end();

  for (; SI != SE; ++SI) {
    uint64_t SAddr = SI->getAddress();
    uint64_t SSize = SI->getSize();
    if ((Addr >= SAddr) && (Addr < SAddr + SSize))
      return SI;
  }

  return SE;
}


// Populate __pointers section.
void RuntimeDyldMachO::populateIndirectSymbolPointersSection(
                                                    const MachOObjectFile &Obj,
                                                    const SectionRef &PTSection,
                                                    unsigned PTSectionID) {
  assert(!Obj.is64Bit() &&
         "Pointer table section not supported in 64-bit MachO.");

  MachO::dysymtab_command DySymTabCmd = Obj.getDysymtabLoadCommand();
  MachO::section Sec32 = Obj.getSection(PTSection.getRawDataRefImpl());
  uint32_t PTSectionSize = Sec32.size;
  unsigned FirstIndirectSymbol = Sec32.reserved1;
  const unsigned PTEntrySize = 4;
  unsigned NumPTEntries = PTSectionSize / PTEntrySize;
  unsigned PTEntryOffset = 0;

  assert((PTSectionSize % PTEntrySize) == 0 &&
         "Pointers section does not contain a whole number of stubs?");

  DEBUG(dbgs() << "Populating pointer table section "
               << Sections[PTSectionID].Name
               << ", Section ID " << PTSectionID << ", "
               << NumPTEntries << " entries, " << PTEntrySize
               << " bytes each:\n");

  for (unsigned i = 0; i < NumPTEntries; ++i) {
    unsigned SymbolIndex =
      Obj.getIndirectSymbolTableEntry(DySymTabCmd, FirstIndirectSymbol + i);
    symbol_iterator SI = Obj.getSymbolByIndex(SymbolIndex);
    StringRef IndirectSymbolName;
    SI->getName(IndirectSymbolName);
    DEBUG(dbgs() << "  " << IndirectSymbolName << ": index " << SymbolIndex
          << ", PT offset: " << PTEntryOffset << "\n");
    RelocationEntry RE(PTSectionID, PTEntryOffset,
                       MachO::GENERIC_RELOC_VANILLA, 0, false, 2);
    addRelocationForSymbol(RE, IndirectSymbolName);
    PTEntryOffset += PTEntrySize;
  }
}

bool RuntimeDyldMachO::isCompatibleFile(const object::ObjectFile &Obj) const {
  return Obj.isMachO();
}

template <typename Impl>
void RuntimeDyldMachOCRTPBase<Impl>::finalizeLoad(const ObjectFile &ObjImg,
                                                  ObjSectionToIDMap &SectionMap) {
  unsigned EHFrameSID = RTDYLD_INVALID_SECTION_ID;
  unsigned TextSID = RTDYLD_INVALID_SECTION_ID;
  unsigned ExceptTabSID = RTDYLD_INVALID_SECTION_ID;
  ObjSectionToIDMap::iterator i, e;

  for (i = SectionMap.begin(), e = SectionMap.end(); i != e; ++i) {
    const SectionRef &Section = i->first;
    StringRef Name;
    Section.getName(Name);
    if (Name == "__eh_frame")
      EHFrameSID = i->second;
    else if (Name == "__text")
      TextSID = i->second;
    else if (Name == "__gcc_except_tab")
      ExceptTabSID = i->second;
    else
      impl().finalizeSection(ObjImg, i->second, Section);
  }
  UnregisteredEHFrameSections.push_back(
    EHFrameRelatedSections(EHFrameSID, TextSID, ExceptTabSID));
}

template <typename Impl>
unsigned char *RuntimeDyldMachOCRTPBase<Impl>::processFDE(unsigned char *P,
                                                          int64_t DeltaForText,
                                                          int64_t DeltaForEH) {
  typedef typename Impl::TargetPtrT TargetPtrT;

  DEBUG(dbgs() << "Processing FDE: Delta for text: " << DeltaForText
               << ", Delta for EH: " << DeltaForEH << "\n");
  uint32_t Length = readBytesUnaligned(P, 4);
  P += 4;
  unsigned char *Ret = P + Length;
  uint32_t Offset = readBytesUnaligned(P, 4);
  if (Offset == 0) // is a CIE
    return Ret;

  P += 4;
  TargetPtrT FDELocation = readBytesUnaligned(P, sizeof(TargetPtrT));
  TargetPtrT NewLocation = FDELocation - DeltaForText;
  writeBytesUnaligned(NewLocation, P, sizeof(TargetPtrT));

  P += sizeof(TargetPtrT);

  // Skip the FDE address range
  P += sizeof(TargetPtrT);

  uint8_t Augmentationsize = *P;
  P += 1;
  if (Augmentationsize != 0) {
    TargetPtrT LSDA = readBytesUnaligned(P, sizeof(TargetPtrT));
    TargetPtrT NewLSDA = LSDA - DeltaForEH;
    writeBytesUnaligned(NewLSDA, P, sizeof(TargetPtrT));
  }

  return Ret;
}

static int64_t computeDelta(SectionEntry *A, SectionEntry *B) {
  int64_t ObjDistance = A->ObjAddress - B->ObjAddress;
  int64_t MemDistance = A->LoadAddress - B->LoadAddress;
  return ObjDistance - MemDistance;
}

template <typename Impl>
void RuntimeDyldMachOCRTPBase<Impl>::registerEHFrames() {

  if (!MemMgr)
    return;
  for (int i = 0, e = UnregisteredEHFrameSections.size(); i != e; ++i) {
    EHFrameRelatedSections &SectionInfo = UnregisteredEHFrameSections[i];
    if (SectionInfo.EHFrameSID == RTDYLD_INVALID_SECTION_ID ||
        SectionInfo.TextSID == RTDYLD_INVALID_SECTION_ID)
      continue;
    SectionEntry *Text = &Sections[SectionInfo.TextSID];
    SectionEntry *EHFrame = &Sections[SectionInfo.EHFrameSID];
    SectionEntry *ExceptTab = nullptr;
    if (SectionInfo.ExceptTabSID != RTDYLD_INVALID_SECTION_ID)
      ExceptTab = &Sections[SectionInfo.ExceptTabSID];

    int64_t DeltaForText = computeDelta(Text, EHFrame);
    int64_t DeltaForEH = 0;
    if (ExceptTab)
      DeltaForEH = computeDelta(ExceptTab, EHFrame);

    unsigned char *P = EHFrame->Address;
    unsigned char *End = P + EHFrame->Size;
    do {
      P = processFDE(P, DeltaForText, DeltaForEH);
    } while (P != End);

    MemMgr->registerEHFrames(EHFrame->Address, EHFrame->LoadAddress,
                             EHFrame->Size);
  }
  UnregisteredEHFrameSections.clear();
}

std::unique_ptr<RuntimeDyldMachO>
RuntimeDyldMachO::create(Triple::ArchType Arch, RTDyldMemoryManager *MM) {
  switch (Arch) {
  default:
    llvm_unreachable("Unsupported target for RuntimeDyldMachO.");
    break;
  case Triple::arm: return make_unique<RuntimeDyldMachOARM>(MM);
  case Triple::aarch64: return make_unique<RuntimeDyldMachOAArch64>(MM);
  case Triple::x86: return make_unique<RuntimeDyldMachOI386>(MM);
  case Triple::x86_64: return make_unique<RuntimeDyldMachOX86_64>(MM);
  }
}

std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
RuntimeDyldMachO::loadObject(const object::ObjectFile &O) {
  unsigned SectionStartIdx, SectionEndIdx;
  std::tie(SectionStartIdx, SectionEndIdx) = loadObjectImpl(O);
  return llvm::make_unique<LoadedMachOObjectInfo>(*this, SectionStartIdx,
                                                  SectionEndIdx);
}

} // end namespace llvm
