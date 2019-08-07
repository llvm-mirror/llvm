//===- MCSymbolEVM.h --------------------------------------------*- C++ -*-===//
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
  EVM::EVMSymbolType Type = EVM::EVM_SYMBOL_TYPE_DATA;
  bool IsWeak = false;
  bool IsHidden = false;
  bool IsComdat = false;
  mutable bool IsUsedInGOT = false;
  Optional<std::string> ImportModule;
  Optional<std::string> ImportName;
  EVM::EVMSignature *Signature = nullptr;
  Optional<EVM::EVMGlobalType> GlobalType;
  Optional<EVM::EVMEventType> EventType;

  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  // Use a module name of "env" for now, for compatibility with existing tools.
  // This is temporary, and may change, as the ABI is not yet stable.
  MCSymbolEVM(const StringMapEntry<bool> *Name, bool isTemporary)
      : MCSymbol(SymbolKindEVM, Name, isTemporary) {}
  static bool classof(const MCSymbol *S) { return S->isEVM(); }

  const MCExpr *getSize() const { return SymbolSize; }
  void setSize(const MCExpr *SS) { SymbolSize = SS; }

  bool isFunction() const { return Type == EVM::EVM_SYMBOL_TYPE_FUNCTION; }
  bool isData() const { return Type == EVM::EVM_SYMBOL_TYPE_DATA; }
  bool isGlobal() const { return Type == EVM::EVM_SYMBOL_TYPE_GLOBAL; }
  bool isSection() const { return Type == EVM::EVM_SYMBOL_TYPE_SECTION; }
  bool isEvent() const { return Type == EVM::EVM_SYMBOL_TYPE_EVENT; }
  EVM::EVMSymbolType getType() const { return Type; }
  void setType(EVM::EVMSymbolType type) { Type = type; }

  bool isExported() const {
    return getFlags() & EVM::EVM_SYMBOL_EXPORTED;
  }
  void setExported() const {
    modifyFlags(EVM::EVM_SYMBOL_EXPORTED, EVM::EVM_SYMBOL_EXPORTED);
  }

  bool isWeak() const { return IsWeak; }
  void setWeak(bool isWeak) { IsWeak = isWeak; }

  bool isHidden() const { return IsHidden; }
  void setHidden(bool isHidden) { IsHidden = isHidden; }

  bool isComdat() const { return IsComdat; }
  void setComdat(bool isComdat) { IsComdat = isComdat; }

  const StringRef getImportModule() const {
      if (ImportModule.hasValue()) {
          return ImportModule.getValue();
      }
      return "env";
  }
  void setImportModule(StringRef Name) { ImportModule = Name; }

  const StringRef getImportName() const {
      if (ImportName.hasValue()) {
          return ImportName.getValue();
      }
      return getName();
  }
  void setImportName(StringRef Name) { ImportName = Name; }

  void setUsedInGOT() const { IsUsedInGOT = true; }
  bool isUsedInGOT() const { return IsUsedInGOT; }

  const EVM::EVMSignature *getSignature() const { return Signature; }
  void setSignature(EVM::EVMSignature *Sig) { Signature = Sig; }

  const EVM::EVMGlobalType &getGlobalType() const {
    assert(GlobalType.hasValue());
    return GlobalType.getValue();
  }
  void setGlobalType(EVM::EVMGlobalType GT) { GlobalType = GT; }

  const EVM::EVMEventType &getEventType() const {
    assert(EventType.hasValue());
    return EventType.getValue();
  }
  void setEventType(EVM::EVMEventType ET) { EventType = ET; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLEVM_H
