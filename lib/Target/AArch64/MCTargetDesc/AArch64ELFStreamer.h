//===-- AArch64ELFStreamer.h - ELF Streamer for AArch64 ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements ELF streamer information for the AArch64 backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ELFSTREAMER_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ELFSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"

namespace llvm {

MCELFStreamer *createAArch64ELFStreamer(MCContext &Context, MCAsmBackend &TAB,
                                        raw_ostream &OS, MCCodeEmitter *Emitter,
                                        bool RelaxAll);
}

#endif
