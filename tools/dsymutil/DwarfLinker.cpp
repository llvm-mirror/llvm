//===- tools/dsymutil/DwarfLinker.cpp - Dwarf debug info linker -----------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "DebugMap.h"
#include "BinaryHolder.h"
#include "DebugMap.h"
#include "dsymutil.h"
#include "MachOUtils.h"
#include "NonRelocatableStringpool.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <string>
#include <tuple>

namespace llvm {
namespace dsymutil {

namespace {

template <typename KeyT, typename ValT>
using HalfOpenIntervalMap =
    IntervalMap<KeyT, ValT, IntervalMapImpl::NodeSizer<KeyT, ValT>::LeafSize,
                IntervalMapHalfOpenInfo<KeyT>>;

typedef HalfOpenIntervalMap<uint64_t, int64_t> FunctionIntervals;

// FIXME: Delete this structure.
struct PatchLocation {
  DIE::value_iterator I;

  PatchLocation() = default;
  PatchLocation(DIE::value_iterator I) : I(I) {}

  void set(uint64_t New) const {
    assert(I);
    const auto &Old = *I;
    assert(Old.getType() == DIEValue::isInteger);
    *I = DIEValue(Old.getAttribute(), Old.getForm(), DIEInteger(New));
  }

  uint64_t get() const {
    assert(I);
    return I->getDIEInteger().getValue();
  }
};

class CompileUnit;
struct DeclMapInfo;

/// A DeclContext is a named program scope that is used for ODR
/// uniquing of types.
/// The set of DeclContext for the ODR-subject parts of a Dwarf link
/// is expanded (and uniqued) with each new object file processed. We
/// need to determine the context of each DIE in an linked object file
/// to see if the corresponding type has already been emitted.
///
/// The contexts are conceptually organised as a tree (eg. a function
/// scope is contained in a namespace scope that contains other
/// scopes), but storing/accessing them in an actual tree is too
/// inefficient: we need to be able to very quickly query a context
/// for a given child context by name. Storing a StringMap in each
/// DeclContext would be too space inefficient.
/// The solution here is to give each DeclContext a link to its parent
/// (this allows to walk up the tree), but to query the existance of a
/// specific DeclContext using a separate DenseMap keyed on the hash
/// of the fully qualified name of the context.
class DeclContext {
  unsigned QualifiedNameHash;
  uint32_t Line;
  uint32_t ByteSize;
  uint16_t Tag;
  StringRef Name;
  StringRef File;
  const DeclContext &Parent;
  const DWARFDebugInfoEntryMinimal *LastSeenDIE;
  uint32_t LastSeenCompileUnitID;
  uint32_t CanonicalDIEOffset;

  friend DeclMapInfo;

public:
  typedef DenseSet<DeclContext *, DeclMapInfo> Map;

  DeclContext()
      : QualifiedNameHash(0), Line(0), ByteSize(0),
        Tag(dwarf::DW_TAG_compile_unit), Name(), File(), Parent(*this),
        LastSeenDIE(nullptr), LastSeenCompileUnitID(0), CanonicalDIEOffset(0) {}

  DeclContext(unsigned Hash, uint32_t Line, uint32_t ByteSize, uint16_t Tag,
              StringRef Name, StringRef File, const DeclContext &Parent,
              const DWARFDebugInfoEntryMinimal *LastSeenDIE = nullptr,
              unsigned CUId = 0)
      : QualifiedNameHash(Hash), Line(Line), ByteSize(ByteSize), Tag(Tag),
        Name(Name), File(File), Parent(Parent), LastSeenDIE(LastSeenDIE),
        LastSeenCompileUnitID(CUId), CanonicalDIEOffset(0) {}

  uint32_t getQualifiedNameHash() const { return QualifiedNameHash; }

  bool setLastSeenDIE(CompileUnit &U, const DWARFDebugInfoEntryMinimal *Die);

  uint32_t getCanonicalDIEOffset() const { return CanonicalDIEOffset; }
  void setCanonicalDIEOffset(uint32_t Offset) { CanonicalDIEOffset = Offset; }

  uint16_t getTag() const { return Tag; }
  StringRef getName() const { return Name; }
};

/// Info type for the DenseMap storing the DeclContext pointers.
struct DeclMapInfo : private DenseMapInfo<DeclContext *> {
  using DenseMapInfo<DeclContext *>::getEmptyKey;
  using DenseMapInfo<DeclContext *>::getTombstoneKey;

  static unsigned getHashValue(const DeclContext *Ctxt) {
    return Ctxt->QualifiedNameHash;
  }

  static bool isEqual(const DeclContext *LHS, const DeclContext *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return RHS == LHS;
    return LHS->QualifiedNameHash == RHS->QualifiedNameHash &&
           LHS->Line == RHS->Line && LHS->ByteSize == RHS->ByteSize &&
           LHS->Name.data() == RHS->Name.data() &&
           LHS->File.data() == RHS->File.data() &&
           LHS->Parent.QualifiedNameHash == RHS->Parent.QualifiedNameHash;
  }
};

/// This class gives a tree-like API to the DenseMap that stores the
/// DeclContext objects. It also holds the BumpPtrAllocator where
/// these objects will be allocated.
class DeclContextTree {
  BumpPtrAllocator Allocator;
  DeclContext Root;
  DeclContext::Map Contexts;

public:
  /// Get the child of \a Context described by \a DIE in \a Unit. The
  /// required strings will be interned in \a StringPool.
  /// \returns The child DeclContext along with one bit that is set if
  /// this context is invalid.
  /// An invalid context means it shouldn't be considered for uniquing, but its
  /// not returning null, because some children of that context might be
  /// uniquing candidates.  FIXME: The invalid bit along the return value is to
  /// emulate some dsymutil-classic functionality.
  PointerIntPair<DeclContext *, 1>
  getChildDeclContext(DeclContext &Context,
                      const DWARFDebugInfoEntryMinimal *DIE, CompileUnit &Unit,
                      NonRelocatableStringpool &StringPool, bool InClangModule);

  DeclContext &getRoot() { return Root; }
};

/// \brief Stores all information relating to a compile unit, be it in
/// its original instance in the object file to its brand new cloned
/// and linked DIE tree.
class CompileUnit {
public:
  /// \brief Information gathered about a DIE in the object file.
  struct DIEInfo {
    int64_t AddrAdjust; ///< Address offset to apply to the described entity.
    DeclContext *Ctxt;  ///< ODR Declaration context.
    DIE *Clone;         ///< Cloned version of that DIE.
    uint32_t ParentIdx; ///< The index of this DIE's parent.
    bool Keep : 1;      ///< Is the DIE part of the linked output?
    bool InDebugMap : 1;///< Was this DIE's entity found in the map?
    bool Prune : 1;     ///< Is this a pure forward declaration we can strip?
  };

  CompileUnit(DWARFUnit &OrigUnit, unsigned ID, bool CanUseODR,
              StringRef ClangModuleName)
      : OrigUnit(OrigUnit), ID(ID), LowPc(UINT64_MAX), HighPc(0), RangeAlloc(),
        Ranges(RangeAlloc), ClangModuleName(ClangModuleName) {
    Info.resize(OrigUnit.getNumDIEs());

    const auto *CUDie = OrigUnit.getUnitDIE(false);
    unsigned Lang = CUDie->getAttributeValueAsUnsignedConstant(
        &OrigUnit, dwarf::DW_AT_language, 0);
    HasODR = CanUseODR && (Lang == dwarf::DW_LANG_C_plus_plus ||
                           Lang == dwarf::DW_LANG_C_plus_plus_03 ||
                           Lang == dwarf::DW_LANG_C_plus_plus_11 ||
                           Lang == dwarf::DW_LANG_C_plus_plus_14 ||
                           Lang == dwarf::DW_LANG_ObjC_plus_plus);
  }

  CompileUnit(CompileUnit &&RHS)
      : OrigUnit(RHS.OrigUnit), Info(std::move(RHS.Info)),
        CUDie(std::move(RHS.CUDie)), StartOffset(RHS.StartOffset),
        NextUnitOffset(RHS.NextUnitOffset), RangeAlloc(), Ranges(RangeAlloc) {
    // The CompileUnit container has been 'reserve()'d with the right
    // size. We cannot move the IntervalMap anyway.
    llvm_unreachable("CompileUnits should not be moved.");
  }

  DWARFUnit &getOrigUnit() const { return OrigUnit; }

  unsigned getUniqueID() const { return ID; }

  DIE *getOutputUnitDIE() const { return CUDie; }
  void setOutputUnitDIE(DIE *Die) { CUDie = Die; }

  bool hasODR() const { return HasODR; }
  bool isClangModule() const { return !ClangModuleName.empty(); }
  const std::string &getClangModuleName() const { return ClangModuleName; }

  DIEInfo &getInfo(unsigned Idx) { return Info[Idx]; }
  const DIEInfo &getInfo(unsigned Idx) const { return Info[Idx]; }

  uint64_t getStartOffset() const { return StartOffset; }
  uint64_t getNextUnitOffset() const { return NextUnitOffset; }
  void setStartOffset(uint64_t DebugInfoSize) { StartOffset = DebugInfoSize; }

  uint64_t getLowPc() const { return LowPc; }
  uint64_t getHighPc() const { return HighPc; }

  Optional<PatchLocation> getUnitRangesAttribute() const {
    return UnitRangeAttribute;
  }
  const FunctionIntervals &getFunctionRanges() const { return Ranges; }
  const std::vector<PatchLocation> &getRangesAttributes() const {
    return RangeAttributes;
  }

  const std::vector<std::pair<PatchLocation, int64_t>> &
  getLocationAttributes() const {
    return LocationAttributes;
  }

  void setHasInterestingContent() { HasInterestingContent = true; }
  bool hasInterestingContent() { return HasInterestingContent; }

  /// Mark every DIE in this unit as kept. This function also
  /// marks variables as InDebugMap so that they appear in the
  /// reconstructed accelerator tables.
  void markEverythingAsKept();

  /// \brief Compute the end offset for this unit. Must be
  /// called after the CU's DIEs have been cloned.
  /// \returns the next unit offset (which is also the current
  /// debug_info section size).
  uint64_t computeNextUnitOffset();

  /// \brief Keep track of a forward reference to DIE \p Die in \p
  /// RefUnit by \p Attr. The attribute should be fixed up later to
  /// point to the absolute offset of \p Die in the debug_info section
  /// or to the canonical offset of \p Ctxt if it is non-null.
  void noteForwardReference(DIE *Die, const CompileUnit *RefUnit,
                            DeclContext *Ctxt, PatchLocation Attr);

  /// \brief Apply all fixups recored by noteForwardReference().
  void fixupForwardReferences();

  /// \brief Add a function range [\p LowPC, \p HighPC) that is
  /// relocatad by applying offset \p PCOffset.
  void addFunctionRange(uint64_t LowPC, uint64_t HighPC, int64_t PCOffset);

  /// \brief Keep track of a DW_AT_range attribute that we will need to
  /// patch up later.
  void noteRangeAttribute(const DIE &Die, PatchLocation Attr);

  /// \brief Keep track of a location attribute pointing to a location
  /// list in the debug_loc section.
  void noteLocationAttribute(PatchLocation Attr, int64_t PcOffset);

  /// \brief Add a name accelerator entry for \p Die with \p Name
  /// which is stored in the string table at \p Offset.
  void addNameAccelerator(const DIE *Die, const char *Name, uint32_t Offset,
                          bool SkipPubnamesSection = false);

  /// \brief Add a type accelerator entry for \p Die with \p Name
  /// which is stored in the string table at \p Offset.
  void addTypeAccelerator(const DIE *Die, const char *Name, uint32_t Offset);

  struct AccelInfo {
    StringRef Name;      ///< Name of the entry.
    const DIE *Die;      ///< DIE this entry describes.
    uint32_t NameOffset; ///< Offset of Name in the string pool.
    bool SkipPubSection; ///< Emit this entry only in the apple_* sections.

    AccelInfo(StringRef Name, const DIE *Die, uint32_t NameOffset,
              bool SkipPubSection = false)
        : Name(Name), Die(Die), NameOffset(NameOffset),
          SkipPubSection(SkipPubSection) {}
  };

  const std::vector<AccelInfo> &getPubnames() const { return Pubnames; }
  const std::vector<AccelInfo> &getPubtypes() const { return Pubtypes; }

  /// Get the full path for file \a FileNum in the line table
  const char *getResolvedPath(unsigned FileNum) {
    if (FileNum >= ResolvedPaths.size())
      return nullptr;
    return ResolvedPaths[FileNum].size() ? ResolvedPaths[FileNum].c_str()
                                         : nullptr;
  }

  /// Set the fully resolved path for the line-table's file \a FileNum
  /// to \a Path.
  void setResolvedPath(unsigned FileNum, const std::string &Path) {
    if (ResolvedPaths.size() <= FileNum)
      ResolvedPaths.resize(FileNum + 1);
    ResolvedPaths[FileNum] = Path;
  }

private:
  DWARFUnit &OrigUnit;
  unsigned ID;
  std::vector<DIEInfo> Info; ///< DIE info indexed by DIE index.
  DIE *CUDie;                ///< Root of the linked DIE tree.

  uint64_t StartOffset;
  uint64_t NextUnitOffset;

  uint64_t LowPc;
  uint64_t HighPc;

  /// \brief A list of attributes to fixup with the absolute offset of
  /// a DIE in the debug_info section.
  ///
  /// The offsets for the attributes in this array couldn't be set while
  /// cloning because for cross-cu forward refences the target DIE's
  /// offset isn't known you emit the reference attribute.
  std::vector<std::tuple<DIE *, const CompileUnit *, DeclContext *,
                         PatchLocation>> ForwardDIEReferences;

  FunctionIntervals::Allocator RangeAlloc;
  /// \brief The ranges in that interval map are the PC ranges for
  /// functions in this unit, associated with the PC offset to apply
  /// to the addresses to get the linked address.
  FunctionIntervals Ranges;

  /// \brief DW_AT_ranges attributes to patch after we have gathered
  /// all the unit's function addresses.
  /// @{
  std::vector<PatchLocation> RangeAttributes;
  Optional<PatchLocation> UnitRangeAttribute;
  /// @}

  /// \brief Location attributes that need to be transfered from th
  /// original debug_loc section to the liked one. They are stored
  /// along with the PC offset that is to be applied to their
  /// function's address.
  std::vector<std::pair<PatchLocation, int64_t>> LocationAttributes;

  /// \brief Accelerator entries for the unit, both for the pub*
  /// sections and the apple* ones.
  /// @{
  std::vector<AccelInfo> Pubnames;
  std::vector<AccelInfo> Pubtypes;
  /// @}

  /// Cached resolved paths from the line table.
  std::vector<std::string> ResolvedPaths;

  /// Is this unit subject to the ODR rule?
  bool HasODR;
  /// Did a DIE actually contain a valid reloc?
  bool HasInterestingContent;
  /// If this is a Clang module, this holds the module's name.
  std::string ClangModuleName;
};

void CompileUnit::markEverythingAsKept() {
  for (auto &I : Info)
    // Mark everything that wasn't explicity marked for pruning.
    I.Keep = !I.Prune;
}

uint64_t CompileUnit::computeNextUnitOffset() {
  NextUnitOffset = StartOffset + 11 /* Header size */;
  // The root DIE might be null, meaning that the Unit had nothing to
  // contribute to the linked output. In that case, we will emit the
  // unit header without any actual DIE.
  if (CUDie)
    NextUnitOffset += CUDie->getSize();
  return NextUnitOffset;
}

/// \brief Keep track of a forward cross-cu reference from this unit
/// to \p Die that lives in \p RefUnit.
void CompileUnit::noteForwardReference(DIE *Die, const CompileUnit *RefUnit,
                                       DeclContext *Ctxt, PatchLocation Attr) {
  ForwardDIEReferences.emplace_back(Die, RefUnit, Ctxt, Attr);
}

/// \brief Apply all fixups recorded by noteForwardReference().
void CompileUnit::fixupForwardReferences() {
  for (const auto &Ref : ForwardDIEReferences) {
    DIE *RefDie;
    const CompileUnit *RefUnit;
    PatchLocation Attr;
    DeclContext *Ctxt;
    std::tie(RefDie, RefUnit, Ctxt, Attr) = Ref;
    if (Ctxt && Ctxt->getCanonicalDIEOffset())
      Attr.set(Ctxt->getCanonicalDIEOffset());
    else
      Attr.set(RefDie->getOffset() + RefUnit->getStartOffset());
  }
}

void CompileUnit::addFunctionRange(uint64_t FuncLowPc, uint64_t FuncHighPc,
                                   int64_t PcOffset) {
  Ranges.insert(FuncLowPc, FuncHighPc, PcOffset);
  this->LowPc = std::min(LowPc, FuncLowPc + PcOffset);
  this->HighPc = std::max(HighPc, FuncHighPc + PcOffset);
}

void CompileUnit::noteRangeAttribute(const DIE &Die, PatchLocation Attr) {
  if (Die.getTag() != dwarf::DW_TAG_compile_unit)
    RangeAttributes.push_back(Attr);
  else
    UnitRangeAttribute = Attr;
}

void CompileUnit::noteLocationAttribute(PatchLocation Attr, int64_t PcOffset) {
  LocationAttributes.emplace_back(Attr, PcOffset);
}

/// \brief Add a name accelerator entry for \p Die with \p Name
/// which is stored in the string table at \p Offset.
void CompileUnit::addNameAccelerator(const DIE *Die, const char *Name,
                                     uint32_t Offset, bool SkipPubSection) {
  Pubnames.emplace_back(Name, Die, Offset, SkipPubSection);
}

/// \brief Add a type accelerator entry for \p Die with \p Name
/// which is stored in the string table at \p Offset.
void CompileUnit::addTypeAccelerator(const DIE *Die, const char *Name,
                                     uint32_t Offset) {
  Pubtypes.emplace_back(Name, Die, Offset, false);
}

/// \brief The Dwarf streaming logic
///
/// All interactions with the MC layer that is used to build the debug
/// information binary representation are handled in this class.
class DwarfStreamer {
  /// \defgroup MCObjects MC layer objects constructed by the streamer
  /// @{
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCObjectFileInfo> MOFI;
  std::unique_ptr<MCContext> MC;
  MCAsmBackend *MAB; // Owned by MCStreamer
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCSubtargetInfo> MSTI;
  MCCodeEmitter *MCE; // Owned by MCStreamer
  MCStreamer *MS;     // Owned by AsmPrinter
  std::unique_ptr<TargetMachine> TM;
  std::unique_ptr<AsmPrinter> Asm;
  /// @}

  /// \brief the file we stream the linked Dwarf to.
  std::unique_ptr<raw_fd_ostream> OutFile;

  uint32_t RangesSectionSize;
  uint32_t LocSectionSize;
  uint32_t LineSectionSize;
  uint32_t FrameSectionSize;

  /// \brief Emit the pubnames or pubtypes section contribution for \p
  /// Unit into \p Sec. The data is provided in \p Names.
  void emitPubSectionForUnit(MCSection *Sec, StringRef Name,
                             const CompileUnit &Unit,
                             const std::vector<CompileUnit::AccelInfo> &Names);

public:
  /// \brief Actually create the streamer and the ouptut file.
  ///
  /// This could be done directly in the constructor, but it feels
  /// more natural to handle errors through return value.
  bool init(Triple TheTriple, StringRef OutputFilename);

  /// \brief Dump the file to the disk.
  bool finish(const DebugMap &);

  AsmPrinter &getAsmPrinter() const { return *Asm; }

  /// \brief Set the current output section to debug_info and change
  /// the MC Dwarf version to \p DwarfVersion.
  void switchToDebugInfoSection(unsigned DwarfVersion);

  /// \brief Emit the compilation unit header for \p Unit in the
  /// debug_info section.
  ///
  /// As a side effect, this also switches the current Dwarf version
  /// of the MC layer to the one of U.getOrigUnit().
  void emitCompileUnitHeader(CompileUnit &Unit);

  /// \brief Recursively emit the DIE tree rooted at \p Die.
  void emitDIE(DIE &Die);

  /// \brief Emit the abbreviation table \p Abbrevs to the
  /// debug_abbrev section.
  void emitAbbrevs(const std::vector<std::unique_ptr<DIEAbbrev>> &Abbrevs);

  /// \brief Emit the string table described by \p Pool.
  void emitStrings(const NonRelocatableStringpool &Pool);

  /// \brief Emit debug_ranges for \p FuncRange by translating the
  /// original \p Entries.
  void emitRangesEntries(
      int64_t UnitPcOffset, uint64_t OrigLowPc,
      FunctionIntervals::const_iterator FuncRange,
      const std::vector<DWARFDebugRangeList::RangeListEntry> &Entries,
      unsigned AddressSize);

  /// \brief Emit debug_aranges entries for \p Unit and if \p
  /// DoRangesSection is true, also emit the debug_ranges entries for
  /// the DW_TAG_compile_unit's DW_AT_ranges attribute.
  void emitUnitRangesEntries(CompileUnit &Unit, bool DoRangesSection);

  uint32_t getRangesSectionSize() const { return RangesSectionSize; }

  /// \brief Emit the debug_loc contribution for \p Unit by copying
  /// the entries from \p Dwarf and offseting them. Update the
  /// location attributes to point to the new entries.
  void emitLocationsForUnit(const CompileUnit &Unit, DWARFContext &Dwarf);

  /// \brief Emit the line table described in \p Rows into the
  /// debug_line section.
  void emitLineTableForUnit(MCDwarfLineTableParams Params,
                            StringRef PrologueBytes, unsigned MinInstLength,
                            std::vector<DWARFDebugLine::Row> &Rows,
                            unsigned AdddressSize);

  uint32_t getLineSectionSize() const { return LineSectionSize; }

  /// \brief Emit the .debug_pubnames contribution for \p Unit.
  void emitPubNamesForUnit(const CompileUnit &Unit);

  /// \brief Emit the .debug_pubtypes contribution for \p Unit.
  void emitPubTypesForUnit(const CompileUnit &Unit);

  /// \brief Emit a CIE.
  void emitCIE(StringRef CIEBytes);

  /// \brief Emit an FDE with data \p Bytes.
  void emitFDE(uint32_t CIEOffset, uint32_t AddreSize, uint32_t Address,
               StringRef Bytes);

  uint32_t getFrameSectionSize() const { return FrameSectionSize; }
};

bool DwarfStreamer::init(Triple TheTriple, StringRef OutputFilename) {
  std::string ErrorStr;
  std::string TripleName;
  StringRef Context = "dwarf streamer init";

  // Get the target.
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TripleName, TheTriple, ErrorStr);
  if (!TheTarget)
    return error(ErrorStr, Context);
  TripleName = TheTriple.getTriple();

  // Create all the MC Objects.
  MRI.reset(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    return error(Twine("no register info for target ") + TripleName, Context);

  MAI.reset(TheTarget->createMCAsmInfo(*MRI, TripleName));
  if (!MAI)
    return error("no asm info for target " + TripleName, Context);

  MOFI.reset(new MCObjectFileInfo);
  MC.reset(new MCContext(MAI.get(), MRI.get(), MOFI.get()));
  MOFI->InitMCObjectFileInfo(TheTriple, Reloc::Default, CodeModel::Default,
                             *MC);

  MAB = TheTarget->createMCAsmBackend(*MRI, TripleName, "");
  if (!MAB)
    return error("no asm backend for target " + TripleName, Context);

  MII.reset(TheTarget->createMCInstrInfo());
  if (!MII)
    return error("no instr info info for target " + TripleName, Context);

  MSTI.reset(TheTarget->createMCSubtargetInfo(TripleName, "", ""));
  if (!MSTI)
    return error("no subtarget info for target " + TripleName, Context);

  MCE = TheTarget->createMCCodeEmitter(*MII, *MRI, *MC);
  if (!MCE)
    return error("no code emitter for target " + TripleName, Context);

  // Create the output file.
  std::error_code EC;
  OutFile =
      llvm::make_unique<raw_fd_ostream>(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    return error(Twine(OutputFilename) + ": " + EC.message(), Context);

  MCTargetOptions MCOptions = InitMCTargetOptionsFromFlags();
  MS = TheTarget->createMCObjectStreamer(
      TheTriple, *MC, *MAB, *OutFile, MCE, *MSTI, MCOptions.MCRelaxAll,
      MCOptions.MCIncrementalLinkerCompatible,
      /*DWARFMustBeAtTheEnd*/ false);
  if (!MS)
    return error("no object streamer for target " + TripleName, Context);

  // Finally create the AsmPrinter we'll use to emit the DIEs.
  TM.reset(TheTarget->createTargetMachine(TripleName, "", "", TargetOptions()));
  if (!TM)
    return error("no target machine for target " + TripleName, Context);

  Asm.reset(TheTarget->createAsmPrinter(*TM, std::unique_ptr<MCStreamer>(MS)));
  if (!Asm)
    return error("no asm printer for target " + TripleName, Context);

  RangesSectionSize = 0;
  LocSectionSize = 0;
  LineSectionSize = 0;
  FrameSectionSize = 0;

  return true;
}

bool DwarfStreamer::finish(const DebugMap &DM) {
  if (DM.getTriple().isOSDarwin() && !DM.getBinaryPath().empty())
    return MachOUtils::generateDsymCompanion(DM, *MS, *OutFile);

  MS->Finish();
  return true;
}

/// \brief Set the current output section to debug_info and change
/// the MC Dwarf version to \p DwarfVersion.
void DwarfStreamer::switchToDebugInfoSection(unsigned DwarfVersion) {
  MS->SwitchSection(MOFI->getDwarfInfoSection());
  MC->setDwarfVersion(DwarfVersion);
}

/// \brief Emit the compilation unit header for \p Unit in the
/// debug_info section.
///
/// A Dwarf scetion header is encoded as:
///  uint32_t   Unit length (omiting this field)
///  uint16_t   Version
///  uint32_t   Abbreviation table offset
///  uint8_t    Address size
///
/// Leading to a total of 11 bytes.
void DwarfStreamer::emitCompileUnitHeader(CompileUnit &Unit) {
  unsigned Version = Unit.getOrigUnit().getVersion();
  switchToDebugInfoSection(Version);

  // Emit size of content not including length itself. The size has
  // already been computed in CompileUnit::computeOffsets(). Substract
  // 4 to that size to account for the length field.
  Asm->EmitInt32(Unit.getNextUnitOffset() - Unit.getStartOffset() - 4);
  Asm->EmitInt16(Version);
  // We share one abbreviations table across all units so it's always at the
  // start of the section.
  Asm->EmitInt32(0);
  Asm->EmitInt8(Unit.getOrigUnit().getAddressByteSize());
}

/// \brief Emit the \p Abbrevs array as the shared abbreviation table
/// for the linked Dwarf file.
void DwarfStreamer::emitAbbrevs(
    const std::vector<std::unique_ptr<DIEAbbrev>> &Abbrevs) {
  MS->SwitchSection(MOFI->getDwarfAbbrevSection());
  Asm->emitDwarfAbbrevs(Abbrevs);
}

/// \brief Recursively emit the DIE tree rooted at \p Die.
void DwarfStreamer::emitDIE(DIE &Die) {
  MS->SwitchSection(MOFI->getDwarfInfoSection());
  Asm->emitDwarfDIE(Die);
}

/// \brief Emit the debug_str section stored in \p Pool.
void DwarfStreamer::emitStrings(const NonRelocatableStringpool &Pool) {
  Asm->OutStreamer->SwitchSection(MOFI->getDwarfStrSection());
  for (auto *Entry = Pool.getFirstEntry(); Entry;
       Entry = Pool.getNextEntry(Entry))
    Asm->OutStreamer->EmitBytes(
        StringRef(Entry->getKey().data(), Entry->getKey().size() + 1));
}

/// \brief Emit the debug_range section contents for \p FuncRange by
/// translating the original \p Entries. The debug_range section
/// format is totally trivial, consisting just of pairs of address
/// sized addresses describing the ranges.
void DwarfStreamer::emitRangesEntries(
    int64_t UnitPcOffset, uint64_t OrigLowPc,
    FunctionIntervals::const_iterator FuncRange,
    const std::vector<DWARFDebugRangeList::RangeListEntry> &Entries,
    unsigned AddressSize) {
  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfRangesSection());

  // Offset each range by the right amount.
  int64_t PcOffset = Entries.empty() ? 0 : FuncRange.value() + UnitPcOffset;
  for (const auto &Range : Entries) {
    if (Range.isBaseAddressSelectionEntry(AddressSize)) {
      warn("unsupported base address selection operation",
           "emitting debug_ranges");
      break;
    }
    // Do not emit empty ranges.
    if (Range.StartAddress == Range.EndAddress)
      continue;

    // All range entries should lie in the function range.
    if (!(Range.StartAddress + OrigLowPc >= FuncRange.start() &&
          Range.EndAddress + OrigLowPc <= FuncRange.stop()))
      warn("inconsistent range data.", "emitting debug_ranges");
    MS->EmitIntValue(Range.StartAddress + PcOffset, AddressSize);
    MS->EmitIntValue(Range.EndAddress + PcOffset, AddressSize);
    RangesSectionSize += 2 * AddressSize;
  }

  // Add the terminator entry.
  MS->EmitIntValue(0, AddressSize);
  MS->EmitIntValue(0, AddressSize);
  RangesSectionSize += 2 * AddressSize;
}

/// \brief Emit the debug_aranges contribution of a unit and
/// if \p DoDebugRanges is true the debug_range contents for a
/// compile_unit level DW_AT_ranges attribute (Which are basically the
/// same thing with a different base address).
/// Just aggregate all the ranges gathered inside that unit.
void DwarfStreamer::emitUnitRangesEntries(CompileUnit &Unit,
                                          bool DoDebugRanges) {
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();
  // Gather the ranges in a vector, so that we can simplify them. The
  // IntervalMap will have coalesced the non-linked ranges, but here
  // we want to coalesce the linked addresses.
  std::vector<std::pair<uint64_t, uint64_t>> Ranges;
  const auto &FunctionRanges = Unit.getFunctionRanges();
  for (auto Range = FunctionRanges.begin(), End = FunctionRanges.end();
       Range != End; ++Range)
    Ranges.push_back(std::make_pair(Range.start() + Range.value(),
                                    Range.stop() + Range.value()));

  // The object addresses where sorted, but again, the linked
  // addresses might end up in a different order.
  std::sort(Ranges.begin(), Ranges.end());

  if (!Ranges.empty()) {
    MS->SwitchSection(MC->getObjectFileInfo()->getDwarfARangesSection());

    MCSymbol *BeginLabel = Asm->createTempSymbol("Barange");
    MCSymbol *EndLabel = Asm->createTempSymbol("Earange");

    unsigned HeaderSize =
        sizeof(int32_t) + // Size of contents (w/o this field
        sizeof(int16_t) + // DWARF ARange version number
        sizeof(int32_t) + // Offset of CU in the .debug_info section
        sizeof(int8_t) +  // Pointer Size (in bytes)
        sizeof(int8_t);   // Segment Size (in bytes)

    unsigned TupleSize = AddressSize * 2;
    unsigned Padding = OffsetToAlignment(HeaderSize, TupleSize);

    Asm->EmitLabelDifference(EndLabel, BeginLabel, 4); // Arange length
    Asm->OutStreamer->EmitLabel(BeginLabel);
    Asm->EmitInt16(dwarf::DW_ARANGES_VERSION); // Version number
    Asm->EmitInt32(Unit.getStartOffset());     // Corresponding unit's offset
    Asm->EmitInt8(AddressSize);                // Address size
    Asm->EmitInt8(0);                          // Segment size

    Asm->OutStreamer->EmitFill(Padding, 0x0);

    for (auto Range = Ranges.begin(), End = Ranges.end(); Range != End;
         ++Range) {
      uint64_t RangeStart = Range->first;
      MS->EmitIntValue(RangeStart, AddressSize);
      while ((Range + 1) != End && Range->second == (Range + 1)->first)
        ++Range;
      MS->EmitIntValue(Range->second - RangeStart, AddressSize);
    }

    // Emit terminator
    Asm->OutStreamer->EmitIntValue(0, AddressSize);
    Asm->OutStreamer->EmitIntValue(0, AddressSize);
    Asm->OutStreamer->EmitLabel(EndLabel);
  }

  if (!DoDebugRanges)
    return;

  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfRangesSection());
  // Offset each range by the right amount.
  int64_t PcOffset = -Unit.getLowPc();
  // Emit coalesced ranges.
  for (auto Range = Ranges.begin(), End = Ranges.end(); Range != End; ++Range) {
    MS->EmitIntValue(Range->first + PcOffset, AddressSize);
    while (Range + 1 != End && Range->second == (Range + 1)->first)
      ++Range;
    MS->EmitIntValue(Range->second + PcOffset, AddressSize);
    RangesSectionSize += 2 * AddressSize;
  }

  // Add the terminator entry.
  MS->EmitIntValue(0, AddressSize);
  MS->EmitIntValue(0, AddressSize);
  RangesSectionSize += 2 * AddressSize;
}

/// \brief Emit location lists for \p Unit and update attribtues to
/// point to the new entries.
void DwarfStreamer::emitLocationsForUnit(const CompileUnit &Unit,
                                         DWARFContext &Dwarf) {
  const auto &Attributes = Unit.getLocationAttributes();

  if (Attributes.empty())
    return;

  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfLocSection());

  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();
  const DWARFSection &InputSec = Dwarf.getLocSection();
  DataExtractor Data(InputSec.Data, Dwarf.isLittleEndian(), AddressSize);
  DWARFUnit &OrigUnit = Unit.getOrigUnit();
  const auto *OrigUnitDie = OrigUnit.getUnitDIE(false);
  int64_t UnitPcOffset = 0;
  uint64_t OrigLowPc = OrigUnitDie->getAttributeValueAsAddress(
      &OrigUnit, dwarf::DW_AT_low_pc, -1ULL);
  if (OrigLowPc != -1ULL)
    UnitPcOffset = int64_t(OrigLowPc) - Unit.getLowPc();

  for (const auto &Attr : Attributes) {
    uint32_t Offset = Attr.first.get();
    Attr.first.set(LocSectionSize);
    // This is the quantity to add to the old location address to get
    // the correct address for the new one.
    int64_t LocPcOffset = Attr.second + UnitPcOffset;
    while (Data.isValidOffset(Offset)) {
      uint64_t Low = Data.getUnsigned(&Offset, AddressSize);
      uint64_t High = Data.getUnsigned(&Offset, AddressSize);
      LocSectionSize += 2 * AddressSize;
      if (Low == 0 && High == 0) {
        Asm->OutStreamer->EmitIntValue(0, AddressSize);
        Asm->OutStreamer->EmitIntValue(0, AddressSize);
        break;
      }
      Asm->OutStreamer->EmitIntValue(Low + LocPcOffset, AddressSize);
      Asm->OutStreamer->EmitIntValue(High + LocPcOffset, AddressSize);
      uint64_t Length = Data.getU16(&Offset);
      Asm->OutStreamer->EmitIntValue(Length, 2);
      // Just copy the bytes over.
      Asm->OutStreamer->EmitBytes(
          StringRef(InputSec.Data.substr(Offset, Length)));
      Offset += Length;
      LocSectionSize += Length + 2;
    }
  }
}

void DwarfStreamer::emitLineTableForUnit(MCDwarfLineTableParams Params,
                                         StringRef PrologueBytes,
                                         unsigned MinInstLength,
                                         std::vector<DWARFDebugLine::Row> &Rows,
                                         unsigned PointerSize) {
  // Switch to the section where the table will be emitted into.
  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfLineSection());
  MCSymbol *LineStartSym = MC->createTempSymbol();
  MCSymbol *LineEndSym = MC->createTempSymbol();

  // The first 4 bytes is the total length of the information for this
  // compilation unit (not including these 4 bytes for the length).
  Asm->EmitLabelDifference(LineEndSym, LineStartSym, 4);
  Asm->OutStreamer->EmitLabel(LineStartSym);
  // Copy Prologue.
  MS->EmitBytes(PrologueBytes);
  LineSectionSize += PrologueBytes.size() + 4;

  SmallString<128> EncodingBuffer;
  raw_svector_ostream EncodingOS(EncodingBuffer);

  if (Rows.empty()) {
    // We only have the dummy entry, dsymutil emits an entry with a 0
    // address in that case.
    MCDwarfLineAddr::Encode(*MC, Params, INT64_MAX, 0, EncodingOS);
    MS->EmitBytes(EncodingOS.str());
    LineSectionSize += EncodingBuffer.size();
    MS->EmitLabel(LineEndSym);
    return;
  }

  // Line table state machine fields
  unsigned FileNum = 1;
  unsigned LastLine = 1;
  unsigned Column = 0;
  unsigned IsStatement = 1;
  unsigned Isa = 0;
  uint64_t Address = -1ULL;

  unsigned RowsSinceLastSequence = 0;

  for (unsigned Idx = 0; Idx < Rows.size(); ++Idx) {
    auto &Row = Rows[Idx];

    int64_t AddressDelta;
    if (Address == -1ULL) {
      MS->EmitIntValue(dwarf::DW_LNS_extended_op, 1);
      MS->EmitULEB128IntValue(PointerSize + 1);
      MS->EmitIntValue(dwarf::DW_LNE_set_address, 1);
      MS->EmitIntValue(Row.Address, PointerSize);
      LineSectionSize += 2 + PointerSize + getULEB128Size(PointerSize + 1);
      AddressDelta = 0;
    } else {
      AddressDelta = (Row.Address - Address) / MinInstLength;
    }

    // FIXME: code copied and transfromed from
    // MCDwarf.cpp::EmitDwarfLineTable. We should find a way to share
    // this code, but the current compatibility requirement with
    // classic dsymutil makes it hard. Revisit that once this
    // requirement is dropped.

    if (FileNum != Row.File) {
      FileNum = Row.File;
      MS->EmitIntValue(dwarf::DW_LNS_set_file, 1);
      MS->EmitULEB128IntValue(FileNum);
      LineSectionSize += 1 + getULEB128Size(FileNum);
    }
    if (Column != Row.Column) {
      Column = Row.Column;
      MS->EmitIntValue(dwarf::DW_LNS_set_column, 1);
      MS->EmitULEB128IntValue(Column);
      LineSectionSize += 1 + getULEB128Size(Column);
    }

    // FIXME: We should handle the discriminator here, but dsymutil
    // doesn' consider it, thus ignore it for now.

    if (Isa != Row.Isa) {
      Isa = Row.Isa;
      MS->EmitIntValue(dwarf::DW_LNS_set_isa, 1);
      MS->EmitULEB128IntValue(Isa);
      LineSectionSize += 1 + getULEB128Size(Isa);
    }
    if (IsStatement != Row.IsStmt) {
      IsStatement = Row.IsStmt;
      MS->EmitIntValue(dwarf::DW_LNS_negate_stmt, 1);
      LineSectionSize += 1;
    }
    if (Row.BasicBlock) {
      MS->EmitIntValue(dwarf::DW_LNS_set_basic_block, 1);
      LineSectionSize += 1;
    }

    if (Row.PrologueEnd) {
      MS->EmitIntValue(dwarf::DW_LNS_set_prologue_end, 1);
      LineSectionSize += 1;
    }

    if (Row.EpilogueBegin) {
      MS->EmitIntValue(dwarf::DW_LNS_set_epilogue_begin, 1);
      LineSectionSize += 1;
    }

    int64_t LineDelta = int64_t(Row.Line) - LastLine;
    if (!Row.EndSequence) {
      MCDwarfLineAddr::Encode(*MC, Params, LineDelta, AddressDelta, EncodingOS);
      MS->EmitBytes(EncodingOS.str());
      LineSectionSize += EncodingBuffer.size();
      EncodingBuffer.resize(0);
      Address = Row.Address;
      LastLine = Row.Line;
      RowsSinceLastSequence++;
    } else {
      if (LineDelta) {
        MS->EmitIntValue(dwarf::DW_LNS_advance_line, 1);
        MS->EmitSLEB128IntValue(LineDelta);
        LineSectionSize += 1 + getSLEB128Size(LineDelta);
      }
      if (AddressDelta) {
        MS->EmitIntValue(dwarf::DW_LNS_advance_pc, 1);
        MS->EmitULEB128IntValue(AddressDelta);
        LineSectionSize += 1 + getULEB128Size(AddressDelta);
      }
      MCDwarfLineAddr::Encode(*MC, Params, INT64_MAX, 0, EncodingOS);
      MS->EmitBytes(EncodingOS.str());
      LineSectionSize += EncodingBuffer.size();
      EncodingBuffer.resize(0);
      Address = -1ULL;
      LastLine = FileNum = IsStatement = 1;
      RowsSinceLastSequence = Column = Isa = 0;
    }
  }

  if (RowsSinceLastSequence) {
    MCDwarfLineAddr::Encode(*MC, Params, INT64_MAX, 0, EncodingOS);
    MS->EmitBytes(EncodingOS.str());
    LineSectionSize += EncodingBuffer.size();
    EncodingBuffer.resize(0);
  }

  MS->EmitLabel(LineEndSym);
}

/// \brief Emit the pubnames or pubtypes section contribution for \p
/// Unit into \p Sec. The data is provided in \p Names.
void DwarfStreamer::emitPubSectionForUnit(
    MCSection *Sec, StringRef SecName, const CompileUnit &Unit,
    const std::vector<CompileUnit::AccelInfo> &Names) {
  if (Names.empty())
    return;

  // Start the dwarf pubnames section.
  Asm->OutStreamer->SwitchSection(Sec);
  MCSymbol *BeginLabel = Asm->createTempSymbol("pub" + SecName + "_begin");
  MCSymbol *EndLabel = Asm->createTempSymbol("pub" + SecName + "_end");

  bool HeaderEmitted = false;
  // Emit the pubnames for this compilation unit.
  for (const auto &Name : Names) {
    if (Name.SkipPubSection)
      continue;

    if (!HeaderEmitted) {
      // Emit the header.
      Asm->EmitLabelDifference(EndLabel, BeginLabel, 4); // Length
      Asm->OutStreamer->EmitLabel(BeginLabel);
      Asm->EmitInt16(dwarf::DW_PUBNAMES_VERSION); // Version
      Asm->EmitInt32(Unit.getStartOffset());      // Unit offset
      Asm->EmitInt32(Unit.getNextUnitOffset() - Unit.getStartOffset()); // Size
      HeaderEmitted = true;
    }
    Asm->EmitInt32(Name.Die->getOffset());
    Asm->OutStreamer->EmitBytes(
        StringRef(Name.Name.data(), Name.Name.size() + 1));
  }

  if (!HeaderEmitted)
    return;
  Asm->EmitInt32(0); // End marker.
  Asm->OutStreamer->EmitLabel(EndLabel);
}

/// \brief Emit .debug_pubnames for \p Unit.
void DwarfStreamer::emitPubNamesForUnit(const CompileUnit &Unit) {
  emitPubSectionForUnit(MC->getObjectFileInfo()->getDwarfPubNamesSection(),
                        "names", Unit, Unit.getPubnames());
}

/// \brief Emit .debug_pubtypes for \p Unit.
void DwarfStreamer::emitPubTypesForUnit(const CompileUnit &Unit) {
  emitPubSectionForUnit(MC->getObjectFileInfo()->getDwarfPubTypesSection(),
                        "types", Unit, Unit.getPubtypes());
}

/// \brief Emit a CIE into the debug_frame section.
void DwarfStreamer::emitCIE(StringRef CIEBytes) {
  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfFrameSection());

  MS->EmitBytes(CIEBytes);
  FrameSectionSize += CIEBytes.size();
}

/// \brief Emit a FDE into the debug_frame section. \p FDEBytes
/// contains the FDE data without the length, CIE offset and address
/// which will be replaced with the paramter values.
void DwarfStreamer::emitFDE(uint32_t CIEOffset, uint32_t AddrSize,
                            uint32_t Address, StringRef FDEBytes) {
  MS->SwitchSection(MC->getObjectFileInfo()->getDwarfFrameSection());

  MS->EmitIntValue(FDEBytes.size() + 4 + AddrSize, 4);
  MS->EmitIntValue(CIEOffset, 4);
  MS->EmitIntValue(Address, AddrSize);
  MS->EmitBytes(FDEBytes);
  FrameSectionSize += FDEBytes.size() + 8 + AddrSize;
}

/// \brief The core of the Dwarf linking logic.
///
/// The link of the dwarf information from the object files will be
/// driven by the selection of 'root DIEs', which are DIEs that
/// describe variables or functions that are present in the linked
/// binary (and thus have entries in the debug map). All the debug
/// information that will be linked (the DIEs, but also the line
/// tables, ranges, ...) is derived from that set of root DIEs.
///
/// The root DIEs are identified because they contain relocations that
/// correspond to a debug map entry at specific places (the low_pc for
/// a function, the location for a variable). These relocations are
/// called ValidRelocs in the DwarfLinker and are gathered as a very
/// first step when we start processing a DebugMapObject.
class DwarfLinker {
public:
  DwarfLinker(StringRef OutputFilename, const LinkOptions &Options)
      : OutputFilename(OutputFilename), Options(Options),
        BinHolder(Options.Verbose), LastCIEOffset(0) {}

  /// \brief Link the contents of the DebugMap.
  bool link(const DebugMap &);

  void reportWarning(const Twine &Warning, const DWARFUnit *Unit = nullptr,
                     const DWARFDebugInfoEntryMinimal *DIE = nullptr) const;

private:
  /// \brief Called at the start of a debug object link.
  void startDebugObject(DWARFContext &, DebugMapObject &);

  /// \brief Called at the end of a debug object link.
  void endDebugObject();

  /// Keeps track of relocations.
  class RelocationManager {
    struct ValidReloc {
      uint32_t Offset;
      uint32_t Size;
      uint64_t Addend;
      const DebugMapObject::DebugMapEntry *Mapping;

      ValidReloc(uint32_t Offset, uint32_t Size, uint64_t Addend,
                 const DebugMapObject::DebugMapEntry *Mapping)
          : Offset(Offset), Size(Size), Addend(Addend), Mapping(Mapping) {}

      bool operator<(const ValidReloc &RHS) const {
        return Offset < RHS.Offset;
      }
    };

    DwarfLinker &Linker;

    /// \brief The valid relocations for the current DebugMapObject.
    /// This vector is sorted by relocation offset.
    std::vector<ValidReloc> ValidRelocs;

    /// \brief Index into ValidRelocs of the next relocation to
    /// consider. As we walk the DIEs in acsending file offset and as
    /// ValidRelocs is sorted by file offset, keeping this index
    /// uptodate is all we have to do to have a cheap lookup during the
    /// root DIE selection and during DIE cloning.
    unsigned NextValidReloc;

  public:
    RelocationManager(DwarfLinker &Linker)
        : Linker(Linker), NextValidReloc(0) {}

    bool hasValidRelocs() const { return !ValidRelocs.empty(); }
    /// \brief Reset the NextValidReloc counter.
    void resetValidRelocs() { NextValidReloc = 0; }

    /// \defgroup FindValidRelocations Translate debug map into a list
    /// of relevant relocations
    ///
    /// @{
    bool findValidRelocsInDebugInfo(const object::ObjectFile &Obj,
                                    const DebugMapObject &DMO);

    bool findValidRelocs(const object::SectionRef &Section,
                         const object::ObjectFile &Obj,
                         const DebugMapObject &DMO);

    void findValidRelocsMachO(const object::SectionRef &Section,
                              const object::MachOObjectFile &Obj,
                              const DebugMapObject &DMO);
    /// @}

    bool hasValidRelocation(uint32_t StartOffset, uint32_t EndOffset,
                            CompileUnit::DIEInfo &Info);

    bool applyValidRelocs(MutableArrayRef<char> Data, uint32_t BaseOffset,
                          bool isLittleEndian);
  };

  /// \defgroup FindRootDIEs Find DIEs corresponding to debug map entries.
  ///
  /// @{
  /// \brief Recursively walk the \p DIE tree and look for DIEs to
  /// keep. Store that information in \p CU's DIEInfo.
  void lookForDIEsToKeep(RelocationManager &RelocMgr,
                         const DWARFDebugInfoEntryMinimal &DIE,
                         const DebugMapObject &DMO, CompileUnit &CU,
                         unsigned Flags);

  /// If this compile unit is really a skeleton CU that points to a
  /// clang module, register it in ClangModules and return true.
  ///
  /// A skeleton CU is a CU without children, a DW_AT_gnu_dwo_name
  /// pointing to the module, and a DW_AT_gnu_dwo_id with the module
  /// hash.
  bool registerModuleReference(const DWARFDebugInfoEntryMinimal &CUDie,
                               const DWARFUnit &Unit, DebugMap &ModuleMap,
                               unsigned Indent = 0);

  /// Recursively add the debug info in this clang module .pcm
  /// file (and all the modules imported by it in a bottom-up fashion)
  /// to Units.
  void loadClangModule(StringRef Filename, StringRef ModulePath,
                       StringRef ModuleName, uint64_t DwoId,
                       DebugMap &ModuleMap, unsigned Indent = 0);

  /// \brief Flags passed to DwarfLinker::lookForDIEsToKeep
  enum TravesalFlags {
    TF_Keep = 1 << 0,            ///< Mark the traversed DIEs as kept.
    TF_InFunctionScope = 1 << 1, ///< Current scope is a fucntion scope.
    TF_DependencyWalk = 1 << 2,  ///< Walking the dependencies of a kept DIE.
    TF_ParentWalk = 1 << 3,      ///< Walking up the parents of a kept DIE.
    TF_ODR = 1 << 4,             ///< Use the ODR whhile keeping dependants.
    TF_SkipPC = 1 << 5,          ///< Skip all location attributes.
  };

  /// \brief Mark the passed DIE as well as all the ones it depends on
  /// as kept.
  void keepDIEAndDependencies(RelocationManager &RelocMgr,
                               const DWARFDebugInfoEntryMinimal &DIE,
                               CompileUnit::DIEInfo &MyInfo,
                               const DebugMapObject &DMO, CompileUnit &CU,
                               bool UseODR);

  unsigned shouldKeepDIE(RelocationManager &RelocMgr,
                         const DWARFDebugInfoEntryMinimal &DIE,
                         CompileUnit &Unit, CompileUnit::DIEInfo &MyInfo,
                         unsigned Flags);

  unsigned shouldKeepVariableDIE(RelocationManager &RelocMgr,
                                 const DWARFDebugInfoEntryMinimal &DIE,
                                 CompileUnit &Unit,
                                 CompileUnit::DIEInfo &MyInfo, unsigned Flags);

  unsigned shouldKeepSubprogramDIE(RelocationManager &RelocMgr,
                                   const DWARFDebugInfoEntryMinimal &DIE,
                                   CompileUnit &Unit,
                                   CompileUnit::DIEInfo &MyInfo,
                                   unsigned Flags);

  bool hasValidRelocation(uint32_t StartOffset, uint32_t EndOffset,
                          CompileUnit::DIEInfo &Info);
  /// @}

  /// \defgroup Linking Methods used to link the debug information
  ///
  /// @{

  class DIECloner {
    DwarfLinker &Linker;
    RelocationManager &RelocMgr;
    /// Allocator used for all the DIEValue objects.
    BumpPtrAllocator &DIEAlloc;
    MutableArrayRef<CompileUnit> CompileUnits;
    LinkOptions Options;

  public:
    DIECloner(DwarfLinker &Linker, RelocationManager &RelocMgr,
              BumpPtrAllocator &DIEAlloc,
              MutableArrayRef<CompileUnit> CompileUnits, LinkOptions &Options)
        : Linker(Linker), RelocMgr(RelocMgr), DIEAlloc(DIEAlloc),
          CompileUnits(CompileUnits), Options(Options) {}

    /// Recursively clone \p InputDIE into an tree of DIE objects
    /// where useless (as decided by lookForDIEsToKeep()) bits have been
    /// stripped out and addresses have been rewritten according to the
    /// debug map.
    ///
    /// \param OutOffset is the offset the cloned DIE in the output
    /// compile unit.
    /// \param PCOffset (while cloning a function scope) is the offset
    /// applied to the entry point of the function to get the linked address.
    ///
    /// \returns the root of the cloned tree or null if nothing was selected.
    DIE *cloneDIE(const DWARFDebugInfoEntryMinimal &InputDIE, CompileUnit &U,
                  int64_t PCOffset, uint32_t OutOffset, unsigned Flags);

    /// Construct the output DIE tree by cloning the DIEs we
    /// chose to keep above. If there are no valid relocs, then there's
    /// nothing to clone/emit.
    void cloneAllCompileUnits(DWARFContextInMemory &DwarfContext);

  private:
    typedef DWARFAbbreviationDeclaration::AttributeSpec AttributeSpec;

    /// Information gathered and exchanged between the various
    /// clone*Attributes helpers about the attributes of a particular DIE.
    struct AttributesInfo {
      const char *Name, *MangledName;         ///< Names.
      uint32_t NameOffset, MangledNameOffset; ///< Offsets in the string pool.

      uint64_t OrigLowPc;  ///< Value of AT_low_pc in the input DIE
      uint64_t OrigHighPc; ///< Value of AT_high_pc in the input DIE
      int64_t PCOffset; ///< Offset to apply to PC addresses inside a function.

      bool HasLowPc;      ///< Does the DIE have a low_pc attribute?
      bool IsDeclaration; ///< Is this DIE only a declaration?

      AttributesInfo()
          : Name(nullptr), MangledName(nullptr), NameOffset(0),
            MangledNameOffset(0), OrigLowPc(UINT64_MAX), OrigHighPc(0),
            PCOffset(0), HasLowPc(false), IsDeclaration(false) {}
    };

    /// Helper for cloneDIE.
    unsigned cloneAttribute(DIE &Die,
                            const DWARFDebugInfoEntryMinimal &InputDIE,
                            CompileUnit &U, const DWARFFormValue &Val,
                            const AttributeSpec AttrSpec, unsigned AttrSize,
                            AttributesInfo &AttrInfo);

    /// Clone a string attribute described by \p AttrSpec and add
    /// it to \p Die.
    /// \returns the size of the new attribute.
    unsigned cloneStringAttribute(DIE &Die, AttributeSpec AttrSpec,
                                  const DWARFFormValue &Val,
                                  const DWARFUnit &U);

    /// Clone an attribute referencing another DIE and add
    /// it to \p Die.
    /// \returns the size of the new attribute.
    unsigned
    cloneDieReferenceAttribute(DIE &Die,
                               const DWARFDebugInfoEntryMinimal &InputDIE,
                               AttributeSpec AttrSpec, unsigned AttrSize,
                               const DWARFFormValue &Val, CompileUnit &Unit);

    /// Clone an attribute referencing another DIE and add
    /// it to \p Die.
    /// \returns the size of the new attribute.
    unsigned cloneBlockAttribute(DIE &Die, AttributeSpec AttrSpec,
                                 const DWARFFormValue &Val, unsigned AttrSize);

    /// Clone an attribute referencing another DIE and add
    /// it to \p Die.
    /// \returns the size of the new attribute.
    unsigned cloneAddressAttribute(DIE &Die, AttributeSpec AttrSpec,
                                   const DWARFFormValue &Val,
                                   const CompileUnit &Unit,
                                   AttributesInfo &Info);

    /// Clone a scalar attribute  and add it to \p Die.
    /// \returns the size of the new attribute.
    unsigned cloneScalarAttribute(DIE &Die,
                                  const DWARFDebugInfoEntryMinimal &InputDIE,
                                  CompileUnit &U, AttributeSpec AttrSpec,
                                  const DWARFFormValue &Val, unsigned AttrSize,
                                  AttributesInfo &Info);

    /// Get the potential name and mangled name for the entity
    /// described by \p Die and store them in \Info if they are not
    /// already there.
    /// \returns is a name was found.
    bool getDIENames(const DWARFDebugInfoEntryMinimal &Die, DWARFUnit &U,
                     AttributesInfo &Info);

    /// Create a copy of abbreviation Abbrev.
    void copyAbbrev(const DWARFAbbreviationDeclaration &Abbrev, bool hasODR);
  };

  /// \brief Assign an abbreviation number to \p Abbrev
  void AssignAbbrev(DIEAbbrev &Abbrev);

  /// \brief FoldingSet that uniques the abbreviations.
  FoldingSet<DIEAbbrev> AbbreviationsSet;
  /// \brief Storage for the unique Abbreviations.
  /// This is passed to AsmPrinter::emitDwarfAbbrevs(), thus it cannot
  /// be changed to a vecot of unique_ptrs.
  std::vector<std::unique_ptr<DIEAbbrev>> Abbreviations;

  /// \brief Compute and emit debug_ranges section for \p Unit, and
  /// patch the attributes referencing it.
  void patchRangesForUnit(const CompileUnit &Unit, DWARFContext &Dwarf) const;

  /// \brief Generate and emit the DW_AT_ranges attribute for a
  /// compile_unit if it had one.
  void generateUnitRanges(CompileUnit &Unit) const;

  /// \brief Extract the line tables fromt he original dwarf, extract
  /// the relevant parts according to the linked function ranges and
  /// emit the result in the debug_line section.
  void patchLineTableForUnit(CompileUnit &Unit, DWARFContext &OrigDwarf);

  /// \brief Emit the accelerator entries for \p Unit.
  void emitAcceleratorEntriesForUnit(CompileUnit &Unit);

  /// \brief Patch the frame info for an object file and emit it.
  void patchFrameInfoForObject(const DebugMapObject &, DWARFContext &,
                               unsigned AddressSize);

  /// \brief DIELoc objects that need to be destructed (but not freed!).
  std::vector<DIELoc *> DIELocs;
  /// \brief DIEBlock objects that need to be destructed (but not freed!).
  std::vector<DIEBlock *> DIEBlocks;
  /// \brief Allocator used for all the DIEValue objects.
  BumpPtrAllocator DIEAlloc;
  /// @}

  /// ODR Contexts for that link.
  DeclContextTree ODRContexts;

  /// \defgroup Helpers Various helper methods.
  ///
  /// @{
  bool createStreamer(Triple TheTriple, StringRef OutputFilename);

  /// \brief Attempt to load a debug object from disk.
  ErrorOr<const object::ObjectFile &> loadObject(BinaryHolder &BinaryHolder,
                                                 DebugMapObject &Obj,
                                                 const DebugMap &Map);
  /// @}

  std::string OutputFilename;
  LinkOptions Options;
  BinaryHolder BinHolder;
  std::unique_ptr<DwarfStreamer> Streamer;
  uint64_t OutputDebugInfoSize;
  unsigned UnitID; ///< A unique ID that identifies each compile unit.

  /// The units of the current debug map object.
  std::vector<CompileUnit> Units;

  /// The debug map object currently under consideration.
  DebugMapObject *CurrentDebugObject;

  /// \brief The Dwarf string pool
  NonRelocatableStringpool StringPool;

  /// \brief This map is keyed by the entry PC of functions in that
  /// debug object and the associated value is a pair storing the
  /// corresponding end PC and the offset to apply to get the linked
  /// address.
  ///
  /// See startDebugObject() for a more complete description of its use.
  std::map<uint64_t, std::pair<uint64_t, int64_t>> Ranges;

  /// \brief The CIEs that have been emitted in the output
  /// section. The actual CIE data serves a the key to this StringMap,
  /// this takes care of comparing the semantics of CIEs defined in
  /// different object files.
  StringMap<uint32_t> EmittedCIEs;

  /// Offset of the last CIE that has been emitted in the output
  /// debug_frame section.
  uint32_t LastCIEOffset;

  /// Mapping the PCM filename to the DwoId.
  StringMap<uint64_t> ClangModules;
};

/// Similar to DWARFUnitSection::getUnitForOffset(), but returning our
/// CompileUnit object instead.
static CompileUnit *getUnitForOffset(MutableArrayRef<CompileUnit> Units,
                                     unsigned Offset) {
  auto CU =
      std::upper_bound(Units.begin(), Units.end(), Offset,
                       [](uint32_t LHS, const CompileUnit &RHS) {
                         return LHS < RHS.getOrigUnit().getNextUnitOffset();
                       });
  return CU != Units.end() ? &*CU : nullptr;
}

/// Resolve the DIE attribute reference that has been
/// extracted in \p RefValue. The resulting DIE migh be in another
/// CompileUnit which is stored into \p ReferencedCU.
/// \returns null if resolving fails for any reason.
static const DWARFDebugInfoEntryMinimal *resolveDIEReference(
    const DwarfLinker &Linker, MutableArrayRef<CompileUnit> Units,
    const DWARFFormValue &RefValue, const DWARFUnit &Unit,
    const DWARFDebugInfoEntryMinimal &DIE, CompileUnit *&RefCU) {
  assert(RefValue.isFormClass(DWARFFormValue::FC_Reference));
  uint64_t RefOffset = *RefValue.getAsReference(&Unit);

  if ((RefCU = getUnitForOffset(Units, RefOffset)))
    if (const auto *RefDie = RefCU->getOrigUnit().getDIEForOffset(RefOffset))
      return RefDie;

  Linker.reportWarning("could not find referenced DIE", &Unit, &DIE);
  return nullptr;
}

/// \returns whether the passed \a Attr type might contain a DIE
/// reference suitable for ODR uniquing.
static bool isODRAttribute(uint16_t Attr) {
  switch (Attr) {
  default:
    return false;
  case dwarf::DW_AT_type:
  case dwarf::DW_AT_containing_type:
  case dwarf::DW_AT_specification:
  case dwarf::DW_AT_abstract_origin:
  case dwarf::DW_AT_import:
    return true;
  }
  llvm_unreachable("Improper attribute.");
}

/// Set the last DIE/CU a context was seen in and, possibly invalidate
/// the context if it is ambiguous.
///
/// In the current implementation, we don't handle overloaded
/// functions well, because the argument types are not taken into
/// account when computing the DeclContext tree.
///
/// Some of this is mitigated byt using mangled names that do contain
/// the arguments types, but sometimes (eg. with function templates)
/// we don't have that. In that case, just do not unique anything that
/// refers to the contexts we are not able to distinguish.
///
/// If a context that is not a namespace appears twice in the same CU,
/// we know it is ambiguous. Make it invalid.
bool DeclContext::setLastSeenDIE(CompileUnit &U,
                                 const DWARFDebugInfoEntryMinimal *Die) {
  if (LastSeenCompileUnitID == U.getUniqueID()) {
    DWARFUnit &OrigUnit = U.getOrigUnit();
    uint32_t FirstIdx = OrigUnit.getDIEIndex(LastSeenDIE);
    U.getInfo(FirstIdx).Ctxt = nullptr;
    return false;
  }

  LastSeenCompileUnitID = U.getUniqueID();
  LastSeenDIE = Die;
  return true;
}

PointerIntPair<DeclContext *, 1> DeclContextTree::getChildDeclContext(
    DeclContext &Context, const DWARFDebugInfoEntryMinimal *DIE, CompileUnit &U,
    NonRelocatableStringpool &StringPool, bool InClangModule) {
  unsigned Tag = DIE->getTag();

  // FIXME: dsymutil-classic compat: We should bail out here if we
  // have a specification or an abstract_origin. We will get the
  // parent context wrong here.

  switch (Tag) {
  default:
    // By default stop gathering child contexts.
    return PointerIntPair<DeclContext *, 1>(nullptr);
  case dwarf::DW_TAG_module:
    break;
  case dwarf::DW_TAG_compile_unit:
    return PointerIntPair<DeclContext *, 1>(&Context);
  case dwarf::DW_TAG_subprogram:
    // Do not unique anything inside CU local functions.
    if ((Context.getTag() == dwarf::DW_TAG_namespace ||
         Context.getTag() == dwarf::DW_TAG_compile_unit) &&
        !DIE->getAttributeValueAsUnsignedConstant(&U.getOrigUnit(),
                                                  dwarf::DW_AT_external, 0))
      return PointerIntPair<DeclContext *, 1>(nullptr);
  // Fallthrough
  case dwarf::DW_TAG_member:
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_typedef:
    // Artificial things might be ambiguous, because they might be
    // created on demand. For example implicitely defined constructors
    // are ambiguous because of the way we identify contexts, and they
    // won't be generated everytime everywhere.
    if (DIE->getAttributeValueAsUnsignedConstant(&U.getOrigUnit(),
                                                 dwarf::DW_AT_artificial, 0))
      return PointerIntPair<DeclContext *, 1>(nullptr);
    break;
  }

  const char *Name = DIE->getName(&U.getOrigUnit(), DINameKind::LinkageName);
  const char *ShortName = DIE->getName(&U.getOrigUnit(), DINameKind::ShortName);
  StringRef NameRef;
  StringRef ShortNameRef;
  StringRef FileRef;

  if (Name)
    NameRef = StringPool.internString(Name);
  else if (Tag == dwarf::DW_TAG_namespace)
    // FIXME: For dsymutil-classic compatibility. I think uniquing
    // within anonymous namespaces is wrong. There is no ODR guarantee
    // there.
    NameRef = StringPool.internString("(anonymous namespace)");

  if (ShortName && ShortName != Name)
    ShortNameRef = StringPool.internString(ShortName);
  else
    ShortNameRef = NameRef;

  if (Tag != dwarf::DW_TAG_class_type && Tag != dwarf::DW_TAG_structure_type &&
      Tag != dwarf::DW_TAG_union_type &&
      Tag != dwarf::DW_TAG_enumeration_type && NameRef.empty())
    return PointerIntPair<DeclContext *, 1>(nullptr);

  std::string File;
  unsigned Line = 0;
  unsigned ByteSize = UINT32_MAX;

  if (!InClangModule) {
    // Gather some discriminating data about the DeclContext we will be
    // creating: File, line number and byte size. This shouldn't be
    // necessary, because the ODR is just about names, but given that we
    // do some approximations with overloaded functions and anonymous
    // namespaces, use these additional data points to make the process
    // safer.  This is disabled for clang modules, because forward
    // declarations of module-defined types do not have a file and line.
    ByteSize = DIE->getAttributeValueAsUnsignedConstant(
        &U.getOrigUnit(), dwarf::DW_AT_byte_size, UINT64_MAX);
    if (Tag != dwarf::DW_TAG_namespace || !Name) {
      if (unsigned FileNum = DIE->getAttributeValueAsUnsignedConstant(
              &U.getOrigUnit(), dwarf::DW_AT_decl_file, 0)) {
        if (const auto *LT = U.getOrigUnit().getContext().getLineTableForUnit(
                &U.getOrigUnit())) {
          // FIXME: dsymutil-classic compatibility. I'd rather not
          // unique anything in anonymous namespaces, but if we do, then
          // verify that the file and line correspond.
          if (!Name && Tag == dwarf::DW_TAG_namespace)
            FileNum = 1;

          // FIXME: Passing U.getOrigUnit().getCompilationDir()
          // instead of "" would allow more uniquing, but for now, do
          // it this way to match dsymutil-classic.
          if (LT->getFileNameByIndex(
                  FileNum, "",
                  DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                  File)) {
            Line = DIE->getAttributeValueAsUnsignedConstant(
                &U.getOrigUnit(), dwarf::DW_AT_decl_line, 0);
#ifdef HAVE_REALPATH
            // Cache the resolved paths, because calling realpath is expansive.
            if (const char *ResolvedPath = U.getResolvedPath(FileNum)) {
              File = ResolvedPath;
            } else {
              char RealPath[PATH_MAX + 1];
              RealPath[PATH_MAX] = 0;
              if (::realpath(File.c_str(), RealPath))
                File = RealPath;
              U.setResolvedPath(FileNum, File);
            }
#endif
            FileRef = StringPool.internString(File);
          }
        }
      }
    }
  }

  if (!Line && NameRef.empty())
    return PointerIntPair<DeclContext *, 1>(nullptr);

  // We hash NameRef, which is the mangled name, in order to get most
  // overloaded functions resolve correctly.
  //
  // Strictly speaking, hashing the Tag is only necessary for a
  // DW_TAG_module, to prevent uniquing of a module and a namespace
  // with the same name.
  //
  // FIXME: dsymutil-classic won't unique the same type presented
  // once as a struct and once as a class. Using the Tag in the fully
  // qualified name hash to get the same effect.
  unsigned Hash = hash_combine(Context.getQualifiedNameHash(), Tag, NameRef);

  // FIXME: dsymutil-classic compatibility: when we don't have a name,
  // use the filename.
  if (Tag == dwarf::DW_TAG_namespace && NameRef == "(anonymous namespace)")
    Hash = hash_combine(Hash, FileRef);

  // Now look if this context already exists.
  DeclContext Key(Hash, Line, ByteSize, Tag, NameRef, FileRef, Context);
  auto ContextIter = Contexts.find(&Key);

  if (ContextIter == Contexts.end()) {
    // The context wasn't found.
    bool Inserted;
    DeclContext *NewContext =
        new (Allocator) DeclContext(Hash, Line, ByteSize, Tag, NameRef, FileRef,
                                    Context, DIE, U.getUniqueID());
    std::tie(ContextIter, Inserted) = Contexts.insert(NewContext);
    assert(Inserted && "Failed to insert DeclContext");
    (void)Inserted;
  } else if (Tag != dwarf::DW_TAG_namespace &&
             !(*ContextIter)->setLastSeenDIE(U, DIE)) {
    // The context was found, but it is ambiguous with another context
    // in the same file. Mark it invalid.
    return PointerIntPair<DeclContext *, 1>(*ContextIter, /* Invalid= */ 1);
  }

  assert(ContextIter != Contexts.end());
  // FIXME: dsymutil-classic compatibility. Union types aren't
  // uniques, but their children might be.
  if ((Tag == dwarf::DW_TAG_subprogram &&
       Context.getTag() != dwarf::DW_TAG_structure_type &&
       Context.getTag() != dwarf::DW_TAG_class_type) ||
      (Tag == dwarf::DW_TAG_union_type))
    return PointerIntPair<DeclContext *, 1>(*ContextIter, /* Invalid= */ 1);

  return PointerIntPair<DeclContext *, 1>(*ContextIter);
}

bool DwarfLinker::DIECloner::getDIENames(const DWARFDebugInfoEntryMinimal &Die,
                                         DWARFUnit &U, AttributesInfo &Info) {
  // FIXME: a bit wasteful as the first getName might return the
  // short name.
  if (!Info.MangledName &&
      (Info.MangledName = Die.getName(&U, DINameKind::LinkageName)))
    Info.MangledNameOffset =
        Linker.StringPool.getStringOffset(Info.MangledName);

  if (!Info.Name && (Info.Name = Die.getName(&U, DINameKind::ShortName)))
    Info.NameOffset = Linker.StringPool.getStringOffset(Info.Name);

  return Info.Name || Info.MangledName;
}

/// \brief Report a warning to the user, optionaly including
/// information about a specific \p DIE related to the warning.
void DwarfLinker::reportWarning(const Twine &Warning, const DWARFUnit *Unit,
                                const DWARFDebugInfoEntryMinimal *DIE) const {
  StringRef Context = "<debug map>";
  if (CurrentDebugObject)
    Context = CurrentDebugObject->getObjectFilename();
  warn(Warning, Context);

  if (!Options.Verbose || !DIE)
    return;

  errs() << "    in DIE:\n";
  DIE->dump(errs(), const_cast<DWARFUnit *>(Unit), 0 /* RecurseDepth */,
            6 /* Indent */);
}

bool DwarfLinker::createStreamer(Triple TheTriple, StringRef OutputFilename) {
  if (Options.NoOutput)
    return true;

  Streamer = llvm::make_unique<DwarfStreamer>();
  return Streamer->init(TheTriple, OutputFilename);
}

/// Recursive helper to build the global DeclContext information and
/// gather the child->parent relationships in the original compile unit.
///
/// \return true when this DIE and all of its children are only
/// forward declarations to types defined in external clang modules
/// (i.e., forward declarations that are children of a DW_TAG_module).
static bool analyzeContextInfo(const DWARFDebugInfoEntryMinimal *DIE,
                               unsigned ParentIdx, CompileUnit &CU,
                               DeclContext *CurrentDeclContext,
                               NonRelocatableStringpool &StringPool,
                               DeclContextTree &Contexts,
                               bool InImportedModule = false) {
  unsigned MyIdx = CU.getOrigUnit().getDIEIndex(DIE);
  CompileUnit::DIEInfo &Info = CU.getInfo(MyIdx);

  // Clang imposes an ODR on modules(!) regardless of the language:
  //  "The module-id should consist of only a single identifier,
  //   which provides the name of the module being defined. Each
  //   module shall have a single definition."
  //
  // This does not extend to the types inside the modules:
  //  "[I]n C, this implies that if two structs are defined in
  //   different submodules with the same name, those two types are
  //   distinct types (but may be compatible types if their
  //   definitions match)."
  //
  // We treat non-C++ modules like namespaces for this reason.
  if (DIE->getTag() == dwarf::DW_TAG_module && ParentIdx == 0 &&
      DIE->getAttributeValueAsString(&CU.getOrigUnit(), dwarf::DW_AT_name,
                                     "") != CU.getClangModuleName()) {
    InImportedModule = true;
  }

  Info.ParentIdx = ParentIdx;
  bool InClangModule = CU.isClangModule() || InImportedModule;
  if (CU.hasODR() || InClangModule) {
    if (CurrentDeclContext) {
      auto PtrInvalidPair = Contexts.getChildDeclContext(
          *CurrentDeclContext, DIE, CU, StringPool, InClangModule);
      CurrentDeclContext = PtrInvalidPair.getPointer();
      Info.Ctxt =
          PtrInvalidPair.getInt() ? nullptr : PtrInvalidPair.getPointer();
    } else
      Info.Ctxt = CurrentDeclContext = nullptr;
  }

  Info.Prune = InImportedModule;
  if (DIE->hasChildren())
    for (auto *Child = DIE->getFirstChild(); Child && !Child->isNULL();
         Child = Child->getSibling())
      Info.Prune &= analyzeContextInfo(Child, MyIdx, CU, CurrentDeclContext,
                                       StringPool, Contexts, InImportedModule);

  // Prune this DIE if it is either a forward declaration inside a
  // DW_TAG_module or a DW_TAG_module that contains nothing but
  // forward declarations.
  Info.Prune &= (DIE->getTag() == dwarf::DW_TAG_module) ||
                DIE->getAttributeValueAsUnsignedConstant(
                    &CU.getOrigUnit(), dwarf::DW_AT_declaration, 0);

  // Don't prune it if there is no definition for the DIE.
  Info.Prune &= Info.Ctxt && Info.Ctxt->getCanonicalDIEOffset();

  return Info.Prune;
}

static bool dieNeedsChildrenToBeMeaningful(uint32_t Tag) {
  switch (Tag) {
  default:
    return false;
  case dwarf::DW_TAG_subprogram:
  case dwarf::DW_TAG_lexical_block:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_union_type:
    return true;
  }
  llvm_unreachable("Invalid Tag");
}

static unsigned getRefAddrSize(const DWARFUnit &U) {
  if (U.getVersion() == 2)
    return U.getAddressByteSize();
  return 4;
}

void DwarfLinker::startDebugObject(DWARFContext &Dwarf, DebugMapObject &Obj) {
  Units.reserve(Dwarf.getNumCompileUnits());
  // Iterate over the debug map entries and put all the ones that are
  // functions (because they have a size) into the Ranges map. This
  // map is very similar to the FunctionRanges that are stored in each
  // unit, with 2 notable differences:
  //  - obviously this one is global, while the other ones are per-unit.
  //  - this one contains not only the functions described in the DIE
  // tree, but also the ones that are only in the debug map.
  // The latter information is required to reproduce dsymutil's logic
  // while linking line tables. The cases where this information
  // matters look like bugs that need to be investigated, but for now
  // we need to reproduce dsymutil's behavior.
  // FIXME: Once we understood exactly if that information is needed,
  // maybe totally remove this (or try to use it to do a real
  // -gline-tables-only on Darwin.
  for (const auto &Entry : Obj.symbols()) {
    const auto &Mapping = Entry.getValue();
    if (Mapping.Size)
      Ranges[Mapping.ObjectAddress] = std::make_pair(
          Mapping.ObjectAddress + Mapping.Size,
          int64_t(Mapping.BinaryAddress) - Mapping.ObjectAddress);
  }
}

void DwarfLinker::endDebugObject() {
  Units.clear();
  Ranges.clear();

  for (auto I = DIEBlocks.begin(), E = DIEBlocks.end(); I != E; ++I)
    (*I)->~DIEBlock();
  for (auto I = DIELocs.begin(), E = DIELocs.end(); I != E; ++I)
    (*I)->~DIELoc();

  DIEBlocks.clear();
  DIELocs.clear();
  DIEAlloc.Reset();
}

/// \brief Iterate over the relocations of the given \p Section and
/// store the ones that correspond to debug map entries into the
/// ValidRelocs array.
void DwarfLinker::RelocationManager::
findValidRelocsMachO(const object::SectionRef &Section,
                     const object::MachOObjectFile &Obj,
                     const DebugMapObject &DMO) {
  StringRef Contents;
  Section.getContents(Contents);
  DataExtractor Data(Contents, Obj.isLittleEndian(), 0);

  for (const object::RelocationRef &Reloc : Section.relocations()) {
    object::DataRefImpl RelocDataRef = Reloc.getRawDataRefImpl();
    MachO::any_relocation_info MachOReloc = Obj.getRelocation(RelocDataRef);
    unsigned RelocSize = 1 << Obj.getAnyRelocationLength(MachOReloc);
    uint64_t Offset64 = Reloc.getOffset();
    if ((RelocSize != 4 && RelocSize != 8)) {
      Linker.reportWarning(" unsupported relocation in debug_info section.");
      continue;
    }
    uint32_t Offset = Offset64;
    // Mach-o uses REL relocations, the addend is at the relocation offset.
    uint64_t Addend = Data.getUnsigned(&Offset, RelocSize);

    auto Sym = Reloc.getSymbol();
    if (Sym != Obj.symbol_end()) {
      ErrorOr<StringRef> SymbolName = Sym->getName();
      if (!SymbolName) {
        Linker.reportWarning("error getting relocation symbol name.");
        continue;
      }
      if (const auto *Mapping = DMO.lookupSymbol(*SymbolName))
        ValidRelocs.emplace_back(Offset64, RelocSize, Addend, Mapping);
    } else if (const auto *Mapping = DMO.lookupObjectAddress(Addend)) {
      // Do not store the addend. The addend was the address of the
      // symbol in the object file, the address in the binary that is
      // stored in the debug map doesn't need to be offseted.
      ValidRelocs.emplace_back(Offset64, RelocSize, 0, Mapping);
    }
  }
}

/// \brief Dispatch the valid relocation finding logic to the
/// appropriate handler depending on the object file format.
bool DwarfLinker::RelocationManager::findValidRelocs(
    const object::SectionRef &Section, const object::ObjectFile &Obj,
    const DebugMapObject &DMO) {
  // Dispatch to the right handler depending on the file type.
  if (auto *MachOObj = dyn_cast<object::MachOObjectFile>(&Obj))
    findValidRelocsMachO(Section, *MachOObj, DMO);
  else
    Linker.reportWarning(Twine("unsupported object file type: ") +
                         Obj.getFileName());

  if (ValidRelocs.empty())
    return false;

  // Sort the relocations by offset. We will walk the DIEs linearly in
  // the file, this allows us to just keep an index in the relocation
  // array that we advance during our walk, rather than resorting to
  // some associative container. See DwarfLinker::NextValidReloc.
  std::sort(ValidRelocs.begin(), ValidRelocs.end());
  return true;
}

/// \brief Look for relocations in the debug_info section that match
/// entries in the debug map. These relocations will drive the Dwarf
/// link by indicating which DIEs refer to symbols present in the
/// linked binary.
/// \returns wether there are any valid relocations in the debug info.
bool DwarfLinker::RelocationManager::
findValidRelocsInDebugInfo(const object::ObjectFile &Obj,
                           const DebugMapObject &DMO) {
  // Find the debug_info section.
  for (const object::SectionRef &Section : Obj.sections()) {
    StringRef SectionName;
    Section.getName(SectionName);
    SectionName = SectionName.substr(SectionName.find_first_not_of("._"));
    if (SectionName != "debug_info")
      continue;
    return findValidRelocs(Section, Obj, DMO);
  }
  return false;
}

/// \brief Checks that there is a relocation against an actual debug
/// map entry between \p StartOffset and \p NextOffset.
///
/// This function must be called with offsets in strictly ascending
/// order because it never looks back at relocations it already 'went past'.
/// \returns true and sets Info.InDebugMap if it is the case.
bool DwarfLinker::RelocationManager::
hasValidRelocation(uint32_t StartOffset, uint32_t EndOffset,
                   CompileUnit::DIEInfo &Info) {
  assert(NextValidReloc == 0 ||
         StartOffset > ValidRelocs[NextValidReloc - 1].Offset);
  if (NextValidReloc >= ValidRelocs.size())
    return false;

  uint64_t RelocOffset = ValidRelocs[NextValidReloc].Offset;

  // We might need to skip some relocs that we didn't consider. For
  // example the high_pc of a discarded DIE might contain a reloc that
  // is in the list because it actually corresponds to the start of a
  // function that is in the debug map.
  while (RelocOffset < StartOffset && NextValidReloc < ValidRelocs.size() - 1)
    RelocOffset = ValidRelocs[++NextValidReloc].Offset;

  if (RelocOffset < StartOffset || RelocOffset >= EndOffset)
    return false;

  const auto &ValidReloc = ValidRelocs[NextValidReloc++];
  const auto &Mapping = ValidReloc.Mapping->getValue();
  if (Linker.Options.Verbose)
    outs() << "Found valid debug map entry: " << ValidReloc.Mapping->getKey()
           << " " << format("\t%016" PRIx64 " => %016" PRIx64,
                            uint64_t(Mapping.ObjectAddress),
                            uint64_t(Mapping.BinaryAddress));

  Info.AddrAdjust = int64_t(Mapping.BinaryAddress) + ValidReloc.Addend -
                    Mapping.ObjectAddress;
  Info.InDebugMap = true;
  return true;
}

/// \brief Get the starting and ending (exclusive) offset for the
/// attribute with index \p Idx descibed by \p Abbrev. \p Offset is
/// supposed to point to the position of the first attribute described
/// by \p Abbrev.
/// \return [StartOffset, EndOffset) as a pair.
static std::pair<uint32_t, uint32_t>
getAttributeOffsets(const DWARFAbbreviationDeclaration *Abbrev, unsigned Idx,
                    unsigned Offset, const DWARFUnit &Unit) {
  DataExtractor Data = Unit.getDebugInfoExtractor();

  for (unsigned i = 0; i < Idx; ++i)
    DWARFFormValue::skipValue(Abbrev->getFormByIndex(i), Data, &Offset, &Unit);

  uint32_t End = Offset;
  DWARFFormValue::skipValue(Abbrev->getFormByIndex(Idx), Data, &End, &Unit);

  return std::make_pair(Offset, End);
}

/// \brief Check if a variable describing DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DwarfLinker::shouldKeepVariableDIE(RelocationManager &RelocMgr,
                                            const DWARFDebugInfoEntryMinimal &DIE,
                                            CompileUnit &Unit,
                                            CompileUnit::DIEInfo &MyInfo,
                                            unsigned Flags) {
  const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

  // Global variables with constant value can always be kept.
  if (!(Flags & TF_InFunctionScope) &&
      Abbrev->findAttributeIndex(dwarf::DW_AT_const_value) != -1U) {
    MyInfo.InDebugMap = true;
    return Flags | TF_Keep;
  }

  uint32_t LocationIdx = Abbrev->findAttributeIndex(dwarf::DW_AT_location);
  if (LocationIdx == -1U)
    return Flags;

  uint32_t Offset = DIE.getOffset() + getULEB128Size(Abbrev->getCode());
  const DWARFUnit &OrigUnit = Unit.getOrigUnit();
  uint32_t LocationOffset, LocationEndOffset;
  std::tie(LocationOffset, LocationEndOffset) =
      getAttributeOffsets(Abbrev, LocationIdx, Offset, OrigUnit);

  // See if there is a relocation to a valid debug map entry inside
  // this variable's location. The order is important here. We want to
  // always check in the variable has a valid relocation, so that the
  // DIEInfo is filled. However, we don't want a static variable in a
  // function to force us to keep the enclosing function.
  if (!RelocMgr.hasValidRelocation(LocationOffset, LocationEndOffset, MyInfo) ||
      (Flags & TF_InFunctionScope))
    return Flags;

  if (Options.Verbose)
    DIE.dump(outs(), const_cast<DWARFUnit *>(&OrigUnit), 0, 8 /* Indent */);

  return Flags | TF_Keep;
}

/// \brief Check if a function describing DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DwarfLinker::shouldKeepSubprogramDIE(
    RelocationManager &RelocMgr,
    const DWARFDebugInfoEntryMinimal &DIE, CompileUnit &Unit,
    CompileUnit::DIEInfo &MyInfo, unsigned Flags) {
  const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

  Flags |= TF_InFunctionScope;

  uint32_t LowPcIdx = Abbrev->findAttributeIndex(dwarf::DW_AT_low_pc);
  if (LowPcIdx == -1U)
    return Flags;

  uint32_t Offset = DIE.getOffset() + getULEB128Size(Abbrev->getCode());
  const DWARFUnit &OrigUnit = Unit.getOrigUnit();
  uint32_t LowPcOffset, LowPcEndOffset;
  std::tie(LowPcOffset, LowPcEndOffset) =
      getAttributeOffsets(Abbrev, LowPcIdx, Offset, OrigUnit);

  uint64_t LowPc =
      DIE.getAttributeValueAsAddress(&OrigUnit, dwarf::DW_AT_low_pc, -1ULL);
  assert(LowPc != -1ULL && "low_pc attribute is not an address.");
  if (LowPc == -1ULL ||
      !RelocMgr.hasValidRelocation(LowPcOffset, LowPcEndOffset, MyInfo))
    return Flags;

  if (Options.Verbose)
    DIE.dump(outs(), const_cast<DWARFUnit *>(&OrigUnit), 0, 8 /* Indent */);

  Flags |= TF_Keep;

  DWARFFormValue HighPcValue;
  if (!DIE.getAttributeValue(&OrigUnit, dwarf::DW_AT_high_pc, HighPcValue)) {
    reportWarning("Function without high_pc. Range will be discarded.\n",
                  &OrigUnit, &DIE);
    return Flags;
  }

  uint64_t HighPc;
  if (HighPcValue.isFormClass(DWARFFormValue::FC_Address)) {
    HighPc = *HighPcValue.getAsAddress(&OrigUnit);
  } else {
    assert(HighPcValue.isFormClass(DWARFFormValue::FC_Constant));
    HighPc = LowPc + *HighPcValue.getAsUnsignedConstant();
  }

  // Replace the debug map range with a more accurate one.
  Ranges[LowPc] = std::make_pair(HighPc, MyInfo.AddrAdjust);
  Unit.addFunctionRange(LowPc, HighPc, MyInfo.AddrAdjust);
  return Flags;
}

/// \brief Check if a DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DwarfLinker::shouldKeepDIE(RelocationManager &RelocMgr,
                                    const DWARFDebugInfoEntryMinimal &DIE,
                                    CompileUnit &Unit,
                                    CompileUnit::DIEInfo &MyInfo,
                                    unsigned Flags) {
  switch (DIE.getTag()) {
  case dwarf::DW_TAG_constant:
  case dwarf::DW_TAG_variable:
    return shouldKeepVariableDIE(RelocMgr, DIE, Unit, MyInfo, Flags);
  case dwarf::DW_TAG_subprogram:
    return shouldKeepSubprogramDIE(RelocMgr, DIE, Unit, MyInfo, Flags);
  case dwarf::DW_TAG_module:
  case dwarf::DW_TAG_imported_module:
  case dwarf::DW_TAG_imported_declaration:
  case dwarf::DW_TAG_imported_unit:
    // We always want to keep these.
    return Flags | TF_Keep;
  }

  return Flags;
}

/// \brief Mark the passed DIE as well as all the ones it depends on
/// as kept.
///
/// This function is called by lookForDIEsToKeep on DIEs that are
/// newly discovered to be needed in the link. It recursively calls
/// back to lookForDIEsToKeep while adding TF_DependencyWalk to the
/// TraversalFlags to inform it that it's not doing the primary DIE
/// tree walk.
void DwarfLinker::keepDIEAndDependencies(RelocationManager &RelocMgr,
                                          const DWARFDebugInfoEntryMinimal &Die,
                                          CompileUnit::DIEInfo &MyInfo,
                                          const DebugMapObject &DMO,
                                          CompileUnit &CU, bool UseODR) {
  const DWARFUnit &Unit = CU.getOrigUnit();
  MyInfo.Keep = true;

  // First mark all the parent chain as kept.
  unsigned AncestorIdx = MyInfo.ParentIdx;
  while (!CU.getInfo(AncestorIdx).Keep) {
    unsigned ODRFlag = UseODR ? TF_ODR : 0;
    lookForDIEsToKeep(RelocMgr, *Unit.getDIEAtIndex(AncestorIdx), DMO, CU,
                      TF_ParentWalk | TF_Keep | TF_DependencyWalk | ODRFlag);
    AncestorIdx = CU.getInfo(AncestorIdx).ParentIdx;
  }

  // Then we need to mark all the DIEs referenced by this DIE's
  // attributes as kept.
  DataExtractor Data = Unit.getDebugInfoExtractor();
  const auto *Abbrev = Die.getAbbreviationDeclarationPtr();
  uint32_t Offset = Die.getOffset() + getULEB128Size(Abbrev->getCode());

  // Mark all DIEs referenced through atttributes as kept.
  for (const auto &AttrSpec : Abbrev->attributes()) {
    DWARFFormValue Val(AttrSpec.Form);

    if (!Val.isFormClass(DWARFFormValue::FC_Reference)) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset, &Unit);
      continue;
    }

    Val.extractValue(Data, &Offset, &Unit);
    CompileUnit *ReferencedCU;
    if (const auto *RefDIE =
            resolveDIEReference(*this, MutableArrayRef<CompileUnit>(Units), Val,
                                Unit, Die, ReferencedCU)) {
      uint32_t RefIdx = ReferencedCU->getOrigUnit().getDIEIndex(RefDIE);
      CompileUnit::DIEInfo &Info = ReferencedCU->getInfo(RefIdx);
      // If the referenced DIE has a DeclContext that has already been
      // emitted, then do not keep the one in this CU. We'll link to
      // the canonical DIE in cloneDieReferenceAttribute.
      // FIXME: compatibility with dsymutil-classic. UseODR shouldn't
      // be necessary and could be advantageously replaced by
      // ReferencedCU->hasODR() && CU.hasODR().
      // FIXME: compatibility with dsymutil-classic. There is no
      // reason not to unique ref_addr references.
      if (AttrSpec.Form != dwarf::DW_FORM_ref_addr && UseODR && Info.Ctxt &&
          Info.Ctxt != ReferencedCU->getInfo(Info.ParentIdx).Ctxt &&
          Info.Ctxt->getCanonicalDIEOffset() && isODRAttribute(AttrSpec.Attr))
        continue;

      // Keep a module forward declaration if there is no definition.
      if (!(isODRAttribute(AttrSpec.Attr) && Info.Ctxt &&
            Info.Ctxt->getCanonicalDIEOffset()))
        Info.Prune = false;

      unsigned ODRFlag = UseODR ? TF_ODR : 0;
      lookForDIEsToKeep(RelocMgr, *RefDIE, DMO, *ReferencedCU,
                        TF_Keep | TF_DependencyWalk | ODRFlag);
    }
  }
}

/// \brief Recursively walk the \p DIE tree and look for DIEs to
/// keep. Store that information in \p CU's DIEInfo.
///
/// This function is the entry point of the DIE selection
/// algorithm. It is expected to walk the DIE tree in file order and
/// (though the mediation of its helper) call hasValidRelocation() on
/// each DIE that might be a 'root DIE' (See DwarfLinker class
/// comment).
/// While walking the dependencies of root DIEs, this function is
/// also called, but during these dependency walks the file order is
/// not respected. The TF_DependencyWalk flag tells us which kind of
/// traversal we are currently doing.
void DwarfLinker::lookForDIEsToKeep(RelocationManager &RelocMgr,
                                    const DWARFDebugInfoEntryMinimal &Die,
                                    const DebugMapObject &DMO, CompileUnit &CU,
                                    unsigned Flags) {
  unsigned Idx = CU.getOrigUnit().getDIEIndex(&Die);
  CompileUnit::DIEInfo &MyInfo = CU.getInfo(Idx);
  bool AlreadyKept = MyInfo.Keep;
  if (MyInfo.Prune)
    return;

  // If the Keep flag is set, we are marking a required DIE's
  // dependencies. If our target is already marked as kept, we're all
  // set.
  if ((Flags & TF_DependencyWalk) && AlreadyKept)
    return;

  // We must not call shouldKeepDIE while called from keepDIEAndDependencies,
  // because it would screw up the relocation finding logic.
  if (!(Flags & TF_DependencyWalk))
    Flags = shouldKeepDIE(RelocMgr, Die, CU, MyInfo, Flags);

  // If it is a newly kept DIE mark it as well as all its dependencies as kept.
  if (!AlreadyKept && (Flags & TF_Keep)) {
    bool UseOdr = (Flags & TF_DependencyWalk) ? (Flags & TF_ODR) : CU.hasODR();
    keepDIEAndDependencies(RelocMgr, Die, MyInfo, DMO, CU, UseOdr);
  }
  // The TF_ParentWalk flag tells us that we are currently walking up
  // the parent chain of a required DIE, and we don't want to mark all
  // the children of the parents as kept (consider for example a
  // DW_TAG_namespace node in the parent chain). There are however a
  // set of DIE types for which we want to ignore that directive and still
  // walk their children.
  if (dieNeedsChildrenToBeMeaningful(Die.getTag()))
    Flags &= ~TF_ParentWalk;

  if (!Die.hasChildren() || (Flags & TF_ParentWalk))
    return;

  for (auto *Child = Die.getFirstChild(); Child && !Child->isNULL();
       Child = Child->getSibling())
    lookForDIEsToKeep(RelocMgr, *Child, DMO, CU, Flags);
}

/// \brief Assign an abbreviation numer to \p Abbrev.
///
/// Our DIEs get freed after every DebugMapObject has been processed,
/// thus the FoldingSet we use to unique DIEAbbrevs cannot refer to
/// the instances hold by the DIEs. When we encounter an abbreviation
/// that we don't know, we create a permanent copy of it.
void DwarfLinker::AssignAbbrev(DIEAbbrev &Abbrev) {
  // Check the set for priors.
  FoldingSetNodeID ID;
  Abbrev.Profile(ID);
  void *InsertToken;
  DIEAbbrev *InSet = AbbreviationsSet.FindNodeOrInsertPos(ID, InsertToken);

  // If it's newly added.
  if (InSet) {
    // Assign existing abbreviation number.
    Abbrev.setNumber(InSet->getNumber());
  } else {
    // Add to abbreviation list.
    Abbreviations.push_back(
        llvm::make_unique<DIEAbbrev>(Abbrev.getTag(), Abbrev.hasChildren()));
    for (const auto &Attr : Abbrev.getData())
      Abbreviations.back()->AddAttribute(Attr.getAttribute(), Attr.getForm());
    AbbreviationsSet.InsertNode(Abbreviations.back().get(), InsertToken);
    // Assign the unique abbreviation number.
    Abbrev.setNumber(Abbreviations.size());
    Abbreviations.back()->setNumber(Abbreviations.size());
  }
}

unsigned DwarfLinker::DIECloner::cloneStringAttribute(DIE &Die,
                                                      AttributeSpec AttrSpec,
                                                      const DWARFFormValue &Val,
                                                      const DWARFUnit &U) {
  // Switch everything to out of line strings.
  const char *String = *Val.getAsCString(&U);
  unsigned Offset = Linker.StringPool.getStringOffset(String);
  Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr), dwarf::DW_FORM_strp,
               DIEInteger(Offset));
  return 4;
}

unsigned DwarfLinker::DIECloner::cloneDieReferenceAttribute(
    DIE &Die, const DWARFDebugInfoEntryMinimal &InputDIE,
    AttributeSpec AttrSpec, unsigned AttrSize, const DWARFFormValue &Val,
    CompileUnit &Unit) {
  const DWARFUnit &U = Unit.getOrigUnit();
  uint32_t Ref = *Val.getAsReference(&U);
  DIE *NewRefDie = nullptr;
  CompileUnit *RefUnit = nullptr;
  DeclContext *Ctxt = nullptr;

  const DWARFDebugInfoEntryMinimal *RefDie =
      resolveDIEReference(Linker, CompileUnits, Val, U, InputDIE, RefUnit);

  // If the referenced DIE is not found,  drop the attribute.
  if (!RefDie)
    return 0;

  unsigned Idx = RefUnit->getOrigUnit().getDIEIndex(RefDie);
  CompileUnit::DIEInfo &RefInfo = RefUnit->getInfo(Idx);

  // If we already have emitted an equivalent DeclContext, just point
  // at it.
  if (isODRAttribute(AttrSpec.Attr)) {
    Ctxt = RefInfo.Ctxt;
    if (Ctxt && Ctxt->getCanonicalDIEOffset()) {
      DIEInteger Attr(Ctxt->getCanonicalDIEOffset());
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::DW_FORM_ref_addr, Attr);
      return getRefAddrSize(U);
    }
  }

  if (!RefInfo.Clone) {
    assert(Ref > InputDIE.getOffset());
    // We haven't cloned this DIE yet. Just create an empty one and
    // store it. It'll get really cloned when we process it.
    RefInfo.Clone = DIE::get(DIEAlloc, dwarf::Tag(RefDie->getTag()));
  }
  NewRefDie = RefInfo.Clone;

  if (AttrSpec.Form == dwarf::DW_FORM_ref_addr ||
      (Unit.hasODR() && isODRAttribute(AttrSpec.Attr))) {
    // We cannot currently rely on a DIEEntry to emit ref_addr
    // references, because the implementation calls back to DwarfDebug
    // to find the unit offset. (We don't have a DwarfDebug)
    // FIXME: we should be able to design DIEEntry reliance on
    // DwarfDebug away.
    uint64_t Attr;
    if (Ref < InputDIE.getOffset()) {
      // We must have already cloned that DIE.
      uint32_t NewRefOffset =
          RefUnit->getStartOffset() + NewRefDie->getOffset();
      Attr = NewRefOffset;
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::DW_FORM_ref_addr, DIEInteger(Attr));
    } else {
      // A forward reference. Note and fixup later.
      Attr = 0xBADDEF;
      Unit.noteForwardReference(
          NewRefDie, RefUnit, Ctxt,
          Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                       dwarf::DW_FORM_ref_addr, DIEInteger(Attr)));
    }
    return getRefAddrSize(U);
  }

  Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
               dwarf::Form(AttrSpec.Form), DIEEntry(*NewRefDie));
  return AttrSize;
}

unsigned DwarfLinker::DIECloner::cloneBlockAttribute(DIE &Die,
                                                     AttributeSpec AttrSpec,
                                                     const DWARFFormValue &Val,
                                                     unsigned AttrSize) {
  DIEValueList *Attr;
  DIEValue Value;
  DIELoc *Loc = nullptr;
  DIEBlock *Block = nullptr;
  // Just copy the block data over.
  if (AttrSpec.Form == dwarf::DW_FORM_exprloc) {
    Loc = new (DIEAlloc) DIELoc;
    Linker.DIELocs.push_back(Loc);
  } else {
    Block = new (DIEAlloc) DIEBlock;
    Linker.DIEBlocks.push_back(Block);
  }
  Attr = Loc ? static_cast<DIEValueList *>(Loc)
             : static_cast<DIEValueList *>(Block);

  if (Loc)
    Value = DIEValue(dwarf::Attribute(AttrSpec.Attr),
                     dwarf::Form(AttrSpec.Form), Loc);
  else
    Value = DIEValue(dwarf::Attribute(AttrSpec.Attr),
                     dwarf::Form(AttrSpec.Form), Block);
  ArrayRef<uint8_t> Bytes = *Val.getAsBlock();
  for (auto Byte : Bytes)
    Attr->addValue(DIEAlloc, static_cast<dwarf::Attribute>(0),
                   dwarf::DW_FORM_data1, DIEInteger(Byte));
  // FIXME: If DIEBlock and DIELoc just reuses the Size field of
  // the DIE class, this if could be replaced by
  // Attr->setSize(Bytes.size()).
  if (Linker.Streamer) {
    auto *AsmPrinter = &Linker.Streamer->getAsmPrinter();
    if (Loc)
      Loc->ComputeSize(AsmPrinter);
    else
      Block->ComputeSize(AsmPrinter);
  }
  Die.addValue(DIEAlloc, Value);
  return AttrSize;
}

unsigned DwarfLinker::DIECloner::cloneAddressAttribute(
    DIE &Die, AttributeSpec AttrSpec, const DWARFFormValue &Val,
    const CompileUnit &Unit, AttributesInfo &Info) {
  uint64_t Addr = *Val.getAsAddress(&Unit.getOrigUnit());
  if (AttrSpec.Attr == dwarf::DW_AT_low_pc) {
    if (Die.getTag() == dwarf::DW_TAG_inlined_subroutine ||
        Die.getTag() == dwarf::DW_TAG_lexical_block)
      // The low_pc of a block or inline subroutine might get
      // relocated because it happens to match the low_pc of the
      // enclosing subprogram. To prevent issues with that, always use
      // the low_pc from the input DIE if relocations have been applied.
      Addr = (Info.OrigLowPc != UINT64_MAX ? Info.OrigLowPc : Addr) +
             Info.PCOffset;
    else if (Die.getTag() == dwarf::DW_TAG_compile_unit) {
      Addr = Unit.getLowPc();
      if (Addr == UINT64_MAX)
        return 0;
    }
    Info.HasLowPc = true;
  } else if (AttrSpec.Attr == dwarf::DW_AT_high_pc) {
    if (Die.getTag() == dwarf::DW_TAG_compile_unit) {
      if (uint64_t HighPc = Unit.getHighPc())
        Addr = HighPc;
      else
        return 0;
    } else
      // If we have a high_pc recorded for the input DIE, use
      // it. Otherwise (when no relocations where applied) just use the
      // one we just decoded.
      Addr = (Info.OrigHighPc ? Info.OrigHighPc : Addr) + Info.PCOffset;
  }

  Die.addValue(DIEAlloc, static_cast<dwarf::Attribute>(AttrSpec.Attr),
               static_cast<dwarf::Form>(AttrSpec.Form), DIEInteger(Addr));
  return Unit.getOrigUnit().getAddressByteSize();
}

unsigned DwarfLinker::DIECloner::cloneScalarAttribute(
    DIE &Die, const DWARFDebugInfoEntryMinimal &InputDIE, CompileUnit &Unit,
    AttributeSpec AttrSpec, const DWARFFormValue &Val, unsigned AttrSize,
    AttributesInfo &Info) {
  uint64_t Value;
  if (AttrSpec.Attr == dwarf::DW_AT_high_pc &&
      Die.getTag() == dwarf::DW_TAG_compile_unit) {
    if (Unit.getLowPc() == -1ULL)
      return 0;
    // Dwarf >= 4 high_pc is an size, not an address.
    Value = Unit.getHighPc() - Unit.getLowPc();
  } else if (AttrSpec.Form == dwarf::DW_FORM_sec_offset)
    Value = *Val.getAsSectionOffset();
  else if (AttrSpec.Form == dwarf::DW_FORM_sdata)
    Value = *Val.getAsSignedConstant();
  else if (auto OptionalValue = Val.getAsUnsignedConstant())
    Value = *OptionalValue;
  else {
    Linker.reportWarning(
        "Unsupported scalar attribute form. Dropping attribute.",
        &Unit.getOrigUnit(), &InputDIE);
    return 0;
  }
  PatchLocation Patch =
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::Form(AttrSpec.Form), DIEInteger(Value));
  if (AttrSpec.Attr == dwarf::DW_AT_ranges)
    Unit.noteRangeAttribute(Die, Patch);

  // A more generic way to check for location attributes would be
  // nice, but it's very unlikely that any other attribute needs a
  // location list.
  else if (AttrSpec.Attr == dwarf::DW_AT_location ||
           AttrSpec.Attr == dwarf::DW_AT_frame_base)
    Unit.noteLocationAttribute(Patch, Info.PCOffset);
  else if (AttrSpec.Attr == dwarf::DW_AT_declaration && Value)
    Info.IsDeclaration = true;

  return AttrSize;
}

/// \brief Clone \p InputDIE's attribute described by \p AttrSpec with
/// value \p Val, and add it to \p Die.
/// \returns the size of the cloned attribute.
unsigned DwarfLinker::DIECloner::cloneAttribute(
    DIE &Die, const DWARFDebugInfoEntryMinimal &InputDIE, CompileUnit &Unit,
    const DWARFFormValue &Val, const AttributeSpec AttrSpec, unsigned AttrSize,
    AttributesInfo &Info) {
  const DWARFUnit &U = Unit.getOrigUnit();

  switch (AttrSpec.Form) {
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_string:
    return cloneStringAttribute(Die, AttrSpec, Val, U);
  case dwarf::DW_FORM_ref_addr:
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
    return cloneDieReferenceAttribute(Die, InputDIE, AttrSpec, AttrSize, Val,
                                      Unit);
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_block1:
  case dwarf::DW_FORM_block2:
  case dwarf::DW_FORM_block4:
  case dwarf::DW_FORM_exprloc:
    return cloneBlockAttribute(Die, AttrSpec, Val, AttrSize);
  case dwarf::DW_FORM_addr:
    return cloneAddressAttribute(Die, AttrSpec, Val, Unit, Info);
  case dwarf::DW_FORM_data1:
  case dwarf::DW_FORM_data2:
  case dwarf::DW_FORM_data4:
  case dwarf::DW_FORM_data8:
  case dwarf::DW_FORM_udata:
  case dwarf::DW_FORM_sdata:
  case dwarf::DW_FORM_sec_offset:
  case dwarf::DW_FORM_flag:
  case dwarf::DW_FORM_flag_present:
    return cloneScalarAttribute(Die, InputDIE, Unit, AttrSpec, Val, AttrSize,
                                Info);
  default:
    Linker.reportWarning(
        "Unsupported attribute form in cloneAttribute. Dropping.", &U,
        &InputDIE);
  }

  return 0;
}

/// \brief Apply the valid relocations found by findValidRelocs() to
/// the buffer \p Data, taking into account that Data is at \p BaseOffset
/// in the debug_info section.
///
/// Like for findValidRelocs(), this function must be called with
/// monotonic \p BaseOffset values.
///
/// \returns wether any reloc has been applied.
bool DwarfLinker::RelocationManager::
applyValidRelocs(MutableArrayRef<char> Data, uint32_t BaseOffset,
                 bool isLittleEndian) {
  assert((NextValidReloc == 0 ||
          BaseOffset > ValidRelocs[NextValidReloc - 1].Offset) &&
         "BaseOffset should only be increasing.");
  if (NextValidReloc >= ValidRelocs.size())
    return false;

  // Skip relocs that haven't been applied.
  while (NextValidReloc < ValidRelocs.size() &&
         ValidRelocs[NextValidReloc].Offset < BaseOffset)
    ++NextValidReloc;

  bool Applied = false;
  uint64_t EndOffset = BaseOffset + Data.size();
  while (NextValidReloc < ValidRelocs.size() &&
         ValidRelocs[NextValidReloc].Offset >= BaseOffset &&
         ValidRelocs[NextValidReloc].Offset < EndOffset) {
    const auto &ValidReloc = ValidRelocs[NextValidReloc++];
    assert(ValidReloc.Offset - BaseOffset < Data.size());
    assert(ValidReloc.Offset - BaseOffset + ValidReloc.Size <= Data.size());
    char Buf[8];
    uint64_t Value = ValidReloc.Mapping->getValue().BinaryAddress;
    Value += ValidReloc.Addend;
    for (unsigned i = 0; i != ValidReloc.Size; ++i) {
      unsigned Index = isLittleEndian ? i : (ValidReloc.Size - i - 1);
      Buf[i] = uint8_t(Value >> (Index * 8));
    }
    assert(ValidReloc.Size <= sizeof(Buf));
    memcpy(&Data[ValidReloc.Offset - BaseOffset], Buf, ValidReloc.Size);
    Applied = true;
  }

  return Applied;
}

static bool isTypeTag(uint16_t Tag) {
  switch (Tag) {
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_pointer_type:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_string_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_ptr_to_member_type:
  case dwarf::DW_TAG_set_type:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_constant:
  case dwarf::DW_TAG_file_type:
  case dwarf::DW_TAG_namelist:
  case dwarf::DW_TAG_packed_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_interface_type:
  case dwarf::DW_TAG_unspecified_type:
  case dwarf::DW_TAG_shared_type:
    return true;
  default:
    break;
  }
  return false;
}

static bool
shouldSkipAttribute(DWARFAbbreviationDeclaration::AttributeSpec AttrSpec,
                    uint16_t Tag, bool InDebugMap, bool SkipPC,
                    bool InFunctionScope) {
  switch (AttrSpec.Attr) {
  default:
    return false;
  case dwarf::DW_AT_low_pc:
  case dwarf::DW_AT_high_pc:
  case dwarf::DW_AT_ranges:
    return SkipPC;
  case dwarf::DW_AT_location:
  case dwarf::DW_AT_frame_base:
    // FIXME: for some reason dsymutil-classic keeps the location
    // attributes when they are of block type (ie. not location
    // lists). This is totally wrong for globals where we will keep a
    // wrong address. It is mostly harmless for locals, but there is
    // no point in keeping these anyway when the function wasn't linked.
    return (SkipPC || (!InFunctionScope && Tag == dwarf::DW_TAG_variable &&
                       !InDebugMap)) &&
           !DWARFFormValue(AttrSpec.Form).isFormClass(DWARFFormValue::FC_Block);
  }
}

DIE *DwarfLinker::DIECloner::cloneDIE(
    const DWARFDebugInfoEntryMinimal &InputDIE, CompileUnit &Unit,
    int64_t PCOffset, uint32_t OutOffset, unsigned Flags) {
  DWARFUnit &U = Unit.getOrigUnit();
  unsigned Idx = U.getDIEIndex(&InputDIE);
  CompileUnit::DIEInfo &Info = Unit.getInfo(Idx);

  // Should the DIE appear in the output?
  if (!Unit.getInfo(Idx).Keep)
    return nullptr;

  uint32_t Offset = InputDIE.getOffset();
  // The DIE might have been already created by a forward reference
  // (see cloneDieReferenceAttribute()).
  DIE *Die = Info.Clone;
  if (!Die)
    Die = Info.Clone = DIE::get(DIEAlloc, dwarf::Tag(InputDIE.getTag()));
  assert(Die->getTag() == InputDIE.getTag());
  Die->setOffset(OutOffset);
  if ((Unit.hasODR() || Unit.isClangModule()) &&
      Die->getTag() != dwarf::DW_TAG_namespace && Info.Ctxt &&
      Info.Ctxt != Unit.getInfo(Info.ParentIdx).Ctxt &&
      !Info.Ctxt->getCanonicalDIEOffset()) {
    // We are about to emit a DIE that is the root of its own valid
    // DeclContext tree. Make the current offset the canonical offset
    // for this context.
    Info.Ctxt->setCanonicalDIEOffset(OutOffset + Unit.getStartOffset());
  }

  // Extract and clone every attribute.
  DataExtractor Data = U.getDebugInfoExtractor();
  // Point to the next DIE (generally there is always at least a NULL
  // entry after the current one). If this is a lone
  // DW_TAG_compile_unit without any children, point to the next unit.
  uint32_t NextOffset =
    (Idx + 1 < U.getNumDIEs())
    ? U.getDIEAtIndex(Idx + 1)->getOffset()
    : U.getNextUnitOffset();
  AttributesInfo AttrInfo;

  // We could copy the data only if we need to aply a relocation to
  // it. After testing, it seems there is no performance downside to
  // doing the copy unconditionally, and it makes the code simpler.
  SmallString<40> DIECopy(Data.getData().substr(Offset, NextOffset - Offset));
  Data = DataExtractor(DIECopy, Data.isLittleEndian(), Data.getAddressSize());
  // Modify the copy with relocated addresses.
  if (RelocMgr.applyValidRelocs(DIECopy, Offset, Data.isLittleEndian())) {
    // If we applied relocations, we store the value of high_pc that was
    // potentially stored in the input DIE. If high_pc is an address
    // (Dwarf version == 2), then it might have been relocated to a
    // totally unrelated value (because the end address in the object
    // file might be start address of another function which got moved
    // independantly by the linker). The computation of the actual
    // high_pc value is done in cloneAddressAttribute().
    AttrInfo.OrigHighPc =
        InputDIE.getAttributeValueAsAddress(&U, dwarf::DW_AT_high_pc, 0);
    // Also store the low_pc. It might get relocated in an
    // inline_subprogram that happens at the beginning of its
    // inlining function.
    AttrInfo.OrigLowPc =
        InputDIE.getAttributeValueAsAddress(&U, dwarf::DW_AT_low_pc, UINT64_MAX);
  }

  // Reset the Offset to 0 as we will be working on the local copy of
  // the data.
  Offset = 0;

  const auto *Abbrev = InputDIE.getAbbreviationDeclarationPtr();
  Offset += getULEB128Size(Abbrev->getCode());

  // We are entering a subprogram. Get and propagate the PCOffset.
  if (Die->getTag() == dwarf::DW_TAG_subprogram)
    PCOffset = Info.AddrAdjust;
  AttrInfo.PCOffset = PCOffset;

  if (Abbrev->getTag() == dwarf::DW_TAG_subprogram) {
    Flags |= TF_InFunctionScope;
    if (!Info.InDebugMap)
      Flags |= TF_SkipPC;
  }

  bool Copied = false;
  for (const auto &AttrSpec : Abbrev->attributes()) {
    if (shouldSkipAttribute(AttrSpec, Die->getTag(), Info.InDebugMap,
                            Flags & TF_SkipPC, Flags & TF_InFunctionScope)) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset, &U);
      // FIXME: dsymutil-classic keeps the old abbreviation around
      // even if it's not used. We can remove this (and the copyAbbrev
      // helper) as soon as bit-for-bit compatibility is not a goal anymore.
      if (!Copied) {
        copyAbbrev(*InputDIE.getAbbreviationDeclarationPtr(), Unit.hasODR());
        Copied = true;
      }
      continue;
    }

    DWARFFormValue Val(AttrSpec.Form);
    uint32_t AttrSize = Offset;
    Val.extractValue(Data, &Offset, &U);
    AttrSize = Offset - AttrSize;

    OutOffset +=
        cloneAttribute(*Die, InputDIE, Unit, Val, AttrSpec, AttrSize, AttrInfo);
  }

  // Look for accelerator entries.
  uint16_t Tag = InputDIE.getTag();
  // FIXME: This is slightly wrong. An inline_subroutine without a
  // low_pc, but with AT_ranges might be interesting to get into the
  // accelerator tables too. For now stick with dsymutil's behavior.
  if ((Info.InDebugMap || AttrInfo.HasLowPc) &&
      Tag != dwarf::DW_TAG_compile_unit &&
      getDIENames(InputDIE, Unit.getOrigUnit(), AttrInfo)) {
    if (AttrInfo.MangledName && AttrInfo.MangledName != AttrInfo.Name)
      Unit.addNameAccelerator(Die, AttrInfo.MangledName,
                              AttrInfo.MangledNameOffset,
                              Tag == dwarf::DW_TAG_inlined_subroutine);
    if (AttrInfo.Name)
      Unit.addNameAccelerator(Die, AttrInfo.Name, AttrInfo.NameOffset,
                              Tag == dwarf::DW_TAG_inlined_subroutine);
  } else if (isTypeTag(Tag) && !AttrInfo.IsDeclaration &&
             getDIENames(InputDIE, Unit.getOrigUnit(), AttrInfo)) {
    Unit.addTypeAccelerator(Die, AttrInfo.Name, AttrInfo.NameOffset);
  }

  // Determine whether there are any children that we want to keep.
  bool HasChildren = false;
  for (auto *Child = InputDIE.getFirstChild(); Child && !Child->isNULL();
       Child = Child->getSibling()) {
    unsigned Idx = U.getDIEIndex(Child);
    if (Unit.getInfo(Idx).Keep) {
      HasChildren = true;
      break;
    }
  }

  DIEAbbrev NewAbbrev = Die->generateAbbrev();
  if (HasChildren)
    NewAbbrev.setChildrenFlag(dwarf::DW_CHILDREN_yes);
  // Assign a permanent abbrev number
  Linker.AssignAbbrev(NewAbbrev);
  Die->setAbbrevNumber(NewAbbrev.getNumber());

  // Add the size of the abbreviation number to the output offset.
  OutOffset += getULEB128Size(Die->getAbbrevNumber());

  if (!HasChildren) {
    // Update our size.
    Die->setSize(OutOffset - Die->getOffset());
    return Die;
  }

  // Recursively clone children.
  for (auto *Child = InputDIE.getFirstChild(); Child && !Child->isNULL();
       Child = Child->getSibling()) {
    if (DIE *Clone = cloneDIE(*Child, Unit, PCOffset, OutOffset, Flags)) {
      Die->addChild(Clone);
      OutOffset = Clone->getOffset() + Clone->getSize();
    }
  }

  // Account for the end of children marker.
  OutOffset += sizeof(int8_t);
  // Update our size.
  Die->setSize(OutOffset - Die->getOffset());
  return Die;
}

/// \brief Patch the input object file relevant debug_ranges entries
/// and emit them in the output file. Update the relevant attributes
/// to point at the new entries.
void DwarfLinker::patchRangesForUnit(const CompileUnit &Unit,
                                     DWARFContext &OrigDwarf) const {
  DWARFDebugRangeList RangeList;
  const auto &FunctionRanges = Unit.getFunctionRanges();
  unsigned AddressSize = Unit.getOrigUnit().getAddressByteSize();
  DataExtractor RangeExtractor(OrigDwarf.getRangeSection(),
                               OrigDwarf.isLittleEndian(), AddressSize);
  auto InvalidRange = FunctionRanges.end(), CurrRange = InvalidRange;
  DWARFUnit &OrigUnit = Unit.getOrigUnit();
  const auto *OrigUnitDie = OrigUnit.getUnitDIE(false);
  uint64_t OrigLowPc = OrigUnitDie->getAttributeValueAsAddress(
      &OrigUnit, dwarf::DW_AT_low_pc, -1ULL);
  // Ranges addresses are based on the unit's low_pc. Compute the
  // offset we need to apply to adapt to the new unit's low_pc.
  int64_t UnitPcOffset = 0;
  if (OrigLowPc != -1ULL)
    UnitPcOffset = int64_t(OrigLowPc) - Unit.getLowPc();

  for (const auto &RangeAttribute : Unit.getRangesAttributes()) {
    uint32_t Offset = RangeAttribute.get();
    RangeAttribute.set(Streamer->getRangesSectionSize());
    RangeList.extract(RangeExtractor, &Offset);
    const auto &Entries = RangeList.getEntries();
    if (!Entries.empty()) {
      const DWARFDebugRangeList::RangeListEntry &First = Entries.front();

      if (CurrRange == InvalidRange ||
          First.StartAddress + OrigLowPc < CurrRange.start() ||
          First.StartAddress + OrigLowPc >= CurrRange.stop()) {
        CurrRange = FunctionRanges.find(First.StartAddress + OrigLowPc);
        if (CurrRange == InvalidRange ||
            CurrRange.start() > First.StartAddress + OrigLowPc) {
          reportWarning("no mapping for range.");
          continue;
        }
      }
    }

    Streamer->emitRangesEntries(UnitPcOffset, OrigLowPc, CurrRange, Entries,
                                AddressSize);
  }
}

/// \brief Generate the debug_aranges entries for \p Unit and if the
/// unit has a DW_AT_ranges attribute, also emit the debug_ranges
/// contribution for this attribute.
/// FIXME: this could actually be done right in patchRangesForUnit,
/// but for the sake of initial bit-for-bit compatibility with legacy
/// dsymutil, we have to do it in a delayed pass.
void DwarfLinker::generateUnitRanges(CompileUnit &Unit) const {
  auto Attr = Unit.getUnitRangesAttribute();
  if (Attr)
    Attr->set(Streamer->getRangesSectionSize());
  Streamer->emitUnitRangesEntries(Unit, static_cast<bool>(Attr));
}

/// \brief Insert the new line info sequence \p Seq into the current
/// set of already linked line info \p Rows.
static void insertLineSequence(std::vector<DWARFDebugLine::Row> &Seq,
                               std::vector<DWARFDebugLine::Row> &Rows) {
  if (Seq.empty())
    return;

  if (!Rows.empty() && Rows.back().Address < Seq.front().Address) {
    Rows.insert(Rows.end(), Seq.begin(), Seq.end());
    Seq.clear();
    return;
  }

  auto InsertPoint = std::lower_bound(
      Rows.begin(), Rows.end(), Seq.front(),
      [](const DWARFDebugLine::Row &LHS, const DWARFDebugLine::Row &RHS) {
        return LHS.Address < RHS.Address;
      });

  // FIXME: this only removes the unneeded end_sequence if the
  // sequences have been inserted in order. using a global sort like
  // described in patchLineTableForUnit() and delaying the end_sequene
  // elimination to emitLineTableForUnit() we can get rid of all of them.
  if (InsertPoint != Rows.end() &&
      InsertPoint->Address == Seq.front().Address && InsertPoint->EndSequence) {
    *InsertPoint = Seq.front();
    Rows.insert(InsertPoint + 1, Seq.begin() + 1, Seq.end());
  } else {
    Rows.insert(InsertPoint, Seq.begin(), Seq.end());
  }

  Seq.clear();
}

static void patchStmtList(DIE &Die, DIEInteger Offset) {
  for (auto &V : Die.values())
    if (V.getAttribute() == dwarf::DW_AT_stmt_list) {
      V = DIEValue(V.getAttribute(), V.getForm(), Offset);
      return;
    }

  llvm_unreachable("Didn't find DW_AT_stmt_list in cloned DIE!");
}

/// \brief Extract the line table for \p Unit from \p OrigDwarf, and
/// recreate a relocated version of these for the address ranges that
/// are present in the binary.
void DwarfLinker::patchLineTableForUnit(CompileUnit &Unit,
                                        DWARFContext &OrigDwarf) {
  const DWARFDebugInfoEntryMinimal *CUDie = Unit.getOrigUnit().getUnitDIE();
  uint64_t StmtList = CUDie->getAttributeValueAsSectionOffset(
      &Unit.getOrigUnit(), dwarf::DW_AT_stmt_list, -1ULL);
  if (StmtList == -1ULL)
    return;

  // Update the cloned DW_AT_stmt_list with the correct debug_line offset.
  if (auto *OutputDIE = Unit.getOutputUnitDIE())
    patchStmtList(*OutputDIE, DIEInteger(Streamer->getLineSectionSize()));

  // Parse the original line info for the unit.
  DWARFDebugLine::LineTable LineTable;
  uint32_t StmtOffset = StmtList;
  StringRef LineData = OrigDwarf.getLineSection().Data;
  DataExtractor LineExtractor(LineData, OrigDwarf.isLittleEndian(),
                              Unit.getOrigUnit().getAddressByteSize());
  LineTable.parse(LineExtractor, &OrigDwarf.getLineSection().Relocs,
                  &StmtOffset);

  // This vector is the output line table.
  std::vector<DWARFDebugLine::Row> NewRows;
  NewRows.reserve(LineTable.Rows.size());

  // Current sequence of rows being extracted, before being inserted
  // in NewRows.
  std::vector<DWARFDebugLine::Row> Seq;
  const auto &FunctionRanges = Unit.getFunctionRanges();
  auto InvalidRange = FunctionRanges.end(), CurrRange = InvalidRange;

  // FIXME: This logic is meant to generate exactly the same output as
  // Darwin's classic dsynutil. There is a nicer way to implement this
  // by simply putting all the relocated line info in NewRows and simply
  // sorting NewRows before passing it to emitLineTableForUnit. This
  // should be correct as sequences for a function should stay
  // together in the sorted output. There are a few corner cases that
  // look suspicious though, and that required to implement the logic
  // this way. Revisit that once initial validation is finished.

  // Iterate over the object file line info and extract the sequences
  // that correspond to linked functions.
  for (auto &Row : LineTable.Rows) {
    // Check wether we stepped out of the range. The range is
    // half-open, but consider accept the end address of the range if
    // it is marked as end_sequence in the input (because in that
    // case, the relocation offset is accurate and that entry won't
    // serve as the start of another function).
    if (CurrRange == InvalidRange || Row.Address < CurrRange.start() ||
        Row.Address > CurrRange.stop() ||
        (Row.Address == CurrRange.stop() && !Row.EndSequence)) {
      // We just stepped out of a known range. Insert a end_sequence
      // corresponding to the end of the range.
      uint64_t StopAddress = CurrRange != InvalidRange
                                 ? CurrRange.stop() + CurrRange.value()
                                 : -1ULL;
      CurrRange = FunctionRanges.find(Row.Address);
      bool CurrRangeValid =
          CurrRange != InvalidRange && CurrRange.start() <= Row.Address;
      if (!CurrRangeValid) {
        CurrRange = InvalidRange;
        if (StopAddress != -1ULL) {
          // Try harder by looking in the DebugMapObject function
          // ranges map. There are corner cases where this finds a
          // valid entry. It's unclear if this is right or wrong, but
          // for now do as dsymutil.
          // FIXME: Understand exactly what cases this addresses and
          // potentially remove it along with the Ranges map.
          auto Range = Ranges.lower_bound(Row.Address);
          if (Range != Ranges.begin() && Range != Ranges.end())
            --Range;

          if (Range != Ranges.end() && Range->first <= Row.Address &&
              Range->second.first >= Row.Address) {
            StopAddress = Row.Address + Range->second.second;
          }
        }
      }
      if (StopAddress != -1ULL && !Seq.empty()) {
        // Insert end sequence row with the computed end address, but
        // the same line as the previous one.
        auto NextLine = Seq.back();
        NextLine.Address = StopAddress;
        NextLine.EndSequence = 1;
        NextLine.PrologueEnd = 0;
        NextLine.BasicBlock = 0;
        NextLine.EpilogueBegin = 0;
        Seq.push_back(NextLine);
        insertLineSequence(Seq, NewRows);
      }

      if (!CurrRangeValid)
        continue;
    }

    // Ignore empty sequences.
    if (Row.EndSequence && Seq.empty())
      continue;

    // Relocate row address and add it to the current sequence.
    Row.Address += CurrRange.value();
    Seq.emplace_back(Row);

    if (Row.EndSequence)
      insertLineSequence(Seq, NewRows);
  }

  // Finished extracting, now emit the line tables.
  uint32_t PrologueEnd = StmtList + 10 + LineTable.Prologue.PrologueLength;
  // FIXME: LLVM hardcodes it's prologue values. We just copy the
  // prologue over and that works because we act as both producer and
  // consumer. It would be nicer to have a real configurable line
  // table emitter.
  if (LineTable.Prologue.Version != 2 ||
      LineTable.Prologue.DefaultIsStmt != DWARF2_LINE_DEFAULT_IS_STMT ||
      LineTable.Prologue.OpcodeBase > 13)
    reportWarning("line table paramters mismatch. Cannot emit.");
  else {
    MCDwarfLineTableParams Params;
    Params.DWARF2LineOpcodeBase = LineTable.Prologue.OpcodeBase;
    Params.DWARF2LineBase = LineTable.Prologue.LineBase;
    Params.DWARF2LineRange = LineTable.Prologue.LineRange;
    Streamer->emitLineTableForUnit(Params,
                                   LineData.slice(StmtList + 4, PrologueEnd),
                                   LineTable.Prologue.MinInstLength, NewRows,
                                   Unit.getOrigUnit().getAddressByteSize());
  }
}

void DwarfLinker::emitAcceleratorEntriesForUnit(CompileUnit &Unit) {
  Streamer->emitPubNamesForUnit(Unit);
  Streamer->emitPubTypesForUnit(Unit);
}

/// \brief Read the frame info stored in the object, and emit the
/// patched frame descriptions for the linked binary.
///
/// This is actually pretty easy as the data of the CIEs and FDEs can
/// be considered as black boxes and moved as is. The only thing to do
/// is to patch the addresses in the headers.
void DwarfLinker::patchFrameInfoForObject(const DebugMapObject &DMO,
                                          DWARFContext &OrigDwarf,
                                          unsigned AddrSize) {
  StringRef FrameData = OrigDwarf.getDebugFrameSection();
  if (FrameData.empty())
    return;

  DataExtractor Data(FrameData, OrigDwarf.isLittleEndian(), 0);
  uint32_t InputOffset = 0;

  // Store the data of the CIEs defined in this object, keyed by their
  // offsets.
  DenseMap<uint32_t, StringRef> LocalCIES;

  while (Data.isValidOffset(InputOffset)) {
    uint32_t EntryOffset = InputOffset;
    uint32_t InitialLength = Data.getU32(&InputOffset);
    if (InitialLength == 0xFFFFFFFF)
      return reportWarning("Dwarf64 bits no supported");

    uint32_t CIEId = Data.getU32(&InputOffset);
    if (CIEId == 0xFFFFFFFF) {
      // This is a CIE, store it.
      StringRef CIEData = FrameData.substr(EntryOffset, InitialLength + 4);
      LocalCIES[EntryOffset] = CIEData;
      // The -4 is to account for the CIEId we just read.
      InputOffset += InitialLength - 4;
      continue;
    }

    uint32_t Loc = Data.getUnsigned(&InputOffset, AddrSize);

    // Some compilers seem to emit frame info that doesn't start at
    // the function entry point, thus we can't just lookup the address
    // in the debug map. Use the linker's range map to see if the FDE
    // describes something that we can relocate.
    auto Range = Ranges.upper_bound(Loc);
    if (Range != Ranges.begin())
      --Range;
    if (Range == Ranges.end() || Range->first > Loc ||
        Range->second.first <= Loc) {
      // The +4 is to account for the size of the InitialLength field itself.
      InputOffset = EntryOffset + InitialLength + 4;
      continue;
    }

    // This is an FDE, and we have a mapping.
    // Have we already emitted a corresponding CIE?
    StringRef CIEData = LocalCIES[CIEId];
    if (CIEData.empty())
      return reportWarning("Inconsistent debug_frame content. Dropping.");

    // Look if we already emitted a CIE that corresponds to the
    // referenced one (the CIE data is the key of that lookup).
    auto IteratorInserted = EmittedCIEs.insert(
        std::make_pair(CIEData, Streamer->getFrameSectionSize()));
    // If there is no CIE yet for this ID, emit it.
    if (IteratorInserted.second ||
        // FIXME: dsymutil-classic only caches the last used CIE for
        // reuse. Mimic that behavior for now. Just removing that
        // second half of the condition and the LastCIEOffset variable
        // makes the code DTRT.
        LastCIEOffset != IteratorInserted.first->getValue()) {
      LastCIEOffset = Streamer->getFrameSectionSize();
      IteratorInserted.first->getValue() = LastCIEOffset;
      Streamer->emitCIE(CIEData);
    }

    // Emit the FDE with updated address and CIE pointer.
    // (4 + AddrSize) is the size of the CIEId + initial_location
    // fields that will get reconstructed by emitFDE().
    unsigned FDERemainingBytes = InitialLength - (4 + AddrSize);
    Streamer->emitFDE(IteratorInserted.first->getValue(), AddrSize,
                      Loc + Range->second.second,
                      FrameData.substr(InputOffset, FDERemainingBytes));
    InputOffset += FDERemainingBytes;
  }
}

void DwarfLinker::DIECloner::copyAbbrev(
    const DWARFAbbreviationDeclaration &Abbrev, bool hasODR) {
  DIEAbbrev Copy(dwarf::Tag(Abbrev.getTag()),
                 dwarf::Form(Abbrev.hasChildren()));

  for (const auto &Attr : Abbrev.attributes()) {
    uint16_t Form = Attr.Form;
    if (hasODR && isODRAttribute(Attr.Attr))
      Form = dwarf::DW_FORM_ref_addr;
    Copy.AddAttribute(dwarf::Attribute(Attr.Attr), dwarf::Form(Form));
  }

  Linker.AssignAbbrev(Copy);
}

static uint64_t getDwoId(const DWARFDebugInfoEntryMinimal &CUDie,
                         const DWARFUnit &Unit) {
  uint64_t DwoId =
      CUDie.getAttributeValueAsUnsignedConstant(&Unit, dwarf::DW_AT_dwo_id, 0);
  if (!DwoId)
    DwoId = CUDie.getAttributeValueAsUnsignedConstant(&Unit,
                                                      dwarf::DW_AT_GNU_dwo_id, 0);
  return DwoId;
}

bool DwarfLinker::registerModuleReference(
    const DWARFDebugInfoEntryMinimal &CUDie, const DWARFUnit &Unit,
    DebugMap &ModuleMap, unsigned Indent) {
  std::string PCMfile =
      CUDie.getAttributeValueAsString(&Unit, dwarf::DW_AT_dwo_name, "");
  if (PCMfile.empty())
    PCMfile =
        CUDie.getAttributeValueAsString(&Unit, dwarf::DW_AT_GNU_dwo_name, "");
  if (PCMfile.empty())
    return false;

  // Clang module DWARF skeleton CUs abuse this for the path to the module.
  std::string PCMpath =
      CUDie.getAttributeValueAsString(&Unit, dwarf::DW_AT_comp_dir, "");
  uint64_t DwoId = getDwoId(CUDie, Unit);

  std::string Name =
      CUDie.getAttributeValueAsString(&Unit, dwarf::DW_AT_name, "");
  if (Name.empty()) {
    reportWarning("Anonymous module skeleton CU for " + PCMfile);
    return true;
  }

  if (Options.Verbose) {
    outs().indent(Indent);
    outs() << "Found clang module reference " << PCMfile;
  }

  auto Cached = ClangModules.find(PCMfile);
  if (Cached != ClangModules.end()) {
    if (Cached->second != DwoId)
      reportWarning(Twine("hash mismatch: this object file was built against a "
                          "different version of the module ") + PCMfile);
    if (Options.Verbose)
      outs() << " [cached].\n";
    return true;
  }
  if (Options.Verbose)
    outs() << " ...\n";

  // Cyclic dependencies are disallowed by Clang, but we still
  // shouldn't run into an infinite loop, so mark it as processed now.
  ClangModules.insert({PCMfile, DwoId});
  loadClangModule(PCMfile, PCMpath, Name, DwoId, ModuleMap, Indent + 2);
  return true;
}

ErrorOr<const object::ObjectFile &>
DwarfLinker::loadObject(BinaryHolder &BinaryHolder, DebugMapObject &Obj,
                        const DebugMap &Map) {
  auto ErrOrObjs =
      BinaryHolder.GetObjectFiles(Obj.getObjectFilename(), Obj.getTimestamp());
  if (std::error_code EC = ErrOrObjs.getError()) {
    reportWarning(Twine(Obj.getObjectFilename()) + ": " + EC.message());
    return EC;
  }
  auto ErrOrObj = BinaryHolder.Get(Map.getTriple());
  if (std::error_code EC = ErrOrObj.getError())
    reportWarning(Twine(Obj.getObjectFilename()) + ": " + EC.message());
  return ErrOrObj;
}

void DwarfLinker::loadClangModule(StringRef Filename, StringRef ModulePath,
                                  StringRef ModuleName, uint64_t DwoId,
                                  DebugMap &ModuleMap, unsigned Indent) {
  SmallString<80> Path(Options.PrependPath);
  if (sys::path::is_relative(Filename))
    sys::path::append(Path, ModulePath, Filename);
  else
    sys::path::append(Path, Filename);
  BinaryHolder ObjHolder(Options.Verbose);
  auto &Obj =
      ModuleMap.addDebugMapObject(Path, sys::TimeValue::PosixZeroTime());
  auto ErrOrObj = loadObject(ObjHolder, Obj, ModuleMap);
  if (!ErrOrObj) {
    ClangModules.erase(ClangModules.find(Filename));
    return;
  }

  std::unique_ptr<CompileUnit> Unit;

  // Setup access to the debug info.
  DWARFContextInMemory DwarfContext(*ErrOrObj);
  RelocationManager RelocMgr(*this);
  for (const auto &CU : DwarfContext.compile_units()) {
    auto *CUDie = CU->getUnitDIE(false);
    // Recursively get all modules imported by this one.
    if (!registerModuleReference(*CUDie, *CU, ModuleMap, Indent)) {
      if (Unit) {
        errs() << Filename << ": Clang modules are expected to have exactly"
               << " 1 compile unit.\n";
        exitDsymutil(1);
      }
      if (getDwoId(*CUDie, *CU) != DwoId)
        reportWarning(
            Twine("hash mismatch: this object file was built against a "
                  "different version of the module ") + Filename);

      // Add this module.
      Unit = llvm::make_unique<CompileUnit>(*CU, UnitID++, !Options.NoODR,
                                            ModuleName);
      Unit->setHasInterestingContent();
      analyzeContextInfo(CUDie, 0, *Unit, &ODRContexts.getRoot(), StringPool,
                         ODRContexts);
      // Keep everything.
      Unit->markEverythingAsKept();
    }
  }
  if (Options.Verbose) {
    outs().indent(Indent);
    outs() << "cloning .debug_info from " << Filename << "\n";
  }

  DIECloner(*this, RelocMgr, DIEAlloc, MutableArrayRef<CompileUnit>(*Unit),
            Options)
      .cloneAllCompileUnits(DwarfContext);
}

void DwarfLinker::DIECloner::cloneAllCompileUnits(
    DWARFContextInMemory &DwarfContext) {
  if (!Linker.Streamer)
    return;

  for (auto &CurrentUnit : CompileUnits) {
    const auto *InputDIE = CurrentUnit.getOrigUnit().getUnitDIE();
    CurrentUnit.setStartOffset(Linker.OutputDebugInfoSize);
    DIE *OutputDIE = cloneDIE(*InputDIE, CurrentUnit, 0 /* PC offset */,
                              11 /* Unit Header size */, 0);
    CurrentUnit.setOutputUnitDIE(OutputDIE);
    Linker.OutputDebugInfoSize = CurrentUnit.computeNextUnitOffset();
    if (Linker.Options.NoOutput)
      continue;
    // FIXME: for compatibility with the classic dsymutil, we emit
    // an empty line table for the unit, even if the unit doesn't
    // actually exist in the DIE tree.
    Linker.patchLineTableForUnit(CurrentUnit, DwarfContext);
    if (!OutputDIE)
      continue;
    Linker.patchRangesForUnit(CurrentUnit, DwarfContext);
    Linker.Streamer->emitLocationsForUnit(CurrentUnit, DwarfContext);
    Linker.emitAcceleratorEntriesForUnit(CurrentUnit);
  }

  if (Linker.Options.NoOutput)
    return;

  // Emit all the compile unit's debug information.
  for (auto &CurrentUnit : CompileUnits) {
    Linker.generateUnitRanges(CurrentUnit);
    CurrentUnit.fixupForwardReferences();
    Linker.Streamer->emitCompileUnitHeader(CurrentUnit);
    if (!CurrentUnit.getOutputUnitDIE())
      continue;
    Linker.Streamer->emitDIE(*CurrentUnit.getOutputUnitDIE());
  }
}

bool DwarfLinker::link(const DebugMap &Map) {

  if (!createStreamer(Map.getTriple(), OutputFilename))
    return false;

  // Size of the DIEs (and headers) generated for the linked output.
  OutputDebugInfoSize = 0;
  // A unique ID that identifies each compile unit.
  UnitID = 0;
  DebugMap ModuleMap(Map.getTriple(), Map.getBinaryPath());

  for (const auto &Obj : Map.objects()) {
    CurrentDebugObject = Obj.get();

    if (Options.Verbose)
      outs() << "DEBUG MAP OBJECT: " << Obj->getObjectFilename() << "\n";
    auto ErrOrObj = loadObject(BinHolder, *Obj, Map);
    if (!ErrOrObj)
      continue;

    // Look for relocations that correspond to debug map entries.
    RelocationManager RelocMgr(*this);
    if (!RelocMgr.findValidRelocsInDebugInfo(*ErrOrObj, *Obj)) {
      if (Options.Verbose)
        outs() << "No valid relocations found. Skipping.\n";
      continue;
    }

    // Setup access to the debug info.
    DWARFContextInMemory DwarfContext(*ErrOrObj);
    startDebugObject(DwarfContext, *Obj);

    // In a first phase, just read in the debug info and load all clang modules.
    for (const auto &CU : DwarfContext.compile_units()) {
      auto *CUDie = CU->getUnitDIE(false);
      if (Options.Verbose) {
        outs() << "Input compilation unit:";
        CUDie->dump(outs(), CU.get(), 0);
      }

      if (!registerModuleReference(*CUDie, *CU, ModuleMap))
        Units.emplace_back(*CU, UnitID++, !Options.NoODR, "");
    }

    // Now build the DIE parent links that we will use during the next phase.
    for (auto &CurrentUnit : Units)
      analyzeContextInfo(CurrentUnit.getOrigUnit().getUnitDIE(), 0, CurrentUnit,
                         &ODRContexts.getRoot(), StringPool, ODRContexts);

    // Then mark all the DIEs that need to be present in the linked
    // output and collect some information about them. Note that this
    // loop can not be merged with the previous one becaue cross-cu
    // references require the ParentIdx to be setup for every CU in
    // the object file before calling this.
    for (auto &CurrentUnit : Units)
      lookForDIEsToKeep(RelocMgr, *CurrentUnit.getOrigUnit().getUnitDIE(), *Obj,
                        CurrentUnit, 0);

    // The calls to applyValidRelocs inside cloneDIE will walk the
    // reloc array again (in the same way findValidRelocsInDebugInfo()
    // did). We need to reset the NextValidReloc index to the beginning.
    RelocMgr.resetValidRelocs();
    if (RelocMgr.hasValidRelocs())
      DIECloner(*this, RelocMgr, DIEAlloc, Units, Options)
          .cloneAllCompileUnits(DwarfContext);
    if (!Options.NoOutput && !Units.empty())
      patchFrameInfoForObject(*Obj, DwarfContext,
                              Units[0].getOrigUnit().getAddressByteSize());

    // Clean-up before starting working on the next object.
    endDebugObject();
  }

  // Emit everything that's global.
  if (!Options.NoOutput) {
    Streamer->emitAbbrevs(Abbreviations);
    Streamer->emitStrings(StringPool);
  }

  return Options.NoOutput ? true : Streamer->finish(Map);
}
}

/// \brief Get the offset of string \p S in the string table. This
/// can insert a new element or return the offset of a preexisitng
/// one.
uint32_t NonRelocatableStringpool::getStringOffset(StringRef S) {
  if (S.empty() && !Strings.empty())
    return 0;

  std::pair<uint32_t, StringMapEntryBase *> Entry(0, nullptr);
  MapTy::iterator It;
  bool Inserted;

  // A non-empty string can't be at offset 0, so if we have an entry
  // with a 0 offset, it must be a previously interned string.
  std::tie(It, Inserted) = Strings.insert(std::make_pair(S, Entry));
  if (Inserted || It->getValue().first == 0) {
    // Set offset and chain at the end of the entries list.
    It->getValue().first = CurrentEndOffset;
    CurrentEndOffset += S.size() + 1; // +1 for the '\0'.
    Last->getValue().second = &*It;
    Last = &*It;
  }
  return It->getValue().first;
}

/// \brief Put \p S into the StringMap so that it gets permanent
/// storage, but do not actually link it in the chain of elements
/// that go into the output section. A latter call to
/// getStringOffset() with the same string will chain it though.
StringRef NonRelocatableStringpool::internString(StringRef S) {
  std::pair<uint32_t, StringMapEntryBase *> Entry(0, nullptr);
  auto InsertResult = Strings.insert(std::make_pair(S, Entry));
  return InsertResult.first->getKey();
}

void warn(const Twine &Warning, const Twine &Context) {
  errs() << Twine("while processing ") + Context + ":\n";
  errs() << Twine("warning: ") + Warning + "\n";
}

bool error(const Twine &Error, const Twine &Context) {
  errs() << Twine("while processing ") + Context + ":\n";
  errs() << Twine("error: ") + Error + "\n";
  return false;
}

bool linkDwarf(StringRef OutputFilename, const DebugMap &DM,
               const LinkOptions &Options) {
  DwarfLinker Linker(OutputFilename, Options);
  return Linker.link(DM);
}
}
}
