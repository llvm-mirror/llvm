//===-- RuntimeDyldMachO.h - Run-time dynamic linker for MC-JIT ---*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MachO support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_RUNTIME_DYLD_MACHO_H
#define LLVM_RUNTIME_DYLD_MACHO_H

#include "RuntimeDyldImpl.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::object;


namespace llvm {
class RuntimeDyldMachO : public RuntimeDyldImpl {
  bool resolveI386Relocation(uint8_t *LocalAddress,
                             uint64_t FinalAddress,
                             uint64_t Value,
                             bool isPCRel,
                             unsigned Type,
                             unsigned Size,
                             int64_t Addend);
  bool resolveX86_64Relocation(uint8_t *LocalAddress,
                               uint64_t FinalAddress,
                               uint64_t Value,
                               bool isPCRel,
                               unsigned Type,
                               unsigned Size,
                               int64_t Addend);
  bool resolveARMRelocation(uint8_t *LocalAddress,
                            uint64_t FinalAddress,
                            uint64_t Value,
                            bool isPCRel,
                            unsigned Type,
                            unsigned Size,
                            int64_t Addend);

  void resolveRelocation(const SectionEntry &Section,
                         uint64_t Offset,
                         uint64_t Value,
                         uint32_t Type,
                         int64_t Addend,
                         bool isPCRel,
                         unsigned Size);
public:
  RuntimeDyldMachO(RTDyldMemoryManager *mm) : RuntimeDyldImpl(mm) {}

  virtual void resolveRelocation(const RelocationEntry &RE, uint64_t Value);
  virtual void processRelocationRef(unsigned SectionID,
                                    RelocationRef RelI,
                                    ObjectImage &Obj,
                                    ObjSectionToIDMap &ObjSectionToID,
                                    const SymbolTableMap &Symbols,
                                    StubMap &Stubs);
  virtual bool isCompatibleFormat(const ObjectBuffer *Buffer) const;
  virtual StringRef getEHFrameSection();
};

} // end namespace llvm

#endif
