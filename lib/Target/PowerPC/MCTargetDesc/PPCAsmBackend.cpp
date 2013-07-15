//===-- PPCAsmBackend.cpp - PPC Assembler Backend -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "MCTargetDesc/PPCFixupKinds.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Object/MachOFormat.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

static uint64_t adjustFixupValue(unsigned Kind, uint64_t Value) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
  case PPC::fixup_ppc_nofixup:
    return Value;
  case PPC::fixup_ppc_brcond14:
  case PPC::fixup_ppc_brcond14abs:
    return Value & 0xfffc;
  case PPC::fixup_ppc_br24:
  case PPC::fixup_ppc_br24abs:
    return Value & 0x3fffffc;
  case PPC::fixup_ppc_half16:
    return Value & 0xffff;
  case PPC::fixup_ppc_half16ds:
    return Value & 0xfffc;
  }
}

static unsigned getFixupKindNumBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
    return 1;
  case FK_Data_2:
  case PPC::fixup_ppc_half16:
  case PPC::fixup_ppc_half16ds:
    return 2;
  case FK_Data_4:
  case PPC::fixup_ppc_brcond14:
  case PPC::fixup_ppc_brcond14abs:
  case PPC::fixup_ppc_br24:
  case PPC::fixup_ppc_br24abs:
    return 4;
  case FK_Data_8:
    return 8;
  case PPC::fixup_ppc_nofixup:
    return 0;
  }
}

namespace {
class PPCMachObjectWriter : public MCMachObjectTargetWriter {
public:
  PPCMachObjectWriter(bool Is64Bit, uint32_t CPUType,
                      uint32_t CPUSubtype)
    : MCMachObjectTargetWriter(Is64Bit, CPUType, CPUSubtype) {}

  void RecordRelocation(MachObjectWriter *Writer,
                        const MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) {
    llvm_unreachable("Relocation emission for MachO/PPC unimplemented!");
  }
};

class PPCAsmBackend : public MCAsmBackend {
const Target &TheTarget;
public:
  PPCAsmBackend(const Target &T) : MCAsmBackend(), TheTarget(T) {}

  unsigned getNumFixupKinds() const { return PPC::NumTargetFixupKinds; }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const {
    const static MCFixupKindInfo Infos[PPC::NumTargetFixupKinds] = {
      // name                    offset  bits  flags
      { "fixup_ppc_br24",        6,      24,   MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_ppc_brcond14",    16,     14,   MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_ppc_br24abs",     6,      24,   0 },
      { "fixup_ppc_brcond14abs", 16,     14,   0 },
      { "fixup_ppc_half16",       0,     16,   0 },
      { "fixup_ppc_half16ds",     0,     14,   0 },
      { "fixup_ppc_nofixup",      0,      0,   0 }
    };

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFixup &Fixup, char *Data, unsigned DataSize,
                  uint64_t Value) const {
    Value = adjustFixupValue(Fixup.getKind(), Value);
    if (!Value) return;           // Doesn't change encoding.

    unsigned Offset = Fixup.getOffset();
    unsigned NumBytes = getFixupKindNumBytes(Fixup.getKind());

    // For each byte of the fragment that the fixup touches, mask in the bits
    // from the fixup value. The Value has been "split up" into the appropriate
    // bitfields above.
    for (unsigned i = 0; i != NumBytes; ++i)
      Data[Offset + i] |= uint8_t((Value >> ((NumBytes - i - 1)*8)) & 0xff);
  }

  bool mayNeedRelaxation(const MCInst &Inst) const {
    // FIXME.
    return false;
  }

  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const {
    // FIXME.
    llvm_unreachable("relaxInstruction() unimplemented");
  }


  void relaxInstruction(const MCInst &Inst, MCInst &Res) const {
    // FIXME.
    llvm_unreachable("relaxInstruction() unimplemented");
  }

  bool writeNopData(uint64_t Count, MCObjectWriter *OW) const {
    // Can't emit NOP with size not multiple of 32-bits
    if (Count % 4 != 0)
      return false;

    uint64_t NumNops = Count / 4;
    for (uint64_t i = 0; i != NumNops; ++i)
      OW->Write32(0x60000000);

    return true;
  }

  unsigned getPointerSize() const {
    StringRef Name = TheTarget.getName();
    if (Name == "ppc64") return 8;
    assert(Name == "ppc32" && "Unknown target name!");
    return 4;
  }
};
} // end anonymous namespace


// FIXME: This should be in a separate file.
namespace {
  class DarwinPPCAsmBackend : public PPCAsmBackend {
  public:
    DarwinPPCAsmBackend(const Target &T) : PPCAsmBackend(T) { }

    MCObjectWriter *createObjectWriter(raw_ostream &OS) const {
      bool is64 = getPointerSize() == 8;
      return createMachObjectWriter(new PPCMachObjectWriter(
                                      /*Is64Bit=*/is64,
                                      (is64 ? object::mach::CTM_PowerPC64 :
                                       object::mach::CTM_PowerPC),
                                      object::mach::CSPPC_ALL),
                                    OS, /*IsLittleEndian=*/false);
    }

    virtual bool doesSectionRequireSymbols(const MCSection &Section) const {
      return false;
    }
  };

  class ELFPPCAsmBackend : public PPCAsmBackend {
    uint8_t OSABI;
  public:
    ELFPPCAsmBackend(const Target &T, uint8_t OSABI) :
      PPCAsmBackend(T), OSABI(OSABI) { }


    MCObjectWriter *createObjectWriter(raw_ostream &OS) const {
      bool is64 = getPointerSize() == 8;
      return createPPCELFObjectWriter(OS, is64, OSABI);
    }

    virtual bool doesSectionRequireSymbols(const MCSection &Section) const {
      return false;
    }
  };

} // end anonymous namespace




MCAsmBackend *llvm::createPPCAsmBackend(const Target &T, StringRef TT, StringRef CPU) {
  if (Triple(TT).isOSDarwin())
    return new DarwinPPCAsmBackend(T);

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(Triple(TT).getOS());
  return new ELFPPCAsmBackend(T, OSABI);
}
