//===-- X86Operand.h - Parsed X86 machine instruction --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_ASMPARSER_X86OPERAND_H
#define LLVM_LIB_TARGET_X86_ASMPARSER_X86OPERAND_H

#include "X86AsmParserCommon.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/ADT/STLExtras.h"

namespace llvm {

/// X86Operand - Instances of this class represent a parsed X86 machine
/// instruction.
struct X86Operand : public MCParsedAsmOperand {
  enum KindTy {
    Token,
    Register,
    Immediate,
    Memory
  } Kind;

  SMLoc StartLoc, EndLoc;
  SMLoc OffsetOfLoc;
  StringRef SymName;
  void *OpDecl;
  bool AddressOf;

  struct TokOp {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNo;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct MemOp {
    unsigned SegReg;
    const MCExpr *Disp;
    unsigned BaseReg;
    unsigned IndexReg;
    unsigned Scale;
    unsigned Size;
    unsigned ModeSize;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
    struct MemOp Mem;
  };

  X86Operand(KindTy K, SMLoc Start, SMLoc End)
    : Kind(K), StartLoc(Start), EndLoc(End) {}

  StringRef getSymName() override { return SymName; }
  void *getOpDecl() override { return OpDecl; }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }
  /// getLocRange - Get the range between the first and last token of this
  /// operand.
  SMRange getLocRange() const { return SMRange(StartLoc, EndLoc); }
  /// getOffsetOfLoc - Get the location of the offset operator.
  SMLoc getOffsetOfLoc() const override { return OffsetOfLoc; }

  void print(raw_ostream &OS) const override {}

  StringRef getToken() const {
    assert(Kind == Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }
  void setTokenValue(StringRef Value) {
    assert(Kind == Token && "Invalid access!");
    Tok.Data = Value.data();
    Tok.Length = Value.size();
  }

  unsigned getReg() const override {
    assert(Kind == Register && "Invalid access!");
    return Reg.RegNo;
  }

  const MCExpr *getImm() const {
    assert(Kind == Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getMemDisp() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Disp;
  }
  unsigned getMemSegReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.SegReg;
  }
  unsigned getMemBaseReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.BaseReg;
  }
  unsigned getMemIndexReg() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.IndexReg;
  }
  unsigned getMemScale() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.Scale;
  }
  unsigned getMemModeSize() const {
    assert(Kind == Memory && "Invalid access!");
    return Mem.ModeSize;
  }

  bool isToken() const override {return Kind == Token; }

  bool isImm() const override { return Kind == Immediate; }

  bool isImmSExti16i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti16i8Value(CE->getValue());
  }
  bool isImmSExti32i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti32i8Value(CE->getValue());
  }
  bool isImmSExti64i8() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti64i8Value(CE->getValue());
  }
  bool isImmSExti64i32() const {
    if (!isImm())
      return false;

    // If this isn't a constant expr, just assume it fits and let relaxation
    // handle it.
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE)
      return true;

    // Otherwise, check the value is in a range that makes sense for this
    // extension.
    return isImmSExti64i32Value(CE->getValue());
  }

  bool isImmUnsignedi8() const {
    if (!isImm()) return false;
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    return isImmUnsignedi8Value(CE->getValue());
  }

  bool isOffsetOf() const override {
    return OffsetOfLoc.getPointer();
  }

  bool needAddressOf() const override {
    return AddressOf;
  }

  bool isMem() const override { return Kind == Memory; }
  bool isMemUnsized() const {
    return Kind == Memory && Mem.Size == 0;
  }
  bool isMem8() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 8);
  }
  bool isMem16() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 16);
  }
  bool isMem32() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 32);
  }
  bool isMem64() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 64);
  }
  bool isMem80() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 80);
  }
  bool isMem128() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 128);
  }
  bool isMem256() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 256);
  }
  bool isMem512() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 512);
  }

  bool isMemVX32() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 32) &&
      getMemIndexReg() >= X86::XMM0 && getMemIndexReg() <= X86::XMM15;
  }
  bool isMemVY32() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 32) &&
      getMemIndexReg() >= X86::YMM0 && getMemIndexReg() <= X86::YMM15;
  }
  bool isMemVX64() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 64) &&
      getMemIndexReg() >= X86::XMM0 && getMemIndexReg() <= X86::XMM15;
  }
  bool isMemVY64() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 64) &&
      getMemIndexReg() >= X86::YMM0 && getMemIndexReg() <= X86::YMM15;
  }
  bool isMemVZ32() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 32) &&
      getMemIndexReg() >= X86::ZMM0 && getMemIndexReg() <= X86::ZMM31;
  }
  bool isMemVZ64() const {
    return Kind == Memory && (!Mem.Size || Mem.Size == 64) &&
      getMemIndexReg() >= X86::ZMM0 && getMemIndexReg() <= X86::ZMM31;
  }

  bool isAbsMem() const {
    return Kind == Memory && !getMemSegReg() && !getMemBaseReg() &&
      !getMemIndexReg() && getMemScale() == 1;
  }

  bool isAbsMem16() const {
    return isAbsMem() && Mem.ModeSize == 16;
  }

  bool isAbsMem32() const {
    return isAbsMem() && Mem.ModeSize != 16;
  }

  bool isSrcIdx() const {
    return !getMemIndexReg() && getMemScale() == 1 &&
      (getMemBaseReg() == X86::RSI || getMemBaseReg() == X86::ESI ||
       getMemBaseReg() == X86::SI) && isa<MCConstantExpr>(getMemDisp()) &&
      cast<MCConstantExpr>(getMemDisp())->getValue() == 0;
  }
  bool isSrcIdx8() const {
    return isMem8() && isSrcIdx();
  }
  bool isSrcIdx16() const {
    return isMem16() && isSrcIdx();
  }
  bool isSrcIdx32() const {
    return isMem32() && isSrcIdx();
  }
  bool isSrcIdx64() const {
    return isMem64() && isSrcIdx();
  }

  bool isDstIdx() const {
    return !getMemIndexReg() && getMemScale() == 1 &&
      (getMemSegReg() == 0 || getMemSegReg() == X86::ES) &&
      (getMemBaseReg() == X86::RDI || getMemBaseReg() == X86::EDI ||
       getMemBaseReg() == X86::DI) && isa<MCConstantExpr>(getMemDisp()) &&
      cast<MCConstantExpr>(getMemDisp())->getValue() == 0;
  }
  bool isDstIdx8() const {
    return isMem8() && isDstIdx();
  }
  bool isDstIdx16() const {
    return isMem16() && isDstIdx();
  }
  bool isDstIdx32() const {
    return isMem32() && isDstIdx();
  }
  bool isDstIdx64() const {
    return isMem64() && isDstIdx();
  }

  bool isMemOffs() const {
    return Kind == Memory && !getMemBaseReg() && !getMemIndexReg() &&
      getMemScale() == 1;
  }

  bool isMemOffs16_8() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs16_16() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs16_32() const {
    return isMemOffs() && Mem.ModeSize == 16 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs32_8() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs32_16() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs32_32() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs32_64() const {
    return isMemOffs() && Mem.ModeSize == 32 && (!Mem.Size || Mem.Size == 64);
  }
  bool isMemOffs64_8() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 8);
  }
  bool isMemOffs64_16() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 16);
  }
  bool isMemOffs64_32() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 32);
  }
  bool isMemOffs64_64() const {
    return isMemOffs() && Mem.ModeSize == 64 && (!Mem.Size || Mem.Size == 64);
  }

  bool isReg() const override { return Kind == Register; }

  bool isGR32orGR64() const {
    return Kind == Register &&
      (X86MCRegisterClasses[X86::GR32RegClassID].contains(getReg()) ||
      X86MCRegisterClasses[X86::GR64RegClassID].contains(getReg()));
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::CreateImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::CreateExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getReg()));
  }

  static unsigned getGR32FromGR64(unsigned RegNo) {
    switch (RegNo) {
    default: llvm_unreachable("Unexpected register");
    case X86::RAX: return X86::EAX;
    case X86::RCX: return X86::ECX;
    case X86::RDX: return X86::EDX;
    case X86::RBX: return X86::EBX;
    case X86::RBP: return X86::EBP;
    case X86::RSP: return X86::ESP;
    case X86::RSI: return X86::ESI;
    case X86::RDI: return X86::EDI;
    case X86::R8: return X86::R8D;
    case X86::R9: return X86::R9D;
    case X86::R10: return X86::R10D;
    case X86::R11: return X86::R11D;
    case X86::R12: return X86::R12D;
    case X86::R13: return X86::R13D;
    case X86::R14: return X86::R14D;
    case X86::R15: return X86::R15D;
    case X86::RIP: return X86::EIP;
    }
  }

  void addGR32orGR64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned RegNo = getReg();
    if (X86MCRegisterClasses[X86::GR64RegClassID].contains(RegNo))
      RegNo = getGR32FromGR64(RegNo);
    Inst.addOperand(MCOperand::CreateReg(RegNo));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 5) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getMemBaseReg()));
    Inst.addOperand(MCOperand::CreateImm(getMemScale()));
    Inst.addOperand(MCOperand::CreateReg(getMemIndexReg()));
    addExpr(Inst, getMemDisp());
    Inst.addOperand(MCOperand::CreateReg(getMemSegReg()));
  }

  void addAbsMemOperands(MCInst &Inst, unsigned N) const {
    assert((N == 1) && "Invalid number of operands!");
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getMemDisp()))
      Inst.addOperand(MCOperand::CreateImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::CreateExpr(getMemDisp()));
  }

  void addSrcIdxOperands(MCInst &Inst, unsigned N) const {
    assert((N == 2) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getMemBaseReg()));
    Inst.addOperand(MCOperand::CreateReg(getMemSegReg()));
  }
  void addDstIdxOperands(MCInst &Inst, unsigned N) const {
    assert((N == 1) && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getMemBaseReg()));
  }

  void addMemOffsOperands(MCInst &Inst, unsigned N) const {
    assert((N == 2) && "Invalid number of operands!");
    // Add as immediates when possible.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getMemDisp()))
      Inst.addOperand(MCOperand::CreateImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::CreateExpr(getMemDisp()));
    Inst.addOperand(MCOperand::CreateReg(getMemSegReg()));
  }

  static std::unique_ptr<X86Operand> CreateToken(StringRef Str, SMLoc Loc) {
    SMLoc EndLoc = SMLoc::getFromPointer(Loc.getPointer() + Str.size());
    auto Res = llvm::make_unique<X86Operand>(Token, Loc, EndLoc);
    Res->Tok.Data = Str.data();
    Res->Tok.Length = Str.size();
    return Res;
  }

  static std::unique_ptr<X86Operand>
  CreateReg(unsigned RegNo, SMLoc StartLoc, SMLoc EndLoc,
            bool AddressOf = false, SMLoc OffsetOfLoc = SMLoc(),
            StringRef SymName = StringRef(), void *OpDecl = nullptr) {
    auto Res = llvm::make_unique<X86Operand>(Register, StartLoc, EndLoc);
    Res->Reg.RegNo = RegNo;
    Res->AddressOf = AddressOf;
    Res->OffsetOfLoc = OffsetOfLoc;
    Res->SymName = SymName;
    Res->OpDecl = OpDecl;
    return Res;
  }

  static std::unique_ptr<X86Operand> CreateImm(const MCExpr *Val,
                                               SMLoc StartLoc, SMLoc EndLoc) {
    auto Res = llvm::make_unique<X86Operand>(Immediate, StartLoc, EndLoc);
    Res->Imm.Val = Val;
    return Res;
  }

  /// Create an absolute memory operand.
  static std::unique_ptr<X86Operand>
  CreateMem(unsigned ModeSize, const MCExpr *Disp, SMLoc StartLoc, SMLoc EndLoc,
            unsigned Size = 0, StringRef SymName = StringRef(),
            void *OpDecl = nullptr) {
    auto Res = llvm::make_unique<X86Operand>(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = 0;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = 0;
    Res->Mem.IndexReg = 0;
    Res->Mem.Scale    = 1;
    Res->Mem.Size     = Size;
    Res->Mem.ModeSize = ModeSize;
    Res->SymName      = SymName;
    Res->OpDecl       = OpDecl;
    Res->AddressOf    = false;
    return Res;
  }

  /// Create a generalized memory operand.
  static std::unique_ptr<X86Operand>
  CreateMem(unsigned ModeSize, unsigned SegReg, const MCExpr *Disp,
            unsigned BaseReg, unsigned IndexReg, unsigned Scale, SMLoc StartLoc,
            SMLoc EndLoc, unsigned Size = 0, StringRef SymName = StringRef(),
            void *OpDecl = nullptr) {
    // We should never just have a displacement, that should be parsed as an
    // absolute memory operand.
    assert((SegReg || BaseReg || IndexReg) && "Invalid memory operand!");

    // The scale should always be one of {1,2,4,8}.
    assert(((Scale == 1 || Scale == 2 || Scale == 4 || Scale == 8)) &&
           "Invalid scale!");
    auto Res = llvm::make_unique<X86Operand>(Memory, StartLoc, EndLoc);
    Res->Mem.SegReg   = SegReg;
    Res->Mem.Disp     = Disp;
    Res->Mem.BaseReg  = BaseReg;
    Res->Mem.IndexReg = IndexReg;
    Res->Mem.Scale    = Scale;
    Res->Mem.Size     = Size;
    Res->Mem.ModeSize = ModeSize;
    Res->SymName      = SymName;
    Res->OpDecl       = OpDecl;
    Res->AddressOf    = false;
    return Res;
  }
};

} // End of namespace llvm

#endif
