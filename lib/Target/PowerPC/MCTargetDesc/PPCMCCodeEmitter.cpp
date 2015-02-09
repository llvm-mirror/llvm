//===-- PPCMCCodeEmitter.cpp - Convert PPC code to machine code -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPCMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "MCTargetDesc/PPCFixupKinds.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOpcodes.h"
using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");

namespace {
class PPCMCCodeEmitter : public MCCodeEmitter {
  PPCMCCodeEmitter(const PPCMCCodeEmitter &) LLVM_DELETED_FUNCTION;
  void operator=(const PPCMCCodeEmitter &) LLVM_DELETED_FUNCTION;

  const MCInstrInfo &MCII;
  const MCContext &CTX;
  bool IsLittleEndian;

public:
  PPCMCCodeEmitter(const MCInstrInfo &mcii, MCContext &ctx, bool isLittle)
    : MCII(mcii), CTX(ctx), IsLittleEndian(isLittle) {
  }
  
  ~PPCMCCodeEmitter() {}

  unsigned getDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;
  unsigned getCondBrEncoding(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  unsigned getAbsDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;
  unsigned getAbsCondBrEncoding(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;
  unsigned getImm16Encoding(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  unsigned getMemRIEncoding(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;
  unsigned getMemRIXEncoding(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  unsigned getSPE8DisEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getSPE4DisEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getSPE2DisEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getTLSRegEncoding(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  unsigned getTLSCallEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned get_crbitm_encoding(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// getMachineOpValue - Return binary encoding of operand. If the machine
  /// operand requires relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI,const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;
  
  // getBinaryCodeForInstr - TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
  void EncodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override {
    // For fast-isel, a float COPY_TO_REGCLASS can survive this long.
    // It's just a nop to keep the register classes happy, so don't
    // generate anything.
    unsigned Opcode = MI.getOpcode();
    const MCInstrDesc &Desc = MCII.get(Opcode);
    if (Opcode == TargetOpcode::COPY_TO_REGCLASS)
      return;

    uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);

    // Output the constant in big/little endian byte order.
    unsigned Size = Desc.getSize();
    switch (Size) {
    case 4:
      if (IsLittleEndian) {
        OS << (char)(Bits);
        OS << (char)(Bits >> 8);
        OS << (char)(Bits >> 16);
        OS << (char)(Bits >> 24);
      } else {
        OS << (char)(Bits >> 24);
        OS << (char)(Bits >> 16);
        OS << (char)(Bits >> 8);
        OS << (char)(Bits);
      }
      break;
    case 8:
      // If we emit a pair of instructions, the first one is
      // always in the top 32 bits, even on little-endian.
      if (IsLittleEndian) {
        OS << (char)(Bits >> 32);
        OS << (char)(Bits >> 40);
        OS << (char)(Bits >> 48);
        OS << (char)(Bits >> 56);
        OS << (char)(Bits);
        OS << (char)(Bits >> 8);
        OS << (char)(Bits >> 16);
        OS << (char)(Bits >> 24);
      } else {
        OS << (char)(Bits >> 56);
        OS << (char)(Bits >> 48);
        OS << (char)(Bits >> 40);
        OS << (char)(Bits >> 32);
        OS << (char)(Bits >> 24);
        OS << (char)(Bits >> 16);
        OS << (char)(Bits >> 8);
        OS << (char)(Bits);
      }
      break;
    default:
      llvm_unreachable ("Invalid instruction size");
    }
    
    ++MCNumEmitted;  // Keep track of the # of mi's emitted.
  }
  
};
  
} // end anonymous namespace
  
MCCodeEmitter *llvm::createPPCMCCodeEmitter(const MCInstrInfo &MCII,
                                            const MCRegisterInfo &MRI,
                                            const MCSubtargetInfo &STI,
                                            MCContext &Ctx) {
  Triple TT(STI.getTargetTriple());
  bool IsLittleEndian = TT.getArch() == Triple::ppc64le;
  return new PPCMCCodeEmitter(MCII, Ctx, IsLittleEndian);
}

unsigned PPCMCCodeEmitter::
getDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);
  
  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_br24));
  return 0;
}

unsigned PPCMCCodeEmitter::getCondBrEncoding(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsDirectBrEncoding(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_br24abs));
  return 0;
}

unsigned PPCMCCodeEmitter::
getAbsCondBrEncoding(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);

  // Add a fixup for the branch target.
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_brcond14abs));
  return 0;
}

unsigned PPCMCCodeEmitter::getImm16Encoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg() || MO.isImm()) return getMachineOpValue(MI, MO, Fixups, STI);
  
  // Add a fixup for the immediate field.
  Fixups.push_back(MCFixup::Create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return 0;
}

unsigned PPCMCCodeEmitter::getMemRIEncoding(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  // Encode (imm, reg) as a memri, which has the low 16-bits as the
  // displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 16;
  
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return (getMachineOpValue(MI, MO, Fixups, STI) & 0xFFFF) | RegBits;
  
  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::Create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16));
  return RegBits;
}


unsigned PPCMCCodeEmitter::getMemRIXEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // Encode (imm, reg) as a memrix, which has the low 14-bits as the
  // displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 14;
  
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return ((getMachineOpValue(MI, MO, Fixups, STI) >> 2) & 0x3FFF) | RegBits;
  
  // Add a fixup for the displacement field.
  Fixups.push_back(MCFixup::Create(IsLittleEndian? 0 : 2, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_half16ds));
  return RegBits;
}


unsigned PPCMCCodeEmitter::getSPE8DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe8dis, which has the low 5-bits of (imm / 8)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 3;
  return reverseBits(Imm | RegBits) >> 22;
}


unsigned PPCMCCodeEmitter::getSPE4DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe4dis, which has the low 5-bits of (imm / 4)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 2;
  return reverseBits(Imm | RegBits) >> 22;
}


unsigned PPCMCCodeEmitter::getSPE2DisEncoding(const MCInst &MI, unsigned OpNo,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI)
                                              const {
  // Encode (imm, reg) as a spe2dis, which has the low 5-bits of (imm / 2)
  // as the displacement and the next 5 bits as the register #.
  assert(MI.getOperand(OpNo+1).isReg());
  uint32_t RegBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI) << 5;

  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm());
  uint32_t Imm = getMachineOpValue(MI, MO, Fixups, STI) >> 1;
  return reverseBits(Imm | RegBits) >> 22;
}


unsigned PPCMCCodeEmitter::getTLSRegEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isReg()) return getMachineOpValue(MI, MO, Fixups, STI);
  
  // Add a fixup for the TLS register, which simply provides a relocation
  // hint to the linker that this statement is part of a relocation sequence.
  // Return the thread-pointer register's encoding.
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  Triple TT(STI.getTargetTriple());
  bool isPPC64 = TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le;
  return CTX.getRegisterInfo()->getEncodingValue(isPPC64 ? PPC::X13 : PPC::R2);
}

unsigned PPCMCCodeEmitter::getTLSCallEncoding(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  // For special TLS calls, we need two fixups; one for the branch target
  // (__tls_get_addr), which we create via getDirectBrEncoding as usual,
  // and one for the TLSGD or TLSLD symbol, which is emitted here.
  const MCOperand &MO = MI.getOperand(OpNo+1);
  Fixups.push_back(MCFixup::Create(0, MO.getExpr(),
                                   (MCFixupKind)PPC::fixup_ppc_nofixup));
  return getDirectBrEncoding(MI, OpNo, Fixups, STI);
}

unsigned PPCMCCodeEmitter::
get_crbitm_encoding(const MCInst &MI, unsigned OpNo,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert((MI.getOpcode() == PPC::MTOCRF || MI.getOpcode() == PPC::MTOCRF8 ||
          MI.getOpcode() == PPC::MFOCRF || MI.getOpcode() == PPC::MFOCRF8) &&
         (MO.getReg() >= PPC::CR0 && MO.getReg() <= PPC::CR7));
  return 0x80 >> CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
}


unsigned PPCMCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    // MTOCRF/MFOCRF should go through get_crbitm_encoding for the CR operand.
    // The GPR operand should come through here though.
    assert((MI.getOpcode() != PPC::MTOCRF && MI.getOpcode() != PPC::MTOCRF8 &&
            MI.getOpcode() != PPC::MFOCRF && MI.getOpcode() != PPC::MFOCRF8) ||
           MO.getReg() < PPC::CR0 || MO.getReg() > PPC::CR7);
    return CTX.getRegisterInfo()->getEncodingValue(MO.getReg());
  }
  
  assert(MO.isImm() &&
         "Relocation required in an instruction that we cannot encode!");
  return MO.getImm();
}


#include "PPCGenMCCodeEmitter.inc"
