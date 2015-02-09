//===-- RuntimeDyldELF.h - Run-time dynamic linker for MC-JIT ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ELF support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDELF_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDELF_H

#include "RuntimeDyldImpl.h"
#include "llvm/ADT/DenseMap.h"

using namespace llvm;

namespace llvm {
namespace {
// Helper for extensive error checking in debug builds.
std::error_code Check(std::error_code Err) {
  if (Err) {
    report_fatal_error(Err.message());
  }
  return Err;
}

} // end anonymous namespace

class RuntimeDyldELF : public RuntimeDyldImpl {

  void resolveRelocation(const SectionEntry &Section, uint64_t Offset,
                         uint64_t Value, uint32_t Type, int64_t Addend,
                         uint64_t SymOffset = 0);

  void resolveX86_64Relocation(const SectionEntry &Section, uint64_t Offset,
                               uint64_t Value, uint32_t Type, int64_t Addend,
                               uint64_t SymOffset);

  void resolveX86Relocation(const SectionEntry &Section, uint64_t Offset,
                            uint32_t Value, uint32_t Type, int32_t Addend);

  void resolveAArch64Relocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend);

  void resolveARMRelocation(const SectionEntry &Section, uint64_t Offset,
                            uint32_t Value, uint32_t Type, int32_t Addend);

  void resolveMIPSRelocation(const SectionEntry &Section, uint64_t Offset,
                             uint32_t Value, uint32_t Type, int32_t Addend);

  void resolvePPC64Relocation(const SectionEntry &Section, uint64_t Offset,
                              uint64_t Value, uint32_t Type, int64_t Addend);

  void resolveSystemZRelocation(const SectionEntry &Section, uint64_t Offset,
                                uint64_t Value, uint32_t Type, int64_t Addend);

  unsigned getMaxStubSize() override {
    if (Arch == Triple::aarch64 || Arch == Triple::aarch64_be)
      return 20; // movz; movk; movk; movk; br
    if (Arch == Triple::arm || Arch == Triple::thumb)
      return 8; // 32-bit instruction and 32-bit address
    else if (Arch == Triple::mipsel || Arch == Triple::mips)
      return 16;
    else if (Arch == Triple::ppc64 || Arch == Triple::ppc64le)
      return 44;
    else if (Arch == Triple::x86_64)
      return 6; // 2-byte jmp instruction + 32-bit relative address
    else if (Arch == Triple::systemz)
      return 16;
    else
      return 0;
  }

  unsigned getStubAlignment() override {
    if (Arch == Triple::systemz)
      return 8;
    else
      return 1;
  }

  void findPPC64TOCSection(const ObjectFile &Obj,
                           ObjSectionToIDMap &LocalSections,
                           RelocationValueRef &Rel);
  void findOPDEntrySection(const ObjectFile &Obj,
                           ObjSectionToIDMap &LocalSections,
                           RelocationValueRef &Rel);

  uint64_t findGOTEntry(uint64_t LoadAddr, uint64_t Offset);
  size_t getGOTEntrySize();

  void updateGOTEntries(StringRef Name, uint64_t Addr) override;

  // Relocation entries for symbols whose position-independent offset is
  // updated in a global offset table.
  typedef SmallVector<RelocationValueRef, 2> GOTRelocations;
  GOTRelocations GOTEntries; // List of entries requiring finalization.
  SmallVector<std::pair<SID, GOTRelocations>, 8> GOTs; // Allocated tables.

  // When a module is loaded we save the SectionID of the EH frame section
  // in a table until we receive a request to register all unregistered
  // EH frame sections with the memory manager.
  SmallVector<SID, 2> UnregisteredEHFrameSections;
  SmallVector<SID, 2> RegisteredEHFrameSections;

public:
  RuntimeDyldELF(RTDyldMemoryManager *mm);
  virtual ~RuntimeDyldELF();

  std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
  loadObject(const object::ObjectFile &O) override;

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override;
  relocation_iterator
  processRelocationRef(unsigned SectionID, relocation_iterator RelI,
                       const ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override;
  bool isCompatibleFile(const object::ObjectFile &Obj) const override;
  void registerEHFrames() override;
  void deregisterEHFrames() override;
  void finalizeLoad(const ObjectFile &Obj,
                    ObjSectionToIDMap &SectionMap) override;
};

} // end namespace llvm

#endif
