//===- BuiltinDumper.cpp ---------------------------------------- *- C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BuiltinDumper.h"
#include "LinePrinter.h"
#include "llvm-pdbdump.h"

#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"

using namespace llvm;

BuiltinDumper::BuiltinDumper(LinePrinter &P)
    : PDBSymDumper(false), Printer(P) {}

void BuiltinDumper::start(const PDBSymbolTypeBuiltin &Symbol) {
  WithColor(Printer, PDB_ColorItem::Type).get() << getTypeName(Symbol);
}

StringRef BuiltinDumper::getTypeName(const PDBSymbolTypeBuiltin &Symbol) {
  PDB_BuiltinType Type = Symbol.getBuiltinType();
  switch (Type) {
  case PDB_BuiltinType::Float:
    if (Symbol.getLength() == 4)
      return "float";
    return "double";
  case PDB_BuiltinType::UInt:
    if (Symbol.getLength() == 8)
      return "unsigned __int64";
    return "unsigned";
  case PDB_BuiltinType::Int:
    if (Symbol.getLength() == 4)
      return "int";
    return "__int64";
  case PDB_BuiltinType::Char:
    return "char";
  case PDB_BuiltinType::WCharT:
    return "wchar_t";
  case PDB_BuiltinType::Void:
    return "void";
  case PDB_BuiltinType::Long:
    return "long";
  case PDB_BuiltinType::ULong:
    return "unsigned long";
  case PDB_BuiltinType::Bool:
    return "bool";
  case PDB_BuiltinType::Currency:
    return "CURRENCY";
  case PDB_BuiltinType::Date:
    return "DATE";
  case PDB_BuiltinType::Variant:
    return "VARIANT";
  case PDB_BuiltinType::Complex:
    return "complex";
  case PDB_BuiltinType::Bitfield:
    return "bitfield";
  case PDB_BuiltinType::BSTR:
    return "BSTR";
  case PDB_BuiltinType::HResult:
    return "HRESULT";
  case PDB_BuiltinType::BCD:
    return "HRESULT";
  default:
    return "void";
  }
}
