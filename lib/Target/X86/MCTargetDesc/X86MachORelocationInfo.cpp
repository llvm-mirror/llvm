//===-- X86MachORelocationInfo.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCRelocationInfo.h"
#include "llvm/Object/MachO.h"

using namespace llvm;
using namespace object;
using namespace macho;

namespace {
class X86_64MachORelocationInfo : public MCRelocationInfo {
public:
  X86_64MachORelocationInfo(MCContext &Ctx) : MCRelocationInfo(Ctx) {}

  const MCExpr *createExprForRelocation(RelocationRef Rel) {
    const MachOObjectFile *Obj = cast<MachOObjectFile>(Rel.getObjectFile());

    uint64_t RelType; Rel.getType(RelType);
    symbol_iterator SymI = Rel.getSymbol();

    StringRef SymName; SymI->getName(SymName);
    uint64_t  SymAddr; SymI->getAddress(SymAddr);

    RelocationEntry RE = Obj->getRelocation(Rel.getRawDataRefImpl());
    bool isPCRel = Obj->getAnyRelocationPCRel(RE);

    MCSymbol *Sym = Ctx.GetOrCreateSymbol(SymName);
    // FIXME: check that the value is actually the same.
    if (Sym->isVariable() == false)
      Sym->setVariableValue(MCConstantExpr::Create(SymAddr, Ctx));
    const MCExpr *Expr = 0;

    switch(RelType) {
    case RIT_X86_64_TLV:
      Expr = MCSymbolRefExpr::Create(Sym, MCSymbolRefExpr::VK_TLVP, Ctx);
      break;
    case RIT_X86_64_Signed4:
      Expr = MCBinaryExpr::CreateAdd(MCSymbolRefExpr::Create(Sym, Ctx),
                                     MCConstantExpr::Create(4, Ctx),
                                     Ctx);
      break;
    case RIT_X86_64_Signed2:
      Expr = MCBinaryExpr::CreateAdd(MCSymbolRefExpr::Create(Sym, Ctx),
                                     MCConstantExpr::Create(2, Ctx),
                                     Ctx);
      break;
    case RIT_X86_64_Signed1:
      Expr = MCBinaryExpr::CreateAdd(MCSymbolRefExpr::Create(Sym, Ctx),
                                     MCConstantExpr::Create(1, Ctx),
                                     Ctx);
      break;
    case RIT_X86_64_GOTLoad:
      Expr = MCSymbolRefExpr::Create(Sym, MCSymbolRefExpr::VK_GOTPCREL, Ctx);
      break;
    case RIT_X86_64_GOT:
      Expr = MCSymbolRefExpr::Create(Sym, isPCRel ?
                                     MCSymbolRefExpr::VK_GOTPCREL :
                                     MCSymbolRefExpr::VK_GOT,
                                     Ctx);
      break;
    case RIT_X86_64_Subtractor:
      {
        RelocationRef RelNext;
        Obj->getRelocationNext(Rel.getRawDataRefImpl(), RelNext);
        RelocationEntry RENext = Obj->getRelocation(RelNext.getRawDataRefImpl());

        // X86_64_SUBTRACTOR must be followed by a relocation of type
        // X86_64_RELOC_UNSIGNED    .
        // NOTE: Scattered relocations don't exist on x86_64.
        unsigned RType = Obj->getAnyRelocationType(RENext);
        if (RType != RIT_X86_64_Unsigned)
          report_fatal_error("Expected X86_64_RELOC_UNSIGNED after "
                             "X86_64_RELOC_SUBTRACTOR.");

        const MCExpr *LHS = MCSymbolRefExpr::Create(Sym, Ctx);

        symbol_iterator RSymI = RelNext.getSymbol();
        uint64_t RSymAddr;
        RSymI->getAddress(RSymAddr);
        StringRef RSymName;
        RSymI->getName(RSymName);

        MCSymbol *RSym = Ctx.GetOrCreateSymbol(RSymName);
        if (RSym->isVariable() == false)
          RSym->setVariableValue(MCConstantExpr::Create(RSymAddr, Ctx));

        const MCExpr *RHS = MCSymbolRefExpr::Create(RSym, Ctx);

        Expr = MCBinaryExpr::CreateSub(LHS, RHS, Ctx);
        break;
      }
    default:
      Expr = MCSymbolRefExpr::Create(Sym, Ctx);
      break;
    }
    return Expr;
  }
};
} // End unnamed namespace

/// createX86_64MachORelocationInfo - Construct an X86-64 Mach-O RelocationInfo.
MCRelocationInfo *llvm::createX86_64MachORelocationInfo(MCContext &Ctx) {
  return new X86_64MachORelocationInfo(Ctx);
}
