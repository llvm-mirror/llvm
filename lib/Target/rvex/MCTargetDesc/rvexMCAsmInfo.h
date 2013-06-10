//=====-- rvexMCAsmInfo.h - rvex asm properties -----------*- C++ -*--====//
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
// This file contains the declaration of the rvexMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef rvexTARGETASMINFO_H
#define rvexTARGETASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
  class Target;

  struct rvexELFMCAsmInfo : public MCAsmInfo {
    explicit rvexELFMCAsmInfo(const Target &T, StringRef TT);
  };

} // namespace llvm

#endif

