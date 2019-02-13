//===-- EVMELFStreamer.cpp - EVM ELF Target Streamer Methods ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides EVM specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "EVMELFStreamer.h"
#include "EVMMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

// This part is for ELF object output.
EVMTargetELFStreamer::EVMTargetELFStreamer(MCStreamer &S,
                                               const MCSubtargetInfo &STI)
    : EVMTargetStreamer(S) {
  MCAssembler &MCA = getStreamer().getAssembler();

  const FeatureBitset &Features = STI.getFeatureBits();

  unsigned EFlags = MCA.getELFHeaderEFlags();

  if (Features[EVM::FeatureStdExtC])
    EFlags |= ELF::EF_EVM_RVC;

  MCA.setELFHeaderEFlags(EFlags);
}

MCELFStreamer &EVMTargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}

void EVMTargetELFStreamer::emitDirectiveOptionPush() {}
void EVMTargetELFStreamer::emitDirectiveOptionPop() {}
void EVMTargetELFStreamer::emitDirectiveOptionRVC() {}
void EVMTargetELFStreamer::emitDirectiveOptionNoRVC() {}
void EVMTargetELFStreamer::emitDirectiveOptionRelax() {}
void EVMTargetELFStreamer::emitDirectiveOptionNoRelax() {}
