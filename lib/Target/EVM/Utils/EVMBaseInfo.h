//===-- EVMBaseInfo.h - Top level definitions for EVM MC ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone enum definitions for the EVM target
// useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMBASEINFO_H
#define LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMBASEINFO_H

#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/SubtargetFeature.h"

namespace llvm {

// EVMII - This namespace holds all of the target specific flags that
// instruction info tracks. All definitions must match EVMInstrFormats.td.
namespace EVMII {
enum {
  InstFormatPseudo = 0,
  InstFormatR = 1,
  InstFormatR4 = 2,
  InstFormatI = 3,
  InstFormatS = 4,
  InstFormatB = 5,
  InstFormatU = 6,
  InstFormatJ = 7,
  InstFormatCR = 8,
  InstFormatCI = 9,
  InstFormatCSS = 10,
  InstFormatCIW = 11,
  InstFormatCL = 12,
  InstFormatCS = 13,
  InstFormatCA = 14,
  InstFormatCB = 15,
  InstFormatCJ = 16,
  InstFormatOther = 17,

  InstFormatMask = 31
};

enum {
  MO_None,
  MO_LO,
  MO_HI,
  MO_PCREL_HI,
};
} // namespace EVMII

// Describes the predecessor/successor bits used in the FENCE instruction.
namespace EVMFenceField {
enum FenceField {
  I = 8,
  O = 4,
  R = 2,
  W = 1
};
}

// Describes the supported floating point rounding mode encodings.
namespace EVMFPRndMode {
enum RoundingMode {
  RNE = 0,
  RTZ = 1,
  RDN = 2,
  RUP = 3,
  RMM = 4,
  DYN = 7,
  Invalid
};

inline static StringRef roundingModeToString(RoundingMode RndMode) {
  switch (RndMode) {
  default:
    llvm_unreachable("Unknown floating point rounding mode");
  case EVMFPRndMode::RNE:
    return "rne";
  case EVMFPRndMode::RTZ:
    return "rtz";
  case EVMFPRndMode::RDN:
    return "rdn";
  case EVMFPRndMode::RUP:
    return "rup";
  case EVMFPRndMode::RMM:
    return "rmm";
  case EVMFPRndMode::DYN:
    return "dyn";
  }
}

inline static RoundingMode stringToRoundingMode(StringRef Str) {
  return StringSwitch<RoundingMode>(Str)
      .Case("rne", EVMFPRndMode::RNE)
      .Case("rtz", EVMFPRndMode::RTZ)
      .Case("rdn", EVMFPRndMode::RDN)
      .Case("rup", EVMFPRndMode::RUP)
      .Case("rmm", EVMFPRndMode::RMM)
      .Case("dyn", EVMFPRndMode::DYN)
      .Default(EVMFPRndMode::Invalid);
}

inline static bool isValidRoundingMode(unsigned Mode) {
  switch (Mode) {
  default:
    return false;
  case EVMFPRndMode::RNE:
  case EVMFPRndMode::RTZ:
  case EVMFPRndMode::RDN:
  case EVMFPRndMode::RUP:
  case EVMFPRndMode::RMM:
  case EVMFPRndMode::DYN:
    return true;
  }
}
} // namespace EVMFPRndMode

namespace EVMSysReg {
struct SysReg {
  const char *Name;
  unsigned Encoding;
  // FIXME: add these additional fields when needed.
  // Privilege Access: Read, Write, Read-Only.
  // unsigned ReadWrite;
  // Privilege Mode: User, System or Machine.
  // unsigned Mode;
  // Check field name.
  // unsigned Extra;
  // Register number without the privilege bits.
  // unsigned Number;
  FeatureBitset FeaturesRequired;
  bool isRV32Only;

  bool haveRequiredFeatures(FeatureBitset ActiveFeatures) const {
    // Not in 32-bit mode.
    if (isRV32Only && ActiveFeatures[EVM::Feature64Bit])
      return false;
    // No required feature associated with the system register.
    if (FeaturesRequired.none())
      return true;
    return (FeaturesRequired & ActiveFeatures) == FeaturesRequired;
  }
};

#define GET_SysRegsList_DECL
#include "EVMGenSystemOperands.inc"
} // end namespace EVMSysReg

} // namespace llvm

#endif
