//===------- SparcTargetObjectFile.cpp - Sparc Object Info Impl -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SparcTargetObjectFile.h"
#include "MCTargetDesc/SparcMCExpr.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Target/TargetLowering.h"

using namespace llvm;

const MCExpr *SparcELFTargetObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, Mangler &Mang,
    const TargetMachine &TM, MachineModuleInfo *MMI,
    MCStreamer &Streamer) const {

  if (Encoding & dwarf::DW_EH_PE_pcrel) {
    MachineModuleInfoELF &ELFMMI = MMI->getObjFileInfo<MachineModuleInfoELF>();

    MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, ".DW.stub", Mang, TM);

    // Add information about the stub reference to ELFMMI so that the stub
    // gets emitted by the asmprinter.
    MachineModuleInfoImpl::StubValueTy &StubSym = ELFMMI.getGVStubEntry(SSym);
    if (!StubSym.getPointer()) {
      MCSymbol *Sym = TM.getSymbol(GV, Mang);
      StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
    }

    MCContext &Ctx = getContext();
    return SparcMCExpr::Create(SparcMCExpr::VK_Sparc_R_DISP32,
                               MCSymbolRefExpr::Create(SSym, Ctx), Ctx);
  }

  return TargetLoweringObjectFileELF::getTTypeGlobalReference(
      GV, Encoding, Mang, TM, MMI, Streamer);
}
