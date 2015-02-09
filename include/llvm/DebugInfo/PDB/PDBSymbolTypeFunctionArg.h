//===- PDBSymbolTypeFunctionArg.h - function arg type info ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;

class PDBSymbolTypeFunctionArg : public PDBSymbol {
public:
  PDBSymbolTypeFunctionArg(const IPDBSession &PDBSession,
                           std::unique_ptr<IPDBRawSymbol> Symbol);

  void dump(llvm::raw_ostream &OS) const override;

  FORWARD_SYMBOL_METHOD(getClassParentId)
  FORWARD_SYMBOL_METHOD(getLexicalParentId)
  FORWARD_SYMBOL_METHOD(getSymIndexId)
  FORWARD_SYMBOL_METHOD(getTypeId)

  static bool classof(const PDBSymbol *S) {
    return S->getSymTag() == PDB_SymType::FunctionArg;
  }
};

} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H
