//===-- X86MCCodeEmitter.cpp - Convert X86 code to machine code -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the X86MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86FixupKinds.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

namespace {
class X86MCCodeEmitter : public MCCodeEmitter {
  X86MCCodeEmitter(const X86MCCodeEmitter &) = delete;
  void operator=(const X86MCCodeEmitter &) = delete;
  const MCInstrInfo &MCII;
  MCContext &Ctx;
public:
  X86MCCodeEmitter(const MCInstrInfo &mcii, MCContext &ctx)
    : MCII(mcii), Ctx(ctx) {
  }

  ~X86MCCodeEmitter() override {}

  bool is64BitMode(const MCSubtargetInfo &STI) const {
    return STI.getFeatureBits()[X86::Mode64Bit];
  }

  bool is32BitMode(const MCSubtargetInfo &STI) const {
    return STI.getFeatureBits()[X86::Mode32Bit];
  }

  bool is16BitMode(const MCSubtargetInfo &STI) const {
    return STI.getFeatureBits()[X86::Mode16Bit];
  }

  /// Is16BitMemOperand - Return true if the specified instruction has
  /// a 16-bit memory operand. Op specifies the operand # of the memoperand.
  bool Is16BitMemOperand(const MCInst &MI, unsigned Op,
                         const MCSubtargetInfo &STI) const {
    const MCOperand &BaseReg  = MI.getOperand(Op+X86::AddrBaseReg);
    const MCOperand &IndexReg = MI.getOperand(Op+X86::AddrIndexReg);
    const MCOperand &Disp     = MI.getOperand(Op+X86::AddrDisp);

    if (is16BitMode(STI) && BaseReg.getReg() == 0 &&
        Disp.isImm() && Disp.getImm() < 0x10000)
      return true;
    if ((BaseReg.getReg() != 0 &&
         X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg.getReg())) ||
        (IndexReg.getReg() != 0 &&
         X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg.getReg())))
      return true;
    return false;
  }

  unsigned GetX86RegNum(const MCOperand &MO) const {
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg()) & 0x7;
  }

  // On regular x86, both XMM0-XMM7 and XMM8-XMM15 are encoded in the range
  // 0-7 and the difference between the 2 groups is given by the REX prefix.
  // In the VEX prefix, registers are seen sequencially from 0-15 and encoded
  // in 1's complement form, example:
  //
  //  ModRM field => XMM9 => 1
  //  VEX.VVVV    => XMM9 => ~9
  //
  // See table 4-35 of Intel AVX Programming Reference for details.
  unsigned char getVEXRegisterEncoding(const MCInst &MI,
                                       unsigned OpNum) const {
    unsigned SrcReg = MI.getOperand(OpNum).getReg();
    unsigned SrcRegNum = GetX86RegNum(MI.getOperand(OpNum));
    if (X86II::isX86_64ExtendedReg(SrcReg))
      SrcRegNum |= 8;

    // The registers represented through VEX_VVVV should
    // be encoded in 1's complement form.
    return (~SrcRegNum) & 0xf;
  }

  unsigned char getWriteMaskRegisterEncoding(const MCInst &MI,
                                             unsigned OpNum) const {
    assert(X86::K0 != MI.getOperand(OpNum).getReg() &&
           "Invalid mask register as write-mask!");
    unsigned MaskRegNum = GetX86RegNum(MI.getOperand(OpNum));
    return MaskRegNum;
  }

  void EmitByte(unsigned char C, unsigned &CurByte, raw_ostream &OS) const {
    OS << (char)C;
    ++CurByte;
  }

  void EmitConstant(uint64_t Val, unsigned Size, unsigned &CurByte,
                    raw_ostream &OS) const {
    // Output the constant in little endian byte order.
    for (unsigned i = 0; i != Size; ++i) {
      EmitByte(Val & 255, CurByte, OS);
      Val >>= 8;
    }
  }

  void EmitImmediate(const MCOperand &Disp, SMLoc Loc,
                     unsigned ImmSize, MCFixupKind FixupKind,
                     unsigned &CurByte, raw_ostream &OS,
                     SmallVectorImpl<MCFixup> &Fixups,
                     int ImmOffset = 0) const;

  inline static unsigned char ModRMByte(unsigned Mod, unsigned RegOpcode,
                                        unsigned RM) {
    assert(Mod < 4 && RegOpcode < 8 && RM < 8 && "ModRM Fields out of range!");
    return RM | (RegOpcode << 3) | (Mod << 6);
  }

  void EmitRegModRMByte(const MCOperand &ModRMReg, unsigned RegOpcodeFld,
                        unsigned &CurByte, raw_ostream &OS) const {
    EmitByte(ModRMByte(3, RegOpcodeFld, GetX86RegNum(ModRMReg)), CurByte, OS);
  }

  void EmitSIBByte(unsigned SS, unsigned Index, unsigned Base,
                   unsigned &CurByte, raw_ostream &OS) const {
    // SIB byte is in the same format as the ModRMByte.
    EmitByte(ModRMByte(SS, Index, Base), CurByte, OS);
  }


  void EmitMemModRMByte(const MCInst &MI, unsigned Op,
                        unsigned RegOpcodeField,
                        uint64_t TSFlags, unsigned &CurByte, raw_ostream &OS,
                        SmallVectorImpl<MCFixup> &Fixups,
                        const MCSubtargetInfo &STI) const;

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  void EmitVEXOpcodePrefix(uint64_t TSFlags, unsigned &CurByte, int MemOperand,
                           const MCInst &MI, const MCInstrDesc &Desc,
                           raw_ostream &OS) const;

  void EmitSegmentOverridePrefix(unsigned &CurByte, unsigned SegOperand,
                                 const MCInst &MI, raw_ostream &OS) const;

  void EmitOpcodePrefix(uint64_t TSFlags, unsigned &CurByte, int MemOperand,
                        const MCInst &MI, const MCInstrDesc &Desc,
                        const MCSubtargetInfo &STI,
                        raw_ostream &OS) const;
};

} // end anonymous namespace

MCCodeEmitter *llvm::createX86MCCodeEmitter(const MCInstrInfo &MCII,
                                            const MCRegisterInfo &MRI,
                                            MCContext &Ctx) {
  return new X86MCCodeEmitter(MCII, Ctx);
}

/// isDisp8 - Return true if this signed displacement fits in a 8-bit
/// sign-extended field.
static bool isDisp8(int Value) {
  return Value == (signed char)Value;
}

/// isCDisp8 - Return true if this signed displacement fits in a 8-bit
/// compressed dispacement field.
static bool isCDisp8(uint64_t TSFlags, int Value, int& CValue) {
  assert(((TSFlags & X86II::EncodingMask) == X86II::EVEX) &&
         "Compressed 8-bit displacement is only valid for EVEX inst.");

  unsigned CD8_Scale =
    (TSFlags & X86II::CD8_Scale_Mask) >> X86II::CD8_Scale_Shift;
  if (CD8_Scale == 0) {
    CValue = Value;
    return isDisp8(Value);
  }

  unsigned Mask = CD8_Scale - 1;
  assert((CD8_Scale & Mask) == 0 && "Invalid memory object size.");
  if (Value & Mask) // Unaligned offset
    return false;
  Value /= (int)CD8_Scale;
  bool Ret = (Value == (signed char)Value);

  if (Ret)
    CValue = Value;
  return Ret;
}

/// getImmFixupKind - Return the appropriate fixup kind to use for an immediate
/// in an instruction with the specified TSFlags.
static MCFixupKind getImmFixupKind(uint64_t TSFlags) {
  unsigned Size = X86II::getSizeOfImm(TSFlags);
  bool isPCRel = X86II::isImmPCRel(TSFlags);

  if (X86II::isImmSigned(TSFlags)) {
    switch (Size) {
    default: llvm_unreachable("Unsupported signed fixup size!");
    case 4: return MCFixupKind(X86::reloc_signed_4byte);
    }
  }
  return MCFixup::getKindForSize(Size, isPCRel);
}

/// Is32BitMemOperand - Return true if the specified instruction has
/// a 32-bit memory operand. Op specifies the operand # of the memoperand.
static bool Is32BitMemOperand(const MCInst &MI, unsigned Op) {
  const MCOperand &BaseReg  = MI.getOperand(Op+X86::AddrBaseReg);
  const MCOperand &IndexReg = MI.getOperand(Op+X86::AddrIndexReg);

  if ((BaseReg.getReg() != 0 &&
       X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg.getReg())) ||
      (IndexReg.getReg() != 0 &&
       X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg.getReg())))
    return true;
  return false;
}

/// Is64BitMemOperand - Return true if the specified instruction has
/// a 64-bit memory operand. Op specifies the operand # of the memoperand.
#ifndef NDEBUG
static bool Is64BitMemOperand(const MCInst &MI, unsigned Op) {
  const MCOperand &BaseReg  = MI.getOperand(Op+X86::AddrBaseReg);
  const MCOperand &IndexReg = MI.getOperand(Op+X86::AddrIndexReg);

  if ((BaseReg.getReg() != 0 &&
       X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg.getReg())) ||
      (IndexReg.getReg() != 0 &&
       X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg.getReg())))
    return true;
  return false;
}
#endif

/// StartsWithGlobalOffsetTable - Check if this expression starts with
///  _GLOBAL_OFFSET_TABLE_ and if it is of the form
///  _GLOBAL_OFFSET_TABLE_-symbol. This is needed to support PIC on ELF
/// i386 as _GLOBAL_OFFSET_TABLE_ is magical. We check only simple case that
/// are know to be used: _GLOBAL_OFFSET_TABLE_ by itself or at the start
/// of a binary expression.
enum GlobalOffsetTableExprKind {
  GOT_None,
  GOT_Normal,
  GOT_SymDiff
};
static GlobalOffsetTableExprKind
StartsWithGlobalOffsetTable(const MCExpr *Expr) {
  const MCExpr *RHS = nullptr;
  if (Expr->getKind() == MCExpr::Binary) {
    const MCBinaryExpr *BE = static_cast<const MCBinaryExpr *>(Expr);
    Expr = BE->getLHS();
    RHS = BE->getRHS();
  }

  if (Expr->getKind() != MCExpr::SymbolRef)
    return GOT_None;

  const MCSymbolRefExpr *Ref = static_cast<const MCSymbolRefExpr*>(Expr);
  const MCSymbol &S = Ref->getSymbol();
  if (S.getName() != "_GLOBAL_OFFSET_TABLE_")
    return GOT_None;
  if (RHS && RHS->getKind() == MCExpr::SymbolRef)
    return GOT_SymDiff;
  return GOT_Normal;
}

static bool HasSecRelSymbolRef(const MCExpr *Expr) {
  if (Expr->getKind() == MCExpr::SymbolRef) {
    const MCSymbolRefExpr *Ref = static_cast<const MCSymbolRefExpr*>(Expr);
    return Ref->getKind() == MCSymbolRefExpr::VK_SECREL;
  }
  return false;
}

void X86MCCodeEmitter::
EmitImmediate(const MCOperand &DispOp, SMLoc Loc, unsigned Size,
              MCFixupKind FixupKind, unsigned &CurByte, raw_ostream &OS,
              SmallVectorImpl<MCFixup> &Fixups, int ImmOffset) const {
  const MCExpr *Expr = nullptr;
  if (DispOp.isImm()) {
    // If this is a simple integer displacement that doesn't require a
    // relocation, emit it now.
    if (FixupKind != FK_PCRel_1 &&
        FixupKind != FK_PCRel_2 &&
        FixupKind != FK_PCRel_4) {
      EmitConstant(DispOp.getImm()+ImmOffset, Size, CurByte, OS);
      return;
    }
    Expr = MCConstantExpr::create(DispOp.getImm(), Ctx);
  } else {
    Expr = DispOp.getExpr();
  }

  // If we have an immoffset, add it to the expression.
  if ((FixupKind == FK_Data_4 ||
       FixupKind == FK_Data_8 ||
       FixupKind == MCFixupKind(X86::reloc_signed_4byte))) {
    GlobalOffsetTableExprKind Kind = StartsWithGlobalOffsetTable(Expr);
    if (Kind != GOT_None) {
      assert(ImmOffset == 0);

      if (Size == 8) {
        FixupKind = MCFixupKind(X86::reloc_global_offset_table8);
      } else {
        assert(Size == 4);
        FixupKind = MCFixupKind(X86::reloc_global_offset_table);
      }

      if (Kind == GOT_Normal)
        ImmOffset = CurByte;
    } else if (Expr->getKind() == MCExpr::SymbolRef) {
      if (HasSecRelSymbolRef(Expr)) {
        FixupKind = MCFixupKind(FK_SecRel_4);
      }
    } else if (Expr->getKind() == MCExpr::Binary) {
      const MCBinaryExpr *Bin = static_cast<const MCBinaryExpr*>(Expr);
      if (HasSecRelSymbolRef(Bin->getLHS())
          || HasSecRelSymbolRef(Bin->getRHS())) {
        FixupKind = MCFixupKind(FK_SecRel_4);
      }
    }
  }

  // If the fixup is pc-relative, we need to bias the value to be relative to
  // the start of the field, not the end of the field.
  if (FixupKind == FK_PCRel_4 ||
      FixupKind == MCFixupKind(X86::reloc_riprel_4byte) ||
      FixupKind == MCFixupKind(X86::reloc_riprel_4byte_movq_load))
    ImmOffset -= 4;
  if (FixupKind == FK_PCRel_2)
    ImmOffset -= 2;
  if (FixupKind == FK_PCRel_1)
    ImmOffset -= 1;

  if (ImmOffset)
    Expr = MCBinaryExpr::createAdd(Expr, MCConstantExpr::create(ImmOffset, Ctx),
                                   Ctx);

  // Emit a symbolic constant as a fixup and 4 zeros.
  Fixups.push_back(MCFixup::create(CurByte, Expr, FixupKind, Loc));
  EmitConstant(0, Size, CurByte, OS);
}

void X86MCCodeEmitter::EmitMemModRMByte(const MCInst &MI, unsigned Op,
                                        unsigned RegOpcodeField,
                                        uint64_t TSFlags, unsigned &CurByte,
                                        raw_ostream &OS,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const{
  const MCOperand &Disp     = MI.getOperand(Op+X86::AddrDisp);
  const MCOperand &Base     = MI.getOperand(Op+X86::AddrBaseReg);
  const MCOperand &Scale    = MI.getOperand(Op+X86::AddrScaleAmt);
  const MCOperand &IndexReg = MI.getOperand(Op+X86::AddrIndexReg);
  unsigned BaseReg = Base.getReg();
  bool HasEVEX = (TSFlags & X86II::EncodingMask) == X86II::EVEX;

  // Handle %rip relative addressing.
  if (BaseReg == X86::RIP) {    // [disp32+RIP] in X86-64 mode
    assert(is64BitMode(STI) && "Rip-relative addressing requires 64-bit mode");
    assert(IndexReg.getReg() == 0 && "Invalid rip-relative address");
    EmitByte(ModRMByte(0, RegOpcodeField, 5), CurByte, OS);

    unsigned FixupKind = X86::reloc_riprel_4byte;

    // movq loads are handled with a special relocation form which allows the
    // linker to eliminate some loads for GOT references which end up in the
    // same linkage unit.
    if (MI.getOpcode() == X86::MOV64rm)
      FixupKind = X86::reloc_riprel_4byte_movq_load;

    // rip-relative addressing is actually relative to the *next* instruction.
    // Since an immediate can follow the mod/rm byte for an instruction, this
    // means that we need to bias the immediate field of the instruction with
    // the size of the immediate field.  If we have this case, add it into the
    // expression to emit.
    int ImmSize = X86II::hasImm(TSFlags) ? X86II::getSizeOfImm(TSFlags) : 0;

    EmitImmediate(Disp, MI.getLoc(), 4, MCFixupKind(FixupKind),
                  CurByte, OS, Fixups, -ImmSize);
    return;
  }

  unsigned BaseRegNo = BaseReg ? GetX86RegNum(Base) : -1U;

  // 16-bit addressing forms of the ModR/M byte have a different encoding for
  // the R/M field and are far more limited in which registers can be used.
  if (Is16BitMemOperand(MI, Op, STI)) {
    if (BaseReg) {
      // For 32-bit addressing, the row and column values in Table 2-2 are
      // basically the same. It's AX/CX/DX/BX/SP/BP/SI/DI in that order, with
      // some special cases. And GetX86RegNum reflects that numbering.
      // For 16-bit addressing it's more fun, as shown in the SDM Vol 2A,
      // Table 2-1 "16-Bit Addressing Forms with the ModR/M byte". We can only
      // use SI/DI/BP/BX, which have "row" values 4-7 in no particular order,
      // while values 0-3 indicate the allowed combinations (base+index) of
      // those: 0 for BX+SI, 1 for BX+DI, 2 for BP+SI, 3 for BP+DI.
      //
      // R16Table[] is a lookup from the normal RegNo, to the row values from
      // Table 2-1 for 16-bit addressing modes. Where zero means disallowed.
      static const unsigned R16Table[] = { 0, 0, 0, 7, 0, 6, 4, 5 };
      unsigned RMfield = R16Table[BaseRegNo];

      assert(RMfield && "invalid 16-bit base register");

      if (IndexReg.getReg()) {
        unsigned IndexReg16 = R16Table[GetX86RegNum(IndexReg)];

        assert(IndexReg16 && "invalid 16-bit index register");
        // We must have one of SI/DI (4,5), and one of BP/BX (6,7).
        assert(((IndexReg16 ^ RMfield) & 2) &&
               "invalid 16-bit base/index register combination");
        assert(Scale.getImm() == 1 &&
               "invalid scale for 16-bit memory reference");

        // Allow base/index to appear in either order (although GAS doesn't).
        if (IndexReg16 & 2)
          RMfield = (RMfield & 1) | ((7 - IndexReg16) << 1);
        else
          RMfield = (IndexReg16 & 1) | ((7 - RMfield) << 1);
      }

      if (Disp.isImm() && isDisp8(Disp.getImm())) {
        if (Disp.getImm() == 0 && BaseRegNo != N86::EBP) {
          // There is no displacement; just the register.
          EmitByte(ModRMByte(0, RegOpcodeField, RMfield), CurByte, OS);
          return;
        }
        // Use the [REG]+disp8 form, including for [BP] which cannot be encoded.
        EmitByte(ModRMByte(1, RegOpcodeField, RMfield), CurByte, OS);
        EmitImmediate(Disp, MI.getLoc(), 1, FK_Data_1, CurByte, OS, Fixups);
        return;
      }
      // This is the [REG]+disp16 case.
      EmitByte(ModRMByte(2, RegOpcodeField, RMfield), CurByte, OS);
    } else {
      // There is no BaseReg; this is the plain [disp16] case.
      EmitByte(ModRMByte(0, RegOpcodeField, 6), CurByte, OS);
    }

    // Emit 16-bit displacement for plain disp16 or [REG]+disp16 cases.
    EmitImmediate(Disp, MI.getLoc(), 2, FK_Data_2, CurByte, OS, Fixups);
    return;
  }

  // Determine whether a SIB byte is needed.
  // If no BaseReg, issue a RIP relative instruction only if the MCE can
  // resolve addresses on-the-fly, otherwise use SIB (Intel Manual 2A, table
  // 2-7) and absolute references.

  if (// The SIB byte must be used if there is an index register.
      IndexReg.getReg() == 0 &&
      // The SIB byte must be used if the base is ESP/RSP/R12, all of which
      // encode to an R/M value of 4, which indicates that a SIB byte is
      // present.
      BaseRegNo != N86::ESP &&
      // If there is no base register and we're in 64-bit mode, we need a SIB
      // byte to emit an addr that is just 'disp32' (the non-RIP relative form).
      (!is64BitMode(STI) || BaseReg != 0)) {

    if (BaseReg == 0) {          // [disp32]     in X86-32 mode
      EmitByte(ModRMByte(0, RegOpcodeField, 5), CurByte, OS);
      EmitImmediate(Disp, MI.getLoc(), 4, FK_Data_4, CurByte, OS, Fixups);
      return;
    }

    // If the base is not EBP/ESP and there is no displacement, use simple
    // indirect register encoding, this handles addresses like [EAX].  The
    // encoding for [EBP] with no displacement means [disp32] so we handle it
    // by emitting a displacement of 0 below.
    if (Disp.isImm() && Disp.getImm() == 0 && BaseRegNo != N86::EBP) {
      EmitByte(ModRMByte(0, RegOpcodeField, BaseRegNo), CurByte, OS);
      return;
    }

    // Otherwise, if the displacement fits in a byte, encode as [REG+disp8].
    if (Disp.isImm()) {
      if (!HasEVEX && isDisp8(Disp.getImm())) {
        EmitByte(ModRMByte(1, RegOpcodeField, BaseRegNo), CurByte, OS);
        EmitImmediate(Disp, MI.getLoc(), 1, FK_Data_1, CurByte, OS, Fixups);
        return;
      }
      // Try EVEX compressed 8-bit displacement first; if failed, fall back to
      // 32-bit displacement.
      int CDisp8 = 0;
      if (HasEVEX && isCDisp8(TSFlags, Disp.getImm(), CDisp8)) {
        EmitByte(ModRMByte(1, RegOpcodeField, BaseRegNo), CurByte, OS);
        EmitImmediate(Disp, MI.getLoc(), 1, FK_Data_1, CurByte, OS, Fixups,
                      CDisp8 - Disp.getImm());
        return;
      }
    }

    // Otherwise, emit the most general non-SIB encoding: [REG+disp32]
    EmitByte(ModRMByte(2, RegOpcodeField, BaseRegNo), CurByte, OS);
    EmitImmediate(Disp, MI.getLoc(), 4, MCFixupKind(X86::reloc_signed_4byte),
                  CurByte, OS, Fixups);
    return;
  }

  // We need a SIB byte, so start by outputting the ModR/M byte first
  assert(IndexReg.getReg() != X86::ESP &&
         IndexReg.getReg() != X86::RSP && "Cannot use ESP as index reg!");

  bool ForceDisp32 = false;
  bool ForceDisp8  = false;
  int CDisp8 = 0;
  int ImmOffset = 0;
  if (BaseReg == 0) {
    // If there is no base register, we emit the special case SIB byte with
    // MOD=0, BASE=5, to JUST get the index, scale, and displacement.
    EmitByte(ModRMByte(0, RegOpcodeField, 4), CurByte, OS);
    ForceDisp32 = true;
  } else if (!Disp.isImm()) {
    // Emit the normal disp32 encoding.
    EmitByte(ModRMByte(2, RegOpcodeField, 4), CurByte, OS);
    ForceDisp32 = true;
  } else if (Disp.getImm() == 0 &&
             // Base reg can't be anything that ends up with '5' as the base
             // reg, it is the magic [*] nomenclature that indicates no base.
             BaseRegNo != N86::EBP) {
    // Emit no displacement ModR/M byte
    EmitByte(ModRMByte(0, RegOpcodeField, 4), CurByte, OS);
  } else if (!HasEVEX && isDisp8(Disp.getImm())) {
    // Emit the disp8 encoding.
    EmitByte(ModRMByte(1, RegOpcodeField, 4), CurByte, OS);
    ForceDisp8 = true;           // Make sure to force 8 bit disp if Base=EBP
  } else if (HasEVEX && isCDisp8(TSFlags, Disp.getImm(), CDisp8)) {
    // Emit the disp8 encoding.
    EmitByte(ModRMByte(1, RegOpcodeField, 4), CurByte, OS);
    ForceDisp8 = true;           // Make sure to force 8 bit disp if Base=EBP
    ImmOffset = CDisp8 - Disp.getImm();
  } else {
    // Emit the normal disp32 encoding.
    EmitByte(ModRMByte(2, RegOpcodeField, 4), CurByte, OS);
  }

  // Calculate what the SS field value should be...
  static const unsigned SSTable[] = { ~0U, 0, 1, ~0U, 2, ~0U, ~0U, ~0U, 3 };
  unsigned SS = SSTable[Scale.getImm()];

  if (BaseReg == 0) {
    // Handle the SIB byte for the case where there is no base, see Intel
    // Manual 2A, table 2-7. The displacement has already been output.
    unsigned IndexRegNo;
    if (IndexReg.getReg())
      IndexRegNo = GetX86RegNum(IndexReg);
    else // Examples: [ESP+1*<noreg>+4] or [scaled idx]+disp32 (MOD=0,BASE=5)
      IndexRegNo = 4;
    EmitSIBByte(SS, IndexRegNo, 5, CurByte, OS);
  } else {
    unsigned IndexRegNo;
    if (IndexReg.getReg())
      IndexRegNo = GetX86RegNum(IndexReg);
    else
      IndexRegNo = 4;   // For example [ESP+1*<noreg>+4]
    EmitSIBByte(SS, IndexRegNo, GetX86RegNum(Base), CurByte, OS);
  }

  // Do we need to output a displacement?
  if (ForceDisp8)
    EmitImmediate(Disp, MI.getLoc(), 1, FK_Data_1, CurByte, OS, Fixups, ImmOffset);
  else if (ForceDisp32 || Disp.getImm() != 0)
    EmitImmediate(Disp, MI.getLoc(), 4, MCFixupKind(X86::reloc_signed_4byte),
                  CurByte, OS, Fixups);
}

/// EmitVEXOpcodePrefix - AVX instructions are encoded using a opcode prefix
/// called VEX.
void X86MCCodeEmitter::EmitVEXOpcodePrefix(uint64_t TSFlags, unsigned &CurByte,
                                           int MemOperand, const MCInst &MI,
                                           const MCInstrDesc &Desc,
                                           raw_ostream &OS) const {
  assert(!(TSFlags & X86II::LOCK) && "Can't have LOCK VEX.");

  uint64_t Encoding = TSFlags & X86II::EncodingMask;
  bool HasEVEX_K = TSFlags & X86II::EVEX_K;
  bool HasVEX_4V = TSFlags & X86II::VEX_4V;
  bool HasVEX_4VOp3 = TSFlags & X86II::VEX_4VOp3;
  bool HasMemOp4 = TSFlags & X86II::MemOp4;
  bool HasEVEX_RC = TSFlags & X86II::EVEX_RC;

  // VEX_R: opcode externsion equivalent to REX.R in
  // 1's complement (inverted) form
  //
  //  1: Same as REX_R=0 (must be 1 in 32-bit mode)
  //  0: Same as REX_R=1 (64 bit mode only)
  //
  unsigned char VEX_R = 0x1;
  unsigned char EVEX_R2 = 0x1;

  // VEX_X: equivalent to REX.X, only used when a
  // register is used for index in SIB Byte.
  //
  //  1: Same as REX.X=0 (must be 1 in 32-bit mode)
  //  0: Same as REX.X=1 (64-bit mode only)
  unsigned char VEX_X = 0x1;

  // VEX_B:
  //
  //  1: Same as REX_B=0 (ignored in 32-bit mode)
  //  0: Same as REX_B=1 (64 bit mode only)
  //
  unsigned char VEX_B = 0x1;

  // VEX_W: opcode specific (use like REX.W, or used for
  // opcode extension, or ignored, depending on the opcode byte)
  unsigned char VEX_W = 0;

  // VEX_5M (VEX m-mmmmm field):
  //
  //  0b00000: Reserved for future use
  //  0b00001: implied 0F leading opcode
  //  0b00010: implied 0F 38 leading opcode bytes
  //  0b00011: implied 0F 3A leading opcode bytes
  //  0b00100-0b11111: Reserved for future use
  //  0b01000: XOP map select - 08h instructions with imm byte
  //  0b01001: XOP map select - 09h instructions with no imm byte
  //  0b01010: XOP map select - 0Ah instructions with imm dword
  unsigned char VEX_5M = 0;

  // VEX_4V (VEX vvvv field): a register specifier
  // (in 1's complement form) or 1111 if unused.
  unsigned char VEX_4V = 0xf;
  unsigned char EVEX_V2 = 0x1;

  // VEX_L (Vector Length):
  //
  //  0: scalar or 128-bit vector
  //  1: 256-bit vector
  //
  unsigned char VEX_L = 0;
  unsigned char EVEX_L2 = 0;

  // VEX_PP: opcode extension providing equivalent
  // functionality of a SIMD prefix
  //
  //  0b00: None
  //  0b01: 66
  //  0b10: F3
  //  0b11: F2
  //
  unsigned char VEX_PP = 0;

  // EVEX_U
  unsigned char EVEX_U = 1; // Always '1' so far

  // EVEX_z
  unsigned char EVEX_z = 0;

  // EVEX_b
  unsigned char EVEX_b = 0;

  // EVEX_rc
  unsigned char EVEX_rc = 0;

  // EVEX_aaa
  unsigned char EVEX_aaa = 0;

  bool EncodeRC = false;

  if (TSFlags & X86II::VEX_W)
    VEX_W = 1;

  if (TSFlags & X86II::VEX_L)
    VEX_L = 1;
  if (TSFlags & X86II::EVEX_L2)
    EVEX_L2 = 1;

  if (HasEVEX_K && (TSFlags & X86II::EVEX_Z))
    EVEX_z = 1;

  if ((TSFlags & X86II::EVEX_B))
    EVEX_b = 1;

  switch (TSFlags & X86II::OpPrefixMask) {
  default: break; // VEX_PP already correct
  case X86II::PD: VEX_PP = 0x1; break; // 66
  case X86II::XS: VEX_PP = 0x2; break; // F3
  case X86II::XD: VEX_PP = 0x3; break; // F2
  }

  switch (TSFlags & X86II::OpMapMask) {
  default: llvm_unreachable("Invalid prefix!");
  case X86II::TB:   VEX_5M = 0x1; break; // 0F
  case X86II::T8:   VEX_5M = 0x2; break; // 0F 38
  case X86II::TA:   VEX_5M = 0x3; break; // 0F 3A
  case X86II::XOP8: VEX_5M = 0x8; break;
  case X86II::XOP9: VEX_5M = 0x9; break;
  case X86II::XOPA: VEX_5M = 0xA; break;
  }

  // Classify VEX_B, VEX_4V, VEX_R, VEX_X
  unsigned NumOps = Desc.getNumOperands();
  unsigned CurOp = X86II::getOperandBias(Desc);

  switch (TSFlags & X86II::FormMask) {
  default: llvm_unreachable("Unexpected form in EmitVEXOpcodePrefix!");
  case X86II::RawFrm:
    break;
  case X86II::MRMDestMem: {
    // MRMDestMem instructions forms:
    //  MemAddr, src1(ModR/M)
    //  MemAddr, src1(VEX_4V), src2(ModR/M)
    //  MemAddr, src1(ModR/M), imm8
    //
    if (X86II::isX86_64ExtendedReg(MI.getOperand(MemOperand +
                                                 X86::AddrBaseReg).getReg()))
      VEX_B = 0x0;
    if (X86II::isX86_64ExtendedReg(MI.getOperand(MemOperand +
                                                 X86::AddrIndexReg).getReg()))
      VEX_X = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(MemOperand +
                                          X86::AddrIndexReg).getReg()))
      EVEX_V2 = 0x0;

    CurOp += X86::AddrNumOperands;

    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
        EVEX_V2 = 0x0;
      CurOp++;
    }

    const MCOperand &MO = MI.getOperand(CurOp);
    if (MO.isReg()) {
      if (X86II::isX86_64ExtendedReg(MO.getReg()))
        VEX_R = 0x0;
      if (X86II::is32ExtendedReg(MO.getReg()))
        EVEX_R2 = 0x0;
    }
    break;
  }
  case X86II::MRMSrcMem:
    // MRMSrcMem instructions forms:
    //  src1(ModR/M), MemAddr
    //  src1(ModR/M), src2(VEX_4V), MemAddr
    //  src1(ModR/M), MemAddr, imm8
    //  src1(ModR/M), MemAddr, src2(VEX_I8IMM)
    //
    //  FMA4:
    //  dst(ModR/M.reg), src1(VEX_4V), src2(ModR/M), src3(VEX_I8IMM)
    //  dst(ModR/M.reg), src1(VEX_4V), src2(VEX_I8IMM), src3(ModR/M),
    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_R = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      EVEX_R2 = 0x0;
    CurOp++;

    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
        EVEX_V2 = 0x0;
      CurOp++;
    }

    if (X86II::isX86_64ExtendedReg(
               MI.getOperand(MemOperand+X86::AddrBaseReg).getReg()))
      VEX_B = 0x0;
    if (X86II::isX86_64ExtendedReg(
               MI.getOperand(MemOperand+X86::AddrIndexReg).getReg()))
      VEX_X = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(MemOperand +
                               X86::AddrIndexReg).getReg()))
      EVEX_V2 = 0x0;

    if (HasVEX_4VOp3)
      // Instruction format for 4VOp3:
      //   src1(ModR/M), MemAddr, src3(VEX_4V)
      // CurOp points to start of the MemoryOperand,
      //   it skips TIED_TO operands if exist, then increments past src1.
      // CurOp + X86::AddrNumOperands will point to src3.
      VEX_4V = getVEXRegisterEncoding(MI, CurOp+X86::AddrNumOperands);
    break;
  case X86II::MRM0m: case X86II::MRM1m:
  case X86II::MRM2m: case X86II::MRM3m:
  case X86II::MRM4m: case X86II::MRM5m:
  case X86II::MRM6m: case X86II::MRM7m: {
    // MRM[0-9]m instructions forms:
    //  MemAddr
    //  src1(VEX_4V), MemAddr
    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
        EVEX_V2 = 0x0;
      CurOp++;
    }

    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (X86II::isX86_64ExtendedReg(
               MI.getOperand(MemOperand+X86::AddrBaseReg).getReg()))
      VEX_B = 0x0;
    if (X86II::isX86_64ExtendedReg(
               MI.getOperand(MemOperand+X86::AddrIndexReg).getReg()))
      VEX_X = 0x0;
    break;
  }
  case X86II::MRMSrcReg:
    // MRMSrcReg instructions forms:
    //  dst(ModR/M), src1(VEX_4V), src2(ModR/M), src3(VEX_I8IMM)
    //  dst(ModR/M), src1(ModR/M)
    //  dst(ModR/M), src1(ModR/M), imm8
    //
    //  FMA4:
    //  dst(ModR/M.reg), src1(VEX_4V), src2(ModR/M), src3(VEX_I8IMM)
    //  dst(ModR/M.reg), src1(VEX_4V), src2(VEX_I8IMM), src3(ModR/M),
    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_R = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      EVEX_R2 = 0x0;
    CurOp++;

    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
        EVEX_V2 = 0x0;
      CurOp++;
    }

    if (HasMemOp4) // Skip second register source (encoded in I8IMM)
      CurOp++;

    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_B = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_X = 0x0;
    CurOp++;
    if (HasVEX_4VOp3)
      VEX_4V = getVEXRegisterEncoding(MI, CurOp++);
    if (EVEX_b) {
      if (HasEVEX_RC) {
        unsigned RcOperand = NumOps-1;
        assert(RcOperand >= CurOp);
        EVEX_rc = MI.getOperand(RcOperand).getImm() & 0x3;
      }
      EncodeRC = true;
    }
    break;
  case X86II::MRMDestReg:
    // MRMDestReg instructions forms:
    //  dst(ModR/M), src(ModR/M)
    //  dst(ModR/M), src(ModR/M), imm8
    //  dst(ModR/M), src1(VEX_4V), src2(ModR/M)
    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_B = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_X = 0x0;
    CurOp++;

    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
        EVEX_V2 = 0x0;
      CurOp++;
    }

    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_R = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      EVEX_R2 = 0x0;
    if (EVEX_b)
      EncodeRC = true;
    break;
  case X86II::MRM0r: case X86II::MRM1r:
  case X86II::MRM2r: case X86II::MRM3r:
  case X86II::MRM4r: case X86II::MRM5r:
  case X86II::MRM6r: case X86II::MRM7r:
    // MRM0r-MRM7r instructions forms:
    //  dst(VEX_4V), src(ModR/M), imm8
    if (HasVEX_4V) {
      VEX_4V = getVEXRegisterEncoding(MI, CurOp);
      if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
          EVEX_V2 = 0x0;
      CurOp++;
    }
    if (HasEVEX_K)
      EVEX_aaa = getWriteMaskRegisterEncoding(MI, CurOp++);

    if (X86II::isX86_64ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_B = 0x0;
    if (X86II::is32ExtendedReg(MI.getOperand(CurOp).getReg()))
      VEX_X = 0x0;
    break;
  }

  if (Encoding == X86II::VEX || Encoding == X86II::XOP) {
    // VEX opcode prefix can have 2 or 3 bytes
    //
    //  3 bytes:
    //    +-----+ +--------------+ +-------------------+
    //    | C4h | | RXB | m-mmmm | | W | vvvv | L | pp |
    //    +-----+ +--------------+ +-------------------+
    //  2 bytes:
    //    +-----+ +-------------------+
    //    | C5h | | R | vvvv | L | pp |
    //    +-----+ +-------------------+
    //
    //  XOP uses a similar prefix:
    //    +-----+ +--------------+ +-------------------+
    //    | 8Fh | | RXB | m-mmmm | | W | vvvv | L | pp |
    //    +-----+ +--------------+ +-------------------+
    unsigned char LastByte = VEX_PP | (VEX_L << 2) | (VEX_4V << 3);

    // Can we use the 2 byte VEX prefix?
    if (Encoding == X86II::VEX && VEX_B && VEX_X && !VEX_W && (VEX_5M == 1)) {
      EmitByte(0xC5, CurByte, OS);
      EmitByte(LastByte | (VEX_R << 7), CurByte, OS);
      return;
    }

    // 3 byte VEX prefix
    EmitByte(Encoding == X86II::XOP ? 0x8F : 0xC4, CurByte, OS);
    EmitByte(VEX_R << 7 | VEX_X << 6 | VEX_B << 5 | VEX_5M, CurByte, OS);
    EmitByte(LastByte | (VEX_W << 7), CurByte, OS);
  } else {
    assert(Encoding == X86II::EVEX && "unknown encoding!");
    // EVEX opcode prefix can have 4 bytes
    //
    // +-----+ +--------------+ +-------------------+ +------------------------+
    // | 62h | | RXBR' | 00mm | | W | vvvv | U | pp | | z | L'L | b | v' | aaa |
    // +-----+ +--------------+ +-------------------+ +------------------------+
    assert((VEX_5M & 0x3) == VEX_5M
           && "More than 2 significant bits in VEX.m-mmmm fields for EVEX!");

    VEX_5M &= 0x3;

    EmitByte(0x62, CurByte, OS);
    EmitByte((VEX_R   << 7) |
             (VEX_X   << 6) |
             (VEX_B   << 5) |
             (EVEX_R2 << 4) |
             VEX_5M, CurByte, OS);
    EmitByte((VEX_W   << 7) |
             (VEX_4V  << 3) |
             (EVEX_U  << 2) |
             VEX_PP, CurByte, OS);
    if (EncodeRC)
      EmitByte((EVEX_z  << 7) |
              (EVEX_rc << 5) |
              (EVEX_b  << 4) |
              (EVEX_V2 << 3) |
              EVEX_aaa, CurByte, OS);
    else
      EmitByte((EVEX_z  << 7) |
              (EVEX_L2 << 6) |
              (VEX_L   << 5) |
              (EVEX_b  << 4) |
              (EVEX_V2 << 3) |
              EVEX_aaa, CurByte, OS);
  }
}

/// DetermineREXPrefix - Determine if the MCInst has to be encoded with a X86-64
/// REX prefix which specifies 1) 64-bit instructions, 2) non-default operand
/// size, and 3) use of X86-64 extended registers.
static unsigned DetermineREXPrefix(const MCInst &MI, uint64_t TSFlags,
                                   const MCInstrDesc &Desc) {
  unsigned REX = 0;
  bool UsesHighByteReg = false;

  if (TSFlags & X86II::REX_W)
    REX |= 1 << 3; // set REX.W

  if (MI.getNumOperands() == 0) return REX;

  unsigned NumOps = MI.getNumOperands();
  // FIXME: MCInst should explicitize the two-addrness.
  bool isTwoAddr = NumOps > 1 &&
                      Desc.getOperandConstraint(1, MCOI::TIED_TO) != -1;

  // If it accesses SPL, BPL, SIL, or DIL, then it requires a 0x40 REX prefix.
  unsigned i = isTwoAddr ? 1 : 0;
  for (; i != NumOps; ++i) {
    const MCOperand &MO = MI.getOperand(i);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == X86::AH || Reg == X86::BH || Reg == X86::CH || Reg == X86::DH)
      UsesHighByteReg = true;
    if (!X86II::isX86_64NonExtLowByteReg(Reg)) continue;
    // FIXME: The caller of DetermineREXPrefix slaps this prefix onto anything
    // that returns non-zero.
    REX |= 0x40; // REX fixed encoding prefix
    break;
  }

  switch (TSFlags & X86II::FormMask) {
  case X86II::MRMSrcReg:
    if (MI.getOperand(0).isReg() &&
        X86II::isX86_64ExtendedReg(MI.getOperand(0).getReg()))
      REX |= 1 << 2; // set REX.R
    i = isTwoAddr ? 2 : 1;
    for (; i != NumOps; ++i) {
      const MCOperand &MO = MI.getOperand(i);
      if (MO.isReg() && X86II::isX86_64ExtendedReg(MO.getReg()))
        REX |= 1 << 0; // set REX.B
    }
    break;
  case X86II::MRMSrcMem: {
    if (MI.getOperand(0).isReg() &&
        X86II::isX86_64ExtendedReg(MI.getOperand(0).getReg()))
      REX |= 1 << 2; // set REX.R
    unsigned Bit = 0;
    i = isTwoAddr ? 2 : 1;
    for (; i != NumOps; ++i) {
      const MCOperand &MO = MI.getOperand(i);
      if (MO.isReg()) {
        if (X86II::isX86_64ExtendedReg(MO.getReg()))
          REX |= 1 << Bit; // set REX.B (Bit=0) and REX.X (Bit=1)
        Bit++;
      }
    }
    break;
  }
  case X86II::MRMXm:
  case X86II::MRM0m: case X86II::MRM1m:
  case X86II::MRM2m: case X86II::MRM3m:
  case X86II::MRM4m: case X86II::MRM5m:
  case X86II::MRM6m: case X86II::MRM7m:
  case X86II::MRMDestMem: {
    unsigned e = (isTwoAddr ? X86::AddrNumOperands+1 : X86::AddrNumOperands);
    i = isTwoAddr ? 1 : 0;
    if (NumOps > e && MI.getOperand(e).isReg() &&
        X86II::isX86_64ExtendedReg(MI.getOperand(e).getReg()))
      REX |= 1 << 2; // set REX.R
    unsigned Bit = 0;
    for (; i != e; ++i) {
      const MCOperand &MO = MI.getOperand(i);
      if (MO.isReg()) {
        if (X86II::isX86_64ExtendedReg(MO.getReg()))
          REX |= 1 << Bit; // REX.B (Bit=0) and REX.X (Bit=1)
        Bit++;
      }
    }
    break;
  }
  default:
    if (MI.getOperand(0).isReg() &&
        X86II::isX86_64ExtendedReg(MI.getOperand(0).getReg()))
      REX |= 1 << 0; // set REX.B
    i = isTwoAddr ? 2 : 1;
    for (unsigned e = NumOps; i != e; ++i) {
      const MCOperand &MO = MI.getOperand(i);
      if (MO.isReg() && X86II::isX86_64ExtendedReg(MO.getReg()))
        REX |= 1 << 2; // set REX.R
    }
    break;
  }
  if (REX && UsesHighByteReg)
    report_fatal_error("Cannot encode high byte register in REX-prefixed instruction");

  return REX;
}

/// EmitSegmentOverridePrefix - Emit segment override opcode prefix as needed
void X86MCCodeEmitter::EmitSegmentOverridePrefix(unsigned &CurByte,
                                                 unsigned SegOperand,
                                                 const MCInst &MI,
                                                 raw_ostream &OS) const {
  // Check for explicit segment override on memory operand.
  switch (MI.getOperand(SegOperand).getReg()) {
  default: llvm_unreachable("Unknown segment register!");
  case 0: break;
  case X86::CS: EmitByte(0x2E, CurByte, OS); break;
  case X86::SS: EmitByte(0x36, CurByte, OS); break;
  case X86::DS: EmitByte(0x3E, CurByte, OS); break;
  case X86::ES: EmitByte(0x26, CurByte, OS); break;
  case X86::FS: EmitByte(0x64, CurByte, OS); break;
  case X86::GS: EmitByte(0x65, CurByte, OS); break;
  }
}

/// EmitOpcodePrefix - Emit all instruction prefixes prior to the opcode.
///
/// MemOperand is the operand # of the start of a memory operand if present.  If
/// Not present, it is -1.
void X86MCCodeEmitter::EmitOpcodePrefix(uint64_t TSFlags, unsigned &CurByte,
                                        int MemOperand, const MCInst &MI,
                                        const MCInstrDesc &Desc,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &OS) const {

  // Emit the operand size opcode prefix as needed.
  if ((TSFlags & X86II::OpSizeMask) == (is16BitMode(STI) ? X86II::OpSize32
                                                         : X86II::OpSize16))
    EmitByte(0x66, CurByte, OS);

  // Emit the LOCK opcode prefix.
  if (TSFlags & X86II::LOCK)
    EmitByte(0xF0, CurByte, OS);

  switch (TSFlags & X86II::OpPrefixMask) {
  case X86II::PD:   // 66
    EmitByte(0x66, CurByte, OS);
    break;
  case X86II::XS:   // F3
    EmitByte(0xF3, CurByte, OS);
    break;
  case X86II::XD:   // F2
    EmitByte(0xF2, CurByte, OS);
    break;
  }

  // Handle REX prefix.
  // FIXME: Can this come before F2 etc to simplify emission?
  if (is64BitMode(STI)) {
    if (unsigned REX = DetermineREXPrefix(MI, TSFlags, Desc))
      EmitByte(0x40 | REX, CurByte, OS);
  }

  // 0x0F escape code must be emitted just before the opcode.
  switch (TSFlags & X86II::OpMapMask) {
  case X86II::TB:  // Two-byte opcode map
  case X86II::T8:  // 0F 38
  case X86II::TA:  // 0F 3A
    EmitByte(0x0F, CurByte, OS);
    break;
  }

  switch (TSFlags & X86II::OpMapMask) {
  case X86II::T8:    // 0F 38
    EmitByte(0x38, CurByte, OS);
    break;
  case X86II::TA:    // 0F 3A
    EmitByte(0x3A, CurByte, OS);
    break;
  }
}

void X86MCCodeEmitter::
encodeInstruction(const MCInst &MI, raw_ostream &OS,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);
  uint64_t TSFlags = Desc.TSFlags;

  // Pseudo instructions don't get encoded.
  if ((TSFlags & X86II::FormMask) == X86II::Pseudo)
    return;

  unsigned NumOps = Desc.getNumOperands();
  unsigned CurOp = X86II::getOperandBias(Desc);

  // Keep track of the current byte being emitted.
  unsigned CurByte = 0;

  // Encoding type for this instruction.
  uint64_t Encoding = TSFlags & X86II::EncodingMask;

  // It uses the VEX.VVVV field?
  bool HasVEX_4V = TSFlags & X86II::VEX_4V;
  bool HasVEX_4VOp3 = TSFlags & X86II::VEX_4VOp3;
  bool HasMemOp4 = TSFlags & X86II::MemOp4;
  const unsigned MemOp4_I8IMMOperand = 2;

  // It uses the EVEX.aaa field?
  bool HasEVEX_K = TSFlags & X86II::EVEX_K;
  bool HasEVEX_RC = TSFlags & X86II::EVEX_RC;

  // Determine where the memory operand starts, if present.
  int MemoryOperand = X86II::getMemoryOperandNo(TSFlags, Opcode);
  if (MemoryOperand != -1) MemoryOperand += CurOp;

  // Emit segment override opcode prefix as needed.
  if (MemoryOperand >= 0)
    EmitSegmentOverridePrefix(CurByte, MemoryOperand+X86::AddrSegmentReg,
                              MI, OS);

  // Emit the repeat opcode prefix as needed.
  if (TSFlags & X86II::REP)
    EmitByte(0xF3, CurByte, OS);

  // Emit the address size opcode prefix as needed.
  bool need_address_override;
  uint64_t AdSize = TSFlags & X86II::AdSizeMask;
  if ((is16BitMode(STI) && AdSize == X86II::AdSize32) ||
      (is32BitMode(STI) && AdSize == X86II::AdSize16) ||
      (is64BitMode(STI) && AdSize == X86II::AdSize32)) {
    need_address_override = true;
  } else if (MemoryOperand < 0) {
    need_address_override = false;
  } else if (is64BitMode(STI)) {
    assert(!Is16BitMemOperand(MI, MemoryOperand, STI));
    need_address_override = Is32BitMemOperand(MI, MemoryOperand);
  } else if (is32BitMode(STI)) {
    assert(!Is64BitMemOperand(MI, MemoryOperand));
    need_address_override = Is16BitMemOperand(MI, MemoryOperand, STI);
  } else {
    assert(is16BitMode(STI));
    assert(!Is64BitMemOperand(MI, MemoryOperand));
    need_address_override = !Is16BitMemOperand(MI, MemoryOperand, STI);
  }

  if (need_address_override)
    EmitByte(0x67, CurByte, OS);

  if (Encoding == 0)
    EmitOpcodePrefix(TSFlags, CurByte, MemoryOperand, MI, Desc, STI, OS);
  else
    EmitVEXOpcodePrefix(TSFlags, CurByte, MemoryOperand, MI, Desc, OS);

  unsigned char BaseOpcode = X86II::getBaseOpcodeFor(TSFlags);

  if (TSFlags & X86II::Has3DNow0F0FOpcode)
    BaseOpcode = 0x0F;   // Weird 3DNow! encoding.

  unsigned SrcRegNum = 0;
  switch (TSFlags & X86II::FormMask) {
  default: errs() << "FORM: " << (TSFlags & X86II::FormMask) << "\n";
    llvm_unreachable("Unknown FormMask value in X86MCCodeEmitter!");
  case X86II::Pseudo:
    llvm_unreachable("Pseudo instruction shouldn't be emitted");
  case X86II::RawFrmDstSrc: {
    unsigned siReg = MI.getOperand(1).getReg();
    assert(((siReg == X86::SI && MI.getOperand(0).getReg() == X86::DI) ||
            (siReg == X86::ESI && MI.getOperand(0).getReg() == X86::EDI) ||
            (siReg == X86::RSI && MI.getOperand(0).getReg() == X86::RDI)) &&
           "SI and DI register sizes do not match");
    // Emit segment override opcode prefix as needed (not for %ds).
    if (MI.getOperand(2).getReg() != X86::DS)
      EmitSegmentOverridePrefix(CurByte, 2, MI, OS);
    // Emit AdSize prefix as needed.
    if ((!is32BitMode(STI) && siReg == X86::ESI) ||
        (is32BitMode(STI) && siReg == X86::SI))
      EmitByte(0x67, CurByte, OS);
    CurOp += 3; // Consume operands.
    EmitByte(BaseOpcode, CurByte, OS);
    break;
  }
  case X86II::RawFrmSrc: {
    unsigned siReg = MI.getOperand(0).getReg();
    // Emit segment override opcode prefix as needed (not for %ds).
    if (MI.getOperand(1).getReg() != X86::DS)
      EmitSegmentOverridePrefix(CurByte, 1, MI, OS);
    // Emit AdSize prefix as needed.
    if ((!is32BitMode(STI) && siReg == X86::ESI) ||
        (is32BitMode(STI) && siReg == X86::SI))
      EmitByte(0x67, CurByte, OS);
    CurOp += 2; // Consume operands.
    EmitByte(BaseOpcode, CurByte, OS);
    break;
  }
  case X86II::RawFrmDst: {
    unsigned siReg = MI.getOperand(0).getReg();
    // Emit AdSize prefix as needed.
    if ((!is32BitMode(STI) && siReg == X86::EDI) ||
        (is32BitMode(STI) && siReg == X86::DI))
      EmitByte(0x67, CurByte, OS);
    ++CurOp; // Consume operand.
    EmitByte(BaseOpcode, CurByte, OS);
    break;
  }
  case X86II::RawFrm:
    EmitByte(BaseOpcode, CurByte, OS);
    break;
  case X86II::RawFrmMemOffs:
    // Emit segment override opcode prefix as needed.
    EmitSegmentOverridePrefix(CurByte, 1, MI, OS);
    EmitByte(BaseOpcode, CurByte, OS);
    EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(),
                  X86II::getSizeOfImm(TSFlags), getImmFixupKind(TSFlags),
                  CurByte, OS, Fixups);
    ++CurOp; // skip segment operand
    break;
  case X86II::RawFrmImm8:
    EmitByte(BaseOpcode, CurByte, OS);
    EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(),
                  X86II::getSizeOfImm(TSFlags), getImmFixupKind(TSFlags),
                  CurByte, OS, Fixups);
    EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(), 1, FK_Data_1, CurByte,
                  OS, Fixups);
    break;
  case X86II::RawFrmImm16:
    EmitByte(BaseOpcode, CurByte, OS);
    EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(),
                  X86II::getSizeOfImm(TSFlags), getImmFixupKind(TSFlags),
                  CurByte, OS, Fixups);
    EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(), 2, FK_Data_2, CurByte,
                  OS, Fixups);
    break;

  case X86II::AddRegFrm:
    EmitByte(BaseOpcode + GetX86RegNum(MI.getOperand(CurOp++)), CurByte, OS);
    break;

  case X86II::MRMDestReg:
    EmitByte(BaseOpcode, CurByte, OS);
    SrcRegNum = CurOp + 1;

    if (HasEVEX_K) // Skip writemask
      SrcRegNum++;

    if (HasVEX_4V) // Skip 1st src (which is encoded in VEX_VVVV)
      ++SrcRegNum;

    EmitRegModRMByte(MI.getOperand(CurOp),
                     GetX86RegNum(MI.getOperand(SrcRegNum)), CurByte, OS);
    CurOp = SrcRegNum + 1;
    break;

  case X86II::MRMDestMem:
    EmitByte(BaseOpcode, CurByte, OS);
    SrcRegNum = CurOp + X86::AddrNumOperands;

    if (HasEVEX_K) // Skip writemask
      SrcRegNum++;

    if (HasVEX_4V) // Skip 1st src (which is encoded in VEX_VVVV)
      ++SrcRegNum;

    EmitMemModRMByte(MI, CurOp,
                     GetX86RegNum(MI.getOperand(SrcRegNum)),
                     TSFlags, CurByte, OS, Fixups, STI);
    CurOp = SrcRegNum + 1;
    break;

  case X86II::MRMSrcReg:
    EmitByte(BaseOpcode, CurByte, OS);
    SrcRegNum = CurOp + 1;

    if (HasEVEX_K) // Skip writemask
      SrcRegNum++;

    if (HasVEX_4V) // Skip 1st src (which is encoded in VEX_VVVV)
      ++SrcRegNum;

    if (HasMemOp4) // Skip 2nd src (which is encoded in I8IMM)
      ++SrcRegNum;

    EmitRegModRMByte(MI.getOperand(SrcRegNum),
                     GetX86RegNum(MI.getOperand(CurOp)), CurByte, OS);

    // 2 operands skipped with HasMemOp4, compensate accordingly
    CurOp = HasMemOp4 ? SrcRegNum : SrcRegNum + 1;
    if (HasVEX_4VOp3)
      ++CurOp;
    // do not count the rounding control operand
    if (HasEVEX_RC)
      NumOps--;
    break;

  case X86II::MRMSrcMem: {
    int AddrOperands = X86::AddrNumOperands;
    unsigned FirstMemOp = CurOp+1;

    if (HasEVEX_K) { // Skip writemask
      ++AddrOperands;
      ++FirstMemOp;
    }

    if (HasVEX_4V) {
      ++AddrOperands;
      ++FirstMemOp;  // Skip the register source (which is encoded in VEX_VVVV).
    }
    if (HasMemOp4) // Skip second register source (encoded in I8IMM)
      ++FirstMemOp;

    EmitByte(BaseOpcode, CurByte, OS);

    EmitMemModRMByte(MI, FirstMemOp, GetX86RegNum(MI.getOperand(CurOp)),
                     TSFlags, CurByte, OS, Fixups, STI);
    CurOp += AddrOperands + 1;
    if (HasVEX_4VOp3)
      ++CurOp;
    break;
  }

  case X86II::MRMXr:
  case X86II::MRM0r: case X86II::MRM1r:
  case X86II::MRM2r: case X86II::MRM3r:
  case X86II::MRM4r: case X86II::MRM5r:
  case X86II::MRM6r: case X86II::MRM7r: {
    if (HasVEX_4V) // Skip the register dst (which is encoded in VEX_VVVV).
      ++CurOp;
    if (HasEVEX_K) // Skip writemask
      ++CurOp;
    EmitByte(BaseOpcode, CurByte, OS);
    uint64_t Form = TSFlags & X86II::FormMask;
    EmitRegModRMByte(MI.getOperand(CurOp++),
                     (Form == X86II::MRMXr) ? 0 : Form-X86II::MRM0r,
                     CurByte, OS);
    break;
  }

  case X86II::MRMXm:
  case X86II::MRM0m: case X86II::MRM1m:
  case X86II::MRM2m: case X86II::MRM3m:
  case X86II::MRM4m: case X86II::MRM5m:
  case X86II::MRM6m: case X86II::MRM7m: {
    if (HasVEX_4V) // Skip the register dst (which is encoded in VEX_VVVV).
      ++CurOp;
    if (HasEVEX_K) // Skip writemask
      ++CurOp;
    EmitByte(BaseOpcode, CurByte, OS);
    uint64_t Form = TSFlags & X86II::FormMask;
    EmitMemModRMByte(MI, CurOp, (Form == X86II::MRMXm) ? 0 : Form-X86II::MRM0m,
                     TSFlags, CurByte, OS, Fixups, STI);
    CurOp += X86::AddrNumOperands;
    break;
  }
  case X86II::MRM_C0: case X86II::MRM_C1: case X86II::MRM_C2:
  case X86II::MRM_C3: case X86II::MRM_C4: case X86II::MRM_C5:
  case X86II::MRM_C6: case X86II::MRM_C7: case X86II::MRM_C8:
  case X86II::MRM_C9: case X86II::MRM_CA: case X86II::MRM_CB:
  case X86II::MRM_CC: case X86II::MRM_CD: case X86II::MRM_CE:
  case X86II::MRM_CF: case X86II::MRM_D0: case X86II::MRM_D1:
  case X86II::MRM_D2: case X86II::MRM_D3: case X86II::MRM_D4:
  case X86II::MRM_D5: case X86II::MRM_D6: case X86II::MRM_D7:
  case X86II::MRM_D8: case X86II::MRM_D9: case X86II::MRM_DA:
  case X86II::MRM_DB: case X86II::MRM_DC: case X86II::MRM_DD:
  case X86II::MRM_DE: case X86II::MRM_DF: case X86II::MRM_E0:
  case X86II::MRM_E1: case X86II::MRM_E2: case X86II::MRM_E3:
  case X86II::MRM_E4: case X86II::MRM_E5: case X86II::MRM_E6:
  case X86II::MRM_E7: case X86II::MRM_E8: case X86II::MRM_E9:
  case X86II::MRM_EA: case X86II::MRM_EB: case X86II::MRM_EC:
  case X86II::MRM_ED: case X86II::MRM_EE: case X86II::MRM_EF:
  case X86II::MRM_F0: case X86II::MRM_F1: case X86II::MRM_F2:
  case X86II::MRM_F3: case X86II::MRM_F4: case X86II::MRM_F5:
  case X86II::MRM_F6: case X86II::MRM_F7: case X86II::MRM_F8:
  case X86II::MRM_F9: case X86II::MRM_FA: case X86II::MRM_FB:
  case X86II::MRM_FC: case X86II::MRM_FD: case X86II::MRM_FE:
  case X86II::MRM_FF:
    EmitByte(BaseOpcode, CurByte, OS);

    uint64_t Form = TSFlags & X86II::FormMask;
    EmitByte(0xC0 + Form - X86II::MRM_C0, CurByte, OS);
    break;
  }

  // If there is a remaining operand, it must be a trailing immediate.  Emit it
  // according to the right size for the instruction. Some instructions
  // (SSE4a extrq and insertq) have two trailing immediates.
  while (CurOp != NumOps && NumOps - CurOp <= 2) {
    // The last source register of a 4 operand instruction in AVX is encoded
    // in bits[7:4] of a immediate byte.
    if (TSFlags & X86II::VEX_I8IMM) {
      const MCOperand &MO = MI.getOperand(HasMemOp4 ? MemOp4_I8IMMOperand
                                                    : CurOp);
      ++CurOp;
      unsigned RegNum = GetX86RegNum(MO) << 4;
      if (X86II::isX86_64ExtendedReg(MO.getReg()))
        RegNum |= 1 << 7;
      // If there is an additional 5th operand it must be an immediate, which
      // is encoded in bits[3:0]
      if (CurOp != NumOps) {
        const MCOperand &MIMM = MI.getOperand(CurOp++);
        if (MIMM.isImm()) {
          unsigned Val = MIMM.getImm();
          assert(Val < 16 && "Immediate operand value out of range");
          RegNum |= Val;
        }
      }
      EmitImmediate(MCOperand::createImm(RegNum), MI.getLoc(), 1, FK_Data_1,
                    CurByte, OS, Fixups);
    } else {
      EmitImmediate(MI.getOperand(CurOp++), MI.getLoc(),
                    X86II::getSizeOfImm(TSFlags), getImmFixupKind(TSFlags),
                    CurByte, OS, Fixups);
    }
  }

  if (TSFlags & X86II::Has3DNow0F0FOpcode)
    EmitByte(X86II::getBaseOpcodeFor(TSFlags), CurByte, OS);

#ifndef NDEBUG
  // FIXME: Verify.
  if (/*!Desc.isVariadic() &&*/ CurOp != NumOps) {
    errs() << "Cannot encode all operands of: ";
    MI.dump();
    errs() << '\n';
    abort();
  }
#endif
}
