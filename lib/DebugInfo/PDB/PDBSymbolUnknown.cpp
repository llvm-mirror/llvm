//===- PDBSymbolUnknown.cpp - -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <utility>

#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolUnknown.h"

using namespace llvm;

PDBSymbolUnknown::PDBSymbolUnknown(const IPDBSession &PDBSession,
                                   std::unique_ptr<IPDBRawSymbol> Symbol)
    : PDBSymbol(PDBSession, std::move(Symbol)) {}

void PDBSymbolUnknown::dump(llvm::raw_ostream &OS) const {}
