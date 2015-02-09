//===-- ARMAsmBackendELF.h  ARM Asm Backend ELF -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ELFARMASMBACKEND_H
#define LLVM_LIB_TARGET_ARM_ELFARMASMBACKEND_H

using namespace llvm;
namespace {
class ARMAsmBackendELF : public ARMAsmBackend {
public:
  uint8_t OSABI;
  ARMAsmBackendELF(const Target &T, StringRef TT, uint8_t OSABI, bool IsLittle)
      : ARMAsmBackend(T, TT, IsLittle), OSABI(OSABI) {}

  MCObjectWriter *createObjectWriter(raw_ostream &OS) const override {
    return createARMELFObjectWriter(OS, OSABI, isLittle());
  }
};
}

#endif
