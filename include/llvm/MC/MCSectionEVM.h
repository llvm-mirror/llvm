//===- MCSectionEVM.h - EVM Machine Code Sections ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionEVM class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONEVM_H
#define LLVM_MC_MCSECTIONEVM_H

#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbolEVM.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MCSymbol;

/// This represents a section on wasm.
class MCSectionEVM final : public MCSection {

  friend class MCContext;
  MCSectionEVM(StringRef Section, SectionKind K, const MCSymbolEVM *group,
                unsigned UniqueID, MCSymbol *Begin)
      : MCSection(SV_EVM, K, Begin) {}

public:

  void PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            const MCExpr *Subsection) const override;
  bool UseCodeAlign() const override;
  bool isVirtualSection() const override;
};

} // end namespace llvm

#endif
