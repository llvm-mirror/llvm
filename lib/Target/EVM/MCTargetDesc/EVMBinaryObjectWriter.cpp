//===-- EVMBinaryObjectWriter.cpp - EVM Binary Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/BinaryFormat/EVM.h"
#include "llvm/MC/MCObjectWriter.h"

using namespace llvm;

namespace {

class EVMBinaryObjectWriter : public MCObjectTargetWriter {
public:
  EVMBinaryObjectWriter();
  ~EVMBinaryObjectWriter() override = default;

  virtual Triple::ObjectFormatType getFormat() const override;
};

} // end anonymous namespace

EVMBinaryObjectWriter::EVMBinaryObjectWriter()
    : MCObjectTargetWriter() {}

Triple::ObjectFormatType EVMBinaryObjectWriter::getFormat() const {
  return Triple::EVMBinary;
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createEVMBinaryObjectWriter() {
  return std::make_unique<EVMBinaryObjectWriter>();
}
