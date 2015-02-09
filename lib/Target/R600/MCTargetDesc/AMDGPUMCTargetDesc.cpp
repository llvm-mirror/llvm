//===-- AMDGPUMCTargetDesc.cpp - AMDGPU Target Descriptions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief This file provides AMDGPU specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCTargetDesc.h"
#include "AMDGPUMCAsmInfo.h"
#include "InstPrinter/AMDGPUInstPrinter.h"
#include "SIDefines.h"
#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "AMDGPUGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AMDGPUGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AMDGPUGenRegisterInfo.inc"

static MCInstrInfo *createAMDGPUMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAMDGPUMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createAMDGPUMCRegisterInfo(StringRef TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAMDGPUMCRegisterInfo(X, 0);
  return X;
}

static MCSubtargetInfo *createAMDGPUMCSubtargetInfo(StringRef TT, StringRef CPU,
                                                   StringRef FS) {
  MCSubtargetInfo * X = new MCSubtargetInfo();
  InitAMDGPUMCSubtargetInfo(X, TT, CPU, FS);
  return X;
}

static MCCodeGenInfo *createAMDGPUMCCodeGenInfo(StringRef TT, Reloc::Model RM,
                                               CodeModel::Model CM,
                                               CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  X->InitMCCodeGenInfo(RM, CM, OL);
  return X;
}

static MCInstPrinter *createAMDGPUMCInstPrinter(const Target &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI,
                                                const MCSubtargetInfo &STI) {
  return new AMDGPUInstPrinter(MAI, MII, MRI);
}

static MCCodeEmitter *createAMDGPUMCCodeEmitter(const MCInstrInfo &MCII,
                                                const MCRegisterInfo &MRI,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  if (STI.getFeatureBits() & AMDGPU::Feature64BitPtr) {
    return createSIMCCodeEmitter(MCII, MRI, STI, Ctx);
  } else {
    return createR600MCCodeEmitter(MCII, MRI, STI);
  }
}

static MCStreamer *createMCStreamer(const Target &T, StringRef TT,
                                    MCContext &Ctx, MCAsmBackend &MAB,
                                    raw_ostream &_OS, MCCodeEmitter *_Emitter,
                                    const MCSubtargetInfo &STI, bool RelaxAll) {
  return createELFStreamer(Ctx, MAB, _OS, _Emitter, false);
}

extern "C" void LLVMInitializeR600TargetMC() {

  RegisterMCAsmInfo<AMDGPUMCAsmInfo> Y(TheAMDGPUTarget);
  RegisterMCAsmInfo<AMDGPUMCAsmInfo> Z(TheGCNTarget);

  TargetRegistry::RegisterMCCodeGenInfo(TheAMDGPUTarget, createAMDGPUMCCodeGenInfo);
  TargetRegistry::RegisterMCCodeGenInfo(TheGCNTarget, createAMDGPUMCCodeGenInfo);

  TargetRegistry::RegisterMCInstrInfo(TheAMDGPUTarget, createAMDGPUMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(TheGCNTarget, createAMDGPUMCInstrInfo);

  TargetRegistry::RegisterMCRegInfo(TheAMDGPUTarget, createAMDGPUMCRegisterInfo);
  TargetRegistry::RegisterMCRegInfo(TheGCNTarget, createAMDGPUMCRegisterInfo);

  TargetRegistry::RegisterMCSubtargetInfo(TheAMDGPUTarget, createAMDGPUMCSubtargetInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheGCNTarget, createAMDGPUMCSubtargetInfo);

  TargetRegistry::RegisterMCInstPrinter(TheAMDGPUTarget, createAMDGPUMCInstPrinter);
  TargetRegistry::RegisterMCInstPrinter(TheGCNTarget, createAMDGPUMCInstPrinter);

  TargetRegistry::RegisterMCCodeEmitter(TheAMDGPUTarget, createAMDGPUMCCodeEmitter);
  TargetRegistry::RegisterMCCodeEmitter(TheGCNTarget, createAMDGPUMCCodeEmitter);

  TargetRegistry::RegisterMCAsmBackend(TheAMDGPUTarget, createAMDGPUAsmBackend);
  TargetRegistry::RegisterMCAsmBackend(TheGCNTarget, createAMDGPUAsmBackend);

  TargetRegistry::RegisterMCObjectStreamer(TheAMDGPUTarget, createMCStreamer);
  TargetRegistry::RegisterMCObjectStreamer(TheGCNTarget, createMCStreamer);
}
