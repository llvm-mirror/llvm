
#include "llvm/MC/MCContext.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCBTFContext.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLabel.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCSymbolMachO.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <tuple>
#include <utility>

using namespace llvm;

void MCBTFContext::showAll() {
  for (size_t i = 0; i < TypeEntries.size(); i++) {
    auto TypeEntry = TypeEntries[i].get();
    TypeEntry->print(outs(), *this);
    outs() << "\n\n";
  }
  StringTable.showTable();
}

void MCBTFContext::emitCommonHeader(MCObjectStreamer *MCOS) {
  MCOS->EmitIntValue(BTF_MAGIC, 2);
  MCOS->EmitIntValue(BTF_VERSION, 1);
  MCOS->EmitIntValue(0, 1);
}

void MCBTFContext::emitBTFSection(MCObjectStreamer *MCOS) {
  MCContext &context = MCOS->getContext();
  MCOS->SwitchSection(context.getObjectFileInfo()->getBTFSection());

  // emit header
  emitCommonHeader(MCOS);
  MCOS->EmitIntValue(sizeof(struct btf_header), 4);

  uint32_t type_len = 0, str_len;
  for (auto &TypeEntry : TypeEntries)
    type_len += TypeEntry->getSize();
  str_len = StringTable.getSize();

  MCOS->EmitIntValue(0, 4);
  MCOS->EmitIntValue(type_len, 4);
  MCOS->EmitIntValue(type_len, 4);
  MCOS->EmitIntValue(str_len, 4);

  // emit type table
  for (auto &TypeEntry: TypeEntries)
    TypeEntry->emitData(MCOS);

  // emit string table
  for (auto &S : StringTable.Table) {
    for (auto C : S)
      MCOS->EmitIntValue(C, 1);
    MCOS->EmitIntValue('\0', 1);
  }
}

void MCBTFContext::emitBTFExtSection(MCObjectStreamer *MCOS) {
  MCContext &context = MCOS->getContext();
  MCOS->SwitchSection(context.getObjectFileInfo()->getBTFExtSection());

  // emit header
  emitCommonHeader(MCOS);
  MCOS->EmitIntValue(sizeof(struct btf_ext_header), 4);

  uint32_t func_len = 0, line_len = 0;
  for (auto &FuncSec : FuncInfoTable) {
    func_len += sizeof(struct btf_sec_func_info);
    func_len += FuncSec.info.size() * sizeof(struct bpf_func_info);
  }
  for (auto &LineSec : LineInfoTable) {
    line_len += sizeof(struct btf_sec_line_info);
    line_len += LineSec.info.size() * sizeof(struct bpf_line_info);
  }

  MCOS->EmitIntValue(0, 4);
  MCOS->EmitIntValue(func_len, 4);
  MCOS->EmitIntValue(func_len, 4);
  MCOS->EmitIntValue(line_len, 4);

  // emit func_info table
  for (const auto &SecTable : FuncInfoTable) {
    MCOS->EmitIntValue(SecTable.secname_off, 4);
    MCOS->EmitIntValue(SecTable.info.size(), 4);
    for (const auto &FuncInfo : SecTable.info) {
      MCOS->EmitBTFAdvanceLineAddr(FuncInfo.Label);
      MCOS->EmitIntValue(FuncInfo.type_id, 4);
      MCOS->EmitIntValue(0, 4);
    }
  }

  // emit line_info table
  for (const auto &SecTable : LineInfoTable) {
    MCOS->EmitIntValue(SecTable.secname_off, 4);
    MCOS->EmitIntValue(SecTable.info.size(), 4);
    for (const auto &LineInfo : SecTable.info) {
      MCOS->EmitBTFAdvanceLineAddr(LineInfo.label);
      MCOS->EmitIntValue(LineInfo.filename_off, 4);
      MCOS->EmitIntValue(LineInfo.line_off, 4);
      MCOS->EmitIntValue(LineInfo.line_num, 4);
      MCOS->EmitIntValue(LineInfo.column_num, 4);
    }
  }
}

void MCBTFContext::emitAll(MCObjectStreamer *MCOS) {
  emitBTFSection(MCOS);
  emitBTFExtSection(MCOS);
}

BTFTypeEntry *MCBTFContext::getReferredTypeEntry(BTFTypeEntry *TypeEntry) {
  if (TypeEntry->getTypeIndex() == 0)
    return nullptr;

  return TypeEntries[TypeEntry->getTypeIndex() - 1].get();
}

BTFTypeEntry *MCBTFContext::getMemberTypeEntry(struct btf_member &Member) {
  if (Member.type == 0)
    return nullptr;
  return TypeEntries[Member.type - 1].get();
}

std::string MCBTFContext::getTypeName(BTFTypeEntry *TypeEntry) {
  if (!TypeEntry)
    return "UNKNOWN";
  int Kind = TypeEntry->getKind();
   switch (Kind) {
    case BTF_KIND_INT:
    case BTF_KIND_STRUCT:
    case BTF_KIND_UNION:
    case BTF_KIND_ARRAY:
    case BTF_KIND_FUNC:
      return StringTable.getStringAtOffset(TypeEntry->getNameOff());
    case BTF_KIND_ENUM:
      return "enum " + StringTable.getStringAtOffset(TypeEntry->getNameOff());
    case BTF_KIND_CONST:
      return "const " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_PTR:
      return "ptr " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_VOLATILE:
      return "volatile " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_TYPEDEF:
      return "typedef " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_RESTRICT:
      return "restrict " + getTypeName(getReferredTypeEntry(TypeEntry));
    default:
      break;
  }
  return "";
}

std::string MCBTFContext::getTypeName(__u32 TypeIndex) {
  if (TypeIndex == 0)
    return "";
  return getTypeName(TypeEntries[TypeIndex - 1].get());
}

void BTFTypeEntry::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  s << "printing kind "
    << btf_kind_str[BTF_INFO_KIND(BTFType.info)] << "\n";
   s << "\tname: " << MCBTFContext.getTypeName(this) << "\n";
  s << "\tname_off: " << BTFType.name_off << "\n";
  s << "\tinfo: " << format("0x%08lx", BTFType.info) << "\n";
  s << "\tsize/type: " << format("0x%08lx", BTFType.size) << "\n";
}

void BTFTypeEntry::emitData(MCObjectStreamer *MCOS) {
  MCOS->EmitIntValue(BTFType.name_off, 4);
  MCOS->EmitIntValue(BTFType.info, 4);
  MCOS->EmitIntValue(BTFType.size, 4);
}

void BTFTypeEntryInt::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  BTFTypeEntry::print(s, MCBTFContext);
   s << "\tdesc: " << format("0x%08lx", IntVal) << "\n";
}

void BTFTypeEntryInt::emitData(MCObjectStreamer *MCOS) {
  BTFTypeEntry::emitData(MCOS);
  MCOS->EmitIntValue(IntVal, 4);
}

void BTFTypeEntryEnum::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  BTFTypeEntry::print(s, MCBTFContext);
   for (size_t i = 0; i < BTF_INFO_VLEN(BTFType.info); i++) {
    auto &EnumValue = EnumValues[i];
    s << "\tSymbol: " << MCBTFContext.getStringAtOffset(EnumValue.name_off)
      << " of value " << EnumValue.val
      << "\n";
  }
}

void BTFTypeEntryEnum::emitData(MCObjectStreamer *MCOS) {
  BTFTypeEntry::emitData(MCOS);
  for (auto &EnumValue : EnumValues) {
    MCOS->EmitIntValue(EnumValue.name_off, 4);
    MCOS->EmitIntValue(EnumValue.val, 4);
  }
}

void BTFTypeEntryArray::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  BTFTypeEntry::print(s, MCBTFContext);
  s << "\tElement type: " << format("0x%08lx", ArrayInfo.type) << "\n";
  s << "\tndex type: " << format("0x%08lx", ArrayInfo.index_type) << "\n";
  s << "\t# of element: " << ArrayInfo.nelems << "\n";
}

void BTFTypeEntryArray::emitData(MCObjectStreamer *MCOS) {
  BTFTypeEntry::emitData(MCOS);
  MCOS->EmitIntValue(ArrayInfo.type, 4);
  MCOS->EmitIntValue(ArrayInfo.index_type, 4);
  MCOS->EmitIntValue(ArrayInfo.nelems, 4);
}

void BTFTypeEntryStruct::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  BTFTypeEntry::print(s, MCBTFContext);
   for (size_t i = 0; i < BTF_INFO_VLEN(BTFType.info); i++) {
    auto &Member = Members[i];
    s << "\tMember: " << MCBTFContext.getStringAtOffset(Member.name_off)
      << " of type: "
      << MCBTFContext.getTypeName(MCBTFContext.getMemberTypeEntry(Member))
      << " (" << Member.type << ")\n";
  }
}

void BTFTypeEntryStruct::emitData(MCObjectStreamer *MCOS) {
  BTFTypeEntry::emitData(MCOS);
  for (auto &Member : Members) {
    MCOS->EmitIntValue(Member.name_off, 4);
    MCOS->EmitIntValue(Member.type, 4);
    MCOS->EmitIntValue(Member.offset, 4);
  }
}

void BTFTypeEntryFunc::print(raw_ostream &s, MCBTFContext& MCBTFContext) {
  BTFTypeEntry::print(s, MCBTFContext);
   for (size_t i = 0; i < BTF_INFO_VLEN(BTFType.info); i++) {
    auto Parameter = Parameters[i];
    s << "\tParameter of type: " << MCBTFContext.getTypeName(Parameter) << "\n";
  }
}

void BTFTypeEntryFunc::emitData(MCObjectStreamer *MCOS) {
  BTFTypeEntry::emitData(MCOS);
  for (auto &Parameter: Parameters)
    MCOS->EmitIntValue(Parameter, 4);
}
