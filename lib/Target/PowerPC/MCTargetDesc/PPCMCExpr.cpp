//===-- PPCMCExpr.cpp - PPC specific MC expression classes ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PPCFixupKinds.h"
#include "PPCMCExpr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"

using namespace llvm;

#define DEBUG_TYPE "ppcmcexpr"

const PPCMCExpr*
PPCMCExpr::Create(VariantKind Kind, const MCExpr *Expr,
                  bool isDarwin, MCContext &Ctx) {
  return new (Ctx) PPCMCExpr(Kind, Expr, isDarwin);
}

void PPCMCExpr::PrintImpl(raw_ostream &OS) const {
  if (isDarwinSyntax()) {
    switch (Kind) {
    default: llvm_unreachable("Invalid kind!");
    case VK_PPC_LO: OS << "lo16"; break;
    case VK_PPC_HI: OS << "hi16"; break;
    case VK_PPC_HA: OS << "ha16"; break;
    }

    OS << '(';
    getSubExpr()->print(OS);
    OS << ')';
  } else {
    getSubExpr()->print(OS);

    switch (Kind) {
    default: llvm_unreachable("Invalid kind!");
    case VK_PPC_LO: OS << "@l"; break;
    case VK_PPC_HI: OS << "@h"; break;
    case VK_PPC_HA: OS << "@ha"; break;
    case VK_PPC_HIGHER: OS << "@higher"; break;
    case VK_PPC_HIGHERA: OS << "@highera"; break;
    case VK_PPC_HIGHEST: OS << "@highest"; break;
    case VK_PPC_HIGHESTA: OS << "@highesta"; break;
    }
  }
}

bool
PPCMCExpr::EvaluateAsConstant(int64_t &Res) const {
  MCValue Value;

  if (!getSubExpr()->EvaluateAsRelocatable(Value, nullptr, nullptr))
    return false;

  if (!Value.isAbsolute())
    return false;

  Res = EvaluateAsInt64(Value.getConstant());
  return true;
}

int64_t
PPCMCExpr::EvaluateAsInt64(int64_t Value) const {
  switch (Kind) {
    case VK_PPC_LO:
      return Value & 0xffff;
    case VK_PPC_HI:
      return (Value >> 16) & 0xffff;
    case VK_PPC_HA:
      return ((Value + 0x8000) >> 16) & 0xffff;
    case VK_PPC_HIGHER:
      return (Value >> 32) & 0xffff;
    case VK_PPC_HIGHERA:
      return ((Value + 0x8000) >> 32) & 0xffff;
    case VK_PPC_HIGHEST:
      return (Value >> 48) & 0xffff;
    case VK_PPC_HIGHESTA:
      return ((Value + 0x8000) >> 48) & 0xffff;
    case VK_PPC_None:
      break;
  }
  llvm_unreachable("Invalid kind!");
}

bool
PPCMCExpr::EvaluateAsRelocatableImpl(MCValue &Res,
                                     const MCAsmLayout *Layout,
                                     const MCFixup *Fixup) const {
  MCValue Value;

  if (!getSubExpr()->EvaluateAsRelocatable(Value, Layout, Fixup))
    return false;

  if (Value.isAbsolute()) {
    int64_t Result = EvaluateAsInt64(Value.getConstant());
    if ((Fixup == nullptr || (unsigned)Fixup->getKind() != PPC::fixup_ppc_half16) &&
        (Result >= 0x8000))
      return false;
    Res = MCValue::get(Result);
  } else {
    if (!Layout)
      return false;

    MCContext &Context = Layout->getAssembler().getContext();
    const MCSymbolRefExpr *Sym = Value.getSymA();
    MCSymbolRefExpr::VariantKind Modifier = Sym->getKind();
    if (Modifier != MCSymbolRefExpr::VK_None)
      return false;
    switch (Kind) {
      default:
        llvm_unreachable("Invalid kind!");
      case VK_PPC_LO:
        Modifier = MCSymbolRefExpr::VK_PPC_LO;
        break;
      case VK_PPC_HI:
        Modifier = MCSymbolRefExpr::VK_PPC_HI;
        break;
      case VK_PPC_HA:
        Modifier = MCSymbolRefExpr::VK_PPC_HA;
        break;
      case VK_PPC_HIGHERA:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHERA;
        break;
      case VK_PPC_HIGHER:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHER;
        break;
      case VK_PPC_HIGHEST:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHEST;
        break;
      case VK_PPC_HIGHESTA:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHESTA;
        break;
    }
    Sym = MCSymbolRefExpr::Create(&Sym->getSymbol(), Modifier, Context);
    Res = MCValue::get(Sym, Value.getSymB(), Value.getConstant());
  }

  return true;
}

void PPCMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}
