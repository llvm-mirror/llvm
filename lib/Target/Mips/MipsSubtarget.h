//===-- MipsSubtarget.h - Define Subtarget for the Mips ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Mips specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef MIPSSUBTARGET_H
#define MIPSSUBTARGET_H

#include "MipsFrameLowering.h"
#include "MipsISelLowering.h"
#include "MipsInstrInfo.h"
#include "MipsJITInfo.h"
#include "MipsSelectionDAGInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "MipsGenSubtargetInfo.inc"

namespace llvm {
class StringRef;

class MipsTargetMachine;

class MipsSubtarget : public MipsGenSubtargetInfo {
  virtual void anchor();

public:
  // NOTE: O64 will not be supported.
  enum MipsABIEnum {
    UnknownABI, O32, N32, N64, EABI
  };

protected:
  enum MipsArchEnum {
    Mips1, Mips2, Mips32, Mips32r2, Mips32r6, Mips3, Mips4, Mips5, Mips64,
    Mips64r2, Mips64r6
  };

  // Mips architecture version
  MipsArchEnum MipsArchVersion;

  // Mips supported ABIs
  MipsABIEnum MipsABI;

  // IsLittle - The target is Little Endian
  bool IsLittle;

  // IsSingleFloat - The target only supports single precision float
  // point operations. This enable the target to use all 32 32-bit
  // floating point registers instead of only using even ones.
  bool IsSingleFloat;

  // IsFP64bit - The target processor has 64-bit floating point registers.
  bool IsFP64bit;

  // IsNan2008 - IEEE 754-2008 NaN encoding.
  bool IsNaN2008bit;

  // IsFP64bit - General-purpose registers are 64 bits wide
  bool IsGP64bit;

  // HasVFPU - Processor has a vector floating point unit.
  bool HasVFPU;

  // CPU supports cnMIPS (Cavium Networks Octeon CPU).
  bool HasCnMips;

  // isLinux - Target system is Linux. Is false we consider ELFOS for now.
  bool IsLinux;

  // UseSmallSection - Small section is used.
  bool UseSmallSection;

  /// Features related to the presence of specific instructions.

  // HasMips3_32 - The subset of MIPS-III instructions added to MIPS32
  bool HasMips3_32;

  // HasMips3_32r2 - The subset of MIPS-III instructions added to MIPS32r2
  bool HasMips3_32r2;

  // HasMips4_32 - Has the subset of MIPS-IV present in MIPS32
  bool HasMips4_32;

  // HasMips4_32r2 - Has the subset of MIPS-IV present in MIPS32r2
  bool HasMips4_32r2;

  // HasMips5_32r2 - Has the subset of MIPS-V present in MIPS32r2
  bool HasMips5_32r2;

  // InMips16 -- can process Mips16 instructions
  bool InMips16Mode;

  // Mips16 hard float
  bool InMips16HardFloat;

  // PreviousInMips16 -- the function we just processed was in Mips 16 Mode
  bool PreviousInMips16Mode;

  // InMicroMips -- can process MicroMips instructions
  bool InMicroMipsMode;

  // HasDSP, HasDSPR2 -- supports DSP ASE.
  bool HasDSP, HasDSPR2;

  // Allow mixed Mips16 and Mips32 in one source file
  bool AllowMixed16_32;

  // Optimize for space by compiling all functions as Mips 16 unless
  // it needs floating point. Functions needing floating point are
  // compiled as Mips32
  bool Os16;

  // HasMSA -- supports MSA ASE.
  bool HasMSA;

  InstrItineraryData InstrItins;

  // Relocation Model
  Reloc::Model RM;

  // We can override the determination of whether we are in mips16 mode
  // as from the command line
  enum {NoOverride, Mips16Override, NoMips16Override} OverrideMode;

  MipsTargetMachine *TM;

  Triple TargetTriple;

  const DataLayout DL; // Calculates type size & alignment
  const MipsSelectionDAGInfo TSInfo;
  MipsJITInfo JITInfo;
  std::unique_ptr<const MipsInstrInfo> InstrInfo;
  std::unique_ptr<const MipsFrameLowering> FrameLowering;
  std::unique_ptr<const MipsTargetLowering> TLInfo;
  std::unique_ptr<const MipsInstrInfo> InstrInfo16;
  std::unique_ptr<const MipsFrameLowering> FrameLowering16;
  std::unique_ptr<const MipsTargetLowering> TLInfo16;
  std::unique_ptr<const MipsInstrInfo> InstrInfoSE;
  std::unique_ptr<const MipsFrameLowering> FrameLoweringSE;
  std::unique_ptr<const MipsTargetLowering> TLInfoSE;

public:
  bool enablePostRAScheduler(CodeGenOpt::Level OptLevel,
                             AntiDepBreakMode& Mode,
                             RegClassVector& CriticalPathRCs) const override;

  /// Only O32 and EABI supported right now.
  bool isABI_EABI() const { return MipsABI == EABI; }
  bool isABI_N64() const { return MipsABI == N64; }
  bool isABI_N32() const { return MipsABI == N32; }
  bool isABI_O32() const { return MipsABI == O32; }
  bool isABI_FPXX() const { return false; } // TODO: add check for FPXX
  unsigned getTargetABI() const { return MipsABI; }

  /// This constructor initializes the data members to match that
  /// of the specified triple.
  MipsSubtarget(const std::string &TT, const std::string &CPU,
                const std::string &FS, bool little, Reloc::Model RM,
                MipsTargetMachine *TM);

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  bool hasMips1() const { return MipsArchVersion >= Mips1; }
  bool hasMips2() const { return MipsArchVersion >= Mips2; }
  bool hasMips3() const { return MipsArchVersion >= Mips3; }
  bool hasMips4() const { return MipsArchVersion >= Mips4; }
  bool hasMips5() const { return MipsArchVersion >= Mips5; }
  bool hasMips4_32() const { return HasMips4_32; }
  bool hasMips4_32r2() const { return HasMips4_32r2; }
  bool hasMips32() const {
    return MipsArchVersion >= Mips32 && MipsArchVersion != Mips3 &&
           MipsArchVersion != Mips4 && MipsArchVersion != Mips5;
  }
  bool hasMips32r2() const {
    return MipsArchVersion == Mips32r2 || MipsArchVersion == Mips32r6 ||
           MipsArchVersion == Mips64r2 || MipsArchVersion == Mips64r6;
  }
  bool hasMips32r6() const {
    return MipsArchVersion == Mips32r6 || MipsArchVersion == Mips64r6;
  }
  bool hasMips64() const { return MipsArchVersion >= Mips64; }
  bool hasMips64r2() const {
    return MipsArchVersion == Mips64r2 || MipsArchVersion == Mips64r6;
  }
  bool hasMips64r6() const { return MipsArchVersion == Mips64r6; }

  bool hasCnMips() const { return HasCnMips; }

  bool isLittle() const { return IsLittle; }
  bool isFP64bit() const { return IsFP64bit; }
  bool isNaN2008() const { return IsNaN2008bit; }
  bool isNotFP64bit() const { return !IsFP64bit; }
  bool isGP64bit() const { return IsGP64bit; }
  bool isGP32bit() const { return !IsGP64bit; }
  bool isSingleFloat() const { return IsSingleFloat; }
  bool isNotSingleFloat() const { return !IsSingleFloat; }
  bool hasVFPU() const { return HasVFPU; }
  bool inMips16Mode() const {
    switch (OverrideMode) {
    case NoOverride:
      return InMips16Mode;
    case Mips16Override:
      return true;
    case NoMips16Override:
      return false;
    }
    llvm_unreachable("Unexpected mode");
  }
  bool inMips16ModeDefault() const {
    return InMips16Mode;
  }
  bool inMips16HardFloat() const {
    return inMips16Mode() && InMips16HardFloat;
  }
  bool inMicroMipsMode() const { return InMicroMipsMode; }
  bool hasDSP() const { return HasDSP; }
  bool hasDSPR2() const { return HasDSPR2; }
  bool hasMSA() const { return HasMSA; }
  bool isLinux() const { return IsLinux; }
  bool useSmallSection() const { return UseSmallSection; }

  bool hasStandardEncoding() const { return !inMips16Mode(); }

  bool mipsSEUsesSoftFloat() const;

  bool enableLongBranchPass() const {
    return hasStandardEncoding() || allowMixed16_32();
  }

  /// Features related to the presence of specific instructions.
  bool hasExtractInsert() const { return !inMips16Mode() && hasMips32r2(); }

  const InstrItineraryData &getInstrItineraryData() const { return InstrItins; }
  bool allowMixed16_32() const { return inMips16ModeDefault() |
                                        AllowMixed16_32;}

  bool os16() const { return Os16;};

  bool isTargetNaCl() const { return TargetTriple.isOSNaCl(); }
  bool isNotTargetNaCl() const { return !TargetTriple.isOSNaCl(); }

  // for now constant islands are on for the whole compilation unit but we only
  // really use them if in addition we are in mips16 mode
  static bool useConstantIslands();

  unsigned stackAlignment() const { return hasMips64() ? 16 : 8; }

  // Grab relocation model
  Reloc::Model getRelocationModel() const {return RM;}

  /// \brief Reset the subtarget for the Mips target.
  void resetSubtarget(MachineFunction *MF);

  MipsSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS,
                                                 const TargetMachine *TM);

  /// Does the system support unaligned memory access.
  ///
  /// MIPS32r6/MIPS64r6 require full unaligned access support but does not
  /// specify which component of the system provides it. Hardware, software, and
  /// hybrid implementations are all valid.
  bool systemSupportsUnalignedAccess() const { return hasMips32r6(); }

  // Set helper classes
  void setHelperClassesMips16();
  void setHelperClassesMipsSE();

  MipsJITInfo *getJITInfo() { return &JITInfo; }
  const MipsSelectionDAGInfo *getSelectionDAGInfo() const { return &TSInfo; }
  const DataLayout *getDataLayout() const { return &DL; }
  const MipsInstrInfo *getInstrInfo() const { return InstrInfo.get(); }
  const TargetFrameLowering *getFrameLowering() const {
    return FrameLowering.get();
  }
  const MipsRegisterInfo *getRegisterInfo() const {
    return &InstrInfo->getRegisterInfo();
  }
  const MipsTargetLowering *getTargetLowering() const { return TLInfo.get(); }
};
} // End llvm namespace

#endif
