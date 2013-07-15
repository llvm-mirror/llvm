//===-- PPCTargetObjectFile.cpp - PPC Object Info -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PPCTargetObjectFile.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Target/Mangler.h"

using namespace llvm;

void
PPC64LinuxTargetObjectFile::
Initialize(MCContext &Ctx, const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  InitializeELF(TM.Options.UseInitArray);
}

const MCSection * PPC64LinuxTargetObjectFile::
SelectSectionForGlobal(const GlobalValue *GV, SectionKind Kind,
                       Mangler *Mang, const TargetMachine &TM) const {

  const MCSection *DefaultSection = 
    TargetLoweringObjectFileELF::SelectSectionForGlobal(GV, Kind, Mang, TM);

  if (DefaultSection != ReadOnlySection)
    return DefaultSection;

  // Here override ReadOnlySection to DataRelROSection for PPC64 SVR4 ABI
  // when we have a constant that contains global relocations.  This is
  // necessary because of this ABI's handling of pointers to functions in
  // a shared library.  The address of a function is actually the address
  // of a function descriptor, which resides in the .opd section.  Generated
  // code uses the descriptor directly rather than going via the GOT as some
  // other ABIs do, which means that initialized function pointers must
  // reference the descriptor.  The linker must convert copy relocs of
  // pointers to functions in shared libraries into dynamic relocations,
  // because of an ordering problem with initialization of copy relocs and
  // PLT entries.  The dynamic relocation will be initialized by the dynamic
  // linker, so we must use DataRelROSection instead of ReadOnlySection.
  // For more information, see the description of ELIMINATE_COPY_RELOCS in
  // GNU ld.
  const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV);

  if (GVar && GVar->isConstant() &&
      (GVar->getInitializer()->getRelocationInfo() ==
       Constant::GlobalRelocations))
    return DataRelROSection;

  return DefaultSection;
}

const MCExpr *PPC64LinuxTargetObjectFile::
getDebugThreadLocalSymbol(const MCSymbol *Sym) const {
  const MCExpr *Expr =
    MCSymbolRefExpr::Create(Sym, MCSymbolRefExpr::VK_PPC_DTPREL, getContext());
  return MCBinaryExpr::CreateAdd(Expr,
                                 MCConstantExpr::Create(0x8000, getContext()),
                                 getContext());
}

