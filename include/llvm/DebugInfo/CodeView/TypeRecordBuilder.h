//===- TypeRecordBuilder.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDBUILDER_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace codeview {

class TypeRecordBuilder {
private:
  TypeRecordBuilder(const TypeRecordBuilder &) = delete;
  TypeRecordBuilder &operator=(const TypeRecordBuilder &) = delete;

public:
  explicit TypeRecordBuilder(TypeRecordKind Kind);

  void writeUInt8(uint8_t Value);
  void writeInt16(int16_t Value);
  void writeUInt16(uint16_t Value);
  void writeInt32(int32_t Value);
  void writeUInt32(uint32_t Value);
  void writeInt64(int64_t Value);
  void writeUInt64(uint64_t Value);
  void writeTypeIndex(TypeIndex TypeInd);
  void writeTypeRecordKind(TypeRecordKind Kind);
  void writeEncodedInteger(int64_t Value);
  void writeEncodedSignedInteger(int64_t Value);
  void writeEncodedUnsignedInteger(uint64_t Value);
  void writeNullTerminatedString(const char *Value);
  void writeNullTerminatedString(StringRef Value);

  llvm::StringRef str();

  uint64_t size() const { return Stream.tell(); }

private:
  llvm::SmallVector<char, 256> Buffer;
  llvm::raw_svector_ostream Stream;
  llvm::support::endian::Writer<llvm::support::endianness::little> Writer;
};
}
}

#endif
