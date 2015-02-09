//===-- llvm/CodeGen/ByteStreamer.h - ByteStreamer class --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a class that can take bytes that would normally be
// streamed via the AsmPrinter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_BYTESTREAMER_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_BYTESTREAMER_H

#include "DIEHash.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {
class ByteStreamer {
 public:
  virtual ~ByteStreamer() {}

  // For now we're just handling the calls we need for dwarf emission/hashing.
  virtual void EmitInt8(uint8_t Byte, const Twine &Comment = "") = 0;
  virtual void EmitSLEB128(uint64_t DWord, const Twine &Comment = "") = 0;
  virtual void EmitULEB128(uint64_t DWord, const Twine &Comment = "") = 0;
};

class APByteStreamer : public ByteStreamer {
private:
  AsmPrinter &AP;

public:
  APByteStreamer(AsmPrinter &Asm) : AP(Asm) {}
  void EmitInt8(uint8_t Byte, const Twine &Comment) override {
    AP.OutStreamer.AddComment(Comment);
    AP.EmitInt8(Byte);
  }
  void EmitSLEB128(uint64_t DWord, const Twine &Comment) override {
    AP.OutStreamer.AddComment(Comment);
    AP.EmitSLEB128(DWord);
  }
  void EmitULEB128(uint64_t DWord, const Twine &Comment) override {
    AP.OutStreamer.AddComment(Comment);
    AP.EmitULEB128(DWord);
  }
};

class HashingByteStreamer : public ByteStreamer {
 private:
  DIEHash &Hash;
 public:
 HashingByteStreamer(DIEHash &H) : Hash(H) {}
  void EmitInt8(uint8_t Byte, const Twine &Comment) override {
    Hash.update(Byte);
  }
  void EmitSLEB128(uint64_t DWord, const Twine &Comment) override {
    Hash.addSLEB128(DWord);
  }
  void EmitULEB128(uint64_t DWord, const Twine &Comment) override {
    Hash.addULEB128(DWord);
  }
};
}

#endif
