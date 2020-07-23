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

#include "llvm/IR/Constants.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

/// A disassembler class for EVM.
class EVMDisassembler : public MCDisassembler {
public:

  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}
  ~EVMDisassembler() override = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
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
}


#include "EVMGenDisassemblerTables.inc"

static unsigned getPushSize(unsigned int opcode) {
  switch (opcode) {
#define CASE(x) \
  case EVM::PUSH##x:\
    return x
    CASE(1);
    CASE(2);
    CASE(3);
    CASE(4);
    CASE(5);
    CASE(6);
    CASE(7);
    CASE(8);
    CASE(9);
    CASE(10);
    CASE(11);
    CASE(12);
    CASE(13);
    CASE(14);
    CASE(15);
    CASE(16);
    CASE(17);
    CASE(18);
    CASE(19);
    CASE(20);
    CASE(21);
    CASE(22);
    CASE(23);
    CASE(24);
    CASE(25);
    CASE(26);
    CASE(27);
    CASE(28);
    CASE(29);
    CASE(30);
    CASE(31);
    CASE(32);
#undef CASE(x)
  default:
    llvm_unreachable("Invalid PUSH opcode.");
  }
}

static ConstantInt* extractPushOperand(ArrayRef<uint8_t> Bytes, uint64_t Address, unsigned opnd_length) {


  // Fail if we reach the end of stream.
  return nullptr;
}

static DecodeStatus addOperandToPush(MCInst &Instr, ArrayRef<uint8_t> Bytes, uint64_t Address, unsigned opnd_length) {
  // extract the operand value from 
  const ConstantInt* CI = extractPushOperand(Bytes, Address, opnd_length);
  if (!CI) {
    return MCDisassembler::Fail;
  }
  Instr.addOperand(MCOperand::createCImm(CI));
}



DecodeStatus EVMDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &CStream) const {
                          
  uint8_t insn;
  DecodeStatus Result = decodeInstruction(DecoderTable8, Instr, insn, Address, this, STI);
  Size = 1;

  // if it is an PUSH instruction then we need to add operands and consume some bytes
  switch (Instr.getOpcode()) {
    case EVM::PUSH1:
    case EVM::PUSH2:
    case EVM::PUSH3:
    case EVM::PUSH4:
    case EVM::PUSH5:
    case EVM::PUSH6:
    case EVM::PUSH7:
    case EVM::PUSH8:
    case EVM::PUSH9:
    case EVM::PUSH10:
    case EVM::PUSH11:
    case EVM::PUSH12:
    case EVM::PUSH13:
    case EVM::PUSH14:
    case EVM::PUSH15:
    case EVM::PUSH16:
    case EVM::PUSH17:
    case EVM::PUSH18:
    case EVM::PUSH19:
    case EVM::PUSH20:
    case EVM::PUSH21:
    case EVM::PUSH22:
    case EVM::PUSH23:
    case EVM::PUSH24:
    case EVM::PUSH25:
    case EVM::PUSH26:
    case EVM::PUSH27:
    case EVM::PUSH28:
    case EVM::PUSH29:
    case EVM::PUSH30:
    case EVM::PUSH31:
    case EVM::PUSH32: {
      unsigned push_size = getPushSize(Instr.getOpcode());
      // plus the length of operand;
      Size += push_size;
      DecodeStatus operandResult = addOperandToPush(Instr, Bytes, Address, push_size);
      if (operandResult == MCDisassembler::Fail) {
        return operandResult;
      }
      break;
    }
    default:
      break;
  }

  return Result;
}
