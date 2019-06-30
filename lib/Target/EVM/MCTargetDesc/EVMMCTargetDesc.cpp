//===-- EVMMCTargetDesc.cpp - EVM Target Descriptions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides EVM specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "EVMTargetStreamer.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "EVM.h"
#include "InstPrinter/EVMInstPrinter.h"
#include "MCTargetDesc/EVMMCAsmInfo.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "EVMGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "EVMGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "EVMGenRegisterInfo.inc"

using namespace llvm;

static MCInstrInfo *createEVMMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitEVMMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createEVMMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitEVMMCRegisterInfo(X, EVM::SP);
  return X;
}

static MCSubtargetInfo *createEVMMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createEVMMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCInstPrinter *createEVMMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new EVMInstPrinter(MAI, MII, MRI);
}

namespace {

class EVMMCInstrAnalysis : public MCInstrAnalysis {
public:
  explicit EVMMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}
};

} // end anonymous namespace

static MCInstrAnalysis *createEVMInstrAnalysis(const MCInstrInfo *Info) {
  return new EVMMCInstrAnalysis(Info);
}


EVMTargetStreamer::EVMTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

static MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) {
  return new EVMTargetNullStreamer(S);
}

static MCTargetStreamer *
createAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                        MCInstPrinter * /*InstPrint*/,
                        bool /*isVerboseAsm*/) {
  return new EVMTargetStreamer(S);
}

extern "C" void LLVMInitializeEVMTargetMC() {
  Target *T = &getTheEVMTarget();
  // Register the MC asm info.
  RegisterMCAsmInfo<EVMMCAsmInfo> X(*T);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(*T, createEVMMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(*T, createEVMMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(*T, createEVMMCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(*T, createEVMMCInstPrinter);

  // Register the MC instruction analyzer.
  TargetRegistry::RegisterMCInstrAnalysis(*T, createEVMInstrAnalysis);

  // Register the MC code emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheEVMTarget(),
                                        createEVMMCCodeEmitter);

  // Register the ASM Backend
  TargetRegistry::RegisterMCAsmBackend(getTheEVMTarget(),
                                       createEVMAsmBackend);

  // Register the asm target streamer.
  TargetRegistry::RegisterAsmTargetStreamer(*T, createAsmTargetStreamer);

  // Register the null target streamer.
  TargetRegistry::RegisterNullTargetStreamer(*T, createNullTargetStreamer);

}
