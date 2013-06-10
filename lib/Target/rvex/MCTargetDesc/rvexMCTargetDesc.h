//===-- rvexMCTargetDesc.h - rvex Target Descriptions -------*- C++ -*-===//
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
// This file provides rvex specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef rvexMCTARGETDESC_H
#define rvexMCTARGETDESC_H

namespace llvm {
class MCSubtargetInfo;
class Target;
class StringRef;

extern Target ThervexTarget;

} // End llvm namespace

// Defines symbolic names for rvex registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "rvexGenRegisterInfo.inc"

// Defines symbolic names for the rvex instructions.
//
#define GET_INSTRINFO_ENUM
#include "rvexGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "rvexGenSubtargetInfo.inc"

#endif

