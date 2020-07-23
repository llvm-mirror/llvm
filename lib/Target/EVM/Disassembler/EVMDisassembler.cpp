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

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "TargetInfo/EVMTargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for EVM.
class EVMDisassembler : public MCDisassembler {
public:
  enum EVM_CLASS {
    EVM_LD = 0x0,
    EVM_LDX = 0x1,
    EVM_ST = 0x2,
    EVM_STX = 0x3,
    EVM_ALU = 0x4,
    EVM_JMP = 0x5,
    EVM_JMP32 = 0x6,
    EVM_ALU64 = 0x7
  };

  enum EVM_SIZE {
    EVM_W = 0x0,
    EVM_H = 0x1,
    EVM_B = 0x2,
    EVM_DW = 0x3
  };

  enum EVM_MODE {
    EVM_IMM = 0x0,
    EVM_ABS = 0x1,
    EVM_IND = 0x2,
    EVM_MEM = 0x3,
    EVM_LEN = 0x4,
    EVM_MSH = 0x5,
    EVM_XADD = 0x6
  };

  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  ~EVMDisassembler() override = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  uint8_t getInstClass(uint64_t Inst) const { return (Inst >> 56) & 0x7; };
  uint8_t getInstSize(uint64_t Inst) const { return (Inst >> 59) & 0x3; };
  uint8_t getInstMode(uint64_t Inst) const { return (Inst >> 61) & 0x7; };
};

} // end anonymous namespace

static MCDisassembler *createEVMDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new EVMDisassembler(STI, Ctx);
}


extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeEVMDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheEVMTarget(),
                                         createEVMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheEVMleTarget(),
                                         createEVMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheEVMbeTarget(),
                                         createEVMDisassembler);
}

static const unsigned GPRDecoderTable[] = {
    EVM::R0,  EVM::R1,  EVM::R2,  EVM::R3,  EVM::R4,  EVM::R5,
    EVM::R6,  EVM::R7,  EVM::R8,  EVM::R9,  EVM::R10, EVM::R11};

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t /*Address*/,
                                           const void * /*Decoder*/) {
  if (RegNo > 11)
    return MCDisassembler::Fail;

  unsigned Reg = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static const unsigned GPR32DecoderTable[] = {
    EVM::W0,  EVM::W1,  EVM::W2,  EVM::W3,  EVM::W4,  EVM::W5,
    EVM::W6,  EVM::W7,  EVM::W8,  EVM::W9,  EVM::W10, EVM::W11};

static DecodeStatus DecodeGPR32RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t /*Address*/,
                                             const void * /*Decoder*/) {
  if (RegNo > 11)
    return MCDisassembler::Fail;

  unsigned Reg = GPR32DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus decodeMemoryOpValue(MCInst &Inst, unsigned Insn,
                                        uint64_t Address, const void *Decoder) {
  unsigned Register = (Insn >> 16) & 0xf;
  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[Register]));
  unsigned Offset = (Insn & 0xffff);
  Inst.addOperand(MCOperand::createImm(SignExtend32<16>(Offset)));

  return MCDisassembler::Success;
}

#include "EVMGenDisassemblerTables.inc"
static DecodeStatus readInstruction64(ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      uint64_t &Size, uint64_t &Insn,
                                      bool IsLittleEndian) {
  uint64_t Lo, Hi;

  if (Bytes.size() < 8) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  Size = 8;
  if (IsLittleEndian) {
    Hi = (Bytes[0] << 24) | (Bytes[1] << 16) | (Bytes[2] << 0) | (Bytes[3] << 8);
    Lo = (Bytes[4] << 0) | (Bytes[5] << 8) | (Bytes[6] << 16) | (Bytes[7] << 24);
  } else {
    Hi = (Bytes[0] << 24) | ((Bytes[1] & 0x0F) << 20) | ((Bytes[1] & 0xF0) << 12) |
         (Bytes[2] << 8) | (Bytes[3] << 0);
    Lo = (Bytes[4] << 24) | (Bytes[5] << 16) | (Bytes[6] << 8) | (Bytes[7] << 0);
  }
  Insn = Make_64(Hi, Lo);

  return MCDisassembler::Success;
}

DecodeStatus EVMDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &CStream) const {
  bool IsLittleEndian = getContext().getAsmInfo()->isLittleEndian();
  uint64_t Insn, Hi;
  DecodeStatus Result;

  Result = readInstruction64(Bytes, Address, Size, Insn, IsLittleEndian);
  if (Result == MCDisassembler::Fail) return MCDisassembler::Fail;

  uint8_t InstClass = getInstClass(Insn);
  uint8_t InstMode = getInstMode(Insn);
  if ((InstClass == EVM_LDX || InstClass == EVM_STX) &&
      getInstSize(Insn) != EVM_DW &&
      (InstMode == EVM_MEM || InstMode == EVM_XADD) &&
      STI.getFeatureBits()[EVM::ALU32])
    Result = decodeInstruction(DecoderTableEVMALU3264, Instr, Insn, Address,
                               this, STI);
  else
    Result = decodeInstruction(DecoderTableEVM64, Instr, Insn, Address, this,
                               STI);

  if (Result == MCDisassembler::Fail) return MCDisassembler::Fail;

  switch (Instr.getOpcode()) {
  case EVM::LD_imm64:
  case EVM::LD_pseudo: {
    if (Bytes.size() < 16) {
      Size = 0;
      return MCDisassembler::Fail;
    }
    Size = 16;
    if (IsLittleEndian)
      Hi = (Bytes[12] << 0) | (Bytes[13] << 8) | (Bytes[14] << 16) | (Bytes[15] << 24);
    else
      Hi = (Bytes[12] << 24) | (Bytes[13] << 16) | (Bytes[14] << 8) | (Bytes[15] << 0);
    auto& Op = Instr.getOperand(1);
    Op.setImm(Make_64(Hi, Op.getImm()));
    break;
  }
  case EVM::LD_ABS_B:
  case EVM::LD_ABS_H:
  case EVM::LD_ABS_W:
  case EVM::LD_IND_B:
  case EVM::LD_IND_H:
  case EVM::LD_IND_W: {
    auto Op = Instr.getOperand(0);
    Instr.clear();
    Instr.addOperand(MCOperand::createReg(EVM::R6));
    Instr.addOperand(Op);
    break;
  }
  }

  return Result;
}

typedef DecodeStatus (*DecodeFunc)(MCInst &MI, unsigned insn, uint64_t Address,
                                   const void *Decoder);
