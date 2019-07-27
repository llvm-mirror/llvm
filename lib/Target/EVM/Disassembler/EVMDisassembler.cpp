//===- EVMDisassembler.cpp - Disassembler for EVM ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the EVM Disassembler.
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMRegisterInfo.h"
#include "EVMSubtarget.h"

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "TargetInfo/EVMTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for EVM.
class EVMDisassembler final : public MCDisassembler {
public:
  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  virtual ~EVMDisassembler() = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};
}

static MCDisassembler *createEVMDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new EVMDisassembler(STI, Ctx);
}


extern "C" void LLVMInitializeEVMDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheEVMTarget(),
                                         createEVMDisassembler);
}

#include "EVMGenDisassemblerTables.inc"

static unsigned get_push_opcode(unsigned s) {
  switch (s) {
    default:
      llvm_unreachable("incorrect size or unimplemented");
    case 1: return EVM::PUSH1;
    case 2: return EVM::PUSH2;
    case 3: return EVM::PUSH3;
    case 4: return EVM::PUSH4;
    case 5: return EVM::PUSH5;
    case 6: return EVM::PUSH6;
    case 7: return EVM::PUSH7;
    case 8: return EVM::PUSH8;
  }
}

DecodeStatus EVMDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &VStream,
                                             raw_ostream &CStream) const {
  DecodeStatus Result = DecodeStatus::Fail;

  // handle PUSH
  unsigned opcode = Bytes[0];
  if (opcode >= 0x60 && opcode <= 0x7f) {
    unsigned length = opcode - 0x60 + 1;
    unsigned opcode = get_push_opcode(length);
     
    for (unsigned i = 0; i < length; ++i) {

    }

    return DecodeStatus::Success;
  }

  //Result = decodeInstruction(DecoderTable8, MI, Insn, Address, this, STI);
  LLVM_DEBUG({
    if (Result != DecodeStatus::Success) {
      dbgs() << "Unsuccessfully decoding at: " << Address << "\n";
    }
  });
  return Result;
}

