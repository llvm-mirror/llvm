//===-- HexagonBaseInfo.h - Top level definitions for Hexagon --*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the Hexagon target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONBASEINFO_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONBASEINFO_H

#include "HexagonMCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include <stdint.h>

namespace llvm {

/// HexagonII - This namespace holds all of the target specific flags that
/// instruction info tracks.
///
namespace HexagonII {
  // *** The code below must match HexagonInstrFormat*.td *** //

  // Insn types.
  // *** Must match HexagonInstrFormat*.td ***
  enum Type {
    TypePSEUDO  = 0,
    TypeALU32   = 1,
    TypeCR      = 2,
    TypeJR      = 3,
    TypeJ       = 4,
    TypeLD      = 5,
    TypeST      = 6,
    TypeSYSTEM  = 7,
    TypeXTYPE   = 8,
    TypeMEMOP   = 9,
    TypeNV      = 10,
    TypePREFIX  = 30, // Such as extenders.
    TypeENDLOOP = 31  // Such as end of a HW loop.
  };

  enum SubTarget {
    HasV2SubT     = 0xf,
    HasV2SubTOnly = 0x1,
    NoV2SubT      = 0x0,
    HasV3SubT     = 0xe,
    HasV3SubTOnly = 0x2,
    NoV3SubT      = 0x1,
    HasV4SubT     = 0xc,
    NoV4SubT      = 0x3,
    HasV5SubT     = 0x8,
    NoV5SubT      = 0x7
  };

  enum AddrMode {
    NoAddrMode     = 0,  // No addressing mode
    Absolute       = 1,  // Absolute addressing mode
    AbsoluteSet    = 2,  // Absolute set addressing mode
    BaseImmOffset  = 3,  // Indirect with offset
    BaseLongOffset = 4,  // Indirect with long offset
    BaseRegOffset  = 5,  // Indirect with register offset
    PostInc        = 6   // Post increment addressing mode
  };

  enum MemAccessSize {
    NoMemAccess = 0,            // Not a memory acces instruction.
    ByteAccess = 1,             // Byte access instruction (memb).
    HalfWordAccess = 2,         // Half word access instruction (memh).
    WordAccess = 3,             // Word access instruction (memw).
    DoubleWordAccess = 4        // Double word access instruction (memd)
  };

  // MCInstrDesc TSFlags
  // *** Must match HexagonInstrFormat*.td ***
  enum {
    // This 5-bit field describes the insn type.
    TypePos  = 0,
    TypeMask = 0x1f,

    // Solo instructions.
    SoloPos  = 5,
    SoloMask = 0x1,
    // Packed only with A or X-type instructions.
    SoloAXPos  = 6,
    SoloAXMask = 0x1,
    // Only A-type instruction in first slot or nothing.
    SoloAin1Pos  = 7,
    SoloAin1Mask = 0x1,

    // Predicated instructions.
    PredicatedPos  = 8,
    PredicatedMask = 0x1,
    PredicatedFalsePos  = 9,
    PredicatedFalseMask = 0x1,
    PredicatedNewPos  = 10,
    PredicatedNewMask = 0x1,
    PredicateLatePos  = 11,
    PredicateLateMask = 0x1,

    // New-Value consumer instructions.
    NewValuePos  = 12,
    NewValueMask = 0x1,
    // New-Value producer instructions.
    hasNewValuePos  = 13,
    hasNewValueMask = 0x1,
    // Which operand consumes or produces a new value.
    NewValueOpPos  = 14,
    NewValueOpMask = 0x7,
    // Stores that can become new-value stores.
    mayNVStorePos  = 17,
    mayNVStoreMask = 0x1,
    // New-value store instructions.
    NVStorePos  = 18,
    NVStoreMask = 0x1,
    // Loads that can become current-value loads.
    mayCVLoadPos  = 19,
    mayCVLoadMask = 0x1,
    // Current-value load instructions.
    CVLoadPos  = 20,
    CVLoadMask = 0x1,

    // Extendable insns.
    ExtendablePos  = 21,
    ExtendableMask = 0x1,
    // Insns must be extended.
    ExtendedPos  = 22,
    ExtendedMask = 0x1,
    // Which operand may be extended.
    ExtendableOpPos  = 23,
    ExtendableOpMask = 0x7,
    // Signed or unsigned range.
    ExtentSignedPos  = 26,
    ExtentSignedMask = 0x1,
    // Number of bits of range before extending operand.
    ExtentBitsPos  = 27,
    ExtentBitsMask = 0x1f,
    // Alignment power-of-two before extending operand.
    ExtentAlignPos  = 32,
    ExtentAlignMask = 0x3,

    // Valid subtargets
    validSubTargetPos  = 34,
    validSubTargetMask = 0xf,

    // Addressing mode for load/store instructions.
    AddrModePos  = 40,
    AddrModeMask = 0x7,
    // Access size for load/store instructions.
    MemAccessSizePos = 43,
    MemAccesSizeMask = 0x7,

    // Branch predicted taken.
    TakenPos = 47,
    TakenMask = 0x1,

    // Floating-point instructions.
    FPPos  = 48,
    FPMask = 0x1
  };

  // *** The code above must match HexagonInstrFormat*.td *** //

  // Hexagon specific MO operand flag mask.
  enum HexagonMOTargetFlagVal {
    //===------------------------------------------------------------------===//
    // Hexagon Specific MachineOperand flags.
    MO_NO_FLAG,

    HMOTF_ConstExtended = 1,

    /// MO_PCREL - On a symbol operand, indicates a PC-relative relocation
    /// Used for computing a global address for PIC compilations
    MO_PCREL,

    /// MO_GOT - Indicates a GOT-relative relocation
    MO_GOT,

    // Low or high part of a symbol.
    MO_LO16, MO_HI16,

    // Offset from the base of the SDA.
    MO_GPREL
  };

  enum class InstParseBits : uint32_t {
    INST_PARSE_MASK       = 0x0000c000,
    INST_PARSE_PACKET_END = 0x0000c000,
    INST_PARSE_LOOP_END   = 0x00008000,
    INST_PARSE_NOT_END    = 0x00004000,
    INST_PARSE_DUPLEX     = 0x00000000,
    INST_PARSE_EXTENDER   = 0x00000000
  };

} // End namespace HexagonII.

} // End namespace llvm.

#endif
