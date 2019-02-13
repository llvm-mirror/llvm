//===-- EVMMCTargetDesc.cpp - EVM Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file provides EVM-specific target descriptions.
///
//===----------------------------------------------------------------------===//

#include "EVMMCTargetDesc.h"
#include "InstPrinter/EVMInstPrinter.h"
#include "EVMELFStreamer.h"
#include "EVMMCAsmInfo.h"
#include "EVMTargetStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "EVMGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "EVMGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "EVMGenSubtargetInfo.inc"

using namespace llvm;

static MCInstrInfo *createEVMMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitEVMMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createEVMMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitEVMMCRegisterInfo(X, EVM::X1);
  return X;
}

static MCAsmInfo *createEVMMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT) {
  return new EVMMCAsmInfo(TT);
}

static MCSubtargetInfo *createEVMMCSubtargetInfo(const Triple &TT,
                                                   StringRef CPU, StringRef FS) {
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = TT.isArch64Bit() ? "generic-rv64" : "generic-rv32";
  return createEVMMCSubtargetInfoImpl(TT, CPUName, FS);
}

static MCInstPrinter *createEVMMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new EVMInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *
createEVMObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new EVMTargetELFStreamer(S, STI);
  return nullptr;
}

static MCTargetStreamer *createEVMAsmTargetStreamer(MCStreamer &S,
                                                      formatted_raw_ostream &OS,
                                                      MCInstPrinter *InstPrint,
                                                      bool isVerboseAsm) {
  return new EVMTargetAsmStreamer(S, OS);
}

extern "C" void LLVMInitializeEVMTargetMC() {
  for (Target *T : {&getTheEVM64Target()}) {
    TargetRegistry::RegisterMCAsmInfo(*T, createEVMMCAsmInfo);
    TargetRegistry::RegisterMCInstrInfo(*T, createEVMMCInstrInfo);
    TargetRegistry::RegisterMCRegInfo(*T, createEVMMCRegisterInfo);
    TargetRegistry::RegisterMCAsmBackend(*T, createEVMAsmBackend);
    TargetRegistry::RegisterMCCodeEmitter(*T, createEVMMCCodeEmitter);
    TargetRegistry::RegisterMCInstPrinter(*T, createEVMMCInstPrinter);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createEVMMCSubtargetInfo);
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createEVMObjectTargetStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createEVMAsmTargetStreamer);
  }
}
