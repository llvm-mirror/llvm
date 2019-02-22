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
  llvm_unreachable("unimplemented.");
}

static MCRegisterInfo *createEVMMCRegisterInfo(const Triple &TT) {
  llvm_unreachable("unimplemented.");
}

static MCSubtargetInfo *createEVMMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createEVMMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCStreamer *createEVMMCStreamer(const Triple &T, MCContext &Ctx,
                                       std::unique_ptr<MCAsmBackend> &&MAB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&Emitter,
                                       bool RelaxAll) {
  return createELFStreamer(Ctx, std::move(MAB), std::move(OW), std::move(Emitter),
                           RelaxAll);
}

static MCInstPrinter *createEVMMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new EVMInstPrinter(MAI, MII, MRI);
  return nullptr;
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

  // Register the object streamer
  TargetRegistry::RegisterELFStreamer(*T, createEVMMCStreamer);

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

  TargetRegistry::RegisterMCCodeEmitter(getTheEVMTarget(),
                                        createEVMMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(getTheEVMTarget(),
                                       createEVMAsmBackend);

}
