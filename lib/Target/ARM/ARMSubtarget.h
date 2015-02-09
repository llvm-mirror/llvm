//===-- ARMSubtarget.h - Define Subtarget for the ARM ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARM specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMSUBTARGET_H
#define LLVM_LIB_TARGET_ARM_ARMSUBTARGET_H


#include "ARMFrameLowering.h"
#include "ARMISelLowering.h"
#include "ARMInstrInfo.h"
#include "ARMSelectionDAGInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "Thumb1FrameLowering.h"
#include "Thumb1InstrInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "ARMGenSubtargetInfo.inc"

namespace llvm {
class GlobalValue;
class StringRef;
class TargetOptions;
class ARMBaseTargetMachine;

class ARMSubtarget : public ARMGenSubtargetInfo {
protected:
  enum ARMProcFamilyEnum {
    Others, CortexA5, CortexA7, CortexA8, CortexA9, CortexA12, CortexA15,
    CortexA17, CortexR5, Swift, CortexA53, CortexA57, Krait, 
  };
  enum ARMProcClassEnum {
    None, AClass, RClass, MClass
  };

  /// ARMProcFamily - ARM processor family: Cortex-A8, Cortex-A9, and others.
  ARMProcFamilyEnum ARMProcFamily;

  /// ARMProcClass - ARM processor class: None, AClass, RClass or MClass.
  ARMProcClassEnum ARMProcClass;

  /// HasV4TOps, HasV5TOps, HasV5TEOps,
  /// HasV6Ops, HasV6MOps, HasV6T2Ops, HasV7Ops, HasV8Ops -
  /// Specify whether target support specific ARM ISA variants.
  bool HasV4TOps;
  bool HasV5TOps;
  bool HasV5TEOps;
  bool HasV6Ops;
  bool HasV6MOps;
  bool HasV6T2Ops;
  bool HasV7Ops;
  bool HasV8Ops;

  /// HasVFPv2, HasVFPv3, HasVFPv4, HasFPARMv8, HasNEON - Specify what
  /// floating point ISAs are supported.
  bool HasVFPv2;
  bool HasVFPv3;
  bool HasVFPv4;
  bool HasFPARMv8;
  bool HasNEON;

  /// UseNEONForSinglePrecisionFP - if the NEONFP attribute has been
  /// specified. Use the method useNEONForSinglePrecisionFP() to
  /// determine if NEON should actually be used.
  bool UseNEONForSinglePrecisionFP;

  /// UseMulOps - True if non-microcoded fused integer multiply-add and
  /// multiply-subtract instructions should be used.
  bool UseMulOps;

  /// SlowFPVMLx - If the VFP2 / NEON instructions are available, indicates
  /// whether the FP VML[AS] instructions are slow (if so, don't use them).
  bool SlowFPVMLx;

  /// HasVMLxForwarding - If true, NEON has special multiplier accumulator
  /// forwarding to allow mul + mla being issued back to back.
  bool HasVMLxForwarding;

  /// SlowFPBrcc - True if floating point compare + branch is slow.
  bool SlowFPBrcc;

  /// InThumbMode - True if compiling for Thumb, false for ARM.
  bool InThumbMode;

  /// HasThumb2 - True if Thumb2 instructions are supported.
  bool HasThumb2;

  /// NoARM - True if subtarget does not support ARM mode execution.
  bool NoARM;

  /// IsR9Reserved - True if R9 is a not available as general purpose register.
  bool IsR9Reserved;

  /// UseMovt - True if MOVT / MOVW pairs are used for materialization of 32-bit
  /// imms (including global addresses).
  bool UseMovt;

  /// SupportsTailCall - True if the OS supports tail call. The dynamic linker
  /// must be able to synthesize call stubs for interworking between ARM and
  /// Thumb.
  bool SupportsTailCall;

  /// HasFP16 - True if subtarget supports half-precision FP (We support VFP+HF
  /// only so far)
  bool HasFP16;

  /// HasD16 - True if subtarget is limited to 16 double precision
  /// FP registers for VFPv3.
  bool HasD16;

  /// HasHardwareDivide - True if subtarget supports [su]div
  bool HasHardwareDivide;

  /// HasHardwareDivideInARM - True if subtarget supports [su]div in ARM mode
  bool HasHardwareDivideInARM;

  /// HasT2ExtractPack - True if subtarget supports thumb2 extract/pack
  /// instructions.
  bool HasT2ExtractPack;

  /// HasDataBarrier - True if the subtarget supports DMB / DSB data barrier
  /// instructions.
  bool HasDataBarrier;

  /// Pref32BitThumb - If true, codegen would prefer 32-bit Thumb instructions
  /// over 16-bit ones.
  bool Pref32BitThumb;

  /// AvoidCPSRPartialUpdate - If true, codegen would avoid using instructions
  /// that partially update CPSR and add false dependency on the previous
  /// CPSR setting instruction.
  bool AvoidCPSRPartialUpdate;

  /// AvoidMOVsShifterOperand - If true, codegen should avoid using flag setting
  /// movs with shifter operand (i.e. asr, lsl, lsr).
  bool AvoidMOVsShifterOperand;

  /// HasRAS - Some processors perform return stack prediction. CodeGen should
  /// avoid issue "normal" call instructions to callees which do not return.
  bool HasRAS;

  /// HasMPExtension - True if the subtarget supports Multiprocessing
  /// extension (ARMv7 only).
  bool HasMPExtension;

  /// HasVirtualization - True if the subtarget supports the Virtualization
  /// extension.
  bool HasVirtualization;

  /// FPOnlySP - If true, the floating point unit only supports single
  /// precision.
  bool FPOnlySP;

  /// If true, the processor supports the Performance Monitor Extensions. These
  /// include a generic cycle-counter as well as more fine-grained (often
  /// implementation-specific) events.
  bool HasPerfMon;

  /// HasTrustZone - if true, processor supports TrustZone security extensions
  bool HasTrustZone;

  /// HasCrypto - if true, processor supports Cryptography extensions
  bool HasCrypto;

  /// HasCRC - if true, processor supports CRC instructions
  bool HasCRC;

  /// If true, the instructions "vmov.i32 d0, #0" and "vmov.i32 q0, #0" are
  /// particularly effective at zeroing a VFP register.
  bool HasZeroCycleZeroing;

  /// AllowsUnalignedMem - If true, the subtarget allows unaligned memory
  /// accesses for some types.  For details, see
  /// ARMTargetLowering::allowsMisalignedMemoryAccesses().
  bool AllowsUnalignedMem;

  /// RestrictIT - If true, the subtarget disallows generation of deprecated IT
  ///  blocks to conform to ARMv8 rule.
  bool RestrictIT;

  /// Thumb2DSP - If true, the subtarget supports the v7 DSP (saturating arith
  /// and such) instructions in Thumb2 code.
  bool Thumb2DSP;

  /// NaCl TRAP instruction is generated instead of the regular TRAP.
  bool UseNaClTrap;

  /// Target machine allowed unsafe FP math (such as use of NEON fp)
  bool UnsafeFPMath;

  /// stackAlignment - The minimum alignment known to hold of the stack frame on
  /// entry to the function and which must be maintained by every function.
  unsigned stackAlignment;

  /// CPUString - String name of used CPU.
  std::string CPUString;

  /// IsLittle - The target is Little Endian
  bool IsLittle;

  /// TargetTriple - What processor and OS we're targeting.
  Triple TargetTriple;

  /// SchedModel - Processor specific instruction costs.
  MCSchedModel SchedModel;

  /// Selected instruction itineraries (one entry per itinerary class.)
  InstrItineraryData InstrItins;

  /// Options passed via command line that could influence the target
  const TargetOptions &Options;

  const ARMBaseTargetMachine &TM;

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  ///
  ARMSubtarget(const std::string &TT, const std::string &CPU,
               const std::string &FS, const ARMBaseTargetMachine &TM, bool IsLittle);

  /// getMaxInlineSizeThreshold - Returns the maximum memset / memcpy size
  /// that still makes it profitable to inline the call.
  unsigned getMaxInlineSizeThreshold() const {
    return 64;
  }
  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  /// initializeSubtargetDependencies - Initializes using a CPU and feature string
  /// so that we can use initializer lists for subtarget initialization.
  ARMSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);

  const ARMSelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  const ARMBaseInstrInfo *getInstrInfo() const override {
    return InstrInfo.get();
  }
  const ARMTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const ARMFrameLowering *getFrameLowering() const override {
    return FrameLowering.get();
  }
  const ARMBaseRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo->getRegisterInfo();
  }

private:
  ARMSelectionDAGInfo TSInfo;
  // Either Thumb1FrameLowering or ARMFrameLowering.
  std::unique_ptr<ARMFrameLowering> FrameLowering;
  // Either Thumb1InstrInfo or Thumb2InstrInfo.
  std::unique_ptr<ARMBaseInstrInfo> InstrInfo;
  ARMTargetLowering   TLInfo;

  void initializeEnvironment();
  void initSubtargetFeatures(StringRef CPU, StringRef FS);
  ARMFrameLowering *initializeFrameLowering(StringRef CPU, StringRef FS);

public:
  void computeIssueWidth();

  bool hasV4TOps()  const { return HasV4TOps;  }
  bool hasV5TOps()  const { return HasV5TOps;  }
  bool hasV5TEOps() const { return HasV5TEOps; }
  bool hasV6Ops()   const { return HasV6Ops;   }
  bool hasV6MOps()  const { return HasV6MOps;  }
  bool hasV6T2Ops() const { return HasV6T2Ops; }
  bool hasV7Ops()   const { return HasV7Ops;  }
  bool hasV8Ops()   const { return HasV8Ops;  }

  bool isCortexA5() const { return ARMProcFamily == CortexA5; }
  bool isCortexA7() const { return ARMProcFamily == CortexA7; }
  bool isCortexA8() const { return ARMProcFamily == CortexA8; }
  bool isCortexA9() const { return ARMProcFamily == CortexA9; }
  bool isCortexA15() const { return ARMProcFamily == CortexA15; }
  bool isSwift()    const { return ARMProcFamily == Swift; }
  bool isCortexM3() const { return CPUString == "cortex-m3"; }
  bool isLikeA9() const { return isCortexA9() || isCortexA15() || isKrait(); }
  bool isCortexR5() const { return ARMProcFamily == CortexR5; }
  bool isKrait() const { return ARMProcFamily == Krait; }

  bool hasARMOps() const { return !NoARM; }

  bool hasVFP2() const { return HasVFPv2; }
  bool hasVFP3() const { return HasVFPv3; }
  bool hasVFP4() const { return HasVFPv4; }
  bool hasFPARMv8() const { return HasFPARMv8; }
  bool hasNEON() const { return HasNEON;  }
  bool hasCrypto() const { return HasCrypto; }
  bool hasCRC() const { return HasCRC; }
  bool hasVirtualization() const { return HasVirtualization; }
  bool useNEONForSinglePrecisionFP() const {
    return hasNEON() && UseNEONForSinglePrecisionFP;
  }

  bool hasDivide() const { return HasHardwareDivide; }
  bool hasDivideInARMMode() const { return HasHardwareDivideInARM; }
  bool hasT2ExtractPack() const { return HasT2ExtractPack; }
  bool hasDataBarrier() const { return HasDataBarrier; }
  bool hasAnyDataBarrier() const {
    return HasDataBarrier || (hasV6Ops() && !isThumb());
  }
  bool useMulOps() const { return UseMulOps; }
  bool useFPVMLx() const { return !SlowFPVMLx; }
  bool hasVMLxForwarding() const { return HasVMLxForwarding; }
  bool isFPBrccSlow() const { return SlowFPBrcc; }
  bool isFPOnlySP() const { return FPOnlySP; }
  bool hasPerfMon() const { return HasPerfMon; }
  bool hasTrustZone() const { return HasTrustZone; }
  bool hasZeroCycleZeroing() const { return HasZeroCycleZeroing; }
  bool prefers32BitThumb() const { return Pref32BitThumb; }
  bool avoidCPSRPartialUpdate() const { return AvoidCPSRPartialUpdate; }
  bool avoidMOVsShifterOperand() const { return AvoidMOVsShifterOperand; }
  bool hasRAS() const { return HasRAS; }
  bool hasMPExtension() const { return HasMPExtension; }
  bool hasThumb2DSP() const { return Thumb2DSP; }
  bool useNaClTrap() const { return UseNaClTrap; }

  bool hasFP16() const { return HasFP16; }
  bool hasD16() const { return HasD16; }

  const Triple &getTargetTriple() const { return TargetTriple; }

  bool isTargetDarwin() const { return TargetTriple.isOSDarwin(); }
  bool isTargetIOS() const { return TargetTriple.isiOS(); }
  bool isTargetLinux() const { return TargetTriple.isOSLinux(); }
  bool isTargetNaCl() const { return TargetTriple.isOSNaCl(); }
  bool isTargetNetBSD() const { return TargetTriple.isOSNetBSD(); }
  bool isTargetWindows() const { return TargetTriple.isOSWindows(); }

  bool isTargetCOFF() const { return TargetTriple.isOSBinFormatCOFF(); }
  bool isTargetELF() const { return TargetTriple.isOSBinFormatELF(); }
  bool isTargetMachO() const { return TargetTriple.isOSBinFormatMachO(); }

  // ARM EABI is the bare-metal EABI described in ARM ABI documents and
  // can be accessed via -target arm-none-eabi. This is NOT GNUEABI.
  // FIXME: Add a flag for bare-metal for that target and set Triple::EABI
  // even for GNUEABI, so we can make a distinction here and still conform to
  // the EABI on GNU (and Android) mode. This requires change in Clang, too.
  // FIXME: The Darwin exception is temporary, while we move users to
  // "*-*-*-macho" triples as quickly as possible.
  bool isTargetAEABI() const {
    return (TargetTriple.getEnvironment() == Triple::EABI ||
            TargetTriple.getEnvironment() == Triple::EABIHF) &&
           !isTargetDarwin() && !isTargetWindows();
  }

  // ARM Targets that support EHABI exception handling standard
  // Darwin uses SjLj. Other targets might need more checks.
  bool isTargetEHABICompatible() const {
    return (TargetTriple.getEnvironment() == Triple::EABI ||
            TargetTriple.getEnvironment() == Triple::GNUEABI ||
            TargetTriple.getEnvironment() == Triple::EABIHF ||
            TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
            TargetTriple.getEnvironment() == Triple::Android) &&
           !isTargetDarwin() && !isTargetWindows();
  }

  bool isTargetHardFloat() const {
    // FIXME: this is invalid for WindowsCE
    return TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
           TargetTriple.getEnvironment() == Triple::EABIHF ||
           isTargetWindows();
  }
  bool isTargetAndroid() const {
    return TargetTriple.getEnvironment() == Triple::Android;
  }

  bool isAPCS_ABI() const;
  bool isAAPCS_ABI() const;

  bool isThumb() const { return InThumbMode; }
  bool isThumb1Only() const { return InThumbMode && !HasThumb2; }
  bool isThumb2() const { return InThumbMode && HasThumb2; }
  bool hasThumb2() const { return HasThumb2; }
  bool isMClass() const { return ARMProcClass == MClass; }
  bool isRClass() const { return ARMProcClass == RClass; }
  bool isAClass() const { return ARMProcClass == AClass; }

  bool isV6M() const {
    return isThumb1Only() && isMClass();
  }

  bool isR9Reserved() const { return IsR9Reserved; }

  bool useMovt(const MachineFunction &MF) const;

  bool supportsTailCall() const { return SupportsTailCall; }

  bool allowsUnalignedMem() const { return AllowsUnalignedMem; }

  bool restrictIT() const { return RestrictIT; }

  const std::string & getCPUString() const { return CPUString; }

  bool isLittle() const { return IsLittle; }

  unsigned getMispredictionPenalty() const;

  /// This function returns true if the target has sincos() routine in its
  /// compiler runtime or math libraries.
  bool hasSinCos() const;

  /// True for some subtargets at > -O0.
  bool enablePostMachineScheduler() const override;

  // enableAtomicExpand- True if we need to expand our atomics.
  bool enableAtomicExpand() const override;

  /// getInstrItins - Return the instruction itineraries based on subtarget
  /// selection.
  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

  /// getStackAlignment - Returns the minimum alignment known to hold of the
  /// stack frame on entry to the function and which must be maintained by every
  /// function for this subtarget.
  unsigned getStackAlignment() const { return stackAlignment; }

  /// GVIsIndirectSymbol - true if the GV will be accessed via an indirect
  /// symbol.
  bool GVIsIndirectSymbol(const GlobalValue *GV, Reloc::Model RelocM) const;

};
} // End llvm namespace

#endif  // ARMSUBTARGET_H
