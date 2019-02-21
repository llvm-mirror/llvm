//==- EVMDisassembler.cpp - Disassembler for EVM -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the EVM Disassembler.
///
/// It contains code to translate the data produced by the decoder into
/// MCInsts.
///
//===----------------------------------------------------------------------===//

#include "InstPrinter/EVMInstPrinter.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

#include "EVMGenDisassemblerTables.inc"

namespace {
static constexpr int EVMInstructionTableSize = 256;

class EVMDisassembler final : public MCDisassembler {
  std::unique_ptr<const MCInstrInfo> MCII;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
  DecodeStatus onSymbolStart(StringRef Name, uint64_t &Size,
                             ArrayRef<uint8_t> Bytes, uint64_t Address,
                             raw_ostream &VStream,
                             raw_ostream &CStream) const override;

public:
  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                  std::unique_ptr<const MCInstrInfo> MCII)
      : MCDisassembler(STI, Ctx), MCII(std::move(MCII)) {}
};
} // end anonymous namespace

static MCDisassembler *createEVMDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  std::unique_ptr<const MCInstrInfo> MCII(T.createMCInstrInfo());
  return new EVMDisassembler(STI, Ctx, std::move(MCII));
}

extern "C" void LLVMInitializeEVMDisassembler() {
  // Register the disassembler for each target.
  TargetRegistry::RegisterMCDisassembler(getTheEVMTarget(),
                                         createEVMDisassembler);
}

static int nextByte(ArrayRef<uint8_t> Bytes, uint64_t &Size) {
  if (Size >= Bytes.size())
    return -1;
  auto V = Bytes[Size];
  Size++;
  return V;
}


MCDisassembler::DecodeStatus EVMDisassembler::onSymbolStart(
    StringRef Name, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &VStream, raw_ostream &CStream) const {

}

MCDisassembler::DecodeStatus EVMDisassembler::getInstruction(
    MCInst &MI, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t /*Address*/,
    raw_ostream & /*OS*/, raw_ostream &CS) const {

  return MCDisassembler::Success;
}
