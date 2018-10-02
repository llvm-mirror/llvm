//===- Dwarf2BTF.h -------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARF2BTF_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARF2BTF_H

#include "DwarfUnit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/MC/MCBTFContext.h"
#include <map>

namespace llvm {

class Dwarf2BTF;

class Die2BTFEntry {
protected:
  const DIE &Die;
  size_t Id;  /* type index in the BTF list, started from 1 */
  struct btf_type BTFType;

public:
  // return desired BTF_KIND for the Die, return BTF_KIND_UNKN for
  // invalid/unsupported Die
  static unsigned char getDieKind(const DIE &Die);

  // Return proper BTF_INT_ENCODING of a basetype.
  // Return BTF_INVALID_ENCODING for unsupported (float, etc.)
  static unsigned char getBaseTypeEncoding(const DIE &Die);

  // Return whether this Die should be skipped.
  // We current skip:
  //  1. Unsupported data type (float) and references to unsupported types
  //  2. Non-array variable names
  static bool shouldSkipDie(const DIE &Die);

  static std::unique_ptr<Die2BTFEntry> dieToBTFTypeEntry(const DIE &Die);

  Die2BTFEntry(const DIE &Die);
  void setId(size_t Id) { this->Id = Id; }
  size_t getId() { return Id; }
  virtual void completeData(class Dwarf2BTF &Dwarf2BTF);
};

// BTF_KIND_INT
class Die2BTFEntryInt : public Die2BTFEntry {
  __u32 IntVal;  // encoding, offset, bits

public:
  Die2BTFEntryInt(const DIE &Die);
  void completeData(class Dwarf2BTF &Dwarf2BTF);
};

// BTF_KIND_ENUM
class Die2BTFEntryEnum : public Die2BTFEntry {
  std::vector<struct btf_enum> EnumValues;

public:
  Die2BTFEntryEnum(const DIE &Die);
  void completeData(class Dwarf2BTF &Dwarf2BTF);
};

// BTF_KIND_ARRAY
class Die2BTFEntryArray : public Die2BTFEntry {
  DIE &ArrayTypeDie;   // use Die for DW_TAG_variable
                       // use ArrayTypeDie for DW_TAG_array_type
  struct btf_array ArrayInfo;

public:
  Die2BTFEntryArray(const DIE &Die);
  void completeData(class Dwarf2BTF &Dwarf2BTF);
};

// BTF_KIND_STRUCT and BTF_KIND_UNION
class Die2BTFEntryStruct : public Die2BTFEntry {
  std::vector<struct btf_member> Members;

public:
  Die2BTFEntryStruct(const DIE &Die);
  void completeData(class Dwarf2BTF &Dwarf2BTF);
};

class Die2BTFEntryFunc : public Die2BTFEntry {
  std::vector<__u32> Parameters;

public:
  Die2BTFEntryFunc(const DIE &Die);
  void completeData(class Dwarf2BTF &Dwarf2BTF);
};

class Dwarf2BTF {
  std::vector<std::unique_ptr<Die2BTFEntry>> TypeEntries;
  std::map<DIE*, size_t> DieToIdMap;

public:
  MCBTFContext *BTFContext;
  Dwarf2BTF() { BTFContext = new MCBTFContext(); }
  void addDwarfCU(DwarfUnit *TheU);
  void finish();
  MCBTFContext *getBTFContext() { return BTFContext; }
  __u32 getTypeIndex(DIE &Die) {
    DIE *DiePtr = const_cast<DIE*>(&Die);
    assert((DieToIdMap.find(DiePtr) != DieToIdMap.end()) &&
           "Die not added to in the BTFContext");
    return DieToIdMap[DiePtr] + 1;
  }

private:
  void addTypeEntry(const DIE &Die);
  bool alreadyAdded(DIE &Die) {
    return DieToIdMap.find(const_cast<DIE*>(&Die)) != DieToIdMap.end();
  }

  void completeData();
};

}
#endif
