//===--- Compression.cpp - Compression implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements compression functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compression.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#if LLVM_ENABLE_ZLIB == 1 && HAVE_ZLIB_H
#include <zlib.h>
#endif

using namespace llvm;

#if LLVM_ENABLE_ZLIB == 1 && HAVE_LIBZ
static int encodeZlibCompressionLevel(zlib::CompressionLevel Level) {
  switch (Level) {
    case zlib::NoCompression: return 0;
    case zlib::BestSpeedCompression: return 1;
    case zlib::DefaultCompression: return Z_DEFAULT_COMPRESSION;
    case zlib::BestSizeCompression: return 9;
  }
  llvm_unreachable("Invalid zlib::CompressionLevel!");
}

static zlib::Status encodeZlibReturnValue(int ReturnValue) {
  switch (ReturnValue) {
    case Z_OK: return zlib::StatusOK;
    case Z_MEM_ERROR: return zlib::StatusOutOfMemory;
    case Z_BUF_ERROR: return zlib::StatusBufferTooShort;
    case Z_STREAM_ERROR: return zlib::StatusInvalidArg;
    case Z_DATA_ERROR: return zlib::StatusInvalidData;
    default: llvm_unreachable("unknown zlib return status!");
  }
}

bool zlib::isAvailable() { return true; }
zlib::Status zlib::compress(StringRef InputBuffer,
                            OwningPtr<MemoryBuffer> &CompressedBuffer,
                            CompressionLevel Level) {
  unsigned long CompressedSize = ::compressBound(InputBuffer.size());
  OwningArrayPtr<char> TmpBuffer(new char[CompressedSize]);
  int CLevel = encodeZlibCompressionLevel(Level);
  Status Res = encodeZlibReturnValue(::compress2(
      (Bytef *)TmpBuffer.get(), &CompressedSize,
      (const Bytef *)InputBuffer.data(), InputBuffer.size(), CLevel));
  if (Res == StatusOK) {
    CompressedBuffer.reset(MemoryBuffer::getMemBufferCopy(
        StringRef(TmpBuffer.get(), CompressedSize)));
    // Tell MSan that memory initialized by zlib is valid.
    __msan_unpoison(CompressedBuffer->getBufferStart(), CompressedSize);
  }
  return Res;
}

zlib::Status zlib::uncompress(StringRef InputBuffer,
                              OwningPtr<MemoryBuffer> &UncompressedBuffer,
                              size_t UncompressedSize) {
  OwningArrayPtr<char> TmpBuffer(new char[UncompressedSize]);
  Status Res = encodeZlibReturnValue(
      ::uncompress((Bytef *)TmpBuffer.get(), (uLongf *)&UncompressedSize,
                   (const Bytef *)InputBuffer.data(), InputBuffer.size()));
  if (Res == StatusOK) {
    UncompressedBuffer.reset(MemoryBuffer::getMemBufferCopy(
        StringRef(TmpBuffer.get(), UncompressedSize)));
    // Tell MSan that memory initialized by zlib is valid.
    __msan_unpoison(UncompressedBuffer->getBufferStart(), UncompressedSize);
  }
  return Res;
}

#else
bool zlib::isAvailable() { return false; }
zlib::Status zlib::compress(StringRef InputBuffer,
                            OwningPtr<MemoryBuffer> &CompressedBuffer,
                            CompressionLevel Level) {
  return zlib::StatusUnsupported;
}
zlib::Status zlib::uncompress(StringRef InputBuffer,
                              OwningPtr<MemoryBuffer> &UncompressedBuffer,
                              size_t UncompressedSize) {
  return zlib::StatusUnsupported;
}
#endif

