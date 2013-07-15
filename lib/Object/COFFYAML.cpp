//===- COFFYAML.cpp - COFF YAMLIO implementation --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of COFF.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/COFFYAML.h"

#define ECase(X) IO.enumCase(Value, #X, COFF::X);
namespace llvm {

namespace COFFYAML {
Section::Section() { memset(&Header, 0, sizeof(COFF::section)); }
Symbol::Symbol() { memset(&Header, 0, sizeof(COFF::symbol)); }
Object::Object() { memset(&Header, 0, sizeof(COFF::header)); }
}

namespace yaml {
void ScalarEnumerationTraits<COFF::MachineTypes>::enumeration(
    IO &IO, COFF::MachineTypes &Value) {
  ECase(IMAGE_FILE_MACHINE_UNKNOWN);
  ECase(IMAGE_FILE_MACHINE_AM33);
  ECase(IMAGE_FILE_MACHINE_AMD64);
  ECase(IMAGE_FILE_MACHINE_ARM);
  ECase(IMAGE_FILE_MACHINE_ARMV7);
  ECase(IMAGE_FILE_MACHINE_EBC);
  ECase(IMAGE_FILE_MACHINE_I386);
  ECase(IMAGE_FILE_MACHINE_IA64);
  ECase(IMAGE_FILE_MACHINE_M32R);
  ECase(IMAGE_FILE_MACHINE_MIPS16);
  ECase(IMAGE_FILE_MACHINE_MIPSFPU);
  ECase(IMAGE_FILE_MACHINE_MIPSFPU16);
  ECase(IMAGE_FILE_MACHINE_POWERPC);
  ECase(IMAGE_FILE_MACHINE_POWERPCFP);
  ECase(IMAGE_FILE_MACHINE_R4000);
  ECase(IMAGE_FILE_MACHINE_SH3);
  ECase(IMAGE_FILE_MACHINE_SH3DSP);
  ECase(IMAGE_FILE_MACHINE_SH4);
  ECase(IMAGE_FILE_MACHINE_SH5);
  ECase(IMAGE_FILE_MACHINE_THUMB);
  ECase(IMAGE_FILE_MACHINE_WCEMIPSV2);
}

void ScalarEnumerationTraits<COFF::SymbolBaseType>::enumeration(
    IO &IO, COFF::SymbolBaseType &Value) {
  ECase(IMAGE_SYM_TYPE_NULL);
  ECase(IMAGE_SYM_TYPE_VOID);
  ECase(IMAGE_SYM_TYPE_CHAR);
  ECase(IMAGE_SYM_TYPE_SHORT);
  ECase(IMAGE_SYM_TYPE_INT);
  ECase(IMAGE_SYM_TYPE_LONG);
  ECase(IMAGE_SYM_TYPE_FLOAT);
  ECase(IMAGE_SYM_TYPE_DOUBLE);
  ECase(IMAGE_SYM_TYPE_STRUCT);
  ECase(IMAGE_SYM_TYPE_UNION);
  ECase(IMAGE_SYM_TYPE_ENUM);
  ECase(IMAGE_SYM_TYPE_MOE);
  ECase(IMAGE_SYM_TYPE_BYTE);
  ECase(IMAGE_SYM_TYPE_WORD);
  ECase(IMAGE_SYM_TYPE_UINT);
  ECase(IMAGE_SYM_TYPE_DWORD);
}

void ScalarEnumerationTraits<COFF::SymbolStorageClass>::enumeration(
    IO &IO, COFF::SymbolStorageClass &Value) {
  ECase(IMAGE_SYM_CLASS_END_OF_FUNCTION);
  ECase(IMAGE_SYM_CLASS_NULL);
  ECase(IMAGE_SYM_CLASS_AUTOMATIC);
  ECase(IMAGE_SYM_CLASS_EXTERNAL);
  ECase(IMAGE_SYM_CLASS_STATIC);
  ECase(IMAGE_SYM_CLASS_REGISTER);
  ECase(IMAGE_SYM_CLASS_EXTERNAL_DEF);
  ECase(IMAGE_SYM_CLASS_LABEL);
  ECase(IMAGE_SYM_CLASS_UNDEFINED_LABEL);
  ECase(IMAGE_SYM_CLASS_MEMBER_OF_STRUCT);
  ECase(IMAGE_SYM_CLASS_ARGUMENT);
  ECase(IMAGE_SYM_CLASS_STRUCT_TAG);
  ECase(IMAGE_SYM_CLASS_MEMBER_OF_UNION);
  ECase(IMAGE_SYM_CLASS_UNION_TAG);
  ECase(IMAGE_SYM_CLASS_TYPE_DEFINITION);
  ECase(IMAGE_SYM_CLASS_UNDEFINED_STATIC);
  ECase(IMAGE_SYM_CLASS_ENUM_TAG);
  ECase(IMAGE_SYM_CLASS_MEMBER_OF_ENUM);
  ECase(IMAGE_SYM_CLASS_REGISTER_PARAM);
  ECase(IMAGE_SYM_CLASS_BIT_FIELD);
  ECase(IMAGE_SYM_CLASS_BLOCK);
  ECase(IMAGE_SYM_CLASS_FUNCTION);
  ECase(IMAGE_SYM_CLASS_END_OF_STRUCT);
  ECase(IMAGE_SYM_CLASS_FILE);
  ECase(IMAGE_SYM_CLASS_SECTION);
  ECase(IMAGE_SYM_CLASS_WEAK_EXTERNAL);
  ECase(IMAGE_SYM_CLASS_CLR_TOKEN);
}

void ScalarEnumerationTraits<COFF::SymbolComplexType>::enumeration(
    IO &IO, COFF::SymbolComplexType &Value) {
  ECase(IMAGE_SYM_DTYPE_NULL);
  ECase(IMAGE_SYM_DTYPE_POINTER);
  ECase(IMAGE_SYM_DTYPE_FUNCTION);
  ECase(IMAGE_SYM_DTYPE_ARRAY);
}

void ScalarEnumerationTraits<COFF::RelocationTypeX86>::enumeration(
    IO &IO, COFF::RelocationTypeX86 &Value) {
  ECase(IMAGE_REL_I386_ABSOLUTE);
  ECase(IMAGE_REL_I386_DIR16);
  ECase(IMAGE_REL_I386_REL16);
  ECase(IMAGE_REL_I386_DIR32);
  ECase(IMAGE_REL_I386_DIR32NB);
  ECase(IMAGE_REL_I386_SEG12);
  ECase(IMAGE_REL_I386_SECTION);
  ECase(IMAGE_REL_I386_SECREL);
  ECase(IMAGE_REL_I386_TOKEN);
  ECase(IMAGE_REL_I386_SECREL7);
  ECase(IMAGE_REL_I386_REL32);
  ECase(IMAGE_REL_AMD64_ABSOLUTE);
  ECase(IMAGE_REL_AMD64_ADDR64);
  ECase(IMAGE_REL_AMD64_ADDR32);
  ECase(IMAGE_REL_AMD64_ADDR32NB);
  ECase(IMAGE_REL_AMD64_REL32);
  ECase(IMAGE_REL_AMD64_REL32_1);
  ECase(IMAGE_REL_AMD64_REL32_2);
  ECase(IMAGE_REL_AMD64_REL32_3);
  ECase(IMAGE_REL_AMD64_REL32_4);
  ECase(IMAGE_REL_AMD64_REL32_5);
  ECase(IMAGE_REL_AMD64_SECTION);
  ECase(IMAGE_REL_AMD64_SECREL);
  ECase(IMAGE_REL_AMD64_SECREL7);
  ECase(IMAGE_REL_AMD64_TOKEN);
  ECase(IMAGE_REL_AMD64_SREL32);
  ECase(IMAGE_REL_AMD64_PAIR);
  ECase(IMAGE_REL_AMD64_SSPAN32);
}
#undef ECase

#define BCase(X) IO.bitSetCase(Value, #X, COFF::X);
void ScalarBitSetTraits<COFF::Characteristics>::bitset(
    IO &IO, COFF::Characteristics &Value) {
  BCase(IMAGE_FILE_RELOCS_STRIPPED);
  BCase(IMAGE_FILE_EXECUTABLE_IMAGE);
  BCase(IMAGE_FILE_LINE_NUMS_STRIPPED);
  BCase(IMAGE_FILE_LOCAL_SYMS_STRIPPED);
  BCase(IMAGE_FILE_AGGRESSIVE_WS_TRIM);
  BCase(IMAGE_FILE_LARGE_ADDRESS_AWARE);
  BCase(IMAGE_FILE_BYTES_REVERSED_LO);
  BCase(IMAGE_FILE_32BIT_MACHINE);
  BCase(IMAGE_FILE_DEBUG_STRIPPED);
  BCase(IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP);
  BCase(IMAGE_FILE_NET_RUN_FROM_SWAP);
  BCase(IMAGE_FILE_SYSTEM);
  BCase(IMAGE_FILE_DLL);
  BCase(IMAGE_FILE_UP_SYSTEM_ONLY);
  BCase(IMAGE_FILE_BYTES_REVERSED_HI);
}

void ScalarBitSetTraits<COFF::SectionCharacteristics>::bitset(
    IO &IO, COFF::SectionCharacteristics &Value) {
  BCase(IMAGE_SCN_TYPE_NO_PAD);
  BCase(IMAGE_SCN_CNT_CODE);
  BCase(IMAGE_SCN_CNT_INITIALIZED_DATA);
  BCase(IMAGE_SCN_CNT_UNINITIALIZED_DATA);
  BCase(IMAGE_SCN_LNK_OTHER);
  BCase(IMAGE_SCN_LNK_INFO);
  BCase(IMAGE_SCN_LNK_REMOVE);
  BCase(IMAGE_SCN_LNK_COMDAT);
  BCase(IMAGE_SCN_GPREL);
  BCase(IMAGE_SCN_MEM_PURGEABLE);
  BCase(IMAGE_SCN_MEM_16BIT);
  BCase(IMAGE_SCN_MEM_LOCKED);
  BCase(IMAGE_SCN_MEM_PRELOAD);
  BCase(IMAGE_SCN_LNK_NRELOC_OVFL);
  BCase(IMAGE_SCN_MEM_DISCARDABLE);
  BCase(IMAGE_SCN_MEM_NOT_CACHED);
  BCase(IMAGE_SCN_MEM_NOT_PAGED);
  BCase(IMAGE_SCN_MEM_SHARED);
  BCase(IMAGE_SCN_MEM_EXECUTE);
  BCase(IMAGE_SCN_MEM_READ);
  BCase(IMAGE_SCN_MEM_WRITE);
}
#undef BCase

namespace {
struct NSectionCharacteristics {
  NSectionCharacteristics(IO &)
      : Characteristics(COFF::SectionCharacteristics(0)) {}
  NSectionCharacteristics(IO &, uint32_t C)
      : Characteristics(COFF::SectionCharacteristics(C)) {}
  uint32_t denormalize(IO &) { return Characteristics; }
  COFF::SectionCharacteristics Characteristics;
};

struct NStorageClass {
  NStorageClass(IO &) : StorageClass(COFF::SymbolStorageClass(0)) {}
  NStorageClass(IO &, uint8_t S) : StorageClass(COFF::SymbolStorageClass(S)) {}
  uint8_t denormalize(IO &) { return StorageClass; }

  COFF::SymbolStorageClass StorageClass;
};

struct NMachine {
  NMachine(IO &) : Machine(COFF::MachineTypes(0)) {}
  NMachine(IO &, uint16_t M) : Machine(COFF::MachineTypes(M)) {}
  uint16_t denormalize(IO &) { return Machine; }
  COFF::MachineTypes Machine;
};

struct NHeaderCharacteristics {
  NHeaderCharacteristics(IO &) : Characteristics(COFF::Characteristics(0)) {}
  NHeaderCharacteristics(IO &, uint16_t C)
      : Characteristics(COFF::Characteristics(C)) {}
  uint16_t denormalize(IO &) { return Characteristics; }

  COFF::Characteristics Characteristics;
};

struct NType {
  NType(IO &) : Type(COFF::RelocationTypeX86(0)) {}
  NType(IO &, uint16_t T) : Type(COFF::RelocationTypeX86(T)) {}
  uint16_t denormalize(IO &) { return Type; }
  COFF::RelocationTypeX86 Type;
};

}

void MappingTraits<COFFYAML::Relocation>::mapping(IO &IO,
                                                  COFFYAML::Relocation &Rel) {
  MappingNormalization<NType, uint16_t> NT(IO, Rel.Type);

  IO.mapRequired("VirtualAddress", Rel.VirtualAddress);
  IO.mapRequired("SymbolName", Rel.SymbolName);
  IO.mapRequired("Type", NT->Type);
}

void MappingTraits<COFF::header>::mapping(IO &IO, COFF::header &H) {
  MappingNormalization<NMachine, uint16_t> NM(IO, H.Machine);
  MappingNormalization<NHeaderCharacteristics, uint16_t> NC(IO,
                                                            H.Characteristics);

  IO.mapRequired("Machine", NM->Machine);
  IO.mapOptional("Characteristics", NC->Characteristics);
}

void MappingTraits<COFFYAML::Symbol>::mapping(IO &IO, COFFYAML::Symbol &S) {
  MappingNormalization<NStorageClass, uint8_t> NS(IO, S.Header.StorageClass);

  IO.mapRequired("Name", S.Name);
  IO.mapRequired("Value", S.Header.Value);
  IO.mapRequired("SectionNumber", S.Header.SectionNumber);
  IO.mapRequired("SimpleType", S.SimpleType);
  IO.mapRequired("ComplexType", S.ComplexType);
  IO.mapRequired("StorageClass", NS->StorageClass);
  IO.mapOptional("NumberOfAuxSymbols", S.Header.NumberOfAuxSymbols,
                 (uint8_t) 0);
  IO.mapOptional("AuxiliaryData", S.AuxiliaryData, object::yaml::BinaryRef());
}

void MappingTraits<COFFYAML::Section>::mapping(IO &IO, COFFYAML::Section &Sec) {
  MappingNormalization<NSectionCharacteristics, uint32_t> NC(
      IO, Sec.Header.Characteristics);
  IO.mapRequired("Name", Sec.Name);
  IO.mapRequired("Characteristics", NC->Characteristics);
  IO.mapOptional("Alignment", Sec.Alignment);
  IO.mapRequired("SectionData", Sec.SectionData);
  IO.mapOptional("Relocations", Sec.Relocations);
}

void MappingTraits<COFFYAML::Object>::mapping(IO &IO, COFFYAML::Object &Obj) {
  IO.mapRequired("header", Obj.Header);
  IO.mapRequired("sections", Obj.Sections);
  IO.mapRequired("symbols", Obj.Symbols);
}

}
}
