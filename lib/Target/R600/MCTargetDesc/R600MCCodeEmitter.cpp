//===- R600MCCodeEmitter.cpp - Code Emitter for R600->Cayman GPU families -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// \brief The R600 code emitter produces machine code that can be executed
/// directly on the GPU device.
//
//===----------------------------------------------------------------------===//

#include "R600Defines.h"
#include "MCTargetDesc/AMDGPUMCCodeEmitter.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class R600MCCodeEmitter : public AMDGPUMCCodeEmitter {
  R600MCCodeEmitter(const R600MCCodeEmitter &) LLVM_DELETED_FUNCTION;
  void operator=(const R600MCCodeEmitter &) LLVM_DELETED_FUNCTION;
  const MCInstrInfo &MCII;
  const MCRegisterInfo &MRI;

public:

  R600MCCodeEmitter(const MCInstrInfo &mcii, const MCRegisterInfo &mri)
    : MCII(mcii), MRI(mri) { }

  /// \brief Encode the instruction and write it to the OS.
  void EncodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  /// \returns the encoding for an MCOperand.
  uint64_t getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const override;
private:

  void EmitByte(unsigned int byte, raw_ostream &OS) const;

  void Emit(uint32_t value, raw_ostream &OS) const;
  void Emit(uint64_t value, raw_ostream &OS) const;

  unsigned getHWRegChan(unsigned reg) const;
  unsigned getHWReg(unsigned regNo) const;

};

} // End anonymous namespace

enum RegElement {
  ELEMENT_X = 0,
  ELEMENT_Y,
  ELEMENT_Z,
  ELEMENT_W
};

enum FCInstr {
  FC_IF_PREDICATE = 0,
  FC_ELSE,
  FC_ENDIF,
  FC_BGNLOOP,
  FC_ENDLOOP,
  FC_BREAK_PREDICATE,
  FC_CONTINUE
};

MCCodeEmitter *llvm::createR600MCCodeEmitter(const MCInstrInfo &MCII,
                                           const MCRegisterInfo &MRI,
                                           const MCSubtargetInfo &STI) {
  return new R600MCCodeEmitter(MCII, MRI);
}

void R600MCCodeEmitter::EncodeInstruction(const MCInst &MI, raw_ostream &OS,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  if (MI.getOpcode() == AMDGPU::RETURN ||
    MI.getOpcode() == AMDGPU::FETCH_CLAUSE ||
    MI.getOpcode() == AMDGPU::ALU_CLAUSE ||
    MI.getOpcode() == AMDGPU::BUNDLE ||
    MI.getOpcode() == AMDGPU::KILL) {
    return;
  } else if (IS_VTX(Desc)) {
    uint64_t InstWord01 = getBinaryCodeForInstr(MI, Fixups, STI);
    uint32_t InstWord2 = MI.getOperand(2).getImm(); // Offset
    if (!(STI.getFeatureBits() & AMDGPU::FeatureCaymanISA)) {
      InstWord2 |= 1 << 19; // Mega-Fetch bit
    }

    Emit(InstWord01, OS);
    Emit(InstWord2, OS);
    Emit((uint32_t) 0, OS);
  } else if (IS_TEX(Desc)) {
      int64_t Sampler = MI.getOperand(14).getImm();

      int64_t SrcSelect[4] = {
        MI.getOperand(2).getImm(),
        MI.getOperand(3).getImm(),
        MI.getOperand(4).getImm(),
        MI.getOperand(5).getImm()
      };
      int64_t Offsets[3] = {
        MI.getOperand(6).getImm() & 0x1F,
        MI.getOperand(7).getImm() & 0x1F,
        MI.getOperand(8).getImm() & 0x1F
      };

      uint64_t Word01 = getBinaryCodeForInstr(MI, Fixups, STI);
      uint32_t Word2 = Sampler << 15 | SrcSelect[ELEMENT_X] << 20 |
          SrcSelect[ELEMENT_Y] << 23 | SrcSelect[ELEMENT_Z] << 26 |
          SrcSelect[ELEMENT_W] << 29 | Offsets[0] << 0 | Offsets[1] << 5 |
          Offsets[2] << 10;

      Emit(Word01, OS);
      Emit(Word2, OS);
      Emit((uint32_t) 0, OS);
  } else {
    uint64_t Inst = getBinaryCodeForInstr(MI, Fixups, STI);
    if ((STI.getFeatureBits() & AMDGPU::FeatureR600ALUInst) &&
       ((Desc.TSFlags & R600_InstFlag::OP1) ||
         Desc.TSFlags & R600_InstFlag::OP2)) {
      uint64_t ISAOpCode = Inst & (0x3FFULL << 39);
      Inst &= ~(0x3FFULL << 39);
      Inst |= ISAOpCode << 1;
    }
    Emit(Inst, OS);
  }
}

void R600MCCodeEmitter::EmitByte(unsigned int Byte, raw_ostream &OS) const {
  OS.write((uint8_t) Byte & 0xff);
}

void R600MCCodeEmitter::Emit(uint32_t Value, raw_ostream &OS) const {
  for (unsigned i = 0; i < 4; i++) {
    OS.write((uint8_t) ((Value >> (8 * i)) & 0xff));
  }
}

void R600MCCodeEmitter::Emit(uint64_t Value, raw_ostream &OS) const {
  for (unsigned i = 0; i < 8; i++) {
    EmitByte((Value >> (8 * i)) & 0xff, OS);
  }
}

unsigned R600MCCodeEmitter::getHWRegChan(unsigned reg) const {
  return MRI.getEncodingValue(reg) >> HW_CHAN_SHIFT;
}

unsigned R600MCCodeEmitter::getHWReg(unsigned RegNo) const {
  return MRI.getEncodingValue(RegNo) & HW_REG_MASK;
}

uint64_t R600MCCodeEmitter::getMachineOpValue(const MCInst &MI,
                                              const MCOperand &MO,
                                        SmallVectorImpl<MCFixup> &Fixup,
                                        const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    if (HAS_NATIVE_OPERANDS(MCII.get(MI.getOpcode()).TSFlags))
      return MRI.getEncodingValue(MO.getReg());
    return getHWReg(MO.getReg());
  }

  assert(MO.isImm());
  return MO.getImm();
}

#include "AMDGPUGenMCCodeEmitter.inc"
