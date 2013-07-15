//===-- RuntimeDyldMachO.cpp - Run-time dynamic linker for MC-JIT -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "dyld"
#include "RuntimeDyldMachO.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
using namespace llvm;
using namespace llvm::object;

namespace llvm {

static unsigned char *processFDE(unsigned char *P, intptr_t DeltaForText, intptr_t DeltaForEH) {
  uint32_t Length = *((uint32_t*)P);
  P += 4;
  unsigned char *Ret = P + Length;
  uint32_t Offset = *((uint32_t*)P);
  if (Offset == 0) // is a CIE
    return Ret;

  P += 4;
  intptr_t FDELocation = *((intptr_t*)P);
  intptr_t NewLocation = FDELocation - DeltaForText;
  *((intptr_t*)P) = NewLocation;
  P += sizeof(intptr_t);

  // Skip the FDE address range
  P += sizeof(intptr_t);

  uint8_t Augmentationsize = *P;
  P += 1;
  if (Augmentationsize != 0) {
    intptr_t LSDA = *((intptr_t*)P);
    intptr_t NewLSDA = LSDA - DeltaForEH;
    *((intptr_t*)P) = NewLSDA;
  }

  return Ret;
}

static intptr_t computeDelta(SectionEntry *A, SectionEntry *B) {
  intptr_t ObjDistance = A->ObjAddress  - B->ObjAddress;
  intptr_t MemDistance = A->LoadAddress - B->LoadAddress;
  return ObjDistance - MemDistance;
}

StringRef RuntimeDyldMachO::getEHFrameSection() {
  SectionEntry *Text = NULL;
  SectionEntry *EHFrame = NULL;
  SectionEntry *ExceptTab = NULL;
  for (int i = 0, e = Sections.size(); i != e; ++i) {
    if (Sections[i].Name == "__eh_frame")
      EHFrame = &Sections[i];
    else if (Sections[i].Name == "__text")
      Text = &Sections[i];
    else if (Sections[i].Name == "__gcc_except_tab")
      ExceptTab = &Sections[i];
  }
  if (Text == NULL || EHFrame == NULL)
    return StringRef();

  intptr_t DeltaForText = computeDelta(Text, EHFrame);
  intptr_t DeltaForEH = 0;
  if (ExceptTab)
    DeltaForEH = computeDelta(ExceptTab, EHFrame);

  unsigned char *P = EHFrame->Address;
  unsigned char *End = P + EHFrame->Size;
  do  {
    P = processFDE(P, DeltaForText, DeltaForEH);
  } while(P != End);

  return StringRef((char*)EHFrame->Address, EHFrame->Size);
}

void RuntimeDyldMachO::resolveRelocation(const RelocationEntry &RE,
                                         uint64_t Value) {
  const SectionEntry &Section = Sections[RE.SectionID];
  return resolveRelocation(Section, RE.Offset, Value, RE.RelType, RE.Addend,
                           RE.IsPCRel, RE.Size);
}

void RuntimeDyldMachO::resolveRelocation(const SectionEntry &Section,
                                         uint64_t Offset,
                                         uint64_t Value,
                                         uint32_t Type,
                                         int64_t Addend,
                                         bool isPCRel,
                                         unsigned LogSize) {
  uint8_t *LocalAddress = Section.Address + Offset;
  uint64_t FinalAddress = Section.LoadAddress + Offset;
  unsigned MachoType = Type;
  unsigned Size = 1 << LogSize;

  DEBUG(dbgs() << "resolveRelocation LocalAddress: " 
        << format("%p", LocalAddress)
        << " FinalAddress: " << format("%p", FinalAddress)
        << " Value: " << format("%p", Value)
        << " Addend: " << Addend
        << " isPCRel: " << isPCRel
        << " MachoType: " << MachoType
        << " Size: " << Size
        << "\n");

  // This just dispatches to the proper target specific routine.
  switch (Arch) {
  default: llvm_unreachable("Unsupported CPU type!");
  case Triple::x86_64:
    resolveX86_64Relocation(LocalAddress,
                            FinalAddress,
                            (uintptr_t)Value,
                            isPCRel,
                            MachoType,
                            Size,
                            Addend);
    break;
  case Triple::x86:
    resolveI386Relocation(LocalAddress,
                          FinalAddress,
                          (uintptr_t)Value,
                          isPCRel,
                          MachoType,
                          Size,
                          Addend);
    break;
  case Triple::arm:    // Fall through.
  case Triple::thumb:
    resolveARMRelocation(LocalAddress,
                         FinalAddress,
                         (uintptr_t)Value,
                         isPCRel,
                         MachoType,
                         Size,
                         Addend);
    break;
  }
}

bool RuntimeDyldMachO::resolveI386Relocation(uint8_t *LocalAddress,
                                             uint64_t FinalAddress,
                                             uint64_t Value,
                                             bool isPCRel,
                                             unsigned Type,
                                             unsigned Size,
                                             int64_t Addend) {
  if (isPCRel)
    Value -= FinalAddress + 4; // see resolveX86_64Relocation

  switch (Type) {
  default:
    llvm_unreachable("Invalid relocation type!");
  case macho::RIT_Vanilla: {
    uint8_t *p = LocalAddress;
    uint64_t ValueToWrite = Value + Addend;
    for (unsigned i = 0; i < Size; ++i) {
      *p++ = (uint8_t)(ValueToWrite & 0xff);
      ValueToWrite >>= 8;
    }
    return false;
  }
  case macho::RIT_Difference:
  case macho::RIT_Generic_LocalDifference:
  case macho::RIT_Generic_PreboundLazyPointer:
    return Error("Relocation type not implemented yet!");
  }
}

bool RuntimeDyldMachO::resolveX86_64Relocation(uint8_t *LocalAddress,
                                               uint64_t FinalAddress,
                                               uint64_t Value,
                                               bool isPCRel,
                                               unsigned Type,
                                               unsigned Size,
                                               int64_t Addend) {
  // If the relocation is PC-relative, the value to be encoded is the
  // pointer difference.
  if (isPCRel)
    // FIXME: It seems this value needs to be adjusted by 4 for an effective PC
    // address. Is that expected? Only for branches, perhaps?
    Value -= FinalAddress + 4;

  switch(Type) {
  default:
    llvm_unreachable("Invalid relocation type!");
  case macho::RIT_X86_64_Signed1:
  case macho::RIT_X86_64_Signed2:
  case macho::RIT_X86_64_Signed4:
  case macho::RIT_X86_64_Signed:
  case macho::RIT_X86_64_Unsigned:
  case macho::RIT_X86_64_Branch: {
    Value += Addend;
    // Mask in the target value a byte at a time (we don't have an alignment
    // guarantee for the target address, so this is safest).
    uint8_t *p = (uint8_t*)LocalAddress;
    for (unsigned i = 0; i < Size; ++i) {
      *p++ = (uint8_t)Value;
      Value >>= 8;
    }
    return false;
  }
  case macho::RIT_X86_64_GOTLoad:
  case macho::RIT_X86_64_GOT:
  case macho::RIT_X86_64_Subtractor:
  case macho::RIT_X86_64_TLV:
    return Error("Relocation type not implemented yet!");
  }
}

bool RuntimeDyldMachO::resolveARMRelocation(uint8_t *LocalAddress,
                                            uint64_t FinalAddress,
                                            uint64_t Value,
                                            bool isPCRel,
                                            unsigned Type,
                                            unsigned Size,
                                            int64_t Addend) {
  // If the relocation is PC-relative, the value to be encoded is the
  // pointer difference.
  if (isPCRel) {
    Value -= FinalAddress;
    // ARM PCRel relocations have an effective-PC offset of two instructions
    // (four bytes in Thumb mode, 8 bytes in ARM mode).
    // FIXME: For now, assume ARM mode.
    Value -= 8;
  }

  switch(Type) {
  default:
    llvm_unreachable("Invalid relocation type!");
  case macho::RIT_Vanilla: {
    // Mask in the target value a byte at a time (we don't have an alignment
    // guarantee for the target address, so this is safest).
    uint8_t *p = (uint8_t*)LocalAddress;
    for (unsigned i = 0; i < Size; ++i) {
      *p++ = (uint8_t)Value;
      Value >>= 8;
    }
    break;
  }
  case macho::RIT_ARM_Branch24Bit: {
    // Mask the value into the target address. We know instructions are
    // 32-bit aligned, so we can do it all at once.
    uint32_t *p = (uint32_t*)LocalAddress;
    // The low two bits of the value are not encoded.
    Value >>= 2;
    // Mask the value to 24 bits.
    Value &= 0xffffff;
    // FIXME: If the destination is a Thumb function (and the instruction
    // is a non-predicated BL instruction), we need to change it to a BLX
    // instruction instead.

    // Insert the value into the instruction.
    *p = (*p & ~0xffffff) | Value;
    break;
  }
  case macho::RIT_ARM_ThumbBranch22Bit:
  case macho::RIT_ARM_ThumbBranch32Bit:
  case macho::RIT_ARM_Half:
  case macho::RIT_ARM_HalfDifference:
  case macho::RIT_Pair:
  case macho::RIT_Difference:
  case macho::RIT_ARM_LocalDifference:
  case macho::RIT_ARM_PreboundLazyPointer:
    return Error("Relocation type not implemented yet!");
  }
  return false;
}

void RuntimeDyldMachO::processRelocationRef(unsigned SectionID,
                                            RelocationRef RelI,
                                            ObjectImage &Obj,
                                            ObjSectionToIDMap &ObjSectionToID,
                                            const SymbolTableMap &Symbols,
                                            StubMap &Stubs) {
  const ObjectFile *OF = Obj.getObjectFile();
  const MachOObjectFile *MachO = static_cast<const MachOObjectFile*>(OF);
  macho::RelocationEntry RE = MachO->getRelocation(RelI.getRawDataRefImpl());

  uint32_t RelType = MachO->getAnyRelocationType(RE);
  RelocationValueRef Value;
  SectionEntry &Section = Sections[SectionID];

  bool isExtern = MachO->getPlainRelocationExternal(RE);
  bool IsPCRel = MachO->getAnyRelocationPCRel(RE);
  unsigned Size = MachO->getAnyRelocationLength(RE);
  uint64_t Offset;
  RelI.getOffset(Offset);
  uint8_t *LocalAddress = Section.Address + Offset;
  unsigned NumBytes = 1 << Size;
  uint64_t Addend = 0;
  memcpy(&Addend, LocalAddress, NumBytes);

  if (isExtern) {
    // Obtain the symbol name which is referenced in the relocation
    symbol_iterator Symbol = RelI.getSymbol();
    StringRef TargetName;
    Symbol->getName(TargetName);
    // First search for the symbol in the local symbol table
    SymbolTableMap::const_iterator lsi = Symbols.find(TargetName.data());
    if (lsi != Symbols.end()) {
      Value.SectionID = lsi->second.first;
      Value.Addend = lsi->second.second + Addend;
    } else {
      // Search for the symbol in the global symbol table
      SymbolTableMap::const_iterator gsi = GlobalSymbolTable.find(TargetName.data());
      if (gsi != GlobalSymbolTable.end()) {
        Value.SectionID = gsi->second.first;
        Value.Addend = gsi->second.second + Addend;
      } else {
        Value.SymbolName = TargetName.data();
        Value.Addend = Addend;
      }
    }
  } else {
    SectionRef Sec = MachO->getRelocationSection(RE);
    Value.SectionID = findOrEmitSection(Obj, Sec, true, ObjSectionToID);
    uint64_t Addr;
    Sec.getAddress(Addr);
    Value.Addend = Addend - Addr;
  }

  if (Arch == Triple::x86_64 && RelType == macho::RIT_X86_64_GOT) {
    assert(IsPCRel);
    assert(Size == 2);
    StubMap::const_iterator i = Stubs.find(Value);
    uint8_t *Addr;
    if (i != Stubs.end()) {
      Addr = Section.Address + i->second;
    } else {
      Stubs[Value] = Section.StubOffset;
      uint8_t *GOTEntry = Section.Address + Section.StubOffset;
      RelocationEntry RE(SectionID, Section.StubOffset,
                         macho::RIT_X86_64_Unsigned, Value.Addend - 4, false,
                         3);
      if (Value.SymbolName)
        addRelocationForSymbol(RE, Value.SymbolName);
      else
        addRelocationForSection(RE, Value.SectionID);
      Section.StubOffset += 8;
      Addr = GOTEntry;
    }
    resolveRelocation(Section, Offset, (uint64_t)Addr,
                      macho::RIT_X86_64_Unsigned, 4, true, 2);
  } else if (Arch == Triple::arm &&
             (RelType & 0xf) == macho::RIT_ARM_Branch24Bit) {
    // This is an ARM branch relocation, need to use a stub function.

    //  Look up for existing stub.
    StubMap::const_iterator i = Stubs.find(Value);
    if (i != Stubs.end())
      resolveRelocation(Section, Offset,
                        (uint64_t)Section.Address + i->second,
                        RelType, 0, IsPCRel, Size);
    else {
      // Create a new stub function.
      Stubs[Value] = Section.StubOffset;
      uint8_t *StubTargetAddr = createStubFunction(Section.Address +
                                                   Section.StubOffset);
      RelocationEntry RE(SectionID, StubTargetAddr - Section.Address,
                         macho::RIT_Vanilla, Value.Addend);
      if (Value.SymbolName)
        addRelocationForSymbol(RE, Value.SymbolName);
      else
        addRelocationForSection(RE, Value.SectionID);
      resolveRelocation(Section, Offset,
                        (uint64_t)Section.Address + Section.StubOffset,
                        RelType, 0, IsPCRel, Size);
      Section.StubOffset += getMaxStubSize();
    }
  } else {
    RelocationEntry RE(SectionID, Offset, RelType, Value.Addend,
                       IsPCRel, Size);
    if (Value.SymbolName)
      addRelocationForSymbol(RE, Value.SymbolName);
    else
      addRelocationForSection(RE, Value.SectionID);
  }
}


bool RuntimeDyldMachO::isCompatibleFormat(
        const ObjectBuffer *InputBuffer) const {
  if (InputBuffer->getBufferSize() < 4)
    return false;
  StringRef Magic(InputBuffer->getBufferStart(), 4);
  if (Magic == "\xFE\xED\xFA\xCE") return true;
  if (Magic == "\xCE\xFA\xED\xFE") return true;
  if (Magic == "\xFE\xED\xFA\xCF") return true;
  if (Magic == "\xCF\xFA\xED\xFE") return true;
  return false;
}

} // end namespace llvm
