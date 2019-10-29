//===- MCSymbolEVM.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLEVM_H
#define LLVM_MC_MCSYMBOLEVM_H

#include "llvm/BinaryFormat/EVM.h"
#include "llvm/MC/MCSymbol.h"

namespace llvm {

class MCSymbolEVM : public MCSymbol {

public:
  MCSymbolEVM(const StringMapEntry<bool> *Name, bool isTemporary)
      : MCSymbol(SymbolKindEVM, Name, isTemporary) {}
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLEVM_H
