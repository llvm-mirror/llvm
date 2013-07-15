//===-- RuntimeDyld.h - Run-time dynamic linker for MC-JIT ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Interface for the runtime dynamic linker facilities of the MC-JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/ObjectBuffer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/Support/Memory.h"

namespace llvm {

class RuntimeDyldImpl;
class ObjectImage;

class RuntimeDyld {
  RuntimeDyld(const RuntimeDyld &) LLVM_DELETED_FUNCTION;
  void operator=(const RuntimeDyld &) LLVM_DELETED_FUNCTION;

  // RuntimeDyldImpl is the actual class. RuntimeDyld is just the public
  // interface.
  RuntimeDyldImpl *Dyld;
  RTDyldMemoryManager *MM;
protected:
  // Change the address associated with a section when resolving relocations.
  // Any relocations already associated with the symbol will be re-resolved.
  void reassignSectionAddress(unsigned SectionID, uint64_t Addr);
public:
  RuntimeDyld(RTDyldMemoryManager *);
  ~RuntimeDyld();

  /// Prepare the object contained in the input buffer for execution.
  /// Ownership of the input buffer is transferred to the ObjectImage
  /// instance returned from this function if successful. In the case of load
  /// failure, the input buffer will be deleted.
  ObjectImage *loadObject(ObjectBuffer *InputBuffer);

  /// Get the address of our local copy of the symbol. This may or may not
  /// be the address used for relocation (clients can copy the data around
  /// and resolve relocatons based on where they put it).
  void *getSymbolAddress(StringRef Name);

  /// Get the address of the target copy of the symbol. This is the address
  /// used for relocation.
  uint64_t getSymbolLoadAddress(StringRef Name);

  /// Resolve the relocations for all symbols we currently know about.
  void resolveRelocations();

  /// Map a section to its target address space value.
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  void mapSectionAddress(const void *LocalAddress, uint64_t TargetAddress);

  StringRef getErrorString();

  StringRef getEHFrameSection();
};

} // end namespace llvm

#endif
