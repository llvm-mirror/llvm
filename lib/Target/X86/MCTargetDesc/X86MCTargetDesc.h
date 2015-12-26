//===-- X86MCTargetDesc.h - X86 Target Descriptions -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides X86 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCTARGETDESC_X86MCTARGETDESC_H
#define LLVM_LIB_TARGET_X86_MCTARGETDESC_X86MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <string>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCRelocationInfo;
class MCStreamer;
class Target;
class Triple;
class StringRef;
class raw_ostream;
class raw_pwrite_stream;

extern Target TheX86_32Target, TheX86_64Target;

/// Flavour of dwarf regnumbers
///
namespace DWARFFlavour {
  enum {
    X86_64 = 0, X86_32_DarwinEH = 1, X86_32_Generic = 2
  };
}

///  Native X86 register numbers
///
namespace N86 {
  enum {
    EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7
  };
}

namespace X86_MC {
std::string ParseX86Triple(const Triple &TT);

unsigned getDwarfRegFlavour(const Triple &TT, bool isEH);

void InitLLVM2SEHRegisterMapping(MCRegisterInfo *MRI);

/// Create a X86 MCSubtargetInfo instance. This is exposed so Asm parser, etc.
/// do not need to go through TargetRegistry.
MCSubtargetInfo *createX86MCSubtargetInfo(const Triple &TT, StringRef CPU,
                                          StringRef FS);
}

MCCodeEmitter *createX86MCCodeEmitter(const MCInstrInfo &MCII,
                                      const MCRegisterInfo &MRI,
                                      MCContext &Ctx);

MCAsmBackend *createX86_32AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                                     const Triple &TT, StringRef CPU);
MCAsmBackend *createX86_64AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                                     const Triple &TT, StringRef CPU);

/// Construct an X86 Windows COFF machine code streamer which will generate
/// PE/COFF format object files.
///
/// Takes ownership of \p AB and \p CE.
MCStreamer *createX86WinCOFFStreamer(MCContext &C, MCAsmBackend &AB,
                                     raw_pwrite_stream &OS, MCCodeEmitter *CE,
                                     bool RelaxAll, bool IncrementalLinkerCompatible);

/// Construct an X86 Mach-O object writer.
MCObjectWriter *createX86MachObjectWriter(raw_pwrite_stream &OS, bool Is64Bit,
                                          uint32_t CPUType,
                                          uint32_t CPUSubtype);

/// Construct an X86 ELF object writer.
MCObjectWriter *createX86ELFObjectWriter(raw_pwrite_stream &OS, bool IsELF64,
                                         uint8_t OSABI, uint16_t EMachine);
/// Construct an X86 Win COFF object writer.
MCObjectWriter *createX86WinCOFFObjectWriter(raw_pwrite_stream &OS,
                                             bool Is64Bit);

/// Construct X86-64 Mach-O relocation info.
MCRelocationInfo *createX86_64MachORelocationInfo(MCContext &Ctx);

/// Construct X86-64 ELF relocation info.
MCRelocationInfo *createX86_64ELFRelocationInfo(MCContext &Ctx);

/// Returns the sub or super register of a specific X86 register.
/// e.g. getX86SubSuperRegister(X86::EAX, 16) returns X86::AX.
/// Aborts on error.
unsigned getX86SubSuperRegister(unsigned, unsigned, bool High=false);

/// Returns the sub or super register of a specific X86 register.
/// Like getX86SubSuperRegister() but returns 0 on error.
unsigned getX86SubSuperRegisterOrZero(unsigned, unsigned,
                                      bool High = false);

} // End llvm namespace


// Defines symbolic names for X86 registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "X86GenRegisterInfo.inc"

// Defines symbolic names for the X86 instructions.
//
#define GET_INSTRINFO_ENUM
#include "X86GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "X86GenSubtargetInfo.inc"

#endif
