//===- MCGenEVMInfo.h - Machine Code EVM support -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCDwarfFile to support the dwarf
// .file directive and the .loc directive.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCGENEVM_H
#define LLVM_MC_MCGENEVM_H

#include "llvm/MC/MCSection.h"

namespace llvm {

class MCAsmBackend;
class MCAsmLayout;
class MCContext;

class MCGenEVMInfo {
public:
  //
  // When generating EVM Metadata for assembly source files this emits the EVM 
  // sections.
  //
  static void Emit(const MCAssembler &Asm, const MCAsmLayout &Layout);

  MCGenEVMInfo() {};

private:
};

}

#endif