//===-- ARMAsmBackend.cpp - ARM Assembler Backend -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMAsmBackend.h"
#include "MCTargetDesc/ARMAsmBackendDarwin.h"
#include "MCTargetDesc/ARMAsmBackendELF.h"
#include "MCTargetDesc/ARMAsmBackendWinCOFF.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMFixupKinds.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MachO.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
class ARMELFObjectWriter : public MCELFObjectTargetWriter {
public:
  ARMELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit*/ false, OSABI, ELF::EM_ARM,
                                /*HasRelocationAddend*/ false) {}
};

const MCFixupKindInfo &ARMAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo InfosLE[ARM::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // ARMFixupKinds.h.
      //
      // Name                      Offset (bits) Size (bits)     Flags
      {"fixup_arm_ldst_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_ldst_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_10_unscaled", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_pcrel_10", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_10", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_thumb_adr_pcrel_10", 0, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_adr_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_adr_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_condbranch", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbranch", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_condbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_uncondbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_br", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbl", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_condbl", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_blx", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_bl", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_blx", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cb", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cp", 0, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_bcc", 0, 8, MCFixupKindInfo::FKF_IsPCRel},
      // movw / movt: 16-bits immediate but scattered into two chunks 0 - 12, 16
      // - 19.
      {"fixup_arm_movt_hi16", 0, 20, 0},
      {"fixup_arm_movw_lo16", 0, 20, 0},
      {"fixup_t2_movt_hi16", 0, 20, 0},
      {"fixup_t2_movw_lo16", 0, 20, 0},
  };
  const static MCFixupKindInfo InfosBE[ARM::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // ARMFixupKinds.h.
      //
      // Name                      Offset (bits) Size (bits)     Flags
      {"fixup_arm_ldst_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_ldst_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_10_unscaled", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_pcrel_10", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_10", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_thumb_adr_pcrel_10", 8, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_adr_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_adr_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_condbranch", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbranch", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_condbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_uncondbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_br", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbl", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_condbl", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_blx", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_bl", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_blx", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cb", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cp", 8, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_bcc", 8, 8, MCFixupKindInfo::FKF_IsPCRel},
      // movw / movt: 16-bits immediate but scattered into two chunks 0 - 12, 16
      // - 19.
      {"fixup_arm_movt_hi16", 12, 20, 0},
      {"fixup_arm_movw_lo16", 12, 20, 0},
      {"fixup_t2_movt_hi16", 12, 20, 0},
      {"fixup_t2_movw_lo16", 12, 20, 0},
  };

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return (IsLittleEndian ? InfosLE : InfosBE)[Kind - FirstTargetFixupKind];
}

void ARMAsmBackend::handleAssemblerFlag(MCAssemblerFlag Flag) {
  switch (Flag) {
  default:
    break;
  case MCAF_Code16:
    setIsThumb(true);
    break;
  case MCAF_Code32:
    setIsThumb(false);
    break;
  }
}
} // end anonymous namespace

unsigned ARMAsmBackend::getRelaxedOpcode(unsigned Op) const {
  bool HasThumb2 = STI->getFeatureBits()[ARM::FeatureThumb2];

  switch (Op) {
  default:
    return Op;
  case ARM::tBcc:
    return HasThumb2 ? (unsigned)ARM::t2Bcc : Op;
  case ARM::tLDRpci:
    return HasThumb2 ? (unsigned)ARM::t2LDRpci : Op;
  case ARM::tADR:
    return HasThumb2 ? (unsigned)ARM::t2ADR : Op;
  case ARM::tB:
    return HasThumb2 ? (unsigned)ARM::t2B : Op;
  case ARM::tCBZ:
    return ARM::tHINT;
  case ARM::tCBNZ:
    return ARM::tHINT;
  }
}

bool ARMAsmBackend::mayNeedRelaxation(const MCInst &Inst) const {
  if (getRelaxedOpcode(Inst.getOpcode()) != Inst.getOpcode())
    return true;
  return false;
}

const char *ARMAsmBackend::reasonForFixupRelaxation(const MCFixup &Fixup,
                                                    uint64_t Value) const {
  switch ((unsigned)Fixup.getKind()) {
  case ARM::fixup_arm_thumb_br: {
    // Relaxing tB to t2B. tB has a signed 12-bit displacement with the
    // low bit being an implied zero. There's an implied +4 offset for the
    // branch, so we adjust the other way here to determine what's
    // encodable.
    //
    // Relax if the value is too big for a (signed) i8.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset > 2046 || Offset < -2048)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_arm_thumb_bcc: {
    // Relaxing tBcc to t2Bcc. tBcc has a signed 9-bit displacement with the
    // low bit being an implied zero. There's an implied +4 offset for the
    // branch, so we adjust the other way here to determine what's
    // encodable.
    //
    // Relax if the value is too big for a (signed) i8.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset > 254 || Offset < -256)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_cp: {
    // If the immediate is negative, greater than 1020, or not a multiple
    // of four, the wide version of the instruction must be used.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset & 3)
      return "misaligned pc-relative fixup value";
    else if (Offset > 1020 || Offset < 0)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_arm_thumb_cb: {
    // If we have a Thumb CBZ or CBNZ instruction and its target is the next
    // instruction it is is actually out of range for the instruction.
    // It will be changed to a NOP.
    int64_t Offset = (Value & ~1);
    if (Offset == 2)
      return "will be converted to nop";
    break;
  }
  default:
    llvm_unreachable("Unexpected fixup kind in reasonForFixupRelaxation()!");
  }
  return nullptr;
}

bool ARMAsmBackend::fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                                         const MCRelaxableFragment *DF,
                                         const MCAsmLayout &Layout) const {
  return reasonForFixupRelaxation(Fixup, Value);
}

void ARMAsmBackend::relaxInstruction(const MCInst &Inst, MCInst &Res) const {
  unsigned RelaxedOp = getRelaxedOpcode(Inst.getOpcode());

  // Sanity check w/ diagnostic if we get here w/ a bogus instruction.
  if (RelaxedOp == Inst.getOpcode()) {
    SmallString<256> Tmp;
    raw_svector_ostream OS(Tmp);
    Inst.dump_pretty(OS);
    OS << "\n";
    report_fatal_error("unexpected instruction to relax: " + OS.str());
  }

  // If we are changing Thumb CBZ or CBNZ instruction to a NOP, aka tHINT, we
  // have to change the operands too.
  if ((Inst.getOpcode() == ARM::tCBZ || Inst.getOpcode() == ARM::tCBNZ) &&
      RelaxedOp == ARM::tHINT) {
    Res.setOpcode(RelaxedOp);
    Res.addOperand(MCOperand::createImm(0));
    Res.addOperand(MCOperand::createImm(14));
    Res.addOperand(MCOperand::createReg(0));
    return;
  }

  // The rest of instructions we're relaxing have the same operands.
  // We just need to update to the proper opcode.
  Res = Inst;
  Res.setOpcode(RelaxedOp);
}

bool ARMAsmBackend::writeNopData(uint64_t Count, MCObjectWriter *OW) const {
  const uint16_t Thumb1_16bitNopEncoding = 0x46c0; // using MOV r8,r8
  const uint16_t Thumb2_16bitNopEncoding = 0xbf00; // NOP
  const uint32_t ARMv4_NopEncoding = 0xe1a00000;   // using MOV r0,r0
  const uint32_t ARMv6T2_NopEncoding = 0xe320f000; // NOP
  if (isThumb()) {
    const uint16_t nopEncoding =
        hasNOP() ? Thumb2_16bitNopEncoding : Thumb1_16bitNopEncoding;
    uint64_t NumNops = Count / 2;
    for (uint64_t i = 0; i != NumNops; ++i)
      OW->write16(nopEncoding);
    if (Count & 1)
      OW->write8(0);
    return true;
  }
  // ARM mode
  const uint32_t nopEncoding =
      hasNOP() ? ARMv6T2_NopEncoding : ARMv4_NopEncoding;
  uint64_t NumNops = Count / 4;
  for (uint64_t i = 0; i != NumNops; ++i)
    OW->write32(nopEncoding);
  // FIXME: should this function return false when unable to write exactly
  // 'Count' bytes with NOP encodings?
  switch (Count % 4) {
  default:
    break; // No leftover bytes to write
  case 1:
    OW->write8(0);
    break;
  case 2:
    OW->write16(0);
    break;
  case 3:
    OW->write16(0);
    OW->write8(0xa0);
    break;
  }

  return true;
}

static uint32_t swapHalfWords(uint32_t Value, bool IsLittleEndian) {
  if (IsLittleEndian) {
    // Note that the halfwords are stored high first and low second in thumb;
    // so we need to swap the fixup value here to map properly.
    uint32_t Swapped = (Value & 0xFFFF0000) >> 16;
    Swapped |= (Value & 0x0000FFFF) << 16;
    return Swapped;
  } else
    return Value;
}

static uint32_t joinHalfWords(uint32_t FirstHalf, uint32_t SecondHalf,
                              bool IsLittleEndian) {
  uint32_t Value;

  if (IsLittleEndian) {
    Value = (SecondHalf & 0xFFFF) << 16;
    Value |= (FirstHalf & 0xFFFF);
  } else {
    Value = (SecondHalf & 0xFFFF);
    Value |= (FirstHalf & 0xFFFF) << 16;
  }

  return Value;
}

unsigned ARMAsmBackend::adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                                         bool IsPCRel, MCContext *Ctx,
                                         bool IsLittleEndian,
                                         bool IsResolved) const {
  unsigned Kind = Fixup.getKind();
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
    return Value;
  case FK_SecRel_2:
    return Value;
  case FK_SecRel_4:
    return Value;
  case ARM::fixup_arm_movt_hi16:
    if (!IsPCRel)
      Value >>= 16;
  // Fallthrough
  case ARM::fixup_arm_movw_lo16: {
    unsigned Hi4 = (Value & 0xF000) >> 12;
    unsigned Lo12 = Value & 0x0FFF;
    // inst{19-16} = Hi4;
    // inst{11-0} = Lo12;
    Value = (Hi4 << 16) | (Lo12);
    return Value;
  }
  case ARM::fixup_t2_movt_hi16:
    if (!IsPCRel)
      Value >>= 16;
  // Fallthrough
  case ARM::fixup_t2_movw_lo16: {
    unsigned Hi4 = (Value & 0xF000) >> 12;
    unsigned i = (Value & 0x800) >> 11;
    unsigned Mid3 = (Value & 0x700) >> 8;
    unsigned Lo8 = Value & 0x0FF;
    // inst{19-16} = Hi4;
    // inst{26} = i;
    // inst{14-12} = Mid3;
    // inst{7-0} = Lo8;
    Value = (Hi4 << 16) | (i << 26) | (Mid3 << 12) | (Lo8);
    return swapHalfWords(Value, IsLittleEndian);
  }
  case ARM::fixup_arm_ldst_pcrel_12:
    // ARM PC-relative values are offset by 8.
    Value -= 4;
  // FALLTHROUGH
  case ARM::fixup_t2_ldst_pcrel_12: {
    // Offset by 4, adjusted by two due to the half-word ordering of thumb.
    Value -= 4;
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    if (Ctx && Value >= 4096) {
      Ctx->reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value |= isAdd << 23;

    // Same addressing mode as fixup_arm_pcrel_10,
    // but with 16-bit halfwords swapped.
    if (Kind == ARM::fixup_t2_ldst_pcrel_12)
      return swapHalfWords(Value, IsLittleEndian);

    return Value;
  }
  case ARM::fixup_arm_adr_pcrel_12: {
    // ARM PC-relative values are offset by 8.
    Value -= 8;
    unsigned opc = 4; // bits {24-21}. Default to add: 0b0100
    if ((int64_t)Value < 0) {
      Value = -Value;
      opc = 2; // 0b0010
    }
    if (Ctx && ARM_AM::getSOImmVal(Value) == -1) {
      Ctx->reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    // Encode the immediate and shift the opcode into place.
    return ARM_AM::getSOImmVal(Value) | (opc << 21);
  }

  case ARM::fixup_t2_adr_pcrel_12: {
    Value -= 4;
    unsigned opc = 0;
    if ((int64_t)Value < 0) {
      Value = -Value;
      opc = 5;
    }

    uint32_t out = (opc << 21);
    out |= (Value & 0x800) << 15;
    out |= (Value & 0x700) << 4;
    out |= (Value & 0x0FF);

    return swapHalfWords(out, IsLittleEndian);
  }

  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
    // These values don't encode the low two bits since they're always zero.
    // Offset by 8 just as above.
    if (const MCSymbolRefExpr *SRE =
            dyn_cast<MCSymbolRefExpr>(Fixup.getValue()))
      if (SRE->getKind() == MCSymbolRefExpr::VK_ARM_TLSCALL)
        return 0;
    return 0xffffff & ((Value - 8) >> 2);
  case ARM::fixup_t2_uncondbranch: {
    Value = Value - 4;
    Value >>= 1; // Low bit is not encoded.

    uint32_t out = 0;
    bool I = Value & 0x800000;
    bool J1 = Value & 0x400000;
    bool J2 = Value & 0x200000;
    J1 ^= I;
    J2 ^= I;

    out |= I << 26;                 // S bit
    out |= !J1 << 13;               // J1 bit
    out |= !J2 << 11;               // J2 bit
    out |= (Value & 0x1FF800) << 5; // imm6 field
    out |= (Value & 0x0007FF);      // imm11 field

    return swapHalfWords(out, IsLittleEndian);
  }
  case ARM::fixup_t2_condbranch: {
    Value = Value - 4;
    Value >>= 1; // Low bit is not encoded.

    uint64_t out = 0;
    out |= (Value & 0x80000) << 7; // S bit
    out |= (Value & 0x40000) >> 7; // J2 bit
    out |= (Value & 0x20000) >> 4; // J1 bit
    out |= (Value & 0x1F800) << 5; // imm6 field
    out |= (Value & 0x007FF);      // imm11 field

    return swapHalfWords(out, IsLittleEndian);
  }
  case ARM::fixup_arm_thumb_bl: {
    // The value doesn't encode the low bit (always zero) and is offset by
    // four. The 32-bit immediate value is encoded as
    //   imm32 = SignExtend(S:I1:I2:imm10:imm11:0)
    // where I1 = NOT(J1 ^ S) and I2 = NOT(J2 ^ S).
    // The value is encoded into disjoint bit positions in the destination
    // opcode. x = unchanged, I = immediate value bit, S = sign extension bit,
    // J = either J1 or J2 bit
    //
    //   BL:  xxxxxSIIIIIIIIII xxJxJIIIIIIIIIII
    //
    // Note that the halfwords are stored high first, low second; so we need
    // to transpose the fixup value here to map properly.
    uint32_t offset = (Value - 4) >> 1;
    uint32_t signBit = (offset & 0x800000) >> 23;
    uint32_t I1Bit = (offset & 0x400000) >> 22;
    uint32_t J1Bit = (I1Bit ^ 0x1) ^ signBit;
    uint32_t I2Bit = (offset & 0x200000) >> 21;
    uint32_t J2Bit = (I2Bit ^ 0x1) ^ signBit;
    uint32_t imm10Bits = (offset & 0x1FF800) >> 11;
    uint32_t imm11Bits = (offset & 0x000007FF);

    uint32_t FirstHalf = (((uint16_t)signBit << 10) | (uint16_t)imm10Bits);
    uint32_t SecondHalf = (((uint16_t)J1Bit << 13) | ((uint16_t)J2Bit << 11) |
                           (uint16_t)imm11Bits);
    return joinHalfWords(FirstHalf, SecondHalf, IsLittleEndian);
  }
  case ARM::fixup_arm_thumb_blx: {
    // The value doesn't encode the low two bits (always zero) and is offset by
    // four (see fixup_arm_thumb_cp). The 32-bit immediate value is encoded as
    //   imm32 = SignExtend(S:I1:I2:imm10H:imm10L:00)
    // where I1 = NOT(J1 ^ S) and I2 = NOT(J2 ^ S).
    // The value is encoded into disjoint bit positions in the destination
    // opcode. x = unchanged, I = immediate value bit, S = sign extension bit,
    // J = either J1 or J2 bit, 0 = zero.
    //
    //   BLX: xxxxxSIIIIIIIIII xxJxJIIIIIIIIII0
    //
    // Note that the halfwords are stored high first, low second; so we need
    // to transpose the fixup value here to map properly.
    uint32_t offset = (Value - 2) >> 2;
    if (const MCSymbolRefExpr *SRE =
            dyn_cast<MCSymbolRefExpr>(Fixup.getValue()))
      if (SRE->getKind() == MCSymbolRefExpr::VK_ARM_TLSCALL)
        offset = 0;
    uint32_t signBit = (offset & 0x400000) >> 22;
    uint32_t I1Bit = (offset & 0x200000) >> 21;
    uint32_t J1Bit = (I1Bit ^ 0x1) ^ signBit;
    uint32_t I2Bit = (offset & 0x100000) >> 20;
    uint32_t J2Bit = (I2Bit ^ 0x1) ^ signBit;
    uint32_t imm10HBits = (offset & 0xFFC00) >> 10;
    uint32_t imm10LBits = (offset & 0x3FF);

    uint32_t FirstHalf = (((uint16_t)signBit << 10) | (uint16_t)imm10HBits);
    uint32_t SecondHalf = (((uint16_t)J1Bit << 13) | ((uint16_t)J2Bit << 11) |
                           ((uint16_t)imm10LBits) << 1);
    return joinHalfWords(FirstHalf, SecondHalf, IsLittleEndian);
  }
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_cp:
    // On CPUs supporting Thumb2, this will be relaxed to an ldr.w, otherwise we
    // could have an error on our hands.
    if (Ctx && !STI->getFeatureBits()[ARM::FeatureThumb2] && IsResolved) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx->reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    // Offset by 4, and don't encode the low two bits.
    return ((Value - 4) >> 2) & 0xff;
  case ARM::fixup_arm_thumb_cb: {
    // Offset by 4 and don't encode the lower bit, which is always 0.
    // FIXME: diagnose if no Thumb2
    uint32_t Binary = (Value - 4) >> 1;
    return ((Binary & 0x20) << 4) | ((Binary & 0x1f) << 3);
  }
  case ARM::fixup_arm_thumb_br:
    // Offset by 4 and don't encode the lower bit, which is always 0.
    if (Ctx && !STI->getFeatureBits()[ARM::FeatureThumb2]) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx->reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    return ((Value - 4) >> 1) & 0x7ff;
  case ARM::fixup_arm_thumb_bcc:
    // Offset by 4 and don't encode the lower bit, which is always 0.
    if (Ctx && !STI->getFeatureBits()[ARM::FeatureThumb2]) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx->reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    return ((Value - 4) >> 1) & 0xff;
  case ARM::fixup_arm_pcrel_10_unscaled: {
    Value = Value - 8; // ARM fixups offset by an additional word and don't
                       // need to adjust for the half-word ordering.
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    // The value has the low 4 bits encoded in [3:0] and the high 4 in [11:8].
    if (Ctx && Value >= 256) {
      Ctx->reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value = (Value & 0xf) | ((Value & 0xf0) << 4);
    return Value | (isAdd << 23);
  }
  case ARM::fixup_arm_pcrel_10:
    Value = Value - 4; // ARM fixups offset by an additional word and don't
                       // need to adjust for the half-word ordering.
                       // Fall through.
  case ARM::fixup_t2_pcrel_10: {
    // Offset by 4, adjusted by two due to the half-word ordering of thumb.
    Value = Value - 4;
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    // These values don't encode the low two bits since they're always zero.
    Value >>= 2;
    if (Ctx && Value >= 256) {
      Ctx->reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value |= isAdd << 23;

    // Same addressing mode as fixup_arm_pcrel_10, but with 16-bit halfwords
    // swapped.
    if (Kind == ARM::fixup_t2_pcrel_10)
      return swapHalfWords(Value, IsLittleEndian);

    return Value;
  }
  }
}

void ARMAsmBackend::processFixupValue(const MCAssembler &Asm,
                                      const MCAsmLayout &Layout,
                                      const MCFixup &Fixup,
                                      const MCFragment *DF,
                                      const MCValue &Target, uint64_t &Value,
                                      bool &IsResolved) {
  const MCSymbolRefExpr *A = Target.getSymA();
  const MCSymbol *Sym = A ? &A->getSymbol() : nullptr;
  // Some fixups to thumb function symbols need the low bit (thumb bit)
  // twiddled.
  if ((unsigned)Fixup.getKind() != ARM::fixup_arm_ldst_pcrel_12 &&
      (unsigned)Fixup.getKind() != ARM::fixup_t2_ldst_pcrel_12 &&
      (unsigned)Fixup.getKind() != ARM::fixup_arm_adr_pcrel_12 &&
      (unsigned)Fixup.getKind() != ARM::fixup_thumb_adr_pcrel_10 &&
      (unsigned)Fixup.getKind() != ARM::fixup_t2_adr_pcrel_12 &&
      (unsigned)Fixup.getKind() != ARM::fixup_arm_thumb_cp) {
    if (Sym) {
      if (Asm.isThumbFunc(Sym))
        Value |= 1;
    }
  }
  if (IsResolved && (unsigned)Fixup.getKind() == ARM::fixup_arm_thumb_bl) {
    assert(Sym && "How did we resolve this?");

    // If the symbol is external the linker will handle it.
    // FIXME: Should we handle it as an optimization?

    // If the symbol is out of range, produce a relocation and hope the
    // linker can handle it. GNU AS produces an error in this case.
    if (Sym->isExternal() || Value >= 0x400004)
      IsResolved = false;
  }
  // We must always generate a relocation for BL/BLX instructions if we have
  // a symbol to reference, as the linker relies on knowing the destination
  // symbol's thumb-ness to get interworking right.
  if (A && ((unsigned)Fixup.getKind() == ARM::fixup_arm_thumb_blx ||
            (unsigned)Fixup.getKind() == ARM::fixup_arm_blx ||
            (unsigned)Fixup.getKind() == ARM::fixup_arm_uncondbl ||
            (unsigned)Fixup.getKind() == ARM::fixup_arm_condbl))
    IsResolved = false;

  // Try to get the encoded value for the fixup as-if we're mapping it into
  // the instruction. This allows adjustFixupValue() to issue a diagnostic
  // if the value aren't invalid.
  (void)adjustFixupValue(Fixup, Value, false, &Asm.getContext(),
                         IsLittleEndian, IsResolved);
}

/// getFixupKindNumBytes - The number of bytes the fixup may change.
static unsigned getFixupKindNumBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
  case ARM::fixup_arm_thumb_bcc:
  case ARM::fixup_arm_thumb_cp:
  case ARM::fixup_thumb_adr_pcrel_10:
    return 1;

  case FK_Data_2:
  case ARM::fixup_arm_thumb_br:
  case ARM::fixup_arm_thumb_cb:
    return 2;

  case ARM::fixup_arm_pcrel_10_unscaled:
  case ARM::fixup_arm_ldst_pcrel_12:
  case ARM::fixup_arm_pcrel_10:
  case ARM::fixup_arm_adr_pcrel_12:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
    return 3;

  case FK_Data_4:
  case ARM::fixup_t2_ldst_pcrel_12:
  case ARM::fixup_t2_condbranch:
  case ARM::fixup_t2_uncondbranch:
  case ARM::fixup_t2_pcrel_10:
  case ARM::fixup_t2_adr_pcrel_12:
  case ARM::fixup_arm_thumb_bl:
  case ARM::fixup_arm_thumb_blx:
  case ARM::fixup_arm_movt_hi16:
  case ARM::fixup_arm_movw_lo16:
  case ARM::fixup_t2_movt_hi16:
  case ARM::fixup_t2_movw_lo16:
    return 4;

  case FK_SecRel_2:
    return 2;
  case FK_SecRel_4:
    return 4;
  }
}

/// getFixupKindContainerSizeBytes - The number of bytes of the
/// container involved in big endian.
static unsigned getFixupKindContainerSizeBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
    return 1;
  case FK_Data_2:
    return 2;
  case FK_Data_4:
    return 4;

  case ARM::fixup_arm_thumb_bcc:
  case ARM::fixup_arm_thumb_cp:
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_br:
  case ARM::fixup_arm_thumb_cb:
    // Instruction size is 2 bytes.
    return 2;

  case ARM::fixup_arm_pcrel_10_unscaled:
  case ARM::fixup_arm_ldst_pcrel_12:
  case ARM::fixup_arm_pcrel_10:
  case ARM::fixup_arm_adr_pcrel_12:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
  case ARM::fixup_t2_ldst_pcrel_12:
  case ARM::fixup_t2_condbranch:
  case ARM::fixup_t2_uncondbranch:
  case ARM::fixup_t2_pcrel_10:
  case ARM::fixup_t2_adr_pcrel_12:
  case ARM::fixup_arm_thumb_bl:
  case ARM::fixup_arm_thumb_blx:
  case ARM::fixup_arm_movt_hi16:
  case ARM::fixup_arm_movw_lo16:
  case ARM::fixup_t2_movt_hi16:
  case ARM::fixup_t2_movw_lo16:
    // Instruction size is 4 bytes.
    return 4;
  }
}

void ARMAsmBackend::applyFixup(const MCFixup &Fixup, char *Data,
                               unsigned DataSize, uint64_t Value,
                               bool IsPCRel) const {
  unsigned NumBytes = getFixupKindNumBytes(Fixup.getKind());
  Value =
      adjustFixupValue(Fixup, Value, IsPCRel, nullptr, IsLittleEndian, true);
  if (!Value)
    return; // Doesn't change encoding.

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= DataSize && "Invalid fixup offset!");

  // Used to point to big endian bytes.
  unsigned FullSizeBytes;
  if (!IsLittleEndian) {
    FullSizeBytes = getFixupKindContainerSizeBytes(Fixup.getKind());
    assert((Offset + FullSizeBytes) <= DataSize && "Invalid fixup size!");
    assert(NumBytes <= FullSizeBytes && "Invalid fixup size!");
  }

  // For each byte of the fragment that the fixup touches, mask in the bits from
  // the fixup value. The Value has been "split up" into the appropriate
  // bitfields above.
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned Idx = IsLittleEndian ? i : (FullSizeBytes - 1 - i);
    Data[Offset + Idx] |= uint8_t((Value >> (i * 8)) & 0xff);
  }
}

namespace CU {

/// \brief Compact unwind encoding values.
enum CompactUnwindEncodings {
  UNWIND_ARM_MODE_MASK                         = 0x0F000000,
  UNWIND_ARM_MODE_FRAME                        = 0x01000000,
  UNWIND_ARM_MODE_FRAME_D                      = 0x02000000,
  UNWIND_ARM_MODE_DWARF                        = 0x04000000,

  UNWIND_ARM_FRAME_STACK_ADJUST_MASK           = 0x00C00000,

  UNWIND_ARM_FRAME_FIRST_PUSH_R4               = 0x00000001,
  UNWIND_ARM_FRAME_FIRST_PUSH_R5               = 0x00000002,
  UNWIND_ARM_FRAME_FIRST_PUSH_R6               = 0x00000004,

  UNWIND_ARM_FRAME_SECOND_PUSH_R8              = 0x00000008,
  UNWIND_ARM_FRAME_SECOND_PUSH_R9              = 0x00000010,
  UNWIND_ARM_FRAME_SECOND_PUSH_R10             = 0x00000020,
  UNWIND_ARM_FRAME_SECOND_PUSH_R11             = 0x00000040,
  UNWIND_ARM_FRAME_SECOND_PUSH_R12             = 0x00000080,

  UNWIND_ARM_FRAME_D_REG_COUNT_MASK            = 0x00000F00,

  UNWIND_ARM_DWARF_SECTION_OFFSET              = 0x00FFFFFF
};

} // end CU namespace

/// Generate compact unwind encoding for the function based on the CFI
/// instructions. If the CFI instructions describe a frame that cannot be
/// encoded in compact unwind, the method returns UNWIND_ARM_MODE_DWARF which
/// tells the runtime to fallback and unwind using dwarf.
uint32_t ARMAsmBackendDarwin::generateCompactUnwindEncoding(
    ArrayRef<MCCFIInstruction> Instrs) const {
  DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs() << "generateCU()\n");
  // Only armv7k uses CFI based unwinding.
  if (Subtype != MachO::CPU_SUBTYPE_ARM_V7K)
    return 0;
  // No .cfi directives means no frame.
  if (Instrs.empty())
    return 0;
  // Start off assuming CFA is at SP+0.
  int CFARegister = ARM::SP;
  int CFARegisterOffset = 0;
  // Mark savable registers as initially unsaved
  DenseMap<unsigned, int> RegOffsets;
  int FloatRegCount = 0;
  // Process each .cfi directive and build up compact unwind info.
  for (size_t i = 0, e = Instrs.size(); i != e; ++i) {
    int Reg;
    const MCCFIInstruction &Inst = Instrs[i];
    switch (Inst.getOperation()) {
    case MCCFIInstruction::OpDefCfa: // DW_CFA_def_cfa
      CFARegisterOffset = -Inst.getOffset();
      CFARegister = MRI.getLLVMRegNum(Inst.getRegister(), true);
      break;
    case MCCFIInstruction::OpDefCfaOffset: // DW_CFA_def_cfa_offset
      CFARegisterOffset = -Inst.getOffset();
      break;
    case MCCFIInstruction::OpDefCfaRegister: // DW_CFA_def_cfa_register
      CFARegister = MRI.getLLVMRegNum(Inst.getRegister(), true);
      break;
    case MCCFIInstruction::OpOffset: // DW_CFA_offset
      Reg = MRI.getLLVMRegNum(Inst.getRegister(), true);
      if (ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Reg))
        RegOffsets[Reg] = Inst.getOffset();
      else if (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Reg)) {
        RegOffsets[Reg] = Inst.getOffset();
        ++FloatRegCount;
      } else {
        DEBUG_WITH_TYPE("compact-unwind",
                        llvm::dbgs() << ".cfi_offset on unknown register="
                                     << Inst.getRegister() << "\n");
        return CU::UNWIND_ARM_MODE_DWARF;
      }
      break;
    case MCCFIInstruction::OpRelOffset: // DW_CFA_advance_loc
      // Ignore
      break;
    default:
      // Directive not convertable to compact unwind, bail out.
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs()
                          << "CFI directive not compatiable with comact "
                             "unwind encoding, opcode=" << Inst.getOperation()
                          << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
      break;
    }
  }

  // If no frame set up, return no unwind info.
  if ((CFARegister == ARM::SP) && (CFARegisterOffset == 0))
    return 0;

  // Verify standard frame (lr/r7) was used.
  if (CFARegister != ARM::R7) {
    DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs() << "frame register is "
                                                   << CFARegister
                                                   << " instead of r7\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  int StackAdjust = CFARegisterOffset - 8;
  if (RegOffsets.lookup(ARM::LR) != (-4 - StackAdjust)) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs()
                        << "LR not saved as standard frame, StackAdjust="
                        << StackAdjust
                        << ", CFARegisterOffset=" << CFARegisterOffset
                        << ", lr save at offset=" << RegOffsets[14] << "\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  if (RegOffsets.lookup(ARM::R7) != (-8 - StackAdjust)) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs() << "r7 not saved as standard frame\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  uint32_t CompactUnwindEncoding = CU::UNWIND_ARM_MODE_FRAME;

  // If var-args are used, there may be a stack adjust required.
  switch (StackAdjust) {
  case 0:
    break;
  case 4:
    CompactUnwindEncoding |= 0x00400000;
    break;
  case 8:
    CompactUnwindEncoding |= 0x00800000;
    break;
  case 12:
    CompactUnwindEncoding |= 0x00C00000;
    break;
  default:
    DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs()
                                          << ".cfi_def_cfa stack adjust ("
                                          << StackAdjust << ") out of range\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }

  // If r6 is saved, it must be right below r7.
  static struct {
    unsigned Reg;
    unsigned Encoding;
  } GPRCSRegs[] = {{ARM::R6, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R6},
                   {ARM::R5, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R5},
                   {ARM::R4, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R4},
                   {ARM::R12, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R12},
                   {ARM::R11, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R11},
                   {ARM::R10, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R10},
                   {ARM::R9, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R9},
                   {ARM::R8, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R8}};

  int CurOffset = -8 - StackAdjust;
  for (auto CSReg : GPRCSRegs) {
    auto Offset = RegOffsets.find(CSReg.Reg);
    if (Offset == RegOffsets.end())
      continue;

    int RegOffset = Offset->second;
    if (RegOffset != CurOffset - 4) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << MRI.getName(CSReg.Reg) << " saved at "
                                   << RegOffset << " but only supported at "
                                   << CurOffset << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    }
    CompactUnwindEncoding |= CSReg.Encoding;
    CurOffset -= 4;
  }

  // If no floats saved, we are done.
  if (FloatRegCount == 0)
    return CompactUnwindEncoding;

  // Switch mode to include D register saving.
  CompactUnwindEncoding &= ~CU::UNWIND_ARM_MODE_MASK;
  CompactUnwindEncoding |= CU::UNWIND_ARM_MODE_FRAME_D;

  // FIXME: supporting more than 4 saved D-registers compactly would be trivial,
  // but needs coordination with the linker and libunwind.
  if (FloatRegCount > 4) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs() << "unsupported number of D registers saved ("
                                 << FloatRegCount << ")\n");
      return CU::UNWIND_ARM_MODE_DWARF;
  }

  // Floating point registers must either be saved sequentially, or we defer to
  // DWARF. No gaps allowed here so check that each saved d-register is
  // precisely where it should be.
  static unsigned FPRCSRegs[] = { ARM::D8, ARM::D10, ARM::D12, ARM::D14 };
  for (int Idx = FloatRegCount - 1; Idx >= 0; --Idx) {
    auto Offset = RegOffsets.find(FPRCSRegs[Idx]);
    if (Offset == RegOffsets.end()) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << FloatRegCount << " D-regs saved, but "
                                   << MRI.getName(FPRCSRegs[Idx])
                                   << " not saved\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    } else if (Offset->second != CurOffset - 8) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << FloatRegCount << " D-regs saved, but "
                                   << MRI.getName(FPRCSRegs[Idx])
                                   << " saved at " << Offset->second
                                   << ", expected at " << CurOffset - 8
                                   << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    }
    CurOffset -= 8;
  }

  return CompactUnwindEncoding | ((FloatRegCount - 1) << 8);
}

static MachO::CPUSubTypeARM getMachOSubTypeFromArch(StringRef Arch) {
  unsigned AK = ARM::parseArch(Arch);
  switch (AK) {
  default:
    return MachO::CPU_SUBTYPE_ARM_V7;
  case ARM::AK_ARMV4T:
    return MachO::CPU_SUBTYPE_ARM_V4T;
  case ARM::AK_ARMV5T:
  case ARM::AK_ARMV5TE:
  case ARM::AK_ARMV5TEJ:
    return MachO::CPU_SUBTYPE_ARM_V5;
  case ARM::AK_ARMV6:
  case ARM::AK_ARMV6K:
    return MachO::CPU_SUBTYPE_ARM_V6;
  case ARM::AK_ARMV7A:
    return MachO::CPU_SUBTYPE_ARM_V7;
  case ARM::AK_ARMV7S:
    return MachO::CPU_SUBTYPE_ARM_V7S;
  case ARM::AK_ARMV7K:
    return MachO::CPU_SUBTYPE_ARM_V7K;
  case ARM::AK_ARMV6M:
    return MachO::CPU_SUBTYPE_ARM_V6M;
  case ARM::AK_ARMV7M:
    return MachO::CPU_SUBTYPE_ARM_V7M;
  case ARM::AK_ARMV7EM:
    return MachO::CPU_SUBTYPE_ARM_V7EM;
  }
}

MCAsmBackend *llvm::createARMAsmBackend(const Target &T,
                                        const MCRegisterInfo &MRI,
                                        const Triple &TheTriple, StringRef CPU,
                                        bool isLittle) {
  switch (TheTriple.getObjectFormat()) {
  default:
    llvm_unreachable("unsupported object format");
  case Triple::MachO: {
    MachO::CPUSubTypeARM CS = getMachOSubTypeFromArch(TheTriple.getArchName());
    return new ARMAsmBackendDarwin(T, TheTriple, MRI, CS);
  }
  case Triple::COFF:
    assert(TheTriple.isOSWindows() && "non-Windows ARM COFF is not supported");
    return new ARMAsmBackendWinCOFF(T, TheTriple);
  case Triple::ELF:
    assert(TheTriple.isOSBinFormatELF() && "using ELF for non-ELF target");
    uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());
    return new ARMAsmBackendELF(T, TheTriple, OSABI, isLittle);
  }
}

MCAsmBackend *llvm::createARMLEAsmBackend(const Target &T,
                                          const MCRegisterInfo &MRI,
                                          const Triple &TT, StringRef CPU) {
  return createARMAsmBackend(T, MRI, TT, CPU, true);
}

MCAsmBackend *llvm::createARMBEAsmBackend(const Target &T,
                                          const MCRegisterInfo &MRI,
                                          const Triple &TT, StringRef CPU) {
  return createARMAsmBackend(T, MRI, TT, CPU, false);
}

MCAsmBackend *llvm::createThumbLEAsmBackend(const Target &T,
                                            const MCRegisterInfo &MRI,
                                            const Triple &TT, StringRef CPU) {
  return createARMAsmBackend(T, MRI, TT, CPU, true);
}

MCAsmBackend *llvm::createThumbBEAsmBackend(const Target &T,
                                            const MCRegisterInfo &MRI,
                                            const Triple &TT, StringRef CPU) {
  return createARMAsmBackend(T, MRI, TT, CPU, false);
}
