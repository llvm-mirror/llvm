//===-- COFFDumper.cpp - COFF-specific dumper -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements the COFF-specific dumper for llvm-readobj.
///
//===----------------------------------------------------------------------===//

#include "llvm-readobj.h"
#include "ObjDumper.h"

#include "Error.h"
#include "StreamWriter.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Win64EH.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"

#include <algorithm>
#include <cstring>
#include <time.h>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::Win64EH;

namespace {

class COFFDumper : public ObjDumper {
public:
  COFFDumper(const llvm::object::COFFObjectFile *Obj, StreamWriter& Writer)
    : ObjDumper(Writer)
    , Obj(Obj) {
    cacheRelocations();
  }

  virtual void printFileHeaders() LLVM_OVERRIDE;
  virtual void printSections() LLVM_OVERRIDE;
  virtual void printRelocations() LLVM_OVERRIDE;
  virtual void printSymbols() LLVM_OVERRIDE;
  virtual void printDynamicSymbols() LLVM_OVERRIDE;
  virtual void printUnwindInfo() LLVM_OVERRIDE;

private:
  void printSymbol(symbol_iterator SymI);

  void printRelocation(section_iterator SecI, relocation_iterator RelI);

  void printX64UnwindInfo();

  void printRuntimeFunction(
    const RuntimeFunction& RTF,
    uint64_t OffsetInSection,
    const std::vector<RelocationRef> &Rels);

  void printUnwindInfo(
    const Win64EH::UnwindInfo& UI,
    uint64_t OffsetInSection,
    const std::vector<RelocationRef> &Rels);

  void printUnwindCode(const Win64EH::UnwindInfo& UI, ArrayRef<UnwindCode> UCs);

  void cacheRelocations();

  error_code getSectionContents(
    const std::vector<RelocationRef> &Rels,
    uint64_t Offset,
    ArrayRef<uint8_t> &Contents,
    uint64_t &Addr);

  error_code getSection(
    const std::vector<RelocationRef> &Rels,
    uint64_t Offset,
    const coff_section **Section,
    uint64_t *AddrPtr);

  typedef DenseMap<const coff_section*, std::vector<RelocationRef> > RelocMapTy;

  const llvm::object::COFFObjectFile *Obj;
  RelocMapTy RelocMap;
  std::vector<RelocationRef> EmptyRelocs;
};

} // namespace


namespace llvm {

error_code createCOFFDumper(const object::ObjectFile *Obj,
                            StreamWriter& Writer,
                            OwningPtr<ObjDumper> &Result) {
  const COFFObjectFile *COFFObj = dyn_cast<COFFObjectFile>(Obj);
  if (!COFFObj)
    return readobj_error::unsupported_obj_file_format;

  Result.reset(new COFFDumper(COFFObj, Writer));
  return readobj_error::success;
}

} // namespace llvm


// Returns the name of the unwind code.
static StringRef getUnwindCodeTypeName(uint8_t Code) {
  switch(Code) {
  default: llvm_unreachable("Invalid unwind code");
  case UOP_PushNonVol: return "PUSH_NONVOL";
  case UOP_AllocLarge: return "ALLOC_LARGE";
  case UOP_AllocSmall: return "ALLOC_SMALL";
  case UOP_SetFPReg: return "SET_FPREG";
  case UOP_SaveNonVol: return "SAVE_NONVOL";
  case UOP_SaveNonVolBig: return "SAVE_NONVOL_FAR";
  case UOP_SaveXMM128: return "SAVE_XMM128";
  case UOP_SaveXMM128Big: return "SAVE_XMM128_FAR";
  case UOP_PushMachFrame: return "PUSH_MACHFRAME";
  }
}

// Returns the name of a referenced register.
static StringRef getUnwindRegisterName(uint8_t Reg) {
  switch(Reg) {
  default: llvm_unreachable("Invalid register");
  case 0: return "RAX";
  case 1: return "RCX";
  case 2: return "RDX";
  case 3: return "RBX";
  case 4: return "RSP";
  case 5: return "RBP";
  case 6: return "RSI";
  case 7: return "RDI";
  case 8: return "R8";
  case 9: return "R9";
  case 10: return "R10";
  case 11: return "R11";
  case 12: return "R12";
  case 13: return "R13";
  case 14: return "R14";
  case 15: return "R15";
  }
}

// Calculates the number of array slots required for the unwind code.
static unsigned getNumUsedSlots(const UnwindCode &UnwindCode) {
  switch (UnwindCode.getUnwindOp()) {
  default: llvm_unreachable("Invalid unwind code");
  case UOP_PushNonVol:
  case UOP_AllocSmall:
  case UOP_SetFPReg:
  case UOP_PushMachFrame:
    return 1;
  case UOP_SaveNonVol:
  case UOP_SaveXMM128:
    return 2;
  case UOP_SaveNonVolBig:
  case UOP_SaveXMM128Big:
    return 3;
  case UOP_AllocLarge:
    return (UnwindCode.getOpInfo() == 0) ? 2 : 3;
  }
}

// Given a symbol sym this functions returns the address and section of it.
static error_code resolveSectionAndAddress(const COFFObjectFile *Obj,
                                           const SymbolRef &Sym,
                                           const coff_section *&ResolvedSection,
                                           uint64_t &ResolvedAddr) {
  if (error_code EC = Sym.getAddress(ResolvedAddr))
    return EC;

  section_iterator iter(Obj->begin_sections());
  if (error_code EC = Sym.getSection(iter))
    return EC;

  ResolvedSection = Obj->getCOFFSection(iter);
  return object_error::success;
}

// Given a vector of relocations for a section and an offset into this section
// the function returns the symbol used for the relocation at the offset.
static error_code resolveSymbol(const std::vector<RelocationRef> &Rels,
                                uint64_t Offset, SymbolRef &Sym) {
  for (std::vector<RelocationRef>::const_iterator RelI = Rels.begin(),
                                                  RelE = Rels.end();
                                                  RelI != RelE; ++RelI) {
    uint64_t Ofs;
    if (error_code EC = RelI->getOffset(Ofs))
      return EC;

    if (Ofs == Offset) {
      Sym = *RelI->getSymbol();
      return readobj_error::success;
    }
  }

  return readobj_error::unknown_symbol;
}

// Given a vector of relocations for a section and an offset into this section
// the function returns the name of the symbol used for the relocation at the
// offset.
static error_code resolveSymbolName(const std::vector<RelocationRef> &Rels,
                                    uint64_t Offset, StringRef &Name) {
  SymbolRef Sym;
  if (error_code EC = resolveSymbol(Rels, Offset, Sym)) return EC;
  if (error_code EC = Sym.getName(Name)) return EC;
  return object_error::success;
}

static const EnumEntry<COFF::MachineTypes> ImageFileMachineType[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_UNKNOWN  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AM33     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AMD64    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARMV7    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_EBC      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_I386     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_IA64     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_M32R     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPS16   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU16),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPC  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPCFP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_R4000    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3DSP   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH4      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH5      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_THUMB    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_WCEMIPSV2)
};

static const EnumEntry<COFF::Characteristics> ImageFileCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_RELOCS_STRIPPED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_EXECUTABLE_IMAGE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LINE_NUMS_STRIPPED     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LOCAL_SYMS_STRIPPED    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_AGGRESSIVE_WS_TRIM     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LARGE_ADDRESS_AWARE    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_LO      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_32BIT_MACHINE          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DEBUG_STRIPPED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_NET_RUN_FROM_SWAP      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_SYSTEM                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DLL                    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_UP_SYSTEM_ONLY         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_HI      )
};

static const EnumEntry<COFF::WindowsSubsystem> PEWindowsSubsystem[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_UNKNOWN                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_NATIVE                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_GUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_POSIX_CUI              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CE_GUI         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_APPLICATION        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_ROM                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_XBOX                   ),
};

static const EnumEntry<COFF::DLLCharacteristics> PEDLLCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_SEH               ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_BIND              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE),
};

static const EnumEntry<COFF::SectionCharacteristics>
ImageSectionCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NO_PAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_CODE              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_INITIALIZED_DATA  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_UNINITIALIZED_DATA),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_OTHER             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_INFO              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_REMOVE            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_COMDAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_GPREL                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PURGEABLE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_16BIT             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_LOCKED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PRELOAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_16BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_32BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_64BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_128BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_256BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_512BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1024BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2048BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4096BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8192BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_NRELOC_OVFL       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_DISCARDABLE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_CACHED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_PAGED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_SHARED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_EXECUTE           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_READ              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_WRITE             )
};

static const EnumEntry<COFF::SymbolBaseType> ImageSymType[] = {
  { "Null"  , COFF::IMAGE_SYM_TYPE_NULL   },
  { "Void"  , COFF::IMAGE_SYM_TYPE_VOID   },
  { "Char"  , COFF::IMAGE_SYM_TYPE_CHAR   },
  { "Short" , COFF::IMAGE_SYM_TYPE_SHORT  },
  { "Int"   , COFF::IMAGE_SYM_TYPE_INT    },
  { "Long"  , COFF::IMAGE_SYM_TYPE_LONG   },
  { "Float" , COFF::IMAGE_SYM_TYPE_FLOAT  },
  { "Double", COFF::IMAGE_SYM_TYPE_DOUBLE },
  { "Struct", COFF::IMAGE_SYM_TYPE_STRUCT },
  { "Union" , COFF::IMAGE_SYM_TYPE_UNION  },
  { "Enum"  , COFF::IMAGE_SYM_TYPE_ENUM   },
  { "MOE"   , COFF::IMAGE_SYM_TYPE_MOE    },
  { "Byte"  , COFF::IMAGE_SYM_TYPE_BYTE   },
  { "Word"  , COFF::IMAGE_SYM_TYPE_WORD   },
  { "UInt"  , COFF::IMAGE_SYM_TYPE_UINT   },
  { "DWord" , COFF::IMAGE_SYM_TYPE_DWORD  }
};

static const EnumEntry<COFF::SymbolComplexType> ImageSymDType[] = {
  { "Null"    , COFF::IMAGE_SYM_DTYPE_NULL     },
  { "Pointer" , COFF::IMAGE_SYM_DTYPE_POINTER  },
  { "Function", COFF::IMAGE_SYM_DTYPE_FUNCTION },
  { "Array"   , COFF::IMAGE_SYM_DTYPE_ARRAY    }
};

static const EnumEntry<COFF::SymbolStorageClass> ImageSymClass[] = {
  { "EndOfFunction"  , COFF::IMAGE_SYM_CLASS_END_OF_FUNCTION  },
  { "Null"           , COFF::IMAGE_SYM_CLASS_NULL             },
  { "Automatic"      , COFF::IMAGE_SYM_CLASS_AUTOMATIC        },
  { "External"       , COFF::IMAGE_SYM_CLASS_EXTERNAL         },
  { "Static"         , COFF::IMAGE_SYM_CLASS_STATIC           },
  { "Register"       , COFF::IMAGE_SYM_CLASS_REGISTER         },
  { "ExternalDef"    , COFF::IMAGE_SYM_CLASS_EXTERNAL_DEF     },
  { "Label"          , COFF::IMAGE_SYM_CLASS_LABEL            },
  { "UndefinedLabel" , COFF::IMAGE_SYM_CLASS_UNDEFINED_LABEL  },
  { "MemberOfStruct" , COFF::IMAGE_SYM_CLASS_MEMBER_OF_STRUCT },
  { "Argument"       , COFF::IMAGE_SYM_CLASS_ARGUMENT         },
  { "StructTag"      , COFF::IMAGE_SYM_CLASS_STRUCT_TAG       },
  { "MemberOfUnion"  , COFF::IMAGE_SYM_CLASS_MEMBER_OF_UNION  },
  { "UnionTag"       , COFF::IMAGE_SYM_CLASS_UNION_TAG        },
  { "TypeDefinition" , COFF::IMAGE_SYM_CLASS_TYPE_DEFINITION  },
  { "UndefinedStatic", COFF::IMAGE_SYM_CLASS_UNDEFINED_STATIC },
  { "EnumTag"        , COFF::IMAGE_SYM_CLASS_ENUM_TAG         },
  { "MemberOfEnum"   , COFF::IMAGE_SYM_CLASS_MEMBER_OF_ENUM   },
  { "RegisterParam"  , COFF::IMAGE_SYM_CLASS_REGISTER_PARAM   },
  { "BitField"       , COFF::IMAGE_SYM_CLASS_BIT_FIELD        },
  { "Block"          , COFF::IMAGE_SYM_CLASS_BLOCK            },
  { "Function"       , COFF::IMAGE_SYM_CLASS_FUNCTION         },
  { "EndOfStruct"    , COFF::IMAGE_SYM_CLASS_END_OF_STRUCT    },
  { "File"           , COFF::IMAGE_SYM_CLASS_FILE             },
  { "Section"        , COFF::IMAGE_SYM_CLASS_SECTION          },
  { "WeakExternal"   , COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL    },
  { "CLRToken"       , COFF::IMAGE_SYM_CLASS_CLR_TOKEN        }
};

static const EnumEntry<COFF::COMDATType> ImageCOMDATSelect[] = {
  { "NoDuplicates", COFF::IMAGE_COMDAT_SELECT_NODUPLICATES },
  { "Any"         , COFF::IMAGE_COMDAT_SELECT_ANY          },
  { "SameSize"    , COFF::IMAGE_COMDAT_SELECT_SAME_SIZE    },
  { "ExactMatch"  , COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH  },
  { "Associative" , COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE  },
  { "Largest"     , COFF::IMAGE_COMDAT_SELECT_LARGEST      },
  { "Newest"      , COFF::IMAGE_COMDAT_SELECT_NEWEST       }
};

static const EnumEntry<COFF::WeakExternalCharacteristics>
WeakExternalCharacteristics[] = {
  { "NoLibrary", COFF::IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY },
  { "Library"  , COFF::IMAGE_WEAK_EXTERN_SEARCH_LIBRARY   },
  { "Alias"    , COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS     }
};

static const EnumEntry<unsigned> UnwindFlags[] = {
  { "ExceptionHandler", Win64EH::UNW_ExceptionHandler },
  { "TerminateHandler", Win64EH::UNW_TerminateHandler },
  { "ChainInfo"       , Win64EH::UNW_ChainInfo        }
};

static const EnumEntry<unsigned> UnwindOpInfo[] = {
  { "RAX",  0 },
  { "RCX",  1 },
  { "RDX",  2 },
  { "RBX",  3 },
  { "RSP",  4 },
  { "RBP",  5 },
  { "RSI",  6 },
  { "RDI",  7 },
  { "R8",   8 },
  { "R9",   9 },
  { "R10", 10 },
  { "R11", 11 },
  { "R12", 12 },
  { "R13", 13 },
  { "R14", 14 },
  { "R15", 15 }
};

// Some additional COFF structures not defined by llvm::object.
namespace {
  struct coff_aux_function_definition {
    support::ulittle32_t TagIndex;
    support::ulittle32_t TotalSize;
    support::ulittle32_t PointerToLineNumber;
    support::ulittle32_t PointerToNextFunction;
    uint8_t Unused[2];
  };

  struct coff_aux_weak_external_definition {
    support::ulittle32_t TagIndex;
    support::ulittle32_t Characteristics;
    uint8_t Unused[10];
  };

  struct coff_aux_file_record {
    char FileName[18];
  };

  struct coff_aux_clr_token {
    support::ulittle8_t AuxType;
    support::ulittle8_t Reserved;
    support::ulittle32_t SymbolTableIndex;
    uint8_t Unused[12];
  };
} // namespace

static uint64_t getOffsetOfLSDA(const Win64EH::UnwindInfo& UI) {
  return static_cast<const char*>(UI.getLanguageSpecificData())
         - reinterpret_cast<const char*>(&UI);
}

static uint32_t getLargeSlotValue(ArrayRef<UnwindCode> UCs) {
  if (UCs.size() < 3)
    return 0;

  return UCs[1].FrameOffset + (static_cast<uint32_t>(UCs[2].FrameOffset) << 16);
}

template<typename T>
static error_code getSymbolAuxData(const COFFObjectFile *Obj,
                                   const coff_symbol *Symbol, const T* &Aux) {
  ArrayRef<uint8_t> AuxData = Obj->getSymbolAuxData(Symbol);
  Aux = reinterpret_cast<const T*>(AuxData.data());
  return readobj_error::success;
}

static std::string formatSymbol(const std::vector<RelocationRef> &Rels,
                                uint64_t Offset, uint32_t Disp) {
  std::string Buffer;
  raw_string_ostream Str(Buffer);

  StringRef Sym;
  if (resolveSymbolName(Rels, Offset, Sym)) {
    Str << format(" (0x%" PRIX64 ")", Offset);
    return Str.str();
  }

  Str << Sym;
  if (Disp > 0) {
    Str << format(" +0x%X (0x%" PRIX64 ")", Disp, Offset);
  } else {
    Str << format(" (0x%" PRIX64 ")", Offset);
  }

  return Str.str();
}

// Given a vector of relocations for a section and an offset into this section
// the function resolves the symbol used for the relocation at the offset and
// returns the section content and the address inside the content pointed to
// by the symbol.
error_code COFFDumper::getSectionContents(
    const std::vector<RelocationRef> &Rels, uint64_t Offset,
    ArrayRef<uint8_t> &Contents, uint64_t &Addr) {

  SymbolRef Sym;
  const coff_section *Section;

  if (error_code EC = resolveSymbol(Rels, Offset, Sym))
    return EC;
  if (error_code EC = resolveSectionAndAddress(Obj, Sym, Section, Addr))
    return EC;
  if (error_code EC = Obj->getSectionContents(Section, Contents))
    return EC;

  return object_error::success;
}

error_code COFFDumper::getSection(
    const std::vector<RelocationRef> &Rels, uint64_t Offset,
    const coff_section **SectionPtr, uint64_t *AddrPtr) {

  SymbolRef Sym;
  if (error_code EC = resolveSymbol(Rels, Offset, Sym))
    return EC;

  const coff_section *Section;
  uint64_t Addr;
  if (error_code EC = resolveSectionAndAddress(Obj, Sym, Section, Addr))
    return EC;

  if (SectionPtr)
    *SectionPtr = Section;
  if (AddrPtr)
    *AddrPtr = Addr;

  return object_error::success;
}

void COFFDumper::cacheRelocations() {
  error_code EC;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    if (error(EC))
      break;

    const coff_section *Section = Obj->getCOFFSection(SecI);

    for (relocation_iterator RelI = SecI->begin_relocations(),
                             RelE = SecI->end_relocations();
                             RelI != RelE; RelI.increment(EC)) {
      if (error(EC))
        break;

      RelocMap[Section].push_back(*RelI);
    }

    // Sort relocations by address.
    std::sort(RelocMap[Section].begin(), RelocMap[Section].end(),
              relocAddressLess);
  }
}

void COFFDumper::printFileHeaders() {
  // Print COFF header
  const coff_file_header *COFFHeader = 0;
  if (error(Obj->getCOFFHeader(COFFHeader)))
    return;

  time_t TDS = COFFHeader->TimeDateStamp;
  char FormattedTime[20] = { };
  strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));

  {
    DictScope D(W, "ImageFileHeader");
    W.printEnum  ("Machine", COFFHeader->Machine,
                    makeArrayRef(ImageFileMachineType));
    W.printNumber("SectionCount", COFFHeader->NumberOfSections);
    W.printHex   ("TimeDateStamp", FormattedTime, COFFHeader->TimeDateStamp);
    W.printHex   ("PointerToSymbolTable", COFFHeader->PointerToSymbolTable);
    W.printNumber("SymbolCount", COFFHeader->NumberOfSymbols);
    W.printNumber("OptionalHeaderSize", COFFHeader->SizeOfOptionalHeader);
    W.printFlags ("Characteristics", COFFHeader->Characteristics,
                    makeArrayRef(ImageFileCharacteristics));
  }

  // Print PE header. This header does not exist if this is an object file and
  // not an executable.
  const pe32_header *PEHeader = 0;
  if (error(Obj->getPE32Header(PEHeader)))
    return;

  if (PEHeader) {
    DictScope D(W, "ImageOptionalHeader");
    W.printNumber("MajorLinkerVersion", PEHeader->MajorLinkerVersion);
    W.printNumber("MinorLinkerVersion", PEHeader->MinorLinkerVersion);
    W.printNumber("SizeOfCode", PEHeader->SizeOfCode);
    W.printNumber("SizeOfInitializedData", PEHeader->SizeOfInitializedData);
    W.printNumber("SizeOfUninitializedData", PEHeader->SizeOfUninitializedData);
    W.printHex   ("AddressOfEntryPoint", PEHeader->AddressOfEntryPoint);
    W.printHex   ("BaseOfCode", PEHeader->BaseOfCode);
    W.printHex   ("BaseOfData", PEHeader->BaseOfData);
    W.printHex   ("ImageBase", PEHeader->ImageBase);
    W.printNumber("SectionAlignment", PEHeader->SectionAlignment);
    W.printNumber("FileAlignment", PEHeader->FileAlignment);
    W.printNumber("MajorOperatingSystemVersion",
                  PEHeader->MajorOperatingSystemVersion);
    W.printNumber("MinorOperatingSystemVersion",
                  PEHeader->MinorOperatingSystemVersion);
    W.printNumber("MajorImageVersion", PEHeader->MajorImageVersion);
    W.printNumber("MinorImageVersion", PEHeader->MinorImageVersion);
    W.printNumber("MajorSubsystemVersion", PEHeader->MajorSubsystemVersion);
    W.printNumber("MinorSubsystemVersion", PEHeader->MinorSubsystemVersion);
    W.printNumber("SizeOfImage", PEHeader->SizeOfImage);
    W.printNumber("SizeOfHeaders", PEHeader->SizeOfHeaders);
    W.printEnum  ("Subsystem", PEHeader->Subsystem,
                    makeArrayRef(PEWindowsSubsystem));
    W.printFlags ("Subsystem", PEHeader->DLLCharacteristics,
                    makeArrayRef(PEDLLCharacteristics));
    W.printNumber("SizeOfStackReserve", PEHeader->SizeOfStackReserve);
    W.printNumber("SizeOfStackCommit", PEHeader->SizeOfStackCommit);
    W.printNumber("SizeOfHeapReserve", PEHeader->SizeOfHeapReserve);
    W.printNumber("SizeOfHeapCommit", PEHeader->SizeOfHeapCommit);
    W.printNumber("NumberOfRvaAndSize", PEHeader->NumberOfRvaAndSize);
  }
}

void COFFDumper::printSections() {
  error_code EC;

  ListScope SectionsD(W, "Sections");
  int SectionNumber = 0;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    if (error(EC))
      break;

    ++SectionNumber;
    const coff_section *Section = Obj->getCOFFSection(SecI);

    StringRef Name;
    if (error(SecI->getName(Name)))
        Name = "";

    DictScope D(W, "Section");
    W.printNumber("Number", SectionNumber);
    W.printBinary("Name", Name, Section->Name);
    W.printHex   ("VirtualSize", Section->VirtualSize);
    W.printHex   ("VirtualAddress", Section->VirtualAddress);
    W.printNumber("RawDataSize", Section->SizeOfRawData);
    W.printHex   ("PointerToRawData", Section->PointerToRawData);
    W.printHex   ("PointerToRelocations", Section->PointerToRelocations);
    W.printHex   ("PointerToLineNumbers", Section->PointerToLinenumbers);
    W.printNumber("RelocationCount", Section->NumberOfRelocations);
    W.printNumber("LineNumberCount", Section->NumberOfLinenumbers);
    W.printFlags ("Characteristics", Section->Characteristics,
                    makeArrayRef(ImageSectionCharacteristics),
                    COFF::SectionCharacteristics(0x00F00000));

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (relocation_iterator RelI = SecI->begin_relocations(),
                               RelE = SecI->end_relocations();
                               RelI != RelE; RelI.increment(EC)) {
        if (error(EC)) break;

        printRelocation(SecI, RelI);
      }
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      for (symbol_iterator SymI = Obj->begin_symbols(),
                           SymE = Obj->end_symbols();
                           SymI != SymE; SymI.increment(EC)) {
        if (error(EC)) break;

        bool Contained = false;
        if (SecI->containsSymbol(*SymI, Contained) || !Contained)
          continue;

        printSymbol(SymI);
      }
    }

    if (opts::SectionData) {
      StringRef Data;
      if (error(SecI->getContents(Data))) break;

      W.printBinaryBlock("SectionData", Data);
    }
  }
}

void COFFDumper::printRelocations() {
  ListScope D(W, "Relocations");

  error_code EC;
  int SectionNumber = 0;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    ++SectionNumber;
    if (error(EC))
      break;

    StringRef Name;
    if (error(SecI->getName(Name)))
      continue;

    bool PrintedGroup = false;
    for (relocation_iterator RelI = SecI->begin_relocations(),
                             RelE = SecI->end_relocations();
                             RelI != RelE; RelI.increment(EC)) {
      if (error(EC)) break;

      if (!PrintedGroup) {
        W.startLine() << "Section (" << SectionNumber << ") " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(SecI, RelI);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void COFFDumper::printRelocation(section_iterator SecI,
                                 relocation_iterator RelI) {
  uint64_t Offset;
  uint64_t RelocType;
  SmallString<32> RelocName;
  StringRef SymbolName;
  StringRef Contents;
  if (error(RelI->getOffset(Offset))) return;
  if (error(RelI->getType(RelocType))) return;
  if (error(RelI->getTypeName(RelocName))) return;
  symbol_iterator Symbol = RelI->getSymbol();
  if (error(Symbol->getName(SymbolName))) return;
  if (error(SecI->getContents(Contents))) return;

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Offset", Offset);
    W.printNumber("Type", RelocName, RelocType);
    W.printString("Symbol", SymbolName.size() > 0 ? SymbolName : "-");
  } else {
    raw_ostream& OS = W.startLine();
    OS << W.hex(Offset)
       << " " << RelocName
       << " " << (SymbolName.size() > 0 ? SymbolName : "-")
       << "\n";
  }
}

void COFFDumper::printSymbols() {
  ListScope Group(W, "Symbols");

  error_code EC;
  for (symbol_iterator SymI = Obj->begin_symbols(),
                       SymE = Obj->end_symbols();
                       SymI != SymE; SymI.increment(EC)) {
    if (error(EC)) break;

    printSymbol(SymI);
  }
}

void COFFDumper::printDynamicSymbols() {
  ListScope Group(W, "DynamicSymbols");
}

void COFFDumper::printSymbol(symbol_iterator SymI) {
  DictScope D(W, "Symbol");

  const coff_symbol *Symbol = Obj->getCOFFSymbol(SymI);
  const coff_section *Section;
  if (error_code EC = Obj->getSection(Symbol->SectionNumber, Section)) {
    W.startLine() << "Invalid section number: " << EC.message() << "\n";
    W.flush();
    return;
  }

  StringRef SymbolName;
  if (Obj->getSymbolName(Symbol, SymbolName))
    SymbolName = "";

  StringRef SectionName = "";
  if (Section)
    Obj->getSectionName(Section, SectionName);

  W.printString("Name", SymbolName);
  W.printNumber("Value", Symbol->Value);
  W.printNumber("Section", SectionName, Symbol->SectionNumber);
  W.printEnum  ("BaseType", Symbol->getBaseType(), makeArrayRef(ImageSymType));
  W.printEnum  ("ComplexType", Symbol->getComplexType(),
                                                   makeArrayRef(ImageSymDType));
  W.printEnum  ("StorageClass", Symbol->StorageClass,
                                                   makeArrayRef(ImageSymClass));
  W.printNumber("AuxSymbolCount", Symbol->NumberOfAuxSymbols);

  for (unsigned I = 0; I < Symbol->NumberOfAuxSymbols; ++I) {
    if (Symbol->StorageClass     == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
        Symbol->getBaseType()    == COFF::IMAGE_SYM_TYPE_NULL &&
        Symbol->getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION &&
        Symbol->SectionNumber > 0) {
      const coff_aux_function_definition *Aux;
      if (error(getSymbolAuxData(Obj, Symbol + I, Aux)))
        break;

      DictScope AS(W, "AuxFunctionDef");
      W.printNumber("TagIndex", Aux->TagIndex);
      W.printNumber("TotalSize", Aux->TotalSize);
      W.printHex("PointerToLineNumber", Aux->PointerToLineNumber);
      W.printHex("PointerToNextFunction", Aux->PointerToNextFunction);
      W.printBinary("Unused", makeArrayRef(Aux->Unused));

    } else if (
        Symbol->StorageClass   == COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL ||
        (Symbol->StorageClass  == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
         Symbol->SectionNumber == 0 &&
         Symbol->Value         == 0)) {
      const coff_aux_weak_external_definition *Aux;
      if (error(getSymbolAuxData(Obj, Symbol + I, Aux)))
        break;

      const coff_symbol *Linked;
      StringRef LinkedName;
      error_code EC;
      if ((EC = Obj->getSymbol(Aux->TagIndex, Linked)) ||
          (EC = Obj->getSymbolName(Linked, LinkedName))) {
        LinkedName = "";
        error(EC);
      }

      DictScope AS(W, "AuxWeakExternal");
      W.printNumber("Linked", LinkedName, Aux->TagIndex);
      W.printEnum  ("Search", Aux->Characteristics,
                    makeArrayRef(WeakExternalCharacteristics));
      W.printBinary("Unused", Aux->Unused);

    } else if (Symbol->StorageClass == COFF::IMAGE_SYM_CLASS_FILE) {
      const coff_aux_file_record *Aux;
      if (error(getSymbolAuxData(Obj, Symbol + I, Aux)))
        break;

      DictScope AS(W, "AuxFileRecord");
      W.printString("FileName", StringRef(Aux->FileName));

    } else if (Symbol->StorageClass == COFF::IMAGE_SYM_CLASS_STATIC ||
               (Symbol->StorageClass == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
                Symbol->SectionNumber != COFF::IMAGE_SYM_UNDEFINED)) {
      const coff_aux_section_definition *Aux;
      if (error(getSymbolAuxData(Obj, Symbol + I, Aux)))
        break;

      DictScope AS(W, "AuxSectionDef");
      W.printNumber("Length", Aux->Length);
      W.printNumber("RelocationCount", Aux->NumberOfRelocations);
      W.printNumber("LineNumberCount", Aux->NumberOfLinenumbers);
      W.printHex("Checksum", Aux->CheckSum);
      W.printNumber("Number", Aux->Number);
      W.printEnum("Selection", Aux->Selection, makeArrayRef(ImageCOMDATSelect));
      W.printBinary("Unused", makeArrayRef(Aux->Unused));

      if (Section && Section->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT
          && Aux->Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
        const coff_section *Assoc;
        StringRef AssocName;
        error_code EC;
        if ((EC = Obj->getSection(Aux->Number, Assoc)) ||
            (EC = Obj->getSectionName(Assoc, AssocName))) {
          AssocName = "";
          error(EC);
        }

        W.printNumber("AssocSection", AssocName, Aux->Number);
      }
    } else if (Symbol->StorageClass == COFF::IMAGE_SYM_CLASS_CLR_TOKEN) {
      const coff_aux_clr_token *Aux;
      if (error(getSymbolAuxData(Obj, Symbol + I, Aux)))
        break;

      DictScope AS(W, "AuxCLRToken");
      W.printNumber("AuxType", Aux->AuxType);
      W.printNumber("Reserved", Aux->Reserved);
      W.printNumber("SymbolTableIndex", Aux->SymbolTableIndex);
      W.printBinary("Unused", Aux->Unused);

    } else {
      W.startLine() << "<unhandled auxiliary record>\n";
    }
  }
}

void COFFDumper::printUnwindInfo() {
  const coff_file_header *Header;
  if (error(Obj->getCOFFHeader(Header)))
    return;

  ListScope D(W, "UnwindInformation");
  if (Header->Machine != COFF::IMAGE_FILE_MACHINE_AMD64) {
    W.startLine() << "Unsupported image machine type "
              "(currently only AMD64 is supported).\n";
    return;
  }

  printX64UnwindInfo();
}

void COFFDumper::printX64UnwindInfo() {
  error_code EC;
  for (section_iterator SecI = Obj->begin_sections(),
                        SecE = Obj->end_sections();
                        SecI != SecE; SecI.increment(EC)) {
    if (error(EC)) break;

    StringRef Name;
    if (error(SecI->getName(Name)))
      continue;
    if (Name != ".pdata" && !Name.startswith(".pdata$"))
      continue;

    const coff_section *PData = Obj->getCOFFSection(SecI);

    ArrayRef<uint8_t> Contents;
    if (error(Obj->getSectionContents(PData, Contents)) ||
        Contents.empty())
      continue;

    ArrayRef<RuntimeFunction> RFs(
      reinterpret_cast<const RuntimeFunction *>(Contents.data()),
      Contents.size() / sizeof(RuntimeFunction));

    for (const RuntimeFunction *I = RFs.begin(), *E = RFs.end(); I < E; ++I) {
      const uint64_t OffsetInSection = std::distance(RFs.begin(), I)
                                     * sizeof(RuntimeFunction);

      printRuntimeFunction(*I, OffsetInSection, RelocMap[PData]);
    }
  }
}

void COFFDumper::printRuntimeFunction(
    const RuntimeFunction& RTF,
    uint64_t OffsetInSection,
    const std::vector<RelocationRef> &Rels) {

  DictScope D(W, "RuntimeFunction");
  W.printString("StartAddress",
                formatSymbol(Rels, OffsetInSection + 0, RTF.StartAddress));
  W.printString("EndAddress",
                formatSymbol(Rels, OffsetInSection + 4, RTF.EndAddress));
  W.printString("UnwindInfoAddress",
                formatSymbol(Rels, OffsetInSection + 8, RTF.UnwindInfoOffset));

  const coff_section* XData = 0;
  uint64_t UnwindInfoOffset = 0;
  if (error(getSection(Rels, OffsetInSection + 8, &XData, &UnwindInfoOffset)))
    return;

  ArrayRef<uint8_t> XContents;
  if (error(Obj->getSectionContents(XData, XContents)) || XContents.empty())
    return;

  UnwindInfoOffset += RTF.UnwindInfoOffset;
  if (UnwindInfoOffset > XContents.size())
    return;

  const Win64EH::UnwindInfo *UI =
    reinterpret_cast<const Win64EH::UnwindInfo *>(
      XContents.data() + UnwindInfoOffset);

  printUnwindInfo(*UI, UnwindInfoOffset, RelocMap[XData]);
}

void COFFDumper::printUnwindInfo(
    const Win64EH::UnwindInfo& UI,
    uint64_t OffsetInSection,
    const std::vector<RelocationRef> &Rels) {
  DictScope D(W, "UnwindInfo");
  W.printNumber("Version", UI.getVersion());
  W.printFlags("Flags", UI.getFlags(), makeArrayRef(UnwindFlags));
  W.printNumber("PrologSize", UI.PrologSize);
  if (UI.getFrameRegister() != 0) {
    W.printEnum("FrameRegister", UI.getFrameRegister(),
                makeArrayRef(UnwindOpInfo));
    W.printHex("FrameOffset", UI.getFrameOffset());
  } else {
    W.printString("FrameRegister", StringRef("-"));
    W.printString("FrameOffset", StringRef("-"));
  }

  W.printNumber("UnwindCodeCount", UI.NumCodes);
  {
    ListScope CodesD(W, "UnwindCodes");
    ArrayRef<UnwindCode> UCs(&UI.UnwindCodes[0], UI.NumCodes);
    for (const UnwindCode *I = UCs.begin(), *E = UCs.end(); I < E; ++I) {
      unsigned UsedSlots = getNumUsedSlots(*I);
      if (UsedSlots > UCs.size()) {
        errs() << "Corrupt unwind data";
        return;
      }
      printUnwindCode(UI, ArrayRef<UnwindCode>(I, E));
      I += UsedSlots - 1;
    }
  }

  uint64_t LSDAOffset = OffsetInSection + getOffsetOfLSDA(UI);
  if (UI.getFlags() & (UNW_ExceptionHandler | UNW_TerminateHandler)) {
    W.printString("Handler", formatSymbol(Rels, LSDAOffset,
                                        UI.getLanguageSpecificHandlerOffset()));
  } else if (UI.getFlags() & UNW_ChainInfo) {
    const RuntimeFunction *Chained = UI.getChainedFunctionEntry();
    if (Chained) {
      DictScope D(W, "Chained");
      W.printString("StartAddress", formatSymbol(Rels, LSDAOffset + 0,
                                                        Chained->StartAddress));
      W.printString("EndAddress", formatSymbol(Rels, LSDAOffset + 4,
                                                          Chained->EndAddress));
      W.printString("UnwindInfoAddress", formatSymbol(Rels, LSDAOffset + 8,
                                                    Chained->UnwindInfoOffset));
    }
  }
}

// Prints one unwind code. Because an unwind code can occupy up to 3 slots in
// the unwind codes array, this function requires that the correct number of
// slots is provided.
void COFFDumper::printUnwindCode(const Win64EH::UnwindInfo& UI,
                                 ArrayRef<UnwindCode> UCs) {
  assert(UCs.size() >= getNumUsedSlots(UCs[0]));

  W.startLine() << format("0x%02X: ", unsigned(UCs[0].u.CodeOffset))
                << getUnwindCodeTypeName(UCs[0].getUnwindOp());

  uint32_t AllocSize = 0;

  switch (UCs[0].getUnwindOp()) {
  case UOP_PushNonVol:
    outs() << " reg=" << getUnwindRegisterName(UCs[0].getOpInfo());
    break;

  case UOP_AllocLarge:
    if (UCs[0].getOpInfo() == 0) {
      AllocSize = UCs[1].FrameOffset * 8;
    } else {
      AllocSize = getLargeSlotValue(UCs);
    }
    outs() << " size=" << AllocSize;
    break;
  case UOP_AllocSmall:
    outs() << " size=" << ((UCs[0].getOpInfo() + 1) * 8);
    break;
  case UOP_SetFPReg:
    if (UI.getFrameRegister() == 0) {
      outs() << " reg=<invalid>";
    } else {
      outs() << " reg=" << getUnwindRegisterName(UI.getFrameRegister())
             << format(", offset=0x%X", UI.getFrameOffset() * 16);
    }
    break;
  case UOP_SaveNonVol:
    outs() << " reg=" << getUnwindRegisterName(UCs[0].getOpInfo())
           << format(", offset=0x%X", UCs[1].FrameOffset * 8);
    break;
  case UOP_SaveNonVolBig:
    outs() << " reg=" << getUnwindRegisterName(UCs[0].getOpInfo())
           << format(", offset=0x%X", getLargeSlotValue(UCs));
    break;
  case UOP_SaveXMM128:
    outs() << " reg=XMM" << static_cast<uint32_t>(UCs[0].getOpInfo())
           << format(", offset=0x%X", UCs[1].FrameOffset * 16);
    break;
  case UOP_SaveXMM128Big:
    outs() << " reg=XMM" << static_cast<uint32_t>(UCs[0].getOpInfo())
           << format(", offset=0x%X", getLargeSlotValue(UCs));
    break;
  case UOP_PushMachFrame:
    outs() << " errcode=" << (UCs[0].getOpInfo() == 0 ? "no" : "yes");
    break;
  }

  outs() << "\n";
}
