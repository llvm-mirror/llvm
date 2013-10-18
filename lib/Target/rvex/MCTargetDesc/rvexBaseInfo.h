//===-- rvexBaseInfo.h - Top level definitions for rvex ----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//                               rvex Backend
//
// Author: David Juhasz
// E-mail: juhda@caesar.elte.hu
// Institute: Dept. of Programming Languages and Compilers, ELTE IK, Hungary
//
// The research is supported by the European Union and co-financed by the
// European Social Fund (grant agreement no. TAMOP
// 4.2.1./B-09/1/KMR-2010-0003).
//
// This file contains small standalone helper functions and enum definitions for
// the rvex target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef rvexBASEINFO_H
#define rvexBASEINFO_H

namespace llvm {

// rvexII - This namespace holds all of the target specific flags that /
//instruction info tracks.
//
namespace rvexII {
  // *** The code below must match rvexInstrFormat*.td *** //

    enum rvexType {
      TypeIIAlu    = 0,
      TypeIILoad   = 1,
      TypeIIStore  = 2,
      TypeIIBranch = 3,
      TypeIIHiLo   = 4,
      TypeIIImul   = 5,
      TypeIIAluImm = 6,
      TypeIIPseudo = 7,

    };

    enum {
    //===------------------------------------------------------------------===//
    // Instruction encodings.  These are the standard/most common forms for
    // rvex instructions.
    //

    // Pseudo - This represents an instruction that is a pseudo instruction
    // or one that has not been implemented yet.  It is illegal to code generate
    // it, but tolerated for intermediate implementation stages.
    Pseudo   = 0,

    /// FrmR - This form is for instructions of the format R.
    FrmR  = 1,
    /// FrmI - This form is for instructions of the format I.
    FrmI  = 2,
    /// FrmJ - This form is for instructions of the format J.
    FrmJ  = 3,
    /// FrmOther - This form is for instructions that have no specific format.
    FrmOther = 4,

    FormMask = 15
  };

  // MCInstrDesc TSFlags
  // *** Must match rvexInstrFormat*.td ***
  enum {
    // This 4-bit field describes the insn type.
    TypePos  = 0,
    TypeMask = 0xf,

    // Solo instructions.
    SoloPos  = 4,
    SoloMask = 0x1,

    // Long instructions. -- only for X bundles
    LongPos = 5,
    LongMask = 0x1
  };

  /// getrvexRegisterNumbering - Given the enum value for some register,
/// return the number that it corresponds to.
inline static unsigned getrvexRegisterNumbering(unsigned RegEnum)
{
  switch (RegEnum) {
    case rvex::R0:
      return 0;
    case rvex::R1:
      return 1;
    case rvex::R2:
      return 2;
    case rvex::R3:
      return 3;
    case rvex::R4:
      return 4;
    case rvex::R5:
      return 5;
    case rvex::R6:
      return 6;
    case rvex::R7:
      return 7;
    case rvex::R8:
      return 8;
    case rvex::R9:
      return 9;
    case rvex::R10:
      return 10;
    case rvex::R11:
      return 11;
    case rvex::R12:
      return 12;
    case rvex::R13:
      return 13;
    case rvex::R14:
      return 14;
    case rvex::R15:
      return 15;
    case rvex::R16:
      return 16;
    case rvex::R17:
      return 17;
    case rvex::R18:
      return 18;
    case rvex::R19:
      return 19;
    case rvex::R20:
      return 20;
    case rvex::R21:
      return 21;
    case rvex::R22:
      return 22;
    case rvex::R23:
      return 23;
    case rvex::R24:
      return 24;
    case rvex::R25:
      return 25;
    case rvex::R26:
      return 26;
    case rvex::R27:
      return 27;
    case rvex::R28:
      return 28;
    case rvex::R29:
      return 29;
    case rvex::R30:
      return 30;
    case rvex::R31:
      return 31;
    case rvex::R32:
      return 32;
    case rvex::R33:
      return 33;
    case rvex::R34:
      return 34;
    case rvex::R35:
      return 35;
    case rvex::R36:
      return 36;
    case rvex::R37:
      return 37;
    case rvex::R38:
      return 38;
    case rvex::R39:
      return 39;
    case rvex::R40:
      return 40;
    case rvex::R41:
      return 41;
    case rvex::R42:
      return 42;
    case rvex::R43:
      return 43;
    case rvex::R44:
      return 44;
    case rvex::R45:
      return 45;
    case rvex::R46:
      return 46;
    case rvex::R47:
      return 47;
    case rvex::R48:
      return 48;
    case rvex::R49:
      return 49;
    case rvex::R50:
      return 50;
    case rvex::R51:
      return 51;
    case rvex::R52:
      return 52;
    case rvex::R53:
      return 53;
    case rvex::R54:
      return 54;
    case rvex::R55:
      return 55;
    case rvex::R56:
      return 56;
    case rvex::R57:
      return 57;
    case rvex::R58:
      return 58;
    case rvex::R59:
      return 59;
    case rvex::R60:
      return 60;
    case rvex::R61:
      return 61;
    case rvex::R62:
      return 62;
    case rvex::R63:
      return 63;
    default: llvm_unreachable("Unknown register number!");
  }
}

enum TOF {
  //===------------------------------------------------------------------===//
  // rvex Specific MachineOperand flags.

  MO_NO_FLAG,

  /// MO_GOT16 - Represents the offset into the global offset table at which
  /// the address the relocation entry symbol resides during execution.
  MO_GOT16,
  MO_GOT,

  /// MO_GOT_CALL - Represents the offset into the global offset table at
  /// which the address of a call site relocation entry symbol resides
  /// during execution. This is different from the above since this flag
  /// can only be present in call instructions.
  MO_GOT_CALL,

  /// MO_GPREL - Represents the offset from the current gp value to be used
  /// for the relocatable object file being produced.
  MO_GPREL,

  /// MO_ABS_HI/LO - Represents the hi or low part of an absolute symbol
  /// address.
  MO_ABS_HI,
  MO_ABS_LO,

  /// MO_TLSGD - Represents the offset into the global offset table at which
  // the module ID and TSL block offset reside during execution (General
  // Dynamic TLS).
  MO_TLSGD,

  /// MO_TLSLDM - Represents the offset into the global offset table at which
  // the module ID and TSL block offset reside during execution (Local
  // Dynamic TLS).
  MO_TLSLDM,
  MO_DTPREL_HI,
  MO_DTPREL_LO,

  /// MO_GOTTPREL - Represents the offset from the thread pointer (Initial
  // Exec TLS).
  MO_GOTTPREL,

  /// MO_TPREL_HI/LO - Represents the hi and low part of the offset from
  // the thread pointer (Local Exec TLS).
  MO_TPREL_HI,
  MO_TPREL_LO,

  // N32/64 Flags.
  MO_GPOFF_HI,
  MO_GPOFF_LO,
  MO_GOT_DISP,
  MO_GOT_PAGE,
  MO_GOT_OFST
};

  // *** The code above must match rvexInstrFormat*.td *** //

} // End namespace rvexII.

} // End namespace llvm.

#endif
