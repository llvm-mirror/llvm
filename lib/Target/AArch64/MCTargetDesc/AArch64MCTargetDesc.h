//===-- AArch64MCTargetDesc.h - AArch64 Target Descriptions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides AArch64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCTARGETDESC_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <string>

namespace llvm {
class formatted_raw_ostream;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCInstPrinter;
class MCRegisterInfo;
class MCObjectWriter;
class MCStreamer;
class MCSubtargetInfo;
class StringRef;
class Target;
class raw_ostream;

extern Target TheAArch64leTarget;
extern Target TheAArch64beTarget;
extern Target TheARM64Target;

MCCodeEmitter *createAArch64MCCodeEmitter(const MCInstrInfo &MCII,
                                        const MCRegisterInfo &MRI,
                                        const MCSubtargetInfo &STI,
                                        MCContext &Ctx);
MCAsmBackend *createAArch64leAsmBackend(const Target &T,
                                        const MCRegisterInfo &MRI, StringRef TT,
                                        StringRef CPU);
MCAsmBackend *createAArch64beAsmBackend(const Target &T,
                                        const MCRegisterInfo &MRI, StringRef TT,
                                        StringRef CPU);

MCObjectWriter *createAArch64ELFObjectWriter(raw_ostream &OS, uint8_t OSABI,
                                             bool IsLittleEndian);

MCObjectWriter *createAArch64MachObjectWriter(raw_ostream &OS, uint32_t CPUType,
                                            uint32_t CPUSubtype);

MCStreamer *
createAArch64MCAsmStreamer(MCContext &Ctx, formatted_raw_ostream &OS,
                           bool isVerboseAsm, bool useDwarfDirectory,
                           MCInstPrinter *InstPrint, MCCodeEmitter *CE,
                           MCAsmBackend *TAB, bool ShowInst);
} // End llvm namespace

// Defines symbolic names for AArch64 registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "AArch64GenRegisterInfo.inc"

// Defines symbolic names for the AArch64 instructions.
//
#define GET_INSTRINFO_ENUM
#include "AArch64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "AArch64GenSubtargetInfo.inc"

#endif
