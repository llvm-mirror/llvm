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

#include "llvm/IR/Type.h"

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for EVM.
class EVMDisassembler final : public MCDisassembler {
public:
  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx), Ctx(Ctx) {}
  virtual ~EVMDisassembler() = default;

  MCContext &Ctx;

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
    unsigned opcode = EVMSubtarget::get_push_opcode(length);
    Instr.clear();
    Instr.setOpcode(opcode);
    Size = 1 + length;

    if (length > 8) {
      // TODO: we have to extend it to 256 bit.
      APInt cimm(256, 0);
      for (unsigned i = 0; i < length; ++i) {
        cimm = (cimm << 8) + Bytes[1 + i];
      }

      Instr.addOperand(MCOperand::createCImm(ConstantInt::get(Ctx, cimm)));
    } else {
      uint64_t imm = 0;
      for (unsigned i = 0; i < length; ++i) {
        imm = (imm << 8) + Bytes[1 + i];
      }
      Instr.addOperand(MCOperand::createImm(imm));
    }
    return DecodeStatus::Success;
  }

  Result = decodeInstruction(DecoderTable8, Instr, Bytes[0], Address, this, STI);
  Size = 1;
  LLVM_DEBUG({
    if (Result != DecodeStatus::Success) {
      dbgs() << "Unsuccessfully decoding at: " << Address << "\n";
    }
  });
  return Result;
}

