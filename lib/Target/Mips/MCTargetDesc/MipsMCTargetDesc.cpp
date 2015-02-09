//===-- MipsMCTargetDesc.cpp - Mips Target Descriptions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Mips specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/MipsInstPrinter.h"
#include "MipsELFStreamer.h"
#include "MipsMCAsmInfo.h"
#include "MipsMCNaCl.h"
#include "MipsMCTargetDesc.h"
#include "MipsTargetStreamer.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "MipsGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "MipsGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MipsGenRegisterInfo.inc"

/// Select the Mips CPU for the given triple and cpu name.
/// FIXME: Merge with the copy in MipsSubtarget.cpp
static inline StringRef selectMipsCPU(StringRef TT, StringRef CPU) {
  if (CPU.empty() || CPU == "generic") {
    Triple TheTriple(TT);
    if (TheTriple.getArch() == Triple::mips ||
        TheTriple.getArch() == Triple::mipsel)
      CPU = "mips32";
    else
      CPU = "mips64";
  }
  return CPU;
}

static MCInstrInfo *createMipsMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitMipsMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createMipsMCRegisterInfo(StringRef TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitMipsMCRegisterInfo(X, Mips::RA);
  return X;
}

static MCSubtargetInfo *createMipsMCSubtargetInfo(StringRef TT, StringRef CPU,
                                                  StringRef FS) {
  CPU = selectMipsCPU(TT, CPU);
  MCSubtargetInfo *X = new MCSubtargetInfo();
  InitMipsMCSubtargetInfo(X, TT, CPU, FS);
  return X;
}

static MCAsmInfo *createMipsMCAsmInfo(const MCRegisterInfo &MRI, StringRef TT) {
  MCAsmInfo *MAI = new MipsMCAsmInfo(TT);

  unsigned SP = MRI.getDwarfRegNum(Mips::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCCodeGenInfo *createMipsMCCodeGenInfo(StringRef TT, Reloc::Model RM,
                                              CodeModel::Model CM,
                                              CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  if (CM == CodeModel::JITDefault)
    RM = Reloc::Static;
  else if (RM == Reloc::Default)
    RM = Reloc::PIC_;
  X->InitMCCodeGenInfo(RM, CM, OL);
  return X;
}

static MCInstPrinter *createMipsMCInstPrinter(const Target &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI,
                                              const MCSubtargetInfo &STI) {
  return new MipsInstPrinter(MAI, MII, MRI);
}

static MCStreamer *createMCStreamer(const Target &T, StringRef TT,
                                    MCContext &Context, MCAsmBackend &MAB,
                                    raw_ostream &OS, MCCodeEmitter *Emitter,
                                    const MCSubtargetInfo &STI, bool RelaxAll) {
  MCStreamer *S;
  if (!Triple(TT).isOSNaCl())
    S = createMipsELFStreamer(Context, MAB, OS, Emitter, STI, RelaxAll);
  else
    S = createMipsNaClELFStreamer(Context, MAB, OS, Emitter, STI, RelaxAll);
  new MipsTargetELFStreamer(*S, STI);
  return S;
}

static MCStreamer *
createMCAsmStreamer(MCContext &Ctx, formatted_raw_ostream &OS,
                    bool isVerboseAsm, bool useDwarfDirectory,
                    MCInstPrinter *InstPrint, MCCodeEmitter *CE,
                    MCAsmBackend *TAB, bool ShowInst) {
  MCStreamer *S = llvm::createAsmStreamer(
      Ctx, OS, isVerboseAsm, useDwarfDirectory, InstPrint, CE, TAB, ShowInst);
  new MipsTargetAsmStreamer(*S, OS);
  return S;
}

static MCStreamer *createMipsNullStreamer(MCContext &Ctx) {
  MCStreamer *S = llvm::createNullStreamer(Ctx);
  new MipsTargetStreamer(*S);
  return S;
}

extern "C" void LLVMInitializeMipsTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(TheMipsTarget, createMipsMCAsmInfo);
  RegisterMCAsmInfoFn Y(TheMipselTarget, createMipsMCAsmInfo);
  RegisterMCAsmInfoFn A(TheMips64Target, createMipsMCAsmInfo);
  RegisterMCAsmInfoFn B(TheMips64elTarget, createMipsMCAsmInfo);

  // Register the MC codegen info.
  TargetRegistry::RegisterMCCodeGenInfo(TheMipsTarget,
                                        createMipsMCCodeGenInfo);
  TargetRegistry::RegisterMCCodeGenInfo(TheMipselTarget,
                                        createMipsMCCodeGenInfo);
  TargetRegistry::RegisterMCCodeGenInfo(TheMips64Target,
                                        createMipsMCCodeGenInfo);
  TargetRegistry::RegisterMCCodeGenInfo(TheMips64elTarget,
                                        createMipsMCCodeGenInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(TheMipsTarget, createMipsMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(TheMipselTarget, createMipsMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(TheMips64Target, createMipsMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(TheMips64elTarget,
                                      createMipsMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheMipsTarget, createMipsMCRegisterInfo);
  TargetRegistry::RegisterMCRegInfo(TheMipselTarget, createMipsMCRegisterInfo);
  TargetRegistry::RegisterMCRegInfo(TheMips64Target, createMipsMCRegisterInfo);
  TargetRegistry::RegisterMCRegInfo(TheMips64elTarget,
                                    createMipsMCRegisterInfo);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(TheMipsTarget,
                                        createMipsMCCodeEmitterEB);
  TargetRegistry::RegisterMCCodeEmitter(TheMipselTarget,
                                        createMipsMCCodeEmitterEL);
  TargetRegistry::RegisterMCCodeEmitter(TheMips64Target,
                                        createMipsMCCodeEmitterEB);
  TargetRegistry::RegisterMCCodeEmitter(TheMips64elTarget,
                                        createMipsMCCodeEmitterEL);

  // Register the object streamer.
  TargetRegistry::RegisterMCObjectStreamer(TheMipsTarget, createMCStreamer);
  TargetRegistry::RegisterMCObjectStreamer(TheMipselTarget, createMCStreamer);
  TargetRegistry::RegisterMCObjectStreamer(TheMips64Target, createMCStreamer);
  TargetRegistry::RegisterMCObjectStreamer(TheMips64elTarget,
                                           createMCStreamer);

  // Register the asm streamer.
  TargetRegistry::RegisterAsmStreamer(TheMipsTarget, createMCAsmStreamer);
  TargetRegistry::RegisterAsmStreamer(TheMipselTarget, createMCAsmStreamer);
  TargetRegistry::RegisterAsmStreamer(TheMips64Target, createMCAsmStreamer);
  TargetRegistry::RegisterAsmStreamer(TheMips64elTarget, createMCAsmStreamer);

  TargetRegistry::RegisterNullStreamer(TheMipsTarget, createMipsNullStreamer);
  TargetRegistry::RegisterNullStreamer(TheMipselTarget, createMipsNullStreamer);
  TargetRegistry::RegisterNullStreamer(TheMips64Target, createMipsNullStreamer);
  TargetRegistry::RegisterNullStreamer(TheMips64elTarget,
                                       createMipsNullStreamer);

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(TheMipsTarget,
                                       createMipsAsmBackendEB32);
  TargetRegistry::RegisterMCAsmBackend(TheMipselTarget,
                                       createMipsAsmBackendEL32);
  TargetRegistry::RegisterMCAsmBackend(TheMips64Target,
                                       createMipsAsmBackendEB64);
  TargetRegistry::RegisterMCAsmBackend(TheMips64elTarget,
                                       createMipsAsmBackendEL64);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheMipsTarget,
                                          createMipsMCSubtargetInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheMipselTarget,
                                          createMipsMCSubtargetInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheMips64Target,
                                          createMipsMCSubtargetInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheMips64elTarget,
                                          createMipsMCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(TheMipsTarget,
                                        createMipsMCInstPrinter);
  TargetRegistry::RegisterMCInstPrinter(TheMipselTarget,
                                        createMipsMCInstPrinter);
  TargetRegistry::RegisterMCInstPrinter(TheMips64Target,
                                        createMipsMCInstPrinter);
  TargetRegistry::RegisterMCInstPrinter(TheMips64elTarget,
                                        createMipsMCInstPrinter);
}
