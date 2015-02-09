//===-- MipsTargetStreamer.cpp - Mips Target Streamer Methods -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Mips specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/MipsInstPrinter.h"
#include "MipsELFStreamer.h"
#include "MipsMCTargetDesc.h"
#include "MipsTargetObjectFile.h"
#include "MipsTargetStreamer.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

MipsTargetStreamer::MipsTargetStreamer(MCStreamer &S)
    : MCTargetStreamer(S), ModuleDirectiveAllowed(true) {
  GPRInfoSet = FPRInfoSet = FrameInfoSet = false;
}
void MipsTargetStreamer::emitDirectiveSetMicroMips() {}
void MipsTargetStreamer::emitDirectiveSetNoMicroMips() {}
void MipsTargetStreamer::emitDirectiveSetMips16() {}
void MipsTargetStreamer::emitDirectiveSetNoMips16() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetReorder() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetNoReorder() {}
void MipsTargetStreamer::emitDirectiveSetMacro() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetNoMacro() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMsa() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetNoMsa() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetAt() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetNoAt() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveEnd(StringRef Name) {}
void MipsTargetStreamer::emitDirectiveEnt(const MCSymbol &Symbol) {}
void MipsTargetStreamer::emitDirectiveAbiCalls() {}
void MipsTargetStreamer::emitDirectiveNaN2008() {}
void MipsTargetStreamer::emitDirectiveNaNLegacy() {}
void MipsTargetStreamer::emitDirectiveOptionPic0() {}
void MipsTargetStreamer::emitDirectiveOptionPic2() {}
void MipsTargetStreamer::emitFrame(unsigned StackReg, unsigned StackSize,
                                   unsigned ReturnReg) {}
void MipsTargetStreamer::emitMask(unsigned CPUBitmask, int CPUTopSavedRegOff) {}
void MipsTargetStreamer::emitFMask(unsigned FPUBitmask, int FPUTopSavedRegOff) {
}
void MipsTargetStreamer::emitDirectiveSetArch(StringRef Arch) {
  forbidModuleDirective();
}
void MipsTargetStreamer::emitDirectiveSetMips0() {}
void MipsTargetStreamer::emitDirectiveSetMips1() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips2() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips3() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips4() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips5() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips32() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips32R2() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips32R6() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips64() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips64R2() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetMips64R6() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetPop() {}
void MipsTargetStreamer::emitDirectiveSetPush() {}
void MipsTargetStreamer::emitDirectiveSetDsp() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveSetNoDsp() { forbidModuleDirective(); }
void MipsTargetStreamer::emitDirectiveCpLoad(unsigned RegNo) {}
void MipsTargetStreamer::emitDirectiveCpsetup(unsigned RegNo, int RegOrOffset,
                                              const MCSymbol &Sym, bool IsReg) {
}
void MipsTargetStreamer::emitDirectiveModuleOddSPReg(bool Enabled,
                                                     bool IsO32ABI) {
  if (!Enabled && !IsO32ABI)
    report_fatal_error("+nooddspreg is only valid for O32");
}

MipsTargetAsmStreamer::MipsTargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : MipsTargetStreamer(S), OS(OS) {}

void MipsTargetAsmStreamer::emitDirectiveSetMicroMips() {
  OS << "\t.set\tmicromips\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoMicroMips() {
  OS << "\t.set\tnomicromips\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips16() {
  OS << "\t.set\tmips16\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoMips16() {
  OS << "\t.set\tnomips16\n";
  MipsTargetStreamer::emitDirectiveSetNoMips16();
}

void MipsTargetAsmStreamer::emitDirectiveSetReorder() {
  OS << "\t.set\treorder\n";
  MipsTargetStreamer::emitDirectiveSetReorder();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoReorder() {
  OS << "\t.set\tnoreorder\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveSetMacro() {
  OS << "\t.set\tmacro\n";
  MipsTargetStreamer::emitDirectiveSetMacro();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoMacro() {
  OS << "\t.set\tnomacro\n";
  MipsTargetStreamer::emitDirectiveSetNoMacro();
}

void MipsTargetAsmStreamer::emitDirectiveSetMsa() {
  OS << "\t.set\tmsa\n";
  MipsTargetStreamer::emitDirectiveSetMsa();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoMsa() {
  OS << "\t.set\tnomsa\n";
  MipsTargetStreamer::emitDirectiveSetNoMsa();
}

void MipsTargetAsmStreamer::emitDirectiveSetAt() {
  OS << "\t.set\tat\n";
  MipsTargetStreamer::emitDirectiveSetAt();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoAt() {
  OS << "\t.set\tnoat\n";
  MipsTargetStreamer::emitDirectiveSetNoAt();
}

void MipsTargetAsmStreamer::emitDirectiveEnd(StringRef Name) {
  OS << "\t.end\t" << Name << '\n';
}

void MipsTargetAsmStreamer::emitDirectiveEnt(const MCSymbol &Symbol) {
  OS << "\t.ent\t" << Symbol.getName() << '\n';
}

void MipsTargetAsmStreamer::emitDirectiveAbiCalls() { OS << "\t.abicalls\n"; }

void MipsTargetAsmStreamer::emitDirectiveNaN2008() { OS << "\t.nan\t2008\n"; }

void MipsTargetAsmStreamer::emitDirectiveNaNLegacy() {
  OS << "\t.nan\tlegacy\n";
}

void MipsTargetAsmStreamer::emitDirectiveOptionPic0() {
  OS << "\t.option\tpic0\n";
}

void MipsTargetAsmStreamer::emitDirectiveOptionPic2() {
  OS << "\t.option\tpic2\n";
}

void MipsTargetAsmStreamer::emitFrame(unsigned StackReg, unsigned StackSize,
                                      unsigned ReturnReg) {
  OS << "\t.frame\t$"
     << StringRef(MipsInstPrinter::getRegisterName(StackReg)).lower() << ","
     << StackSize << ",$"
     << StringRef(MipsInstPrinter::getRegisterName(ReturnReg)).lower() << '\n';
}

void MipsTargetAsmStreamer::emitDirectiveSetArch(StringRef Arch) {
  OS << "\t.set arch=" << Arch << "\n";
  MipsTargetStreamer::emitDirectiveSetArch(Arch);
}

void MipsTargetAsmStreamer::emitDirectiveSetMips0() { OS << "\t.set\tmips0\n"; }

void MipsTargetAsmStreamer::emitDirectiveSetMips1() {
  OS << "\t.set\tmips1\n";
  MipsTargetStreamer::emitDirectiveSetMips1();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips2() {
  OS << "\t.set\tmips2\n";
  MipsTargetStreamer::emitDirectiveSetMips2();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips3() {
  OS << "\t.set\tmips3\n";
  MipsTargetStreamer::emitDirectiveSetMips3();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips4() {
  OS << "\t.set\tmips4\n";
  MipsTargetStreamer::emitDirectiveSetMips4();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips5() {
  OS << "\t.set\tmips5\n";
  MipsTargetStreamer::emitDirectiveSetMips5();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips32() {
  OS << "\t.set\tmips32\n";
  MipsTargetStreamer::emitDirectiveSetMips32();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips32R2() {
  OS << "\t.set\tmips32r2\n";
  MipsTargetStreamer::emitDirectiveSetMips32R2();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips32R6() {
  OS << "\t.set\tmips32r6\n";
  MipsTargetStreamer::emitDirectiveSetMips32R6();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips64() {
  OS << "\t.set\tmips64\n";
  MipsTargetStreamer::emitDirectiveSetMips64();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips64R2() {
  OS << "\t.set\tmips64r2\n";
  MipsTargetStreamer::emitDirectiveSetMips64R2();
}

void MipsTargetAsmStreamer::emitDirectiveSetMips64R6() {
  OS << "\t.set\tmips64r6\n";
  MipsTargetStreamer::emitDirectiveSetMips64R6();
}

void MipsTargetAsmStreamer::emitDirectiveSetDsp() {
  OS << "\t.set\tdsp\n";
  MipsTargetStreamer::emitDirectiveSetDsp();
}

void MipsTargetAsmStreamer::emitDirectiveSetNoDsp() {
  OS << "\t.set\tnodsp\n";
  MipsTargetStreamer::emitDirectiveSetNoDsp();
}

void MipsTargetAsmStreamer::emitDirectiveSetPop() { OS << "\t.set\tpop\n"; }

void MipsTargetAsmStreamer::emitDirectiveSetPush() { OS << "\t.set\tpush\n"; }

// Print a 32 bit hex number with all numbers.
static void printHex32(unsigned Value, raw_ostream &OS) {
  OS << "0x";
  for (int i = 7; i >= 0; i--)
    OS.write_hex((Value & (0xF << (i * 4))) >> (i * 4));
}

void MipsTargetAsmStreamer::emitMask(unsigned CPUBitmask,
                                     int CPUTopSavedRegOff) {
  OS << "\t.mask \t";
  printHex32(CPUBitmask, OS);
  OS << ',' << CPUTopSavedRegOff << '\n';
}

void MipsTargetAsmStreamer::emitFMask(unsigned FPUBitmask,
                                      int FPUTopSavedRegOff) {
  OS << "\t.fmask\t";
  printHex32(FPUBitmask, OS);
  OS << "," << FPUTopSavedRegOff << '\n';
}

void MipsTargetAsmStreamer::emitDirectiveCpLoad(unsigned RegNo) {
  OS << "\t.cpload\t$"
     << StringRef(MipsInstPrinter::getRegisterName(RegNo)).lower() << "\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveCpsetup(unsigned RegNo,
                                                 int RegOrOffset,
                                                 const MCSymbol &Sym,
                                                 bool IsReg) {
  OS << "\t.cpsetup\t$"
     << StringRef(MipsInstPrinter::getRegisterName(RegNo)).lower() << ", ";

  if (IsReg)
    OS << "$"
       << StringRef(MipsInstPrinter::getRegisterName(RegOrOffset)).lower();
  else
    OS << RegOrOffset;

  OS << ", ";

  OS << Sym.getName() << "\n";
  forbidModuleDirective();
}

void MipsTargetAsmStreamer::emitDirectiveModuleFP(
    MipsABIFlagsSection::FpABIKind Value, bool Is32BitABI) {
  MipsTargetStreamer::emitDirectiveModuleFP(Value, Is32BitABI);

  StringRef ModuleValue;
  OS << "\t.module\tfp=";
  OS << ABIFlagsSection.getFpABIString(Value) << "\n";
}

void MipsTargetAsmStreamer::emitDirectiveSetFp(
    MipsABIFlagsSection::FpABIKind Value) {
  StringRef ModuleValue;
  OS << "\t.set\tfp=";
  OS << ABIFlagsSection.getFpABIString(Value) << "\n";
}

void MipsTargetAsmStreamer::emitMipsAbiFlags() {
  // No action required for text output.
}

void MipsTargetAsmStreamer::emitDirectiveModuleOddSPReg(bool Enabled,
                                                        bool IsO32ABI) {
  MipsTargetStreamer::emitDirectiveModuleOddSPReg(Enabled, IsO32ABI);

  OS << "\t.module\t" << (Enabled ? "" : "no") << "oddspreg\n";
}

// This part is for ELF object output.
MipsTargetELFStreamer::MipsTargetELFStreamer(MCStreamer &S,
                                             const MCSubtargetInfo &STI)
    : MipsTargetStreamer(S), MicroMipsEnabled(false), STI(STI) {
  MCAssembler &MCA = getStreamer().getAssembler();
  Triple T(STI.getTargetTriple());
  Pic = (MCA.getContext().getObjectFileInfo()->getRelocM() == Reloc::PIC_)
            ? true
            : false;

  uint64_t Features = STI.getFeatureBits();

  // Set the header flags that we can in the constructor.
  // FIXME: This is a fairly terrible hack. We set the rest
  // of these in the destructor. The problem here is two-fold:
  //
  // a: Some of the eflags can be set/reset by directives.
  // b: There aren't any usage paths that initialize the ABI
  //    pointer until after we initialize either an assembler
  //    or the target machine.
  // We can fix this by making the target streamer construct
  // the ABI, but this is fraught with wide ranging dependency
  // issues as well.
  unsigned EFlags = MCA.getELFHeaderEFlags();

  // Architecture
  if (Features & Mips::FeatureMips64r6)
    EFlags |= ELF::EF_MIPS_ARCH_64R6;
  else if (Features & Mips::FeatureMips64r2)
    EFlags |= ELF::EF_MIPS_ARCH_64R2;
  else if (Features & Mips::FeatureMips64)
    EFlags |= ELF::EF_MIPS_ARCH_64;
  else if (Features & Mips::FeatureMips5)
    EFlags |= ELF::EF_MIPS_ARCH_5;
  else if (Features & Mips::FeatureMips4)
    EFlags |= ELF::EF_MIPS_ARCH_4;
  else if (Features & Mips::FeatureMips3)
    EFlags |= ELF::EF_MIPS_ARCH_3;
  else if (Features & Mips::FeatureMips32r6)
    EFlags |= ELF::EF_MIPS_ARCH_32R6;
  else if (Features & Mips::FeatureMips32r2)
    EFlags |= ELF::EF_MIPS_ARCH_32R2;
  else if (Features & Mips::FeatureMips32)
    EFlags |= ELF::EF_MIPS_ARCH_32;
  else if (Features & Mips::FeatureMips2)
    EFlags |= ELF::EF_MIPS_ARCH_2;
  else
    EFlags |= ELF::EF_MIPS_ARCH_1;

  // Other options.
  if (Features & Mips::FeatureNaN2008)
    EFlags |= ELF::EF_MIPS_NAN2008;

  // -mabicalls and -mplt are not implemented but we should act as if they were
  // given.
  EFlags |= ELF::EF_MIPS_CPIC;

  MCA.setELFHeaderEFlags(EFlags);
}

void MipsTargetELFStreamer::emitLabel(MCSymbol *Symbol) {
  if (!isMicroMipsEnabled())
    return;
  MCSymbolData &Data = getStreamer().getOrCreateSymbolData(Symbol);
  uint8_t Type = MCELF::GetType(Data);
  if (Type != ELF::STT_FUNC)
    return;

  // The "other" values are stored in the last 6 bits of the second byte
  // The traditional defines for STO values assume the full byte and thus
  // the shift to pack it.
  MCELF::setOther(Data, ELF::STO_MIPS_MICROMIPS >> 2);
}

void MipsTargetELFStreamer::finish() {
  MCAssembler &MCA = getStreamer().getAssembler();
  const MCObjectFileInfo &OFI = *MCA.getContext().getObjectFileInfo();

  // .bss, .text and .data are always at least 16-byte aligned.
  MCSectionData &TextSectionData =
      MCA.getOrCreateSectionData(*OFI.getTextSection());
  MCSectionData &DataSectionData =
      MCA.getOrCreateSectionData(*OFI.getDataSection());
  MCSectionData &BSSSectionData =
      MCA.getOrCreateSectionData(*OFI.getBSSSection());

  TextSectionData.setAlignment(std::max(16u, TextSectionData.getAlignment()));
  DataSectionData.setAlignment(std::max(16u, DataSectionData.getAlignment()));
  BSSSectionData.setAlignment(std::max(16u, BSSSectionData.getAlignment()));

  uint64_t Features = STI.getFeatureBits();

  // Update e_header flags. See the FIXME and comment above in
  // the constructor for a full rundown on this.
  unsigned EFlags = MCA.getELFHeaderEFlags();

  // ABI
  // N64 does not require any ABI bits.
  if (getABI().IsO32())
    EFlags |= ELF::EF_MIPS_ABI_O32;
  else if (getABI().IsN32())
    EFlags |= ELF::EF_MIPS_ABI2;

  if (Features & Mips::FeatureGP64Bit) {
    if (getABI().IsO32())
      EFlags |= ELF::EF_MIPS_32BITMODE; /* Compatibility Mode */
  } else if (Features & Mips::FeatureMips64r2 || Features & Mips::FeatureMips64)
    EFlags |= ELF::EF_MIPS_32BITMODE;

  // If we've set the cpic eflag and we're n64, go ahead and set the pic
  // one as well.
  if (EFlags & ELF::EF_MIPS_CPIC && getABI().IsN64())
    EFlags |= ELF::EF_MIPS_PIC;

  MCA.setELFHeaderEFlags(EFlags);

  // Emit all the option records.
  // At the moment we are only emitting .Mips.options (ODK_REGINFO) and
  // .reginfo.
  MipsELFStreamer &MEF = static_cast<MipsELFStreamer &>(Streamer);
  MEF.EmitMipsOptionRecords();

  emitMipsAbiFlags();
}

void MipsTargetELFStreamer::emitAssignment(MCSymbol *Symbol,
                                           const MCExpr *Value) {
  // If on rhs is micromips symbol then mark Symbol as microMips.
  if (Value->getKind() != MCExpr::SymbolRef)
    return;
  const MCSymbol &RhsSym =
      static_cast<const MCSymbolRefExpr *>(Value)->getSymbol();
  MCSymbolData &Data = getStreamer().getOrCreateSymbolData(&RhsSym);
  uint8_t Type = MCELF::GetType(Data);
  if ((Type != ELF::STT_FUNC) ||
      !(MCELF::getOther(Data) & (ELF::STO_MIPS_MICROMIPS >> 2)))
    return;

  MCSymbolData &SymbolData = getStreamer().getOrCreateSymbolData(Symbol);
  // The "other" values are stored in the last 6 bits of the second byte.
  // The traditional defines for STO values assume the full byte and thus
  // the shift to pack it.
  MCELF::setOther(SymbolData, ELF::STO_MIPS_MICROMIPS >> 2);
}

MCELFStreamer &MipsTargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}

void MipsTargetELFStreamer::emitDirectiveSetMicroMips() {
  MicroMipsEnabled = true;

  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags |= ELF::EF_MIPS_MICROMIPS;
  MCA.setELFHeaderEFlags(Flags);
  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitDirectiveSetNoMicroMips() {
  MicroMipsEnabled = false;
  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitDirectiveSetMips16() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags |= ELF::EF_MIPS_ARCH_ASE_M16;
  MCA.setELFHeaderEFlags(Flags);
  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitDirectiveSetNoReorder() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags |= ELF::EF_MIPS_NOREORDER;
  MCA.setELFHeaderEFlags(Flags);
  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitDirectiveEnd(StringRef Name) {
  MCAssembler &MCA = getStreamer().getAssembler();
  MCContext &Context = MCA.getContext();
  MCStreamer &OS = getStreamer();

  const MCSectionELF *Sec = Context.getELFSection(
      ".pdr", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHT_REL);

  const MCSymbolRefExpr *ExprRef =
      MCSymbolRefExpr::Create(Name, MCSymbolRefExpr::VK_None, Context);

  MCSectionData &SecData = MCA.getOrCreateSectionData(*Sec);
  SecData.setAlignment(4);

  OS.PushSection();

  OS.SwitchSection(Sec);

  OS.EmitValueImpl(ExprRef, 4);

  OS.EmitIntValue(GPRInfoSet ? GPRBitMask : 0, 4); // reg_mask
  OS.EmitIntValue(GPRInfoSet ? GPROffset : 0, 4);  // reg_offset

  OS.EmitIntValue(FPRInfoSet ? FPRBitMask : 0, 4); // fpreg_mask
  OS.EmitIntValue(FPRInfoSet ? FPROffset : 0, 4);  // fpreg_offset

  OS.EmitIntValue(FrameInfoSet ? FrameOffset : 0, 4); // frame_offset
  OS.EmitIntValue(FrameInfoSet ? FrameReg : 0, 4);    // frame_reg
  OS.EmitIntValue(FrameInfoSet ? ReturnReg : 0, 4);   // return_reg

  // The .end directive marks the end of a procedure. Invalidate
  // the information gathered up until this point.
  GPRInfoSet = FPRInfoSet = FrameInfoSet = false;

  OS.PopSection();
}

void MipsTargetELFStreamer::emitDirectiveEnt(const MCSymbol &Symbol) {
  GPRInfoSet = FPRInfoSet = FrameInfoSet = false;
}

void MipsTargetELFStreamer::emitDirectiveAbiCalls() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags |= ELF::EF_MIPS_CPIC | ELF::EF_MIPS_PIC;
  MCA.setELFHeaderEFlags(Flags);
}

void MipsTargetELFStreamer::emitDirectiveNaN2008() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags |= ELF::EF_MIPS_NAN2008;
  MCA.setELFHeaderEFlags(Flags);
}

void MipsTargetELFStreamer::emitDirectiveNaNLegacy() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Flags &= ~ELF::EF_MIPS_NAN2008;
  MCA.setELFHeaderEFlags(Flags);
}

void MipsTargetELFStreamer::emitDirectiveOptionPic0() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  // This option overrides other PIC options like -KPIC.
  Pic = false;
  Flags &= ~ELF::EF_MIPS_PIC;
  MCA.setELFHeaderEFlags(Flags);
}

void MipsTargetELFStreamer::emitDirectiveOptionPic2() {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned Flags = MCA.getELFHeaderEFlags();
  Pic = true;
  // NOTE: We are following the GAS behaviour here which means the directive
  // 'pic2' also sets the CPIC bit in the ELF header. This is different from
  // what is stated in the SYSV ABI which consider the bits EF_MIPS_PIC and
  // EF_MIPS_CPIC to be mutually exclusive.
  Flags |= ELF::EF_MIPS_PIC | ELF::EF_MIPS_CPIC;
  MCA.setELFHeaderEFlags(Flags);
}

void MipsTargetELFStreamer::emitFrame(unsigned StackReg, unsigned StackSize,
                                      unsigned ReturnReg_) {
  MCContext &Context = getStreamer().getAssembler().getContext();
  const MCRegisterInfo *RegInfo = Context.getRegisterInfo();

  FrameInfoSet = true;
  FrameReg = RegInfo->getEncodingValue(StackReg);
  FrameOffset = StackSize;
  ReturnReg = RegInfo->getEncodingValue(ReturnReg_);
}

void MipsTargetELFStreamer::emitMask(unsigned CPUBitmask,
                                     int CPUTopSavedRegOff) {
  GPRInfoSet = true;
  GPRBitMask = CPUBitmask;
  GPROffset = CPUTopSavedRegOff;
}

void MipsTargetELFStreamer::emitFMask(unsigned FPUBitmask,
                                      int FPUTopSavedRegOff) {
  FPRInfoSet = true;
  FPRBitMask = FPUBitmask;
  FPROffset = FPUTopSavedRegOff;
}

void MipsTargetELFStreamer::emitDirectiveCpLoad(unsigned RegNo) {
  // .cpload $reg
  // This directive expands to:
  // lui   $gp, %hi(_gp_disp)
  // addui $gp, $gp, %lo(_gp_disp)
  // addu  $gp, $gp, $reg
  // when support for position independent code is enabled.
  if (!Pic || (getABI().IsN32() || getABI().IsN64()))
    return;

  // There's a GNU extension controlled by -mno-shared that allows
  // locally-binding symbols to be accessed using absolute addresses.
  // This is currently not supported. When supported -mno-shared makes
  // .cpload expand to:
  //   lui     $gp, %hi(__gnu_local_gp)
  //   addiu   $gp, $gp, %lo(__gnu_local_gp)

  StringRef SymName("_gp_disp");
  MCAssembler &MCA = getStreamer().getAssembler();
  MCSymbol *GP_Disp = MCA.getContext().GetOrCreateSymbol(SymName);
  MCA.getOrCreateSymbolData(*GP_Disp);

  MCInst TmpInst;
  TmpInst.setOpcode(Mips::LUi);
  TmpInst.addOperand(MCOperand::CreateReg(Mips::GP));
  const MCSymbolRefExpr *HiSym = MCSymbolRefExpr::Create(
      "_gp_disp", MCSymbolRefExpr::VK_Mips_ABS_HI, MCA.getContext());
  TmpInst.addOperand(MCOperand::CreateExpr(HiSym));
  getStreamer().EmitInstruction(TmpInst, STI);

  TmpInst.clear();

  TmpInst.setOpcode(Mips::ADDiu);
  TmpInst.addOperand(MCOperand::CreateReg(Mips::GP));
  TmpInst.addOperand(MCOperand::CreateReg(Mips::GP));
  const MCSymbolRefExpr *LoSym = MCSymbolRefExpr::Create(
      "_gp_disp", MCSymbolRefExpr::VK_Mips_ABS_LO, MCA.getContext());
  TmpInst.addOperand(MCOperand::CreateExpr(LoSym));
  getStreamer().EmitInstruction(TmpInst, STI);

  TmpInst.clear();

  TmpInst.setOpcode(Mips::ADDu);
  TmpInst.addOperand(MCOperand::CreateReg(Mips::GP));
  TmpInst.addOperand(MCOperand::CreateReg(Mips::GP));
  TmpInst.addOperand(MCOperand::CreateReg(RegNo));
  getStreamer().EmitInstruction(TmpInst, STI);

  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitDirectiveCpsetup(unsigned RegNo,
                                                 int RegOrOffset,
                                                 const MCSymbol &Sym,
                                                 bool IsReg) {
  // Only N32 and N64 emit anything for .cpsetup iff PIC is set.
  if (!Pic || !(getABI().IsN32() || getABI().IsN64()))
    return;

  MCAssembler &MCA = getStreamer().getAssembler();
  MCInst Inst;

  // Either store the old $gp in a register or on the stack
  if (IsReg) {
    // move $save, $gpreg
    Inst.setOpcode(Mips::DADDu);
    Inst.addOperand(MCOperand::CreateReg(RegOrOffset));
    Inst.addOperand(MCOperand::CreateReg(Mips::GP));
    Inst.addOperand(MCOperand::CreateReg(Mips::ZERO));
  } else {
    // sd $gpreg, offset($sp)
    Inst.setOpcode(Mips::SD);
    Inst.addOperand(MCOperand::CreateReg(Mips::GP));
    Inst.addOperand(MCOperand::CreateReg(Mips::SP));
    Inst.addOperand(MCOperand::CreateImm(RegOrOffset));
  }
  getStreamer().EmitInstruction(Inst, STI);
  Inst.clear();

  const MCSymbolRefExpr *HiExpr = MCSymbolRefExpr::Create(
      Sym.getName(), MCSymbolRefExpr::VK_Mips_GPOFF_HI, MCA.getContext());
  const MCSymbolRefExpr *LoExpr = MCSymbolRefExpr::Create(
      Sym.getName(), MCSymbolRefExpr::VK_Mips_GPOFF_LO, MCA.getContext());
  // lui $gp, %hi(%neg(%gp_rel(funcSym)))
  Inst.setOpcode(Mips::LUi);
  Inst.addOperand(MCOperand::CreateReg(Mips::GP));
  Inst.addOperand(MCOperand::CreateExpr(HiExpr));
  getStreamer().EmitInstruction(Inst, STI);
  Inst.clear();

  // addiu  $gp, $gp, %lo(%neg(%gp_rel(funcSym)))
  Inst.setOpcode(Mips::ADDiu);
  Inst.addOperand(MCOperand::CreateReg(Mips::GP));
  Inst.addOperand(MCOperand::CreateReg(Mips::GP));
  Inst.addOperand(MCOperand::CreateExpr(LoExpr));
  getStreamer().EmitInstruction(Inst, STI);
  Inst.clear();

  // daddu  $gp, $gp, $funcreg
  Inst.setOpcode(Mips::DADDu);
  Inst.addOperand(MCOperand::CreateReg(Mips::GP));
  Inst.addOperand(MCOperand::CreateReg(Mips::GP));
  Inst.addOperand(MCOperand::CreateReg(RegNo));
  getStreamer().EmitInstruction(Inst, STI);

  forbidModuleDirective();
}

void MipsTargetELFStreamer::emitMipsAbiFlags() {
  MCAssembler &MCA = getStreamer().getAssembler();
  MCContext &Context = MCA.getContext();
  MCStreamer &OS = getStreamer();
  const MCSectionELF *Sec = Context.getELFSection(
      ".MIPS.abiflags", ELF::SHT_MIPS_ABIFLAGS, ELF::SHF_ALLOC, 24, "");
  MCSectionData &ABIShndxSD = MCA.getOrCreateSectionData(*Sec);
  ABIShndxSD.setAlignment(8);
  OS.SwitchSection(Sec);

  OS << ABIFlagsSection;
}

void MipsTargetELFStreamer::emitDirectiveModuleOddSPReg(bool Enabled,
                                                        bool IsO32ABI) {
  MipsTargetStreamer::emitDirectiveModuleOddSPReg(Enabled, IsO32ABI);

  ABIFlagsSection.OddSPReg = Enabled;
}
