//===-- EVMDisassembler.cpp - Disassembler for EVM --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the EVMDisassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "Utils/EVMBaseInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "evm-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
class EVMDisassembler : public MCDisassembler {

public:
  EVMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &VStream,
                              raw_ostream &CStream) const override;
};
} // end anonymous namespace

static MCDisassembler *createEVMDisassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new EVMDisassembler(STI, Ctx);
}

extern "C" void LLVMInitializeEVMDisassembler() {
  // Register the disassembler for each target.
  TargetRegistry::RegisterMCDisassembler(getTheEVM32Target(),
                                         createEVMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheEVM64Target(),
                                         createEVMDisassembler);
}

static const unsigned GPRDecoderTable[] = {
  EVM::X0,  EVM::X1,  EVM::X2,  EVM::X3,
  EVM::X4,  EVM::X5,  EVM::X6,  EVM::X7,
  EVM::X8,  EVM::X9,  EVM::X10, EVM::X11,
  EVM::X12, EVM::X13, EVM::X14, EVM::X15,
  EVM::X16, EVM::X17, EVM::X18, EVM::X19,
  EVM::X20, EVM::X21, EVM::X22, EVM::X23,
  EVM::X24, EVM::X25, EVM::X26, EVM::X27,
  EVM::X28, EVM::X29, EVM::X30, EVM::X31
};

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, uint64_t RegNo,
                                           uint64_t Address,
                                           const void *Decoder) {
  if (RegNo > sizeof(GPRDecoderTable))
    return MCDisassembler::Fail;

  // We must define our own mapping from RegNo to register identifier.
  // Accessing index RegNo in the register class will work in the case that
  // registers were added in ascending order, but not in general.
  unsigned Reg = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static const unsigned FPR32DecoderTable[] = {
  EVM::F0_32,  EVM::F1_32,  EVM::F2_32,  EVM::F3_32,
  EVM::F4_32,  EVM::F5_32,  EVM::F6_32,  EVM::F7_32,
  EVM::F8_32,  EVM::F9_32,  EVM::F10_32, EVM::F11_32,
  EVM::F12_32, EVM::F13_32, EVM::F14_32, EVM::F15_32,
  EVM::F16_32, EVM::F17_32, EVM::F18_32, EVM::F19_32,
  EVM::F20_32, EVM::F21_32, EVM::F22_32, EVM::F23_32,
  EVM::F24_32, EVM::F25_32, EVM::F26_32, EVM::F27_32,
  EVM::F28_32, EVM::F29_32, EVM::F30_32, EVM::F31_32
};

static DecodeStatus DecodeFPR32RegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const void *Decoder) {
  if (RegNo > sizeof(FPR32DecoderTable))
    return MCDisassembler::Fail;

  // We must define our own mapping from RegNo to register identifier.
  // Accessing index RegNo in the register class will work in the case that
  // registers were added in ascending order, but not in general.
  unsigned Reg = FPR32DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR32CRegisterClass(MCInst &Inst, uint64_t RegNo,
                                              uint64_t Address,
                                              const void *Decoder) {
  if (RegNo > 8) {
    return MCDisassembler::Fail;
  }
  unsigned Reg = FPR32DecoderTable[RegNo + 8];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static const unsigned FPR64DecoderTable[] = {
  EVM::F0_64,  EVM::F1_64,  EVM::F2_64,  EVM::F3_64,
  EVM::F4_64,  EVM::F5_64,  EVM::F6_64,  EVM::F7_64,
  EVM::F8_64,  EVM::F9_64,  EVM::F10_64, EVM::F11_64,
  EVM::F12_64, EVM::F13_64, EVM::F14_64, EVM::F15_64,
  EVM::F16_64, EVM::F17_64, EVM::F18_64, EVM::F19_64,
  EVM::F20_64, EVM::F21_64, EVM::F22_64, EVM::F23_64,
  EVM::F24_64, EVM::F25_64, EVM::F26_64, EVM::F27_64,
  EVM::F28_64, EVM::F29_64, EVM::F30_64, EVM::F31_64
};

static DecodeStatus DecodeFPR64RegisterClass(MCInst &Inst, uint64_t RegNo,
                                             uint64_t Address,
                                             const void *Decoder) {
  if (RegNo > sizeof(FPR64DecoderTable))
    return MCDisassembler::Fail;

  // We must define our own mapping from RegNo to register identifier.
  // Accessing index RegNo in the register class will work in the case that
  // registers were added in ascending order, but not in general.
  unsigned Reg = FPR64DecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeFPR64CRegisterClass(MCInst &Inst, uint64_t RegNo,
                                              uint64_t Address,
                                              const void *Decoder) {
  if (RegNo > 8) {
    return MCDisassembler::Fail;
  }
  unsigned Reg = FPR64DecoderTable[RegNo + 8];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRNoX0RegisterClass(MCInst &Inst, uint64_t RegNo,
                                               uint64_t Address,
                                               const void *Decoder) {
  if (RegNo == 0) {
    return MCDisassembler::Fail;
  }

  return DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeGPRNoX0X2RegisterClass(MCInst &Inst, uint64_t RegNo,
                                                 uint64_t Address,
                                                 const void *Decoder) {
  if (RegNo == 2) {
    return MCDisassembler::Fail;
  }

  return DecodeGPRNoX0RegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeGPRCRegisterClass(MCInst &Inst, uint64_t RegNo,
                                            uint64_t Address,
                                            const void *Decoder) {
  if (RegNo > 8)
    return MCDisassembler::Fail;

  unsigned Reg = GPRDecoderTable[RegNo + 8];
  Inst.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

// Add implied SP operand for instructions *SP compressed instructions. The SP
// operand isn't explicitly encoded in the instruction.
static void addImplySP(MCInst &Inst, int64_t Address, const void *Decoder) {
  if (Inst.getOpcode() == EVM::C_LWSP || Inst.getOpcode() == EVM::C_SWSP ||
      Inst.getOpcode() == EVM::C_LDSP || Inst.getOpcode() == EVM::C_SDSP ||
      Inst.getOpcode() == EVM::C_FLWSP ||
      Inst.getOpcode() == EVM::C_FSWSP ||
      Inst.getOpcode() == EVM::C_FLDSP ||
      Inst.getOpcode() == EVM::C_FSDSP ||
      Inst.getOpcode() == EVM::C_ADDI4SPN) {
    DecodeGPRRegisterClass(Inst, 2, Address, Decoder);
  }
  if (Inst.getOpcode() == EVM::C_ADDI16SP) {
    DecodeGPRRegisterClass(Inst, 2, Address, Decoder);
    DecodeGPRRegisterClass(Inst, 2, Address, Decoder);
  }
}

template <unsigned N>
static DecodeStatus decodeUImmOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  addImplySP(Inst, Address, Decoder);
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeUImmNonZeroOperand(MCInst &Inst, uint64_t Imm,
                                             int64_t Address,
                                             const void *Decoder) {
  if (Imm == 0)
    return MCDisassembler::Fail;
  return decodeUImmOperand<N>(Inst, Imm, Address, Decoder);
}

template <unsigned N>
static DecodeStatus decodeSImmOperand(MCInst &Inst, uint64_t Imm,
                                      int64_t Address, const void *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  addImplySP(Inst, Address, Decoder);
  // Sign-extend the number in the bottom N bits of Imm
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm)));
  return MCDisassembler::Success;
}

template <unsigned N>
static DecodeStatus decodeSImmNonZeroOperand(MCInst &Inst, uint64_t Imm,
                                             int64_t Address,
                                             const void *Decoder) {
  if (Imm == 0)
    return MCDisassembler::Fail;
  return decodeSImmOperand<N>(Inst, Imm, Address, Decoder);
}

template <unsigned N>
static DecodeStatus decodeSImmOperandAndLsl1(MCInst &Inst, uint64_t Imm,
                                             int64_t Address,
                                             const void *Decoder) {
  assert(isUInt<N>(Imm) && "Invalid immediate");
  // Sign-extend the number in the bottom N bits of Imm after accounting for
  // the fact that the N bit immediate is stored in N-1 bits (the LSB is
  // always zero)
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm << 1)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCLUIImmOperand(MCInst &Inst, uint64_t Imm,
                                         int64_t Address,
                                         const void *Decoder) {
  assert(isUInt<6>(Imm) && "Invalid immediate");
  if (Imm > 31) {
    Imm = (SignExtend64<6>(Imm) & 0xfffff);
  }
  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeFRMArg(MCInst &Inst, uint64_t Imm,
                                 int64_t Address,
                                 const void *Decoder) {
  assert(isUInt<3>(Imm) && "Invalid immediate");
  if (!llvm::EVMFPRndMode::isValidRoundingMode(Imm))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

#include "EVMGenDisassemblerTables.inc"

DecodeStatus EVMDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream &OS,
                                               raw_ostream &CS) const {
  // TODO: This will need modification when supporting instruction set
  // extensions with instructions > 32-bits (up to 176 bits wide).
  uint32_t Insn;
  DecodeStatus Result;

  // It's a 32 bit instruction if bit 0 and 1 are 1.
  if ((Bytes[0] & 0x3) == 0x3) {
    if (Bytes.size() < 4) {
      Size = 0;
      return MCDisassembler::Fail;
    }
    Insn = support::endian::read32le(Bytes.data());
    LLVM_DEBUG(dbgs() << "Trying EVM32 table :\n");
    Result = decodeInstruction(DecoderTable32, MI, Insn, Address, this, STI);
    Size = 4;
  } else {
    if (Bytes.size() < 2) {
      Size = 0;
      return MCDisassembler::Fail;
    }
    Insn = support::endian::read16le(Bytes.data());

    if (!STI.getFeatureBits()[EVM::Feature64Bit]) {
      LLVM_DEBUG(
          dbgs() << "Trying EVM32Only_16 table (16-bit Instruction):\n");
      // Calling the auto-generated decoder function.
      Result = decodeInstruction(DecoderTableEVM32Only_16, MI, Insn, Address,
                                 this, STI);
      if (Result != MCDisassembler::Fail) {
        Size = 2;
        return Result;
      }
    }

    LLVM_DEBUG(dbgs() << "Trying EVM_C table (16-bit Instruction):\n");
    // Calling the auto-generated decoder function.
    Result = decodeInstruction(DecoderTable16, MI, Insn, Address, this, STI);
    Size = 2;
  }

  return Result;
}
