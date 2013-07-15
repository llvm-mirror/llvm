//===-- DWARFFormValue.cpp ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARFFormValue.h"
#include "DWARFCompileUnit.h"
#include "DWARFContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
using namespace llvm;
using namespace dwarf;

namespace {
template <uint8_t AddrSize, uint8_t RefAddrSize> struct FixedFormSizes {
  static const uint8_t sizes[];
};
}

template <uint8_t AddrSize, uint8_t RefAddrSize>
const uint8_t FixedFormSizes<AddrSize, RefAddrSize>::sizes[] = {
  0,           // 0x00 unused
  AddrSize,    // 0x01 DW_FORM_addr
  0,           // 0x02 unused
  0,           // 0x03 DW_FORM_block2
  0,           // 0x04 DW_FORM_block4
  2,           // 0x05 DW_FORM_data2
  4,           // 0x06 DW_FORM_data4
  8,           // 0x07 DW_FORM_data8
  0,           // 0x08 DW_FORM_string
  0,           // 0x09 DW_FORM_block
  0,           // 0x0a DW_FORM_block1
  1,           // 0x0b DW_FORM_data1
  1,           // 0x0c DW_FORM_flag
  0,           // 0x0d DW_FORM_sdata
  4,           // 0x0e DW_FORM_strp
  0,           // 0x0f DW_FORM_udata
  RefAddrSize, // 0x10 DW_FORM_ref_addr
  1,           // 0x11 DW_FORM_ref1
  2,           // 0x12 DW_FORM_ref2
  4,           // 0x13 DW_FORM_ref4
  8,           // 0x14 DW_FORM_ref8
  0,           // 0x15 DW_FORM_ref_udata
  0,           // 0x16 DW_FORM_indirect
  4,           // 0x17 DW_FORM_sec_offset
  0,           // 0x18 DW_FORM_exprloc
  0,           // 0x19 DW_FORM_flag_present
  8,           // 0x20 DW_FORM_ref_sig8
};

static uint8_t getRefAddrSize(uint8_t AddrSize, uint16_t Version) {
  // FIXME: Support DWARF64.
  return (Version == 2) ? AddrSize : 4;
}

const uint8_t *
DWARFFormValue::getFixedFormSizes(uint8_t AddrSize, uint16_t Version) {
  uint8_t RefAddrSize = getRefAddrSize(AddrSize, Version);
  if (AddrSize == 4 && RefAddrSize == 4)
    return FixedFormSizes<4, 4>::sizes;
  if (AddrSize == 4 && RefAddrSize == 8)
    return FixedFormSizes<4, 8>::sizes;
  if (AddrSize == 8 && RefAddrSize == 4)
    return FixedFormSizes<8, 4>::sizes;
  if (AddrSize == 8 && RefAddrSize == 8)
    return FixedFormSizes<8, 8>::sizes;
  return 0;
}

bool
DWARFFormValue::extractValue(DataExtractor data, uint32_t *offset_ptr,
                             const DWARFCompileUnit *cu) {
  bool indirect = false;
  bool is_block = false;
  Value.data = NULL;
  // Read the value for the form into value and follow and DW_FORM_indirect
  // instances we run into
  do {
    indirect = false;
    switch (Form) {
    case DW_FORM_addr:
    case DW_FORM_ref_addr: {
      uint16_t AddrSize =
          (Form == DW_FORM_addr)
              ? cu->getAddressByteSize()
              : getRefAddrSize(cu->getAddressByteSize(), cu->getVersion());
      RelocAddrMap::const_iterator AI = cu->getRelocMap()->find(*offset_ptr);
      if (AI != cu->getRelocMap()->end()) {
        const std::pair<uint8_t, int64_t> &R = AI->second;
        Value.uval = data.getUnsigned(offset_ptr, AddrSize) + R.second;
      } else
        Value.uval = data.getUnsigned(offset_ptr, AddrSize);
      break;
    }
    case DW_FORM_exprloc:
    case DW_FORM_block:
      Value.uval = data.getULEB128(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block1:
      Value.uval = data.getU8(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block2:
      Value.uval = data.getU16(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block4:
      Value.uval = data.getU32(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_data1:
    case DW_FORM_ref1:
    case DW_FORM_flag:
      Value.uval = data.getU8(offset_ptr);
      break;
    case DW_FORM_data2:
    case DW_FORM_ref2:
      Value.uval = data.getU16(offset_ptr);
      break;
    case DW_FORM_data4:
    case DW_FORM_ref4: {
      RelocAddrMap::const_iterator AI = cu->getRelocMap()->find(*offset_ptr);
      Value.uval = data.getU32(offset_ptr);
      if (AI != cu->getRelocMap()->end())
        Value.uval += AI->second.second;
      break;
    }
    case DW_FORM_data8:
    case DW_FORM_ref8:
      Value.uval = data.getU64(offset_ptr);
      break;
    case DW_FORM_sdata:
      Value.sval = data.getSLEB128(offset_ptr);
      break;
    case DW_FORM_strp: {
      RelocAddrMap::const_iterator AI
        = cu->getRelocMap()->find(*offset_ptr);
      if (AI != cu->getRelocMap()->end()) {
        const std::pair<uint8_t, int64_t> &R = AI->second;
        Value.uval = data.getU32(offset_ptr) + R.second;
      } else
        Value.uval = data.getU32(offset_ptr);
      break;
    }
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
      Value.uval = data.getULEB128(offset_ptr);
      break;
    case DW_FORM_string:
      Value.cstr = data.getCStr(offset_ptr);
      // Set the string value to also be the data for inlined cstr form
      // values only so we can tell the differnence between DW_FORM_string
      // and DW_FORM_strp form values
      Value.data = (const uint8_t*)Value.cstr;
      break;
    case DW_FORM_indirect:
      Form = data.getULEB128(offset_ptr);
      indirect = true;
      break;
    case DW_FORM_sec_offset: {
      // FIXME: This is 64-bit for DWARF64.
      RelocAddrMap::const_iterator AI
        = cu->getRelocMap()->find(*offset_ptr);
      if (AI != cu->getRelocMap()->end()) {
        const std::pair<uint8_t, int64_t> &R = AI->second;
        Value.uval = data.getU32(offset_ptr) + R.second;
      } else
        Value.uval = data.getU32(offset_ptr);
      break;
    }
    case DW_FORM_flag_present:
      Value.uval = 1;
      break;
    case DW_FORM_ref_sig8:
      Value.uval = data.getU64(offset_ptr);
      break;
    case DW_FORM_GNU_addr_index:
      Value.uval = data.getULEB128(offset_ptr);
      break;
    case DW_FORM_GNU_str_index:
      Value.uval = data.getULEB128(offset_ptr);
      break;
    default:
      return false;
    }
  } while (indirect);

  if (is_block) {
    StringRef str = data.getData().substr(*offset_ptr, Value.uval);
    Value.data = NULL;
    if (!str.empty()) {
      Value.data = reinterpret_cast<const uint8_t *>(str.data());
      *offset_ptr += Value.uval;
    }
  }

  return true;
}

bool
DWARFFormValue::skipValue(DataExtractor debug_info_data, uint32_t* offset_ptr,
                          const DWARFCompileUnit *cu) const {
  return DWARFFormValue::skipValue(Form, debug_info_data, offset_ptr, cu);
}

bool
DWARFFormValue::skipValue(uint16_t form, DataExtractor debug_info_data,
                          uint32_t *offset_ptr, const DWARFCompileUnit *cu) {
  bool indirect = false;
  do {
    switch (form) {
    // Blocks if inlined data that have a length field and the data bytes
    // inlined in the .debug_info
    case DW_FORM_exprloc:
    case DW_FORM_block: {
      uint64_t size = debug_info_data.getULEB128(offset_ptr);
      *offset_ptr += size;
      return true;
    }
    case DW_FORM_block1: {
      uint8_t size = debug_info_data.getU8(offset_ptr);
      *offset_ptr += size;
      return true;
    }
    case DW_FORM_block2: {
      uint16_t size = debug_info_data.getU16(offset_ptr);
      *offset_ptr += size;
      return true;
    }
    case DW_FORM_block4: {
      uint32_t size = debug_info_data.getU32(offset_ptr);
      *offset_ptr += size;
      return true;
    }

    // Inlined NULL terminated C-strings
    case DW_FORM_string:
      debug_info_data.getCStr(offset_ptr);
      return true;

    // Compile unit address sized values
    case DW_FORM_addr:
      *offset_ptr += cu->getAddressByteSize();
      return true;
    case DW_FORM_ref_addr:
      *offset_ptr += getRefAddrSize(cu->getAddressByteSize(), cu->getVersion());
      return true;

    // 0 byte values - implied from the form.
    case DW_FORM_flag_present:
      return true;

    // 1 byte values
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
      *offset_ptr += 1;
      return true;

    // 2 byte values
    case DW_FORM_data2:
    case DW_FORM_ref2:
      *offset_ptr += 2;
      return true;

    // 4 byte values
    case DW_FORM_strp:
    case DW_FORM_data4:
    case DW_FORM_ref4:
      *offset_ptr += 4;
      return true;

    // 8 byte values
    case DW_FORM_data8:
    case DW_FORM_ref8:
    case DW_FORM_ref_sig8:
      *offset_ptr += 8;
      return true;

    // signed or unsigned LEB 128 values
    //  case DW_FORM_APPLE_db_str:
    case DW_FORM_sdata:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
    case DW_FORM_GNU_str_index:
    case DW_FORM_GNU_addr_index:
      debug_info_data.getULEB128(offset_ptr);
      return true;

    case DW_FORM_indirect:
      indirect = true;
      form = debug_info_data.getULEB128(offset_ptr);
      break;

    // FIXME: 4 for DWARF32, 8 for DWARF64.
    case DW_FORM_sec_offset:
      *offset_ptr += 4;
      return true;

    default:
      return false;
    }
  } while (indirect);
  return true;
}

void
DWARFFormValue::dump(raw_ostream &OS, const DWARFCompileUnit *cu) const {
  DataExtractor debug_str_data(cu->getStringSection(), true, 0);
  DataExtractor debug_str_offset_data(cu->getStringOffsetSection(), true, 0);
  uint64_t uvalue = getUnsigned();
  bool cu_relative_offset = false;

  switch (Form) {
  case DW_FORM_addr:      OS << format("0x%016" PRIx64, uvalue); break;
  case DW_FORM_GNU_addr_index: {
    StringRef AddrOffsetSec = cu->getAddrOffsetSection();
    OS << format(" indexed (%8.8x) address = ", (uint32_t)uvalue);
    if (AddrOffsetSec.size() != 0) {
      DataExtractor DA(AddrOffsetSec, true, cu->getAddressByteSize());
      OS << format("0x%016" PRIx64, getIndirectAddress(&DA, cu));
    } else
      OS << "<no .debug_addr section>";
    break;
  }
  case DW_FORM_flag_present: OS << "true"; break;
  case DW_FORM_flag:
  case DW_FORM_data1:     OS << format("0x%02x", (uint8_t)uvalue); break;
  case DW_FORM_data2:     OS << format("0x%04x", (uint16_t)uvalue); break;
  case DW_FORM_data4:     OS << format("0x%08x", (uint32_t)uvalue); break;
  case DW_FORM_ref_sig8:
  case DW_FORM_data8:     OS << format("0x%016" PRIx64, uvalue); break;
  case DW_FORM_string:
    OS << '"';
    OS.write_escaped(getAsCString(NULL));
    OS << '"';
    break;
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    if (uvalue > 0) {
      switch (Form) {
      case DW_FORM_exprloc:
      case DW_FORM_block:  OS << format("<0x%" PRIx64 "> ", uvalue);     break;
      case DW_FORM_block1: OS << format("<0x%2.2x> ", (uint8_t)uvalue);  break;
      case DW_FORM_block2: OS << format("<0x%4.4x> ", (uint16_t)uvalue); break;
      case DW_FORM_block4: OS << format("<0x%8.8x> ", (uint32_t)uvalue); break;
      default: break;
      }

      const uint8_t* data_ptr = Value.data;
      if (data_ptr) {
        // uvalue contains size of block
        const uint8_t* end_data_ptr = data_ptr + uvalue;
        while (data_ptr < end_data_ptr) {
          OS << format("%2.2x ", *data_ptr);
          ++data_ptr;
        }
      }
      else
        OS << "NULL";
    }
    break;

  case DW_FORM_sdata:     OS << getSigned();   break;
  case DW_FORM_udata:     OS << getUnsigned(); break;
  case DW_FORM_strp: {
    OS << format(" .debug_str[0x%8.8x] = ", (uint32_t)uvalue);
    const char* dbg_str = getAsCString(&debug_str_data);
    if (dbg_str) {
      OS << '"';
      OS.write_escaped(dbg_str);
      OS << '"';
    }
    break;
  }
  case DW_FORM_GNU_str_index: {
    OS << format(" indexed (%8.8x) string = ", (uint32_t)uvalue);
    const char *dbg_str = getIndirectCString(&debug_str_data,
                                             &debug_str_offset_data);
    if (dbg_str) {
      OS << '"';
      OS.write_escaped(dbg_str);
      OS << '"';
    }
    break;
  }
  case DW_FORM_ref_addr:
    OS << format("0x%016" PRIx64, uvalue);
    break;
  case DW_FORM_ref1:
    cu_relative_offset = true;
    OS << format("cu + 0x%2.2x", (uint8_t)uvalue);
    break;
  case DW_FORM_ref2:
    cu_relative_offset = true;
    OS << format("cu + 0x%4.4x", (uint16_t)uvalue);
    break;
  case DW_FORM_ref4:
    cu_relative_offset = true;
    OS << format("cu + 0x%4.4x", (uint32_t)uvalue);
    break;
  case DW_FORM_ref8:
    cu_relative_offset = true;
    OS << format("cu + 0x%8.8" PRIx64, uvalue);
    break;
  case DW_FORM_ref_udata:
    cu_relative_offset = true;
    OS << format("cu + 0x%" PRIx64, uvalue);
    break;

    // All DW_FORM_indirect attributes should be resolved prior to calling
    // this function
  case DW_FORM_indirect:
    OS << "DW_FORM_indirect";
    break;

    // Should be formatted to 64-bit for DWARF64.
  case DW_FORM_sec_offset:
    OS << format("0x%08x", (uint32_t)uvalue);
    break;

  default:
    OS << format("DW_FORM(0x%4.4x)", Form);
    break;
  }

  if (cu_relative_offset)
    OS << format(" => {0x%8.8" PRIx64 "}", uvalue + (cu ? cu->getOffset() : 0));
}

const char*
DWARFFormValue::getAsCString(const DataExtractor *debug_str_data_ptr) const {
  if (isInlinedCStr()) {
    return Value.cstr;
  } else if (debug_str_data_ptr) {
    uint32_t offset = Value.uval;
    return debug_str_data_ptr->getCStr(&offset);
  }
  return NULL;
}

const char*
DWARFFormValue::getIndirectCString(const DataExtractor *DS,
                                   const DataExtractor *DSO) const {
  if (!DS || !DSO) return NULL;

  uint32_t offset = Value.uval * 4;
  uint32_t soffset = DSO->getU32(&offset);
  return DS->getCStr(&soffset);
}

uint64_t
DWARFFormValue::getIndirectAddress(const DataExtractor *DA,
                                   const DWARFCompileUnit *cu) const {
  if (!DA) return 0;

  uint32_t offset = Value.uval * cu->getAddressByteSize();
  return DA->getAddress(&offset);
}

uint64_t DWARFFormValue::getReference(const DWARFCompileUnit *cu) const {
  uint64_t die_offset = Value.uval;
  switch (Form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
      die_offset += (cu ? cu->getOffset() : 0);
      break;
  default:
      break;
  }

  return die_offset;
}

bool
DWARFFormValue::resolveCompileUnitReferences(const DWARFCompileUnit *cu) {
  switch (Form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
    Value.uval += cu->getOffset();
    Form = DW_FORM_ref_addr;
    return true;
  default:
    break;
  }
  return false;
}

const uint8_t *DWARFFormValue::BlockData() const {
  if (!isInlinedCStr())
    return Value.data;
  return NULL;
}

bool DWARFFormValue::isBlockForm(uint16_t form) {
  switch (form) {
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    return true;
  }
  return false;
}

bool DWARFFormValue::isDataForm(uint16_t form) {
  switch (form) {
  case DW_FORM_sdata:
  case DW_FORM_udata:
  case DW_FORM_data1:
  case DW_FORM_data2:
  case DW_FORM_data4:
  case DW_FORM_data8:
    return true;
  }
  return false;
}
