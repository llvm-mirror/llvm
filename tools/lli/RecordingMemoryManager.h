//===- RecordingMemoryManager.h - LLI MCJIT recording memory manager ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This memory manager allocates local storage and keeps a record of each
// allocation. Iterators are provided for all data and code allocations.
//
//===----------------------------------------------------------------------===//

#ifndef RECORDINGMEMORYMANAGER_H
#define RECORDINGMEMORYMANAGER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/JITMemoryManager.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Memory.h"
#include <utility>

namespace llvm {

class RecordingMemoryManager : public JITMemoryManager {
public:
  typedef std::pair<sys::MemoryBlock, unsigned> Allocation;

private:
  SmallVector<Allocation, 16> AllocatedDataMem;
  SmallVector<Allocation, 16> AllocatedCodeMem;

  // FIXME: This is part of a work around to keep sections near one another
  // when MCJIT performs relocations after code emission but before
  // the generated code is moved to the remote target.
  sys::MemoryBlock Near;
  sys::MemoryBlock allocateSection(uintptr_t Size);

public:
  RecordingMemoryManager() {}
  virtual ~RecordingMemoryManager();

  typedef SmallVectorImpl<Allocation>::const_iterator const_data_iterator;
  typedef SmallVectorImpl<Allocation>::const_iterator const_code_iterator;

  const_data_iterator data_begin() const { return AllocatedDataMem.begin(); }
  const_data_iterator   data_end() const { return AllocatedDataMem.end(); }
  const_code_iterator code_begin() const { return AllocatedCodeMem.begin(); }
  const_code_iterator   code_end() const { return AllocatedCodeMem.end(); }

  uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                       unsigned SectionID);

  uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                       unsigned SectionID, bool IsReadOnly);

  void *getPointerToNamedFunction(const std::string &Name,
                                  bool AbortOnFailure = true);

  bool finalizeMemory(std::string *ErrMsg) { return false; }

  // The following obsolete JITMemoryManager calls are stubbed out for
  // this model.
  void setMemoryWritable();
  void setMemoryExecutable();
  void setPoisonMemory(bool poison);
  void AllocateGOT();
  uint8_t *getGOTBase() const;
  uint8_t *startFunctionBody(const Function *F, uintptr_t &ActualSize);
  uint8_t *allocateStub(const GlobalValue* F, unsigned StubSize,
                        unsigned Alignment);
  void endFunctionBody(const Function *F, uint8_t *FunctionStart,
                       uint8_t *FunctionEnd);
  uint8_t *allocateSpace(intptr_t Size, unsigned Alignment);
  uint8_t *allocateGlobal(uintptr_t Size, unsigned Alignment);
  void deallocateFunctionBody(void *Body);
};

} // end namespace llvm

#endif
