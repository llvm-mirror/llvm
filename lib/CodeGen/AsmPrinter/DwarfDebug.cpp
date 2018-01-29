//===- llvm/CodeGen/DwarfDebug.cpp - Dwarf Debug Framework ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf debug info into asm files.
//
//===----------------------------------------------------------------------===//

#include "DwarfDebug.h"
#include "ByteStreamer.h"
#include "DIEHash.h"
#include "DebugLocEntry.h"
#include "DebugLocStream.h"
#include "DwarfCompileUnit.h"
#include "DwarfExpression.h"
#include "DwarfFile.h"
#include "DwarfUnit.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AccelTable.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetLoweringObjectFile.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

static cl::opt<bool>
DisableDebugInfoPrinting("disable-debug-info-print", cl::Hidden,
                         cl::desc("Disable debug info printing"));

static cl::opt<bool> UseDwarfRangesBaseAddressSpecifier(
    "use-dwarf-ranges-base-address-specifier", cl::Hidden,
    cl::desc("Use base address specifiers in debug_ranges"), cl::init(false));

static cl::opt<bool> GenerateARangeSection("generate-arange-section",
                                           cl::Hidden,
                                           cl::desc("Generate dwarf aranges"),
                                           cl::init(false));

static cl::opt<bool> SplitDwarfCrossCuReferences(
    "split-dwarf-cross-cu-references", cl::Hidden,
    cl::desc("Enable cross-cu references in DWO files"), cl::init(false));

enum DefaultOnOff { Default, Enable, Disable };

static cl::opt<DefaultOnOff> UnknownLocations(
    "use-unknown-locations", cl::Hidden,
    cl::desc("Make an absence of debug location information explicit."),
    cl::values(clEnumVal(Default, "At top of block or after label"),
               clEnumVal(Enable, "In all cases"), clEnumVal(Disable, "Never")),
    cl::init(Default));

static cl::opt<DefaultOnOff>
DwarfAccelTables("dwarf-accel-tables", cl::Hidden,
                 cl::desc("Output prototype dwarf accelerator tables."),
                 cl::values(clEnumVal(Default, "Default for platform"),
                            clEnumVal(Enable, "Enabled"),
                            clEnumVal(Disable, "Disabled")),
                 cl::init(Default));

enum LinkageNameOption {
  DefaultLinkageNames,
  AllLinkageNames,
  AbstractLinkageNames
};

static cl::opt<LinkageNameOption>
    DwarfLinkageNames("dwarf-linkage-names", cl::Hidden,
                      cl::desc("Which DWARF linkage-name attributes to emit."),
                      cl::values(clEnumValN(DefaultLinkageNames, "Default",
                                            "Default for platform"),
                                 clEnumValN(AllLinkageNames, "All", "All"),
                                 clEnumValN(AbstractLinkageNames, "Abstract",
                                            "Abstract subprograms")),
                      cl::init(DefaultLinkageNames));

static const char *const DWARFGroupName = "dwarf";
static const char *const DWARFGroupDescription = "DWARF Emission";
static const char *const DbgTimerName = "writer";
static const char *const DbgTimerDescription = "DWARF Debug Writer";

void DebugLocDwarfExpression::emitOp(uint8_t Op, const char *Comment) {
  BS.EmitInt8(
      Op, Comment ? Twine(Comment) + " " + dwarf::OperationEncodingString(Op)
                  : dwarf::OperationEncodingString(Op));
}

void DebugLocDwarfExpression::emitSigned(int64_t Value) {
  BS.EmitSLEB128(Value, Twine(Value));
}

void DebugLocDwarfExpression::emitUnsigned(uint64_t Value) {
  BS.EmitULEB128(Value, Twine(Value));
}

bool DebugLocDwarfExpression::isFrameRegister(const TargetRegisterInfo &TRI,
                                              unsigned MachineReg) {
  // This information is not available while emitting .debug_loc entries.
  return false;
}

bool DbgVariable::isBlockByrefVariable() const {
  assert(Var && "Invalid complex DbgVariable!");
  return Var->getType().resolve()->isBlockByrefStruct();
}

const DIType *DbgVariable::getType() const {
  DIType *Ty = Var->getType().resolve();
  // FIXME: isBlockByrefVariable should be reformulated in terms of complex
  // addresses instead.
  if (Ty->isBlockByrefStruct()) {
    /* Byref variables, in Blocks, are declared by the programmer as
       "SomeType VarName;", but the compiler creates a
       __Block_byref_x_VarName struct, and gives the variable VarName
       either the struct, or a pointer to the struct, as its type.  This
       is necessary for various behind-the-scenes things the compiler
       needs to do with by-reference variables in blocks.

       However, as far as the original *programmer* is concerned, the
       variable should still have type 'SomeType', as originally declared.

       The following function dives into the __Block_byref_x_VarName
       struct to find the original type of the variable.  This will be
       passed back to the code generating the type for the Debug
       Information Entry for the variable 'VarName'.  'VarName' will then
       have the original type 'SomeType' in its debug information.

       The original type 'SomeType' will be the type of the field named
       'VarName' inside the __Block_byref_x_VarName struct.

       NOTE: In order for this to not completely fail on the debugger
       side, the Debug Information Entry for the variable VarName needs to
       have a DW_AT_location that tells the debugger how to unwind through
       the pointers and __Block_byref_x_VarName struct to find the actual
       value of the variable.  The function addBlockByrefType does this.  */
    DIType *subType = Ty;
    uint16_t tag = Ty->getTag();

    if (tag == dwarf::DW_TAG_pointer_type)
      subType = resolve(cast<DIDerivedType>(Ty)->getBaseType());

    auto Elements = cast<DICompositeType>(subType)->getElements();
    for (unsigned i = 0, N = Elements.size(); i < N; ++i) {
      auto *DT = cast<DIDerivedType>(Elements[i]);
      if (getName() == DT->getName())
        return resolve(DT->getBaseType());
    }
  }
  return Ty;
}

ArrayRef<DbgVariable::FrameIndexExpr> DbgVariable::getFrameIndexExprs() const {
  if (FrameIndexExprs.size() == 1)
    return FrameIndexExprs;

  assert(llvm::all_of(FrameIndexExprs,
                      [](const FrameIndexExpr &A) {
                        return A.Expr->isFragment();
                      }) &&
         "multiple FI expressions without DW_OP_LLVM_fragment");
  std::sort(FrameIndexExprs.begin(), FrameIndexExprs.end(),
            [](const FrameIndexExpr &A, const FrameIndexExpr &B) -> bool {
              return A.Expr->getFragmentInfo()->OffsetInBits <
                     B.Expr->getFragmentInfo()->OffsetInBits;
            });

  return FrameIndexExprs;
}

void DbgVariable::addMMIEntry(const DbgVariable &V) {
  assert(DebugLocListIndex == ~0U && !MInsn && "not an MMI entry");
  assert(V.DebugLocListIndex == ~0U && !V.MInsn && "not an MMI entry");
  assert(V.Var == Var && "conflicting variable");
  assert(V.IA == IA && "conflicting inlined-at location");

  assert(!FrameIndexExprs.empty() && "Expected an MMI entry");
  assert(!V.FrameIndexExprs.empty() && "Expected an MMI entry");

  // FIXME: This logic should not be necessary anymore, as we now have proper
  // deduplication. However, without it, we currently run into the assertion
  // below, which means that we are likely dealing with broken input, i.e. two
  // non-fragment entries for the same variable at different frame indices.
  if (FrameIndexExprs.size()) {
    auto *Expr = FrameIndexExprs.back().Expr;
    if (!Expr || !Expr->isFragment())
      return;
  }

  for (const auto &FIE : V.FrameIndexExprs)
    // Ignore duplicate entries.
    if (llvm::none_of(FrameIndexExprs, [&](const FrameIndexExpr &Other) {
          return FIE.FI == Other.FI && FIE.Expr == Other.Expr;
        }))
      FrameIndexExprs.push_back(FIE);

  assert((FrameIndexExprs.size() == 1 ||
          llvm::all_of(FrameIndexExprs,
                       [](FrameIndexExpr &FIE) {
                         return FIE.Expr && FIE.Expr->isFragment();
                       })) &&
         "conflicting locations for variable");
}

DwarfDebug::DwarfDebug(AsmPrinter *A, Module *M)
    : DebugHandlerBase(A), DebugLocs(A->OutStreamer->isVerboseAsm()),
      InfoHolder(A, "info_string", DIEValueAllocator),
      SkeletonHolder(A, "skel_string", DIEValueAllocator),
      IsDarwin(A->TM.getTargetTriple().isOSDarwin()), AccelNames(), AccelObjC(),
      AccelNamespace(), AccelTypes() {
  const Triple &TT = Asm->TM.getTargetTriple();

  // Make sure we know our "debugger tuning."  The target option takes
  // precedence; fall back to triple-based defaults.
  if (Asm->TM.Options.DebuggerTuning != DebuggerKind::Default)
    DebuggerTuning = Asm->TM.Options.DebuggerTuning;
  else if (IsDarwin)
    DebuggerTuning = DebuggerKind::LLDB;
  else if (TT.isPS4CPU())
    DebuggerTuning = DebuggerKind::SCE;
  else
    DebuggerTuning = DebuggerKind::GDB;

  // Turn on accelerator tables by default, if tuning for LLDB and the target is
  // supported.
  if (DwarfAccelTables == Default)
    HasDwarfAccelTables =
        tuneForLLDB() && A->TM.getTargetTriple().isOSBinFormatMachO();
  else
    HasDwarfAccelTables = DwarfAccelTables == Enable;

  HasAppleExtensionAttributes = tuneForLLDB();

  // Handle split DWARF.
  HasSplitDwarf = !Asm->TM.Options.MCOptions.SplitDwarfFile.empty();

  // SCE defaults to linkage names only for abstract subprograms.
  if (DwarfLinkageNames == DefaultLinkageNames)
    UseAllLinkageNames = !tuneForSCE();
  else
    UseAllLinkageNames = DwarfLinkageNames == AllLinkageNames;

  unsigned DwarfVersionNumber = Asm->TM.Options.MCOptions.DwarfVersion;
  unsigned DwarfVersion = DwarfVersionNumber ? DwarfVersionNumber
                                    : MMI->getModule()->getDwarfVersion();
  // Use dwarf 4 by default if nothing is requested.
  DwarfVersion = DwarfVersion ? DwarfVersion : dwarf::DWARF_VERSION;

  // Work around a GDB bug. GDB doesn't support the standard opcode;
  // SCE doesn't support GNU's; LLDB prefers the standard opcode, which
  // is defined as of DWARF 3.
  // See GDB bug 11616 - DW_OP_form_tls_address is unimplemented
  // https://sourceware.org/bugzilla/show_bug.cgi?id=11616
  UseGNUTLSOpcode = tuneForGDB() || DwarfVersion < 3;

  // GDB does not fully support the DWARF 4 representation for bitfields.
  UseDWARF2Bitfields = (DwarfVersion < 4) || tuneForGDB();

  // The DWARF v5 string offsets table has - possibly shared - contributions
  // from each compile and type unit each preceded by a header. The string
  // offsets table used by the pre-DWARF v5 split-DWARF implementation uses
  // a monolithic string offsets table without any header.
  UseSegmentedStringOffsetsTable = DwarfVersion >= 5;

  Asm->OutStreamer->getContext().setDwarfVersion(DwarfVersion);
}

// Define out of line so we don't have to include DwarfUnit.h in DwarfDebug.h.
DwarfDebug::~DwarfDebug() = default;

static bool isObjCClass(StringRef Name) {
  return Name.startswith("+") || Name.startswith("-");
}

static bool hasObjCCategory(StringRef Name) {
  if (!isObjCClass(Name))
    return false;

  return Name.find(") ") != StringRef::npos;
}

static void getObjCClassCategory(StringRef In, StringRef &Class,
                                 StringRef &Category) {
  if (!hasObjCCategory(In)) {
    Class = In.slice(In.find('[') + 1, In.find(' '));
    Category = "";
    return;
  }

  Class = In.slice(In.find('[') + 1, In.find('('));
  Category = In.slice(In.find('[') + 1, In.find(' '));
}

static StringRef getObjCMethodName(StringRef In) {
  return In.slice(In.find(' ') + 1, In.find(']'));
}

// Add the various names to the Dwarf accelerator table names.
// TODO: Determine whether or not we should add names for programs
// that do not have a DW_AT_name or DW_AT_linkage_name field - this
// is only slightly different than the lookup of non-standard ObjC names.
void DwarfDebug::addSubprogramNames(const DISubprogram *SP, DIE &Die) {
  if (!SP->isDefinition())
    return;
  addAccelName(SP->getName(), Die);

  // If the linkage name is different than the name, go ahead and output
  // that as well into the name table.
  if (SP->getLinkageName() != "" && SP->getName() != SP->getLinkageName())
    addAccelName(SP->getLinkageName(), Die);

  // If this is an Objective-C selector name add it to the ObjC accelerator
  // too.
  if (isObjCClass(SP->getName())) {
    StringRef Class, Category;
    getObjCClassCategory(SP->getName(), Class, Category);
    addAccelObjC(Class, Die);
    if (Category != "")
      addAccelObjC(Category, Die);
    // Also add the base method name to the name table.
    addAccelName(getObjCMethodName(SP->getName()), Die);
  }
}

/// Check whether we should create a DIE for the given Scope, return true
/// if we don't create a DIE (the corresponding DIE is null).
bool DwarfDebug::isLexicalScopeDIENull(LexicalScope *Scope) {
  if (Scope->isAbstractScope())
    return false;

  // We don't create a DIE if there is no Range.
  const SmallVectorImpl<InsnRange> &Ranges = Scope->getRanges();
  if (Ranges.empty())
    return true;

  if (Ranges.size() > 1)
    return false;

  // We don't create a DIE if we have a single Range and the end label
  // is null.
  return !getLabelAfterInsn(Ranges.front().second);
}

template <typename Func> static void forBothCUs(DwarfCompileUnit &CU, Func F) {
  F(CU);
  if (auto *SkelCU = CU.getSkeleton())
    if (CU.getCUNode()->getSplitDebugInlining())
      F(*SkelCU);
}

bool DwarfDebug::shareAcrossDWOCUs() const {
  return SplitDwarfCrossCuReferences;
}

void DwarfDebug::constructAbstractSubprogramScopeDIE(DwarfCompileUnit &SrcCU,
                                                     LexicalScope *Scope) {
  assert(Scope && Scope->getScopeNode());
  assert(Scope->isAbstractScope());
  assert(!Scope->getInlinedAt());

  auto *SP = cast<DISubprogram>(Scope->getScopeNode());

  // Find the subprogram's DwarfCompileUnit in the SPMap in case the subprogram
  // was inlined from another compile unit.
  if (useSplitDwarf() && !shareAcrossDWOCUs() && !SP->getUnit()->getSplitDebugInlining())
    // Avoid building the original CU if it won't be used
    SrcCU.constructAbstractSubprogramScopeDIE(Scope);
  else {
    auto &CU = getOrCreateDwarfCompileUnit(SP->getUnit());
    if (auto *SkelCU = CU.getSkeleton()) {
      (shareAcrossDWOCUs() ? CU : SrcCU)
          .constructAbstractSubprogramScopeDIE(Scope);
      if (CU.getCUNode()->getSplitDebugInlining())
        SkelCU->constructAbstractSubprogramScopeDIE(Scope);
    } else
      CU.constructAbstractSubprogramScopeDIE(Scope);
  }
}

void DwarfDebug::addGnuPubAttributes(DwarfCompileUnit &U, DIE &D) const {
  if (!U.hasDwarfPubSections())
    return;

  U.addFlag(D, dwarf::DW_AT_GNU_pubnames);
}

// Create new DwarfCompileUnit for the given metadata node with tag
// DW_TAG_compile_unit.
DwarfCompileUnit &
DwarfDebug::getOrCreateDwarfCompileUnit(const DICompileUnit *DIUnit) {
  if (auto *CU = CUMap.lookup(DIUnit))
    return *CU;
  StringRef FN = DIUnit->getFilename();
  CompilationDir = DIUnit->getDirectory();

  auto OwnedUnit = llvm::make_unique<DwarfCompileUnit>(
      InfoHolder.getUnits().size(), DIUnit, Asm, this, &InfoHolder);
  DwarfCompileUnit &NewCU = *OwnedUnit;
  DIE &Die = NewCU.getUnitDie();
  InfoHolder.addUnit(std::move(OwnedUnit));
  if (useSplitDwarf()) {
    NewCU.setSkeleton(constructSkeletonCU(NewCU));
    NewCU.addString(Die, dwarf::DW_AT_GNU_dwo_name,
                  Asm->TM.Options.MCOptions.SplitDwarfFile);
  }

  for (auto *IE : DIUnit->getImportedEntities())
    NewCU.addImportedEntity(IE);

  // LTO with assembly output shares a single line table amongst multiple CUs.
  // To avoid the compilation directory being ambiguous, let the line table
  // explicitly describe the directory of all files, never relying on the
  // compilation directory.
  if (!Asm->OutStreamer->hasRawTextSupport() || SingleCU)
    Asm->OutStreamer->getContext().setMCLineTableCompilationDir(
        NewCU.getUniqueID(), CompilationDir);

  StringRef Producer = DIUnit->getProducer();
  StringRef Flags = DIUnit->getFlags();
  if (!Flags.empty()) {
    std::string ProducerWithFlags = Producer.str() + " " + Flags.str();
    NewCU.addString(Die, dwarf::DW_AT_producer, ProducerWithFlags);
  } else
    NewCU.addString(Die, dwarf::DW_AT_producer, Producer);

  NewCU.addUInt(Die, dwarf::DW_AT_language, dwarf::DW_FORM_data2,
                DIUnit->getSourceLanguage());
  NewCU.addString(Die, dwarf::DW_AT_name, FN);

  // Add DW_str_offsets_base to the unit DIE, except for split units.
  if (useSegmentedStringOffsetsTable() && !useSplitDwarf())
    NewCU.addStringOffsetsStart();

  if (!useSplitDwarf()) {
    NewCU.initStmtList();

    // If we're using split dwarf the compilation dir is going to be in the
    // skeleton CU and so we don't need to duplicate it here.
    if (!CompilationDir.empty())
      NewCU.addString(Die, dwarf::DW_AT_comp_dir, CompilationDir);

    addGnuPubAttributes(NewCU, Die);
  }

  if (useAppleExtensionAttributes()) {
    if (DIUnit->isOptimized())
      NewCU.addFlag(Die, dwarf::DW_AT_APPLE_optimized);

    StringRef Flags = DIUnit->getFlags();
    if (!Flags.empty())
      NewCU.addString(Die, dwarf::DW_AT_APPLE_flags, Flags);

    if (unsigned RVer = DIUnit->getRuntimeVersion())
      NewCU.addUInt(Die, dwarf::DW_AT_APPLE_major_runtime_vers,
                    dwarf::DW_FORM_data1, RVer);
  }

  if (useSplitDwarf())
    NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoDWOSection());
  else
    NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoSection());

  if (DIUnit->getDWOId()) {
    // This CU is either a clang module DWO or a skeleton CU.
    NewCU.addUInt(Die, dwarf::DW_AT_GNU_dwo_id, dwarf::DW_FORM_data8,
                  DIUnit->getDWOId());
    if (!DIUnit->getSplitDebugFilename().empty())
      // This is a prefabricated skeleton CU.
      NewCU.addString(Die, dwarf::DW_AT_GNU_dwo_name,
                      DIUnit->getSplitDebugFilename());
  }

  CUMap.insert({DIUnit, &NewCU});
  CUDieMap.insert({&Die, &NewCU});
  return NewCU;
}

void DwarfDebug::constructAndAddImportedEntityDIE(DwarfCompileUnit &TheCU,
                                                  const DIImportedEntity *N) {
  if (isa<DILocalScope>(N->getScope()))
    return;
  if (DIE *D = TheCU.getOrCreateContextDIE(N->getScope()))
    D->addChild(TheCU.constructImportedEntityDIE(N));
}

/// Sort and unique GVEs by comparing their fragment offset.
static SmallVectorImpl<DwarfCompileUnit::GlobalExpr> &
sortGlobalExprs(SmallVectorImpl<DwarfCompileUnit::GlobalExpr> &GVEs) {
  std::sort(GVEs.begin(), GVEs.end(),
            [](DwarfCompileUnit::GlobalExpr A, DwarfCompileUnit::GlobalExpr B) {
              // Sort order: first null exprs, then exprs without fragment
              // info, then sort by fragment offset in bits.
              // FIXME: Come up with a more comprehensive comparator so
              // the sorting isn't non-deterministic, and so the following
              // std::unique call works correctly.
              if (!A.Expr || !B.Expr)
                return !!B.Expr;
              auto FragmentA = A.Expr->getFragmentInfo();
              auto FragmentB = B.Expr->getFragmentInfo();
              if (!FragmentA || !FragmentB)
                return !!FragmentB;
              return FragmentA->OffsetInBits < FragmentB->OffsetInBits;
            });
  GVEs.erase(std::unique(GVEs.begin(), GVEs.end(),
                         [](DwarfCompileUnit::GlobalExpr A,
                            DwarfCompileUnit::GlobalExpr B) {
                           return A.Expr == B.Expr;
                         }),
             GVEs.end());
  return GVEs;
}

// Emit all Dwarf sections that should come prior to the content. Create
// global DIEs and emit initial debug info sections. This is invoked by
// the target AsmPrinter.
void DwarfDebug::beginModule() {
  NamedRegionTimer T(DbgTimerName, DbgTimerDescription, DWARFGroupName,
                     DWARFGroupDescription, TimePassesIsEnabled);
  if (DisableDebugInfoPrinting)
    return;

  const Module *M = MMI->getModule();

  unsigned NumDebugCUs = std::distance(M->debug_compile_units_begin(),
                                       M->debug_compile_units_end());
  // Tell MMI whether we have debug info.
  MMI->setDebugInfoAvailability(NumDebugCUs > 0);
  SingleCU = NumDebugCUs == 1;
  DenseMap<DIGlobalVariable *, SmallVector<DwarfCompileUnit::GlobalExpr, 1>>
      GVMap;
  for (const GlobalVariable &Global : M->globals()) {
    SmallVector<DIGlobalVariableExpression *, 1> GVs;
    Global.getDebugInfo(GVs);
    for (auto *GVE : GVs)
      GVMap[GVE->getVariable()].push_back({&Global, GVE->getExpression()});
  }

  // Create the symbol that designates the start of the unit's contribution
  // to the string offsets table. In a split DWARF scenario, only the skeleton
  // unit has the DW_AT_str_offsets_base attribute (and hence needs the symbol).
  if (useSegmentedStringOffsetsTable())
    (useSplitDwarf() ? SkeletonHolder : InfoHolder)
        .setStringOffsetsStartSym(Asm->createTempSymbol("str_offsets_base"));

  for (DICompileUnit *CUNode : M->debug_compile_units()) {
    // FIXME: Move local imported entities into a list attached to the
    // subprogram, then this search won't be needed and a
    // getImportedEntities().empty() test should go below with the rest.
    bool HasNonLocalImportedEntities = llvm::any_of(
        CUNode->getImportedEntities(), [](const DIImportedEntity *IE) {
          return !isa<DILocalScope>(IE->getScope());
        });

    if (!HasNonLocalImportedEntities && CUNode->getEnumTypes().empty() &&
        CUNode->getRetainedTypes().empty() &&
        CUNode->getGlobalVariables().empty() && CUNode->getMacros().empty())
      continue;

    DwarfCompileUnit &CU = getOrCreateDwarfCompileUnit(CUNode);

    // Global Variables.
    for (auto *GVE : CUNode->getGlobalVariables()) {
      // Don't bother adding DIGlobalVariableExpressions listed in the CU if we
      // already know about the variable and it isn't adding a constant
      // expression.
      auto &GVMapEntry = GVMap[GVE->getVariable()];
      auto *Expr = GVE->getExpression();
      if (!GVMapEntry.size() || (Expr && Expr->isConstant()))
        GVMapEntry.push_back({nullptr, Expr});
    }
    DenseSet<DIGlobalVariable *> Processed;
    for (auto *GVE : CUNode->getGlobalVariables()) {
      DIGlobalVariable *GV = GVE->getVariable();
      if (Processed.insert(GV).second)
        CU.getOrCreateGlobalVariableDIE(GV, sortGlobalExprs(GVMap[GV]));
    }

    for (auto *Ty : CUNode->getEnumTypes()) {
      // The enum types array by design contains pointers to
      // MDNodes rather than DIRefs. Unique them here.
      CU.getOrCreateTypeDIE(cast<DIType>(Ty));
    }
    for (auto *Ty : CUNode->getRetainedTypes()) {
      // The retained types array by design contains pointers to
      // MDNodes rather than DIRefs. Unique them here.
      if (DIType *RT = dyn_cast<DIType>(Ty))
          // There is no point in force-emitting a forward declaration.
          CU.getOrCreateTypeDIE(RT);
    }
    // Emit imported_modules last so that the relevant context is already
    // available.
    for (auto *IE : CUNode->getImportedEntities())
      constructAndAddImportedEntityDIE(CU, IE);
  }
}

void DwarfDebug::finishVariableDefinitions() {
  for (const auto &Var : ConcreteVariables) {
    DIE *VariableDie = Var->getDIE();
    assert(VariableDie);
    // FIXME: Consider the time-space tradeoff of just storing the unit pointer
    // in the ConcreteVariables list, rather than looking it up again here.
    // DIE::getUnit isn't simple - it walks parent pointers, etc.
    DwarfCompileUnit *Unit = CUDieMap.lookup(VariableDie->getUnitDie());
    assert(Unit);
    Unit->finishVariableDefinition(*Var);
  }
}

void DwarfDebug::finishSubprogramDefinitions() {
  for (const DISubprogram *SP : ProcessedSPNodes) {
    assert(SP->getUnit()->getEmissionKind() != DICompileUnit::NoDebug);
    forBothCUs(
        getOrCreateDwarfCompileUnit(SP->getUnit()),
        [&](DwarfCompileUnit &CU) { CU.finishSubprogramDefinition(SP); });
  }
}

void DwarfDebug::finalizeModuleInfo() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  finishSubprogramDefinitions();

  finishVariableDefinitions();

  // Include the DWO file name in the hash if there's more than one CU.
  // This handles ThinLTO's situation where imported CUs may very easily be
  // duplicate with the same CU partially imported into another ThinLTO unit.
  StringRef DWOName;
  if (CUMap.size() > 1)
    DWOName = Asm->TM.Options.MCOptions.SplitDwarfFile;

  // Handle anything that needs to be done on a per-unit basis after
  // all other generation.
  for (const auto &P : CUMap) {
    auto &TheCU = *P.second;
    // Emit DW_AT_containing_type attribute to connect types with their
    // vtable holding type.
    TheCU.constructContainingTypeDIEs();

    // Add CU specific attributes if we need to add any.
    // If we're splitting the dwarf out now that we've got the entire
    // CU then add the dwo id to it.
    auto *SkCU = TheCU.getSkeleton();
    if (useSplitDwarf()) {
      // Emit a unique identifier for this CU.
      uint64_t ID =
          DIEHash(Asm).computeCUSignature(DWOName, TheCU.getUnitDie());
      TheCU.addUInt(TheCU.getUnitDie(), dwarf::DW_AT_GNU_dwo_id,
                    dwarf::DW_FORM_data8, ID);
      SkCU->addUInt(SkCU->getUnitDie(), dwarf::DW_AT_GNU_dwo_id,
                    dwarf::DW_FORM_data8, ID);

      // We don't keep track of which addresses are used in which CU so this
      // is a bit pessimistic under LTO.
      if (!AddrPool.isEmpty()) {
        const MCSymbol *Sym = TLOF.getDwarfAddrSection()->getBeginSymbol();
        SkCU->addSectionLabel(SkCU->getUnitDie(), dwarf::DW_AT_GNU_addr_base,
                              Sym, Sym);
      }
      if (!SkCU->getRangeLists().empty()) {
        const MCSymbol *Sym = TLOF.getDwarfRangesSection()->getBeginSymbol();
        SkCU->addSectionLabel(SkCU->getUnitDie(), dwarf::DW_AT_GNU_ranges_base,
                              Sym, Sym);
      }
    }

    // If we have code split among multiple sections or non-contiguous
    // ranges of code then emit a DW_AT_ranges attribute on the unit that will
    // remain in the .o file, otherwise add a DW_AT_low_pc.
    // FIXME: We should use ranges allow reordering of code ala
    // .subsections_via_symbols in mach-o. This would mean turning on
    // ranges for all subprogram DIEs for mach-o.
    DwarfCompileUnit &U = SkCU ? *SkCU : TheCU;
    if (unsigned NumRanges = TheCU.getRanges().size()) {
      if (NumRanges > 1)
        // A DW_AT_low_pc attribute may also be specified in combination with
        // DW_AT_ranges to specify the default base address for use in
        // location lists (see Section 2.6.2) and range lists (see Section
        // 2.17.3).
        U.addUInt(U.getUnitDie(), dwarf::DW_AT_low_pc, dwarf::DW_FORM_addr, 0);
      else
        U.setBaseAddress(TheCU.getRanges().front().getStart());
      U.attachRangesOrLowHighPC(U.getUnitDie(), TheCU.takeRanges());
    }

    auto *CUNode = cast<DICompileUnit>(P.first);
    // If compile Unit has macros, emit "DW_AT_macro_info" attribute.
    if (CUNode->getMacros())
      U.addSectionLabel(U.getUnitDie(), dwarf::DW_AT_macro_info,
                        U.getMacroLabelBegin(),
                        TLOF.getDwarfMacinfoSection()->getBeginSymbol());
  }

  // Emit all frontend-produced Skeleton CUs, i.e., Clang modules.
  for (auto *CUNode : MMI->getModule()->debug_compile_units())
    if (CUNode->getDWOId())
      getOrCreateDwarfCompileUnit(CUNode);

  // Compute DIE offsets and sizes.
  InfoHolder.computeSizeAndOffsets();
  if (useSplitDwarf())
    SkeletonHolder.computeSizeAndOffsets();
}

// Emit all Dwarf sections that should come after the content.
void DwarfDebug::endModule() {
  assert(CurFn == nullptr);
  assert(CurMI == nullptr);

  // If we aren't actually generating debug info (check beginModule -
  // conditionalized on !DisableDebugInfoPrinting and the presence of the
  // llvm.dbg.cu metadata node)
  if (!MMI->hasDebugInfo())
    return;

  // Finalize the debug info for the module.
  finalizeModuleInfo();

  emitDebugStr();

  if (useSplitDwarf())
    emitDebugLocDWO();
  else
    // Emit info into a debug loc section.
    emitDebugLoc();

  // Corresponding abbreviations into a abbrev section.
  emitAbbreviations();

  // Emit all the DIEs into a debug info section.
  emitDebugInfo();

  // Emit info into a debug aranges section.
  if (GenerateARangeSection)
    emitDebugARanges();

  // Emit info into a debug ranges section.
  emitDebugRanges();

  // Emit info into a debug macinfo section.
  emitDebugMacinfo();

  if (useSplitDwarf()) {
    emitDebugStrDWO();
    emitDebugInfoDWO();
    emitDebugAbbrevDWO();
    emitDebugLineDWO();
    // Emit DWO addresses.
    AddrPool.emit(*Asm, Asm->getObjFileLowering().getDwarfAddrSection());
  }

  // Emit info into the dwarf accelerator table sections.
  if (useDwarfAccelTables()) {
    emitAccelNames();
    emitAccelObjC();
    emitAccelNamespaces();
    emitAccelTypes();
  }

  // Emit the pubnames and pubtypes sections if requested.
  emitDebugPubSections();

  // clean up.
  // FIXME: AbstractVariables.clear();
}

void DwarfDebug::ensureAbstractVariableIsCreated(DwarfCompileUnit &CU, InlinedVariable IV,
                                                 const MDNode *ScopeNode) {
  const DILocalVariable *Cleansed = nullptr;
  if (CU.getExistingAbstractVariable(IV, Cleansed))
    return;

  CU.createAbstractVariable(Cleansed, LScopes.getOrCreateAbstractScope(
                                       cast<DILocalScope>(ScopeNode)));
}

void DwarfDebug::ensureAbstractVariableIsCreatedIfScoped(DwarfCompileUnit &CU,
    InlinedVariable IV, const MDNode *ScopeNode) {
  const DILocalVariable *Cleansed = nullptr;
  if (CU.getExistingAbstractVariable(IV, Cleansed))
    return;

  if (LexicalScope *Scope =
          LScopes.findAbstractScope(cast_or_null<DILocalScope>(ScopeNode)))
    CU.createAbstractVariable(Cleansed, Scope);
}

DIE *DwarfDebug::getSubrangeDie(const DIFortranSubrange *SR) const {
  auto I = SubrangeDieMap.find(SR);
  return (I == SubrangeDieMap.end()) ? nullptr : I->second;
}

void DwarfDebug::constructSubrangeDie(const DIFortranArrayType *AT,
                                      DbgVariable &DV,
                                      DwarfCompileUnit &TheCU) {
  dwarf::Attribute Attribute;
  const DIFortranSubrange *WFS = nullptr;
  DIExpression *WEx = nullptr;
  const DIVariable *DI = DV.getVariable();
  DINodeArray Elements = AT->getElements();

  for (unsigned i = 0, N = Elements.size(); i < N; ++i) {
    DINode *Element = cast<DINode>(Elements[i]);
    if (const DIFortranSubrange *FS = dyn_cast<DIFortranSubrange>(Element)) {
      if (DIVariable *UBV = FS->getUpperBound())
        if (UBV == DI) {
          Attribute = dwarf::DW_AT_upper_bound;
          WFS = FS;
          WEx = FS->getUpperBoundExp();
          break;
        }
      if (DIVariable *LBV = FS->getLowerBound())
        if (LBV == DI) {
          Attribute = dwarf::DW_AT_lower_bound;
          WFS = FS;
          WEx = FS->getLowerBoundExp();
          break;
        }          
    }
  }

  if (!WFS)
    return;

  DIE *Die;
  auto I = SubrangeDieMap.find(WFS);
  if (I == SubrangeDieMap.end()) {
    Die = DIE::get(DIEValueAllocator, dwarf::DW_TAG_subrange_type);
    SubrangeDieMap[WFS] = Die;
  } else {
    Die = I->second;
  }

  TheCU.constructDieLocationAddExpr(*Die, Attribute, DV, WEx);
}

// Collect variable information from side table maintained by MF.
void DwarfDebug::collectVariableInfoFromMFTable(
    DwarfCompileUnit &TheCU, DenseSet<InlinedVariable> &Processed) {
  SmallDenseMap<InlinedVariable, DbgVariable *> MFVars;
  for (const auto &VI : Asm->MF->getVariableDbgInfo()) {
    if (!VI.Var)
      continue;
    assert(VI.Var->isValidLocationForIntrinsic(VI.Loc) &&
           "Expected inlined-at fields to agree");

    InlinedVariable Var(VI.Var, VI.Loc->getInlinedAt());
    Processed.insert(Var);
    LexicalScope *Scope = LScopes.findLexicalScope(VI.Loc);

    // If variable scope is not found then skip this variable.
    if (!Scope)
      continue;

    ensureAbstractVariableIsCreatedIfScoped(TheCU, Var, Scope->getScopeNode());
    auto RegVar = llvm::make_unique<DbgVariable>(Var.first, Var.second);
    RegVar->initializeMMI(VI.Expr, VI.Slot);
    if (VariableInDependentType.count(VI.Var)) {
      const DIType *DT = VariableInDependentType[VI.Var];
      if (const DIFortranArrayType *AT = dyn_cast<DIFortranArrayType>(DT))
        constructSubrangeDie(AT, *RegVar.get(), TheCU);
    } 
    if (DbgVariable *DbgVar = MFVars.lookup(Var))
      DbgVar->addMMIEntry(*RegVar);
    else if (InfoHolder.addScopeVariable(Scope, RegVar.get())) {
      MFVars.insert({Var, RegVar.get()});
      ConcreteVariables.push_back(std::move(RegVar));
    }
  }
}

// Get .debug_loc entry for the instruction range starting at MI.
static DebugLocEntry::Value getDebugLocValue(const MachineInstr *MI) {
  const DIExpression *Expr = MI->getDebugExpression();
  assert(MI->getNumOperands() == 4);
  if (MI->getOperand(0).isReg()) {
    auto RegOp = MI->getOperand(0);
    auto Op1 = MI->getOperand(1);
    // If the second operand is an immediate, this is a
    // register-indirect address.
    assert((!Op1.isImm() || (Op1.getImm() == 0)) && "unexpected offset");
    MachineLocation MLoc(RegOp.getReg(), Op1.isImm());
    return DebugLocEntry::Value(Expr, MLoc);
  }
  if (MI->getOperand(0).isImm())
    return DebugLocEntry::Value(Expr, MI->getOperand(0).getImm());
  if (MI->getOperand(0).isFPImm())
    return DebugLocEntry::Value(Expr, MI->getOperand(0).getFPImm());
  if (MI->getOperand(0).isCImm())
    return DebugLocEntry::Value(Expr, MI->getOperand(0).getCImm());

  llvm_unreachable("Unexpected 4-operand DBG_VALUE instruction!");
}

/// \brief If this and Next are describing different fragments of the same
/// variable, merge them by appending Next's values to the current
/// list of values.
/// Return true if the merge was successful.
bool DebugLocEntry::MergeValues(const DebugLocEntry &Next) {
  if (Begin == Next.Begin) {
    auto *FirstExpr = cast<DIExpression>(Values[0].Expression);
    auto *FirstNextExpr = cast<DIExpression>(Next.Values[0].Expression);
    if (!FirstExpr->isFragment() || !FirstNextExpr->isFragment())
      return false;

    // We can only merge entries if none of the fragments overlap any others.
    // In doing so, we can take advantage of the fact that both lists are
    // sorted.
    for (unsigned i = 0, j = 0; i < Values.size(); ++i) {
      for (; j < Next.Values.size(); ++j) {
        int res = DebugHandlerBase::fragmentCmp(
            cast<DIExpression>(Values[i].Expression),
            cast<DIExpression>(Next.Values[j].Expression));
        if (res == 0) // The two expressions overlap, we can't merge.
          return false;
        // Values[i] is entirely before Next.Values[j],
        // so go back to the next entry of Values.
        else if (res == -1)
          break;
        // Next.Values[j] is entirely before Values[i], so go on to the
        // next entry of Next.Values.
      }
    }

    addValues(Next.Values);
    End = Next.End;
    return true;
  }
  return false;
}

/// Build the location list for all DBG_VALUEs in the function that
/// describe the same variable.  If the ranges of several independent
/// fragments of the same variable overlap partially, split them up and
/// combine the ranges. The resulting DebugLocEntries are will have
/// strict monotonically increasing begin addresses and will never
/// overlap.
//
// Input:
//
//   Ranges History [var, loc, fragment ofs size]
// 0 |      [x, (reg0, fragment 0, 32)]
// 1 | |    [x, (reg1, fragment 32, 32)] <- IsFragmentOfPrevEntry
// 2 | |    ...
// 3   |    [clobber reg0]
// 4        [x, (mem, fragment 0, 64)] <- overlapping with both previous fragments of
//                                     x.
//
// Output:
//
// [0-1]    [x, (reg0, fragment  0, 32)]
// [1-3]    [x, (reg0, fragment  0, 32), (reg1, fragment 32, 32)]
// [3-4]    [x, (reg1, fragment 32, 32)]
// [4- ]    [x, (mem,  fragment  0, 64)]
void
DwarfDebug::buildLocationList(SmallVectorImpl<DebugLocEntry> &DebugLoc,
                              const DbgValueHistoryMap::InstrRanges &Ranges) {
  SmallVector<DebugLocEntry::Value, 4> OpenRanges;

  for (auto I = Ranges.begin(), E = Ranges.end(); I != E; ++I) {
    const MachineInstr *Begin = I->first;
    const MachineInstr *End = I->second;
    assert(Begin->isDebugValue() && "Invalid History entry");

    // Check if a variable is inaccessible in this range.
    if (Begin->getNumOperands() > 1 &&
        Begin->getOperand(0).isReg() && !Begin->getOperand(0).getReg()) {
      OpenRanges.clear();
      continue;
    }

    // If this fragment overlaps with any open ranges, truncate them.
    const DIExpression *DIExpr = Begin->getDebugExpression();
    auto Last = remove_if(OpenRanges, [&](DebugLocEntry::Value R) {
      return fragmentsOverlap(DIExpr, R.getExpression());
    });
    OpenRanges.erase(Last, OpenRanges.end());

    const MCSymbol *StartLabel = getLabelBeforeInsn(Begin);
    assert(StartLabel && "Forgot label before DBG_VALUE starting a range!");

    const MCSymbol *EndLabel;
    if (End != nullptr)
      EndLabel = getLabelAfterInsn(End);
    else if (std::next(I) == Ranges.end())
      EndLabel = Asm->getFunctionEnd();
    else
      EndLabel = getLabelBeforeInsn(std::next(I)->first);
    assert(EndLabel && "Forgot label after instruction ending a range!");

    DEBUG(dbgs() << "DotDebugLoc: " << *Begin << "\n");

    auto Value = getDebugLocValue(Begin);
    DebugLocEntry Loc(StartLabel, EndLabel, Value);
    bool couldMerge = false;

    // If this is a fragment, it may belong to the current DebugLocEntry.
    if (DIExpr->isFragment()) {
      // Add this value to the list of open ranges.
      OpenRanges.push_back(Value);

      // Attempt to add the fragment to the last entry.
      if (!DebugLoc.empty())
        if (DebugLoc.back().MergeValues(Loc))
          couldMerge = true;
    }

    if (!couldMerge) {
      // Need to add a new DebugLocEntry. Add all values from still
      // valid non-overlapping fragments.
      if (OpenRanges.size())
        Loc.addValues(OpenRanges);

      DebugLoc.push_back(std::move(Loc));
    }

    // Attempt to coalesce the ranges of two otherwise identical
    // DebugLocEntries.
    auto CurEntry = DebugLoc.rbegin();
    DEBUG({
      dbgs() << CurEntry->getValues().size() << " Values:\n";
      for (auto &Value : CurEntry->getValues())
        Value.dump();
      dbgs() << "-----\n";
    });

    auto PrevEntry = std::next(CurEntry);
    if (PrevEntry != DebugLoc.rend() && PrevEntry->MergeRanges(*CurEntry))
      DebugLoc.pop_back();
  }
}

DbgVariable *DwarfDebug::createConcreteVariable(DwarfCompileUnit &TheCU,
                                                LexicalScope &Scope,
                                                InlinedVariable IV) {
  ensureAbstractVariableIsCreatedIfScoped(TheCU, IV, Scope.getScopeNode());
  ConcreteVariables.push_back(
      llvm::make_unique<DbgVariable>(IV.first, IV.second));
  InfoHolder.addScopeVariable(&Scope, ConcreteVariables.back().get());
  return ConcreteVariables.back().get();
}

/// Determine whether a *singular* DBG_VALUE is valid for the entirety of its
/// enclosing lexical scope. The check ensures there are no other instructions
/// in the same lexical scope preceding the DBG_VALUE and that its range is
/// either open or otherwise rolls off the end of the scope.
static bool validThroughout(LexicalScopes &LScopes,
                            const MachineInstr *DbgValue,
                            const MachineInstr *RangeEnd) {
  assert(DbgValue->getDebugLoc() && "DBG_VALUE without a debug location");
  auto MBB = DbgValue->getParent();
  auto DL = DbgValue->getDebugLoc();
  auto *LScope = LScopes.findLexicalScope(DL);
  // Scope doesn't exist; this is a dead DBG_VALUE.
  if (!LScope)
    return false;
  auto &LSRange = LScope->getRanges();
  if (LSRange.size() == 0)
    return false;

  // Determine if the DBG_VALUE is valid at the beginning of its lexical block.
  const MachineInstr *LScopeBegin = LSRange.front().first;
  // Early exit if the lexical scope begins outside of the current block.
  if (LScopeBegin->getParent() != MBB)
    return false;
  MachineBasicBlock::const_reverse_iterator Pred(DbgValue);
  for (++Pred; Pred != MBB->rend(); ++Pred) {
    if (Pred->getFlag(MachineInstr::FrameSetup))
      break;
    auto PredDL = Pred->getDebugLoc();
    if (!PredDL || Pred->isMetaInstruction())
      continue;
    // Check whether the instruction preceding the DBG_VALUE is in the same
    // (sub)scope as the DBG_VALUE.
    if (DL->getScope() == PredDL->getScope())
      return false;
    auto *PredScope = LScopes.findLexicalScope(PredDL);
    if (!PredScope || LScope->dominates(PredScope))
      return false;
  }

  // If the range of the DBG_VALUE is open-ended, report success.
  if (!RangeEnd)
    return true;

  // Fail if there are instructions belonging to our scope in another block.
  const MachineInstr *LScopeEnd = LSRange.back().second;
  if (LScopeEnd->getParent() != MBB)
    return false;

  // Single, constant DBG_VALUEs in the prologue are promoted to be live
  // throughout the function. This is a hack, presumably for DWARF v2 and not
  // necessarily correct. It would be much better to use a dbg.declare instead
  // if we know the constant is live throughout the scope.
  if (DbgValue->getOperand(0).isImm() && MBB->pred_empty())
    return true;

  return false;
}

void DwarfDebug::populateDependentTypeMap() {
  for (const auto &I : DbgValues) {
    InlinedVariable IV = I.first;
    if (I.second.empty())
      continue;

    if (const DIStringType *ST = dyn_cast<DIStringType>(
            static_cast<const Metadata *>(IV.first->getType())))
      if (const DIVariable *LV = ST->getStringLength())
        VariableInDependentType[LV] = ST;

    if (const DIFortranArrayType *AT = dyn_cast<DIFortranArrayType>(
            static_cast<const Metadata *>(IV.first->getType())))
      for (const DINode *S : AT->getElements())
        if (const DIFortranSubrange *FS = dyn_cast<DIFortranSubrange>(S)) {
          if (const DIVariable *LBV = FS->getLowerBound())
            VariableInDependentType[LBV] = AT;
          if (const DIVariable *UBV = FS->getUpperBound())
            VariableInDependentType[UBV] = AT;
        }
  }
}

// Find variables for each lexical scope.
void DwarfDebug::collectVariableInfo(DwarfCompileUnit &TheCU,
                                     const DISubprogram *SP,
                                     DenseSet<InlinedVariable> &Processed) {
  clearDependentTracking();
  populateDependentTypeMap();
  // Grab the variable info that was squirreled away in the MMI side-table.
  collectVariableInfoFromMFTable(TheCU, Processed);

  for (const auto &I : DbgValues) {
    InlinedVariable IV = I.first;
    if (Processed.count(IV))
      continue;

    // Instruction ranges, specifying where IV is accessible.
    const auto &Ranges = I.second;
    if (Ranges.empty())
      continue;

    LexicalScope *Scope = nullptr;
    if (const DILocation *IA = IV.second)
      Scope = LScopes.findInlinedScope(IV.first->getScope(), IA);
    else
      Scope = LScopes.findLexicalScope(IV.first->getScope());
    // If variable scope is not found then skip this variable.
    if (!Scope)
      continue;

    Processed.insert(IV);
    DbgVariable *RegVar = createConcreteVariable(TheCU, *Scope, IV);

    const MachineInstr *MInsn = Ranges.front().first;
    assert(MInsn->isDebugValue() && "History must begin with debug value");

    // Check if there is a single DBG_VALUE, valid throughout the var's scope.
    if (Ranges.size() == 1 &&
        validThroughout(LScopes, MInsn, Ranges.front().second)) {
      RegVar->initializeDbgValue(MInsn);
      continue;
    }

    // Handle multiple DBG_VALUE instructions describing one variable.
    DebugLocStream::ListBuilder List(DebugLocs, TheCU, *Asm, *RegVar, *MInsn);

    // Build the location list for this variable.
    SmallVector<DebugLocEntry, 8> Entries;
    buildLocationList(Entries, Ranges);

    // If the variable has a DIBasicType, extract it.  Basic types cannot have
    // unique identifiers, so don't bother resolving the type with the
    // identifier map.
    const DIBasicType *BT = dyn_cast<DIBasicType>(
        static_cast<const Metadata *>(IV.first->getType()));

    // Finalize the entry by lowering it into a DWARF bytestream.
    for (auto &Entry : Entries)
      Entry.finalize(*Asm, List, BT);
    List.finalize();

    if (VariableInDependentType.count(IV.first)) {
      const DIType *DT = VariableInDependentType[IV.first];
      if (const DIStringType *ST = dyn_cast<DIStringType>(DT)) {
        unsigned Offset;
        DbgVariable TVar = {IV.first, IV.second};
        DebugLocStream::ListBuilder LB(DebugLocs, TheCU, *Asm, TVar, *MInsn);
        for (auto &Entry : Entries)
          Entry.finalize(*Asm, LB, ST);
        LB.finalize();
        Offset = TVar.getDebugLocListIndex();
        if (Offset != ~0u)
          addStringTypeLoc(ST, Offset);
      }
    }
  }


  // Collect info for variables that were optimized out.
  for (const DILocalVariable *DV : SP->getVariables()) {
    if (Processed.insert(InlinedVariable(DV, nullptr)).second)
      if (LexicalScope *Scope = LScopes.findLexicalScope(DV->getScope()))
        createConcreteVariable(TheCU, *Scope, InlinedVariable(DV, nullptr));
  }
}

// Process beginning of an instruction.
void DwarfDebug::beginInstruction(const MachineInstr *MI) {
  DebugHandlerBase::beginInstruction(MI);
  assert(CurMI);

  const auto *SP = MI->getMF()->getFunction().getSubprogram();
  if (!SP || SP->getUnit()->getEmissionKind() == DICompileUnit::NoDebug)
    return;

  // Check if source location changes, but ignore DBG_VALUE and CFI locations.
  if (MI->isMetaInstruction())
    return;
  const DebugLoc &DL = MI->getDebugLoc();
  // When we emit a line-0 record, we don't update PrevInstLoc; so look at
  // the last line number actually emitted, to see if it was line 0.
  unsigned LastAsmLine =
      Asm->OutStreamer->getContext().getCurrentDwarfLoc().getLine();

  if (DL == PrevInstLoc) {
    // If we have an ongoing unspecified location, nothing to do here.
    if (!DL)
      return;
    // We have an explicit location, same as the previous location.
    // But we might be coming back to it after a line 0 record.
    if (LastAsmLine == 0 && DL.getLine() != 0) {
      // Reinstate the source location but not marked as a statement.
      const MDNode *Scope = DL.getScope();
      recordSourceLine(DL.getLine(), DL.getCol(), Scope, /*Flags=*/0);
    }
    return;
  }

  if (!DL) {
    // We have an unspecified location, which might want to be line 0.
    // If we have already emitted a line-0 record, don't repeat it.
    if (LastAsmLine == 0)
      return;
    // If user said Don't Do That, don't do that.
    if (UnknownLocations == Disable)
      return;
    // See if we have a reason to emit a line-0 record now.
    // Reasons to emit a line-0 record include:
    // - User asked for it (UnknownLocations).
    // - Instruction has a label, so it's referenced from somewhere else,
    //   possibly debug information; we want it to have a source location.
    // - Instruction is at the top of a block; we don't want to inherit the
    //   location from the physically previous (maybe unrelated) block.
    if (UnknownLocations == Enable || PrevLabel ||
        (PrevInstBB && PrevInstBB != MI->getParent())) {
      // Preserve the file and column numbers, if we can, to save space in
      // the encoded line table.
      // Do not update PrevInstLoc, it remembers the last non-0 line.
      const MDNode *Scope = nullptr;
      unsigned Column = 0;
      if (PrevInstLoc) {
        Scope = PrevInstLoc.getScope();
        Column = PrevInstLoc.getCol();
      }
      recordSourceLine(/*Line=*/0, Column, Scope, /*Flags=*/0);
    }
    return;
  }

  // We have an explicit location, different from the previous location.
  // Don't repeat a line-0 record, but otherwise emit the new location.
  // (The new location might be an explicit line 0, which we do emit.)
  if (PrevInstLoc && DL.getLine() == 0 && LastAsmLine == 0)
    return;
  unsigned Flags = 0;
  if (DL == PrologEndLoc) {
    Flags |= DWARF2_FLAG_PROLOGUE_END | DWARF2_FLAG_IS_STMT;
    PrologEndLoc = DebugLoc();
  }
  // If the line changed, we call that a new statement; unless we went to
  // line 0 and came back, in which case it is not a new statement.
  unsigned OldLine = PrevInstLoc ? PrevInstLoc.getLine() : LastAsmLine;
  if (DL.getLine() && DL.getLine() != OldLine)
    Flags |= DWARF2_FLAG_IS_STMT;

  const MDNode *Scope = DL.getScope();
  recordSourceLine(DL.getLine(), DL.getCol(), Scope, Flags);

  // If we're not at line 0, remember this location.
  if (DL.getLine())
    PrevInstLoc = DL;
}

static DebugLoc findPrologueEndLoc(const MachineFunction *MF) {
  // First known non-DBG_VALUE and non-frame setup location marks
  // the beginning of the function body.
  for (const auto &MBB : *MF)
    for (const auto &MI : MBB)
      if (!MI.isMetaInstruction() && !MI.getFlag(MachineInstr::FrameSetup) &&
          MI.getDebugLoc())
        return MI.getDebugLoc();
  return DebugLoc();
}

// Gather pre-function debug information.  Assumes being called immediately
// after the function entry point has been emitted.
void DwarfDebug::beginFunctionImpl(const MachineFunction *MF) {
  CurFn = MF;

  auto *SP = MF->getFunction().getSubprogram();
  assert(LScopes.empty() || SP == LScopes.getCurrentFunctionScope()->getScopeNode());
  if (SP->getUnit()->getEmissionKind() == DICompileUnit::NoDebug)
    return;

  DwarfCompileUnit &CU = getOrCreateDwarfCompileUnit(SP->getUnit());

  // Set DwarfDwarfCompileUnitID in MCContext to the Compile Unit this function
  // belongs to so that we add to the correct per-cu line table in the
  // non-asm case.
  if (Asm->OutStreamer->hasRawTextSupport())
    // Use a single line table if we are generating assembly.
    Asm->OutStreamer->getContext().setDwarfCompileUnitID(0);
  else
    Asm->OutStreamer->getContext().setDwarfCompileUnitID(CU.getUniqueID());

  // Record beginning of function.
  PrologEndLoc = findPrologueEndLoc(MF);
  if (PrologEndLoc) {
    // We'd like to list the prologue as "not statements" but GDB behaves
    // poorly if we do that. Revisit this with caution/GDB (7.5+) testing.
    auto *SP = PrologEndLoc->getInlinedAtScope()->getSubprogram();
    recordSourceLine(SP->getScopeLine(), 0, SP, DWARF2_FLAG_IS_STMT);
  }
}

void DwarfDebug::skippedNonDebugFunction() {
  // If we don't have a subprogram for this function then there will be a hole
  // in the range information. Keep note of this by setting the previously used
  // section to nullptr.
  PrevCU = nullptr;
  CurFn = nullptr;
}

// Gather and emit post-function debug information.
void DwarfDebug::endFunctionImpl(const MachineFunction *MF) {
  const DISubprogram *SP = MF->getFunction().getSubprogram();

  assert(CurFn == MF &&
      "endFunction should be called with the same function as beginFunction");

  // Set DwarfDwarfCompileUnitID in MCContext to default value.
  Asm->OutStreamer->getContext().setDwarfCompileUnitID(0);

  LexicalScope *FnScope = LScopes.getCurrentFunctionScope();
  assert(!FnScope || SP == FnScope->getScopeNode());
  DwarfCompileUnit &TheCU = *CUMap.lookup(SP->getUnit());

  DenseSet<InlinedVariable> ProcessedVars;
  collectVariableInfo(TheCU, SP, ProcessedVars);

  // Add the range of this function to the list of ranges for the CU.
  TheCU.addRange(RangeSpan(Asm->getFunctionBegin(), Asm->getFunctionEnd()));

  // Under -gmlt, skip building the subprogram if there are no inlined
  // subroutines inside it. But with -fdebug-info-for-profiling, the subprogram
  // is still needed as we need its source location.
  if (!TheCU.getCUNode()->getDebugInfoForProfiling() &&
      TheCU.getCUNode()->getEmissionKind() == DICompileUnit::LineTablesOnly &&
      LScopes.getAbstractScopesList().empty() && !IsDarwin) {
    assert(InfoHolder.getScopeVariables().empty());
    PrevLabel = nullptr;
    CurFn = nullptr;
    return;
  }

#ifndef NDEBUG
  size_t NumAbstractScopes = LScopes.getAbstractScopesList().size();
#endif
  // Construct abstract scopes.
  for (LexicalScope *AScope : LScopes.getAbstractScopesList()) {
    auto *SP = cast<DISubprogram>(AScope->getScopeNode());
    // Collect info for variables that were optimized out.
    for (const DILocalVariable *DV : SP->getVariables()) {
      if (!ProcessedVars.insert(InlinedVariable(DV, nullptr)).second)
        continue;
      ensureAbstractVariableIsCreated(TheCU, InlinedVariable(DV, nullptr),
                                      DV->getScope());
      assert(LScopes.getAbstractScopesList().size() == NumAbstractScopes
             && "ensureAbstractVariableIsCreated inserted abstract scopes");
    }
    constructAbstractSubprogramScopeDIE(TheCU, AScope);
  }

  ProcessedSPNodes.insert(SP);
  TheCU.constructSubprogramScopeDIE(SP, FnScope);
  if (auto *SkelCU = TheCU.getSkeleton())
    if (!LScopes.getAbstractScopesList().empty() &&
        TheCU.getCUNode()->getSplitDebugInlining())
      SkelCU->constructSubprogramScopeDIE(SP, FnScope);

  // Clear debug info
  // Ownership of DbgVariables is a bit subtle - ScopeVariables owns all the
  // DbgVariables except those that are also in AbstractVariables (since they
  // can be used cross-function)
  InfoHolder.getScopeVariables().clear();
  PrevLabel = nullptr;
  CurFn = nullptr;
}

// Register a source line with debug info. Returns the  unique label that was
// emitted and which provides correspondence to the source line list.
void DwarfDebug::recordSourceLine(unsigned Line, unsigned Col, const MDNode *S,
                                  unsigned Flags) {
  StringRef Fn;
  unsigned Src = 1;
  unsigned Discriminator = 0;
  if (auto *Scope = cast_or_null<DIScope>(S)) {
    Fn = Scope->getFilename();
    if (Line != 0 && getDwarfVersion() >= 4)
      if (auto *LBF = dyn_cast<DILexicalBlockFile>(Scope))
        Discriminator = LBF->getDiscriminator();

    unsigned CUID = Asm->OutStreamer->getContext().getDwarfCompileUnitID();
    Src = static_cast<DwarfCompileUnit &>(*InfoHolder.getUnits()[CUID])
              .getOrCreateSourceID(Scope->getFile());
  }
  Asm->OutStreamer->EmitDwarfLocDirective(Src, Line, Col, Flags, 0,
                                          Discriminator, Fn);
}

//===----------------------------------------------------------------------===//
// Emit Methods
//===----------------------------------------------------------------------===//

// Emit the debug info section.
void DwarfDebug::emitDebugInfo() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.emitUnits(/* UseOffsets */ false);
}

// Emit the abbreviation section.
void DwarfDebug::emitAbbreviations() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;

  Holder.emitAbbrevs(Asm->getObjFileLowering().getDwarfAbbrevSection());
}

void DwarfDebug::emitStringOffsetsTableHeader() {
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.emitStringOffsetsTableHeader(
      Asm->getObjFileLowering().getDwarfStrOffSection());
}

template <typename AccelTableT>
void DwarfDebug::emitAccel(AccelTableT &Accel, MCSection *Section,
                           StringRef TableName) {
  Accel.finalizeTable(Asm, TableName);
  Asm->OutStreamer->SwitchSection(Section);

  // Emit the full data.
  Accel.emit(Asm, Section->getBeginSymbol());
}

// Emit visible names into a hashed accelerator table section.
void DwarfDebug::emitAccelNames() {
  emitAccel(AccelNames, Asm->getObjFileLowering().getDwarfAccelNamesSection(),
            "Names");
}

// Emit objective C classes and categories into a hashed accelerator table
// section.
void DwarfDebug::emitAccelObjC() {
  emitAccel(AccelObjC, Asm->getObjFileLowering().getDwarfAccelObjCSection(),
            "ObjC");
}

// Emit namespace dies into a hashed accelerator table.
void DwarfDebug::emitAccelNamespaces() {
  emitAccel(AccelNamespace,
            Asm->getObjFileLowering().getDwarfAccelNamespaceSection(),
            "namespac");
}

// Emit type dies into a hashed accelerator table.
void DwarfDebug::emitAccelTypes() {
  emitAccel(AccelTypes, Asm->getObjFileLowering().getDwarfAccelTypesSection(),
            "types");
}

// Public name handling.
// The format for the various pubnames:
//
// dwarf pubnames - offset/name pairs where the offset is the offset into the CU
// for the DIE that is named.
//
// gnu pubnames - offset/index value/name tuples where the offset is the offset
// into the CU and the index value is computed according to the type of value
// for the DIE that is named.
//
// For type units the offset is the offset of the skeleton DIE. For split dwarf
// it's the offset within the debug_info/debug_types dwo section, however, the
// reference in the pubname header doesn't change.

/// computeIndexValue - Compute the gdb index value for the DIE and CU.
static dwarf::PubIndexEntryDescriptor computeIndexValue(DwarfUnit *CU,
                                                        const DIE *Die) {
  // Entities that ended up only in a Type Unit reference the CU instead (since
  // the pub entry has offsets within the CU there's no real offset that can be
  // provided anyway). As it happens all such entities (namespaces and types,
  // types only in C++ at that) are rendered as TYPE+EXTERNAL. If this turns out
  // not to be true it would be necessary to persist this information from the
  // point at which the entry is added to the index data structure - since by
  // the time the index is built from that, the original type/namespace DIE in a
  // type unit has already been destroyed so it can't be queried for properties
  // like tag, etc.
  if (Die->getTag() == dwarf::DW_TAG_compile_unit)
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_TYPE,
                                          dwarf::GIEL_EXTERNAL);
  dwarf::GDBIndexEntryLinkage Linkage = dwarf::GIEL_STATIC;

  // We could have a specification DIE that has our most of our knowledge,
  // look for that now.
  if (DIEValue SpecVal = Die->findAttribute(dwarf::DW_AT_specification)) {
    DIE &SpecDIE = SpecVal.getDIEEntry().getEntry();
    if (SpecDIE.findAttribute(dwarf::DW_AT_external))
      Linkage = dwarf::GIEL_EXTERNAL;
  } else if (Die->findAttribute(dwarf::DW_AT_external))
    Linkage = dwarf::GIEL_EXTERNAL;

  switch (Die->getTag()) {
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
    return dwarf::PubIndexEntryDescriptor(
        dwarf::GIEK_TYPE, CU->getLanguage() != dwarf::DW_LANG_C_plus_plus
                              ? dwarf::GIEL_STATIC
                              : dwarf::GIEL_EXTERNAL);
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_subrange_type:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_TYPE, dwarf::GIEL_STATIC);
  case dwarf::DW_TAG_namespace:
    return dwarf::GIEK_TYPE;
  case dwarf::DW_TAG_subprogram:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_FUNCTION, Linkage);
  case dwarf::DW_TAG_variable:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_VARIABLE, Linkage);
  case dwarf::DW_TAG_enumerator:
    return dwarf::PubIndexEntryDescriptor(dwarf::GIEK_VARIABLE,
                                          dwarf::GIEL_STATIC);
  default:
    return dwarf::GIEK_NONE;
  }
}

/// emitDebugPubSections - Emit visible names and types into debug pubnames and
/// pubtypes sections.
void DwarfDebug::emitDebugPubSections() {
  for (const auto &NU : CUMap) {
    DwarfCompileUnit *TheU = NU.second;
    if (!TheU->hasDwarfPubSections())
      continue;

    bool GnuStyle = TheU->getCUNode()->getGnuPubnames();

    Asm->OutStreamer->SwitchSection(
        GnuStyle ? Asm->getObjFileLowering().getDwarfGnuPubNamesSection()
                 : Asm->getObjFileLowering().getDwarfPubNamesSection());
    emitDebugPubSection(GnuStyle, "Names", TheU, TheU->getGlobalNames());

    Asm->OutStreamer->SwitchSection(
        GnuStyle ? Asm->getObjFileLowering().getDwarfGnuPubTypesSection()
                 : Asm->getObjFileLowering().getDwarfPubTypesSection());
    emitDebugPubSection(GnuStyle, "Types", TheU, TheU->getGlobalTypes());
  }
}

void DwarfDebug::emitDebugPubSection(bool GnuStyle, StringRef Name,
                                     DwarfCompileUnit *TheU,
                                     const StringMap<const DIE *> &Globals) {
  if (auto *Skeleton = TheU->getSkeleton())
    TheU = Skeleton;

  // Emit the header.
  Asm->OutStreamer->AddComment("Length of Public " + Name + " Info");
  MCSymbol *BeginLabel = Asm->createTempSymbol("pub" + Name + "_begin");
  MCSymbol *EndLabel = Asm->createTempSymbol("pub" + Name + "_end");
  Asm->EmitLabelDifference(EndLabel, BeginLabel, 4);

  Asm->OutStreamer->EmitLabel(BeginLabel);

  Asm->OutStreamer->AddComment("DWARF Version");
  Asm->EmitInt16(dwarf::DW_PUBNAMES_VERSION);

  Asm->OutStreamer->AddComment("Offset of Compilation Unit Info");
  Asm->emitDwarfSymbolReference(TheU->getLabelBegin());

  Asm->OutStreamer->AddComment("Compilation Unit Length");
  Asm->EmitInt32(TheU->getLength());

  // Emit the pubnames for this compilation unit.
  for (const auto &GI : Globals) {
    const char *Name = GI.getKeyData();
    const DIE *Entity = GI.second;

    Asm->OutStreamer->AddComment("DIE offset");
    Asm->EmitInt32(Entity->getOffset());

    if (GnuStyle) {
      dwarf::PubIndexEntryDescriptor Desc = computeIndexValue(TheU, Entity);
      Asm->OutStreamer->AddComment(
          Twine("Kind: ") + dwarf::GDBIndexEntryKindString(Desc.Kind) + ", " +
          dwarf::GDBIndexEntryLinkageString(Desc.Linkage));
      Asm->EmitInt8(Desc.toBits());
    }

    Asm->OutStreamer->AddComment("External Name");
    Asm->OutStreamer->EmitBytes(StringRef(Name, GI.getKeyLength() + 1));
  }

  Asm->OutStreamer->AddComment("End Mark");
  Asm->EmitInt32(0);
  Asm->OutStreamer->EmitLabel(EndLabel);
}

/// Emit null-terminated strings into a debug str section.
void DwarfDebug::emitDebugStr() {
  MCSection *StringOffsetsSection = nullptr;
  if (useSegmentedStringOffsetsTable()) {
    emitStringOffsetsTableHeader();
    StringOffsetsSection = Asm->getObjFileLowering().getDwarfStrOffSection();
  }
  DwarfFile &Holder = useSplitDwarf() ? SkeletonHolder : InfoHolder;
  Holder.emitStrings(Asm->getObjFileLowering().getDwarfStrSection(),
                     StringOffsetsSection, /* UseRelativeOffsets = */ true);
}

void DwarfDebug::emitDebugLocEntry(ByteStreamer &Streamer,
                                   const DebugLocStream::Entry &Entry) {
  auto &&Comments = DebugLocs.getComments(Entry);
  auto Comment = Comments.begin();
  auto End = Comments.end();
  for (uint8_t Byte : DebugLocs.getBytes(Entry))
    Streamer.EmitInt8(Byte, Comment != End ? *(Comment++) : "");
}

static void emitDebugLocValue(const AsmPrinter &AP, const DIBasicType *BT,
                              ByteStreamer &Streamer,
                              const DebugLocEntry::Value &Value,
                              DwarfExpression &DwarfExpr) {
  auto *DIExpr = Value.getExpression();
  DIExpressionCursor ExprCursor(DIExpr);
  DwarfExpr.addFragmentOffset(DIExpr);
  // Regular entry.
  if (Value.isInt()) {
    if (BT && (BT->getEncoding() == dwarf::DW_ATE_signed ||
               BT->getEncoding() == dwarf::DW_ATE_signed_char))
      DwarfExpr.addSignedConstant(Value.getInt());
    else
      DwarfExpr.addUnsignedConstant(Value.getInt());
  } else if (Value.isLocation()) {
    MachineLocation Location = Value.getLoc();
    if (Location.isIndirect())
      DwarfExpr.setMemoryLocationKind();
    DIExpressionCursor Cursor(DIExpr);
    const TargetRegisterInfo &TRI = *AP.MF->getSubtarget().getRegisterInfo();
    if (!DwarfExpr.addMachineRegExpression(TRI, Cursor, Location.getReg()))
      return;
    return DwarfExpr.addExpression(std::move(Cursor));
  } else if (Value.isConstantFP()) {
    APInt RawBytes = Value.getConstantFP()->getValueAPF().bitcastToAPInt();
    DwarfExpr.addUnsignedConstant(RawBytes);
  }
  DwarfExpr.addExpression(std::move(ExprCursor));
}

void DebugLocEntry::finalize(const AsmPrinter &AP,
                             DebugLocStream::ListBuilder &List,
                             const DIBasicType *BT) {
  DebugLocStream::EntryBuilder Entry(List, Begin, End);
  BufferByteStreamer Streamer = Entry.getStreamer();
  DebugLocDwarfExpression DwarfExpr(AP.getDwarfVersion(), Streamer);
  const DebugLocEntry::Value &Value = Values[0];
  if (Value.isFragment()) {
    // Emit all fragments that belong to the same variable and range.
    assert(llvm::all_of(Values, [](DebugLocEntry::Value P) {
          return P.isFragment();
        }) && "all values are expected to be fragments");
    assert(std::is_sorted(Values.begin(), Values.end()) &&
           "fragments are expected to be sorted");

    for (auto Fragment : Values)
      emitDebugLocValue(AP, BT, Streamer, Fragment, DwarfExpr);

  } else {
    assert(Values.size() == 1 && "only fragments may have >1 value");
    emitDebugLocValue(AP, BT, Streamer, Value, DwarfExpr);
  }
  DwarfExpr.finalize();
}

void DebugLocEntry::finalize(const AsmPrinter &AP,
                             DebugLocStream::ListBuilder &List,
                             const DIStringType *ST) {
  DebugLocStream::EntryBuilder Entry(List, Begin, End);
  BufferByteStreamer Streamer = Entry.getStreamer();
  DebugLocDwarfExpression DwarfExpr(AP.getDwarfVersion(), Streamer);
  DebugLocEntry::Value Value = Values[0];
  assert(!Value.isFragment());
  assert(Values.size() == 1 && "only fragments may have >1 value");
  Value.Expression = ST->getStringLengthExp();
  emitDebugLocValue(AP, nullptr, Streamer, Value, DwarfExpr);
  DwarfExpr.finalize();
}

void DwarfDebug::emitDebugLocEntryLocation(const DebugLocStream::Entry &Entry) {
  // Emit the size.
  Asm->OutStreamer->AddComment("Loc expr size");
  Asm->EmitInt16(DebugLocs.getBytes(Entry).size());

  // Emit the entry.
  APByteStreamer Streamer(*Asm);
  emitDebugLocEntry(Streamer, Entry);
}

// Emit locations into the debug loc section.
void DwarfDebug::emitDebugLoc() {
  if (DebugLocs.getLists().empty())
    return;

  // Start the dwarf loc section.
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfLocSection());
  unsigned char Size = Asm->MAI->getCodePointerSize();
  for (const auto &List : DebugLocs.getLists()) {
    Asm->OutStreamer->EmitLabel(List.Label);
    const DwarfCompileUnit *CU = List.CU;
    for (const auto &Entry : DebugLocs.getEntries(List)) {
      // Set up the range. This range is relative to the entry point of the
      // compile unit. This is a hard coded 0 for low_pc when we're emitting
      // ranges, or the DW_AT_low_pc on the compile unit otherwise.
      if (auto *Base = CU->getBaseAddress()) {
        Asm->EmitLabelDifference(Entry.BeginSym, Base, Size);
        Asm->EmitLabelDifference(Entry.EndSym, Base, Size);
      } else {
        Asm->OutStreamer->EmitSymbolValue(Entry.BeginSym, Size);
        Asm->OutStreamer->EmitSymbolValue(Entry.EndSym, Size);
      }

      emitDebugLocEntryLocation(Entry);
    }
    Asm->OutStreamer->EmitIntValue(0, Size);
    Asm->OutStreamer->EmitIntValue(0, Size);
  }
}

void DwarfDebug::emitDebugLocDWO() {
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfLocDWOSection());
  for (const auto &List : DebugLocs.getLists()) {
    Asm->OutStreamer->EmitLabel(List.Label);
    for (const auto &Entry : DebugLocs.getEntries(List)) {
      // Just always use start_length for now - at least that's one address
      // rather than two. We could get fancier and try to, say, reuse an
      // address we know we've emitted elsewhere (the start of the function?
      // The start of the CU or CU subrange that encloses this range?)
      Asm->EmitInt8(dwarf::DW_LLE_startx_length);
      unsigned idx = AddrPool.getIndex(Entry.BeginSym);
      Asm->EmitULEB128(idx);
      Asm->EmitLabelDifference(Entry.EndSym, Entry.BeginSym, 4);

      emitDebugLocEntryLocation(Entry);
    }
    Asm->EmitInt8(dwarf::DW_LLE_end_of_list);
  }
}

struct ArangeSpan {
  const MCSymbol *Start, *End;
};

// Emit a debug aranges section, containing a CU lookup for any
// address we can tie back to a CU.
void DwarfDebug::emitDebugARanges() {
  // Provides a unique id per text section.
  MapVector<MCSection *, SmallVector<SymbolCU, 8>> SectionMap;

  // Filter labels by section.
  for (const SymbolCU &SCU : ArangeLabels) {
    if (SCU.Sym->isInSection()) {
      // Make a note of this symbol and it's section.
      MCSection *Section = &SCU.Sym->getSection();
      if (!Section->getKind().isMetadata())
        SectionMap[Section].push_back(SCU);
    } else {
      // Some symbols (e.g. common/bss on mach-o) can have no section but still
      // appear in the output. This sucks as we rely on sections to build
      // arange spans. We can do it without, but it's icky.
      SectionMap[nullptr].push_back(SCU);
    }
  }

  DenseMap<DwarfCompileUnit *, std::vector<ArangeSpan>> Spans;

  for (auto &I : SectionMap) {
    MCSection *Section = I.first;
    SmallVector<SymbolCU, 8> &List = I.second;
    if (List.size() < 1)
      continue;

    // If we have no section (e.g. common), just write out
    // individual spans for each symbol.
    if (!Section) {
      for (const SymbolCU &Cur : List) {
        ArangeSpan Span;
        Span.Start = Cur.Sym;
        Span.End = nullptr;
        assert(Cur.CU);
        Spans[Cur.CU].push_back(Span);
      }
      continue;
    }

    // Sort the symbols by offset within the section.
    std::sort(
        List.begin(), List.end(), [&](const SymbolCU &A, const SymbolCU &B) {
          unsigned IA = A.Sym ? Asm->OutStreamer->GetSymbolOrder(A.Sym) : 0;
          unsigned IB = B.Sym ? Asm->OutStreamer->GetSymbolOrder(B.Sym) : 0;

          // Symbols with no order assigned should be placed at the end.
          // (e.g. section end labels)
          if (IA == 0)
            return false;
          if (IB == 0)
            return true;
          return IA < IB;
        });

    // Insert a final terminator.
    List.push_back(SymbolCU(nullptr, Asm->OutStreamer->endSection(Section)));

    // Build spans between each label.
    const MCSymbol *StartSym = List[0].Sym;
    for (size_t n = 1, e = List.size(); n < e; n++) {
      const SymbolCU &Prev = List[n - 1];
      const SymbolCU &Cur = List[n];

      // Try and build the longest span we can within the same CU.
      if (Cur.CU != Prev.CU) {
        ArangeSpan Span;
        Span.Start = StartSym;
        Span.End = Cur.Sym;
        assert(Prev.CU);
        Spans[Prev.CU].push_back(Span);
        StartSym = Cur.Sym;
      }
    }
  }

  // Start the dwarf aranges section.
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfARangesSection());

  unsigned PtrSize = Asm->MAI->getCodePointerSize();

  // Build a list of CUs used.
  std::vector<DwarfCompileUnit *> CUs;
  for (const auto &it : Spans) {
    DwarfCompileUnit *CU = it.first;
    CUs.push_back(CU);
  }

  // Sort the CU list (again, to ensure consistent output order).
  std::sort(CUs.begin(), CUs.end(),
            [](const DwarfCompileUnit *A, const DwarfCompileUnit *B) {
              return A->getUniqueID() < B->getUniqueID();
            });

  // Emit an arange table for each CU we used.
  for (DwarfCompileUnit *CU : CUs) {
    std::vector<ArangeSpan> &List = Spans[CU];

    // Describe the skeleton CU's offset and length, not the dwo file's.
    if (auto *Skel = CU->getSkeleton())
      CU = Skel;

    // Emit size of content not including length itself.
    unsigned ContentSize =
        sizeof(int16_t) + // DWARF ARange version number
        sizeof(int32_t) + // Offset of CU in the .debug_info section
        sizeof(int8_t) +  // Pointer Size (in bytes)
        sizeof(int8_t);   // Segment Size (in bytes)

    unsigned TupleSize = PtrSize * 2;

    // 7.20 in the Dwarf specs requires the table to be aligned to a tuple.
    unsigned Padding =
        OffsetToAlignment(sizeof(int32_t) + ContentSize, TupleSize);

    ContentSize += Padding;
    ContentSize += (List.size() + 1) * TupleSize;

    // For each compile unit, write the list of spans it covers.
    Asm->OutStreamer->AddComment("Length of ARange Set");
    Asm->EmitInt32(ContentSize);
    Asm->OutStreamer->AddComment("DWARF Arange version number");
    Asm->EmitInt16(dwarf::DW_ARANGES_VERSION);
    Asm->OutStreamer->AddComment("Offset Into Debug Info Section");
    Asm->emitDwarfSymbolReference(CU->getLabelBegin());
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->EmitInt8(PtrSize);
    Asm->OutStreamer->AddComment("Segment Size (in bytes)");
    Asm->EmitInt8(0);

    Asm->OutStreamer->emitFill(Padding, 0xff);

    for (const ArangeSpan &Span : List) {
      Asm->EmitLabelReference(Span.Start, PtrSize);

      // Calculate the size as being from the span start to it's end.
      if (Span.End) {
        Asm->EmitLabelDifference(Span.End, Span.Start, PtrSize);
      } else {
        // For symbols without an end marker (e.g. common), we
        // write a single arange entry containing just that one symbol.
        uint64_t Size = SymSize[Span.Start];
        if (Size == 0)
          Size = 1;

        Asm->OutStreamer->EmitIntValue(Size, PtrSize);
      }
    }

    Asm->OutStreamer->AddComment("ARange terminator");
    Asm->OutStreamer->EmitIntValue(0, PtrSize);
    Asm->OutStreamer->EmitIntValue(0, PtrSize);
  }
}

/// Emit address ranges into a debug ranges section.
void DwarfDebug::emitDebugRanges() {
  if (CUMap.empty())
    return;

  // Start the dwarf ranges section.
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfRangesSection());

  // Size for our labels.
  unsigned char Size = Asm->MAI->getCodePointerSize();

  // Grab the specific ranges for the compile units in the module.
  for (const auto &I : CUMap) {
    DwarfCompileUnit *TheCU = I.second;

    if (auto *Skel = TheCU->getSkeleton())
      TheCU = Skel;

    // Iterate over the misc ranges for the compile units in the module.
    for (const RangeSpanList &List : TheCU->getRangeLists()) {
      // Emit our symbol so we can find the beginning of the range.
      Asm->OutStreamer->EmitLabel(List.getSym());

      // Gather all the ranges that apply to the same section so they can share
      // a base address entry.
      MapVector<const MCSection *, std::vector<const RangeSpan *>> MV;
      for (const RangeSpan &Range : List.getRanges()) {
        MV[&Range.getStart()->getSection()].push_back(&Range);
      }

      auto *CUBase = TheCU->getBaseAddress();
      bool BaseIsSet = false;
      for (const auto &P : MV) {
        // Don't bother with a base address entry if there's only one range in
        // this section in this range list - for example ranges for a CU will
        // usually consist of single regions from each of many sections
        // (-ffunction-sections, or just C++ inline functions) except under LTO
        // or optnone where there may be holes in a single CU's section
        // contrubutions.
        auto *Base = CUBase;
        if (!Base && P.second.size() > 1 &&
            UseDwarfRangesBaseAddressSpecifier) {
          BaseIsSet = true;
          // FIXME/use care: This may not be a useful base address if it's not
          // the lowest address/range in this object.
          Base = P.second.front()->getStart();
          Asm->OutStreamer->EmitIntValue(-1, Size);
          Asm->OutStreamer->EmitSymbolValue(Base, Size);
        } else if (BaseIsSet) {
          BaseIsSet = false;
          Asm->OutStreamer->EmitIntValue(-1, Size);
          Asm->OutStreamer->EmitIntValue(0, Size);
        }

        for (const auto *RS : P.second) {
          const MCSymbol *Begin = RS->getStart();
          const MCSymbol *End = RS->getEnd();
          assert(Begin && "Range without a begin symbol?");
          assert(End && "Range without an end symbol?");
          if (Base) {
            Asm->EmitLabelDifference(Begin, Base, Size);
            Asm->EmitLabelDifference(End, Base, Size);
          } else {
            Asm->OutStreamer->EmitSymbolValue(Begin, Size);
            Asm->OutStreamer->EmitSymbolValue(End, Size);
          }
        }
      }

      // And terminate the list with two 0 values.
      Asm->OutStreamer->EmitIntValue(0, Size);
      Asm->OutStreamer->EmitIntValue(0, Size);
    }
  }
}

void DwarfDebug::handleMacroNodes(DIMacroNodeArray Nodes, DwarfCompileUnit &U) {
  for (auto *MN : Nodes) {
    if (auto *M = dyn_cast<DIMacro>(MN))
      emitMacro(*M);
    else if (auto *F = dyn_cast<DIMacroFile>(MN))
      emitMacroFile(*F, U);
    else
      llvm_unreachable("Unexpected DI type!");
  }
}

void DwarfDebug::emitMacro(DIMacro &M) {
  Asm->EmitULEB128(M.getMacinfoType());
  Asm->EmitULEB128(M.getLine());
  StringRef Name = M.getName();
  StringRef Value = M.getValue();
  Asm->OutStreamer->EmitBytes(Name);
  if (!Value.empty()) {
    // There should be one space between macro name and macro value.
    Asm->EmitInt8(' ');
    Asm->OutStreamer->EmitBytes(Value);
  }
  Asm->EmitInt8('\0');
}

void DwarfDebug::emitMacroFile(DIMacroFile &F, DwarfCompileUnit &U) {
  assert(F.getMacinfoType() == dwarf::DW_MACINFO_start_file);
  Asm->EmitULEB128(dwarf::DW_MACINFO_start_file);
  Asm->EmitULEB128(F.getLine());
  Asm->EmitULEB128(U.getOrCreateSourceID(F.getFile()));
  handleMacroNodes(F.getElements(), U);
  Asm->EmitULEB128(dwarf::DW_MACINFO_end_file);
}

/// Emit macros into a debug macinfo section.
void DwarfDebug::emitDebugMacinfo() {
  if (CUMap.empty())
    return;

  // Start the dwarf macinfo section.
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfMacinfoSection());

  for (const auto &P : CUMap) {
    auto &TheCU = *P.second;
    auto *SkCU = TheCU.getSkeleton();
    DwarfCompileUnit &U = SkCU ? *SkCU : TheCU;
    auto *CUNode = cast<DICompileUnit>(P.first);
    Asm->OutStreamer->EmitLabel(U.getMacroLabelBegin());
    handleMacroNodes(CUNode->getMacros(), U);
  }
  Asm->OutStreamer->AddComment("End Of Macro List Mark");
  Asm->EmitInt8(0);
}

// DWARF5 Experimental Separate Dwarf emitters.

void DwarfDebug::initSkeletonUnit(const DwarfUnit &U, DIE &Die,
                                  std::unique_ptr<DwarfCompileUnit> NewU) {
  NewU->addString(Die, dwarf::DW_AT_GNU_dwo_name,
                  Asm->TM.Options.MCOptions.SplitDwarfFile);

  if (!CompilationDir.empty())
    NewU->addString(Die, dwarf::DW_AT_comp_dir, CompilationDir);

  addGnuPubAttributes(*NewU, Die);

  SkeletonHolder.addUnit(std::move(NewU));
}

// This DIE has the following attributes: DW_AT_comp_dir, DW_AT_stmt_list,
// DW_AT_low_pc, DW_AT_high_pc, DW_AT_ranges, DW_AT_dwo_name, DW_AT_dwo_id,
// DW_AT_addr_base, DW_AT_ranges_base.
DwarfCompileUnit &DwarfDebug::constructSkeletonCU(const DwarfCompileUnit &CU) {

  auto OwnedUnit = llvm::make_unique<DwarfCompileUnit>(
      CU.getUniqueID(), CU.getCUNode(), Asm, this, &SkeletonHolder);
  DwarfCompileUnit &NewCU = *OwnedUnit;
  NewCU.setSection(Asm->getObjFileLowering().getDwarfInfoSection());

  NewCU.initStmtList();

  if (useSegmentedStringOffsetsTable())
    NewCU.addStringOffsetsStart();

  initSkeletonUnit(CU, NewCU.getUnitDie(), std::move(OwnedUnit));

  return NewCU;
}

// Emit the .debug_info.dwo section for separated dwarf. This contains the
// compile units that would normally be in debug_info.
void DwarfDebug::emitDebugInfoDWO() {
  assert(useSplitDwarf() && "No split dwarf debug info?");
  // Don't emit relocations into the dwo file.
  InfoHolder.emitUnits(/* UseOffsets */ true);
}

// Emit the .debug_abbrev.dwo section for separated dwarf. This contains the
// abbreviations for the .debug_info.dwo section.
void DwarfDebug::emitDebugAbbrevDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  InfoHolder.emitAbbrevs(Asm->getObjFileLowering().getDwarfAbbrevDWOSection());
}

void DwarfDebug::emitDebugLineDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  Asm->OutStreamer->SwitchSection(
      Asm->getObjFileLowering().getDwarfLineDWOSection());
  SplitTypeUnitFileTable.Emit(*Asm->OutStreamer, MCDwarfLineTableParams());
}

void DwarfDebug::emitStringOffsetsTableHeaderDWO() {
  assert(useSplitDwarf() && "No split dwarf?");
  InfoHolder.emitStringOffsetsTableHeader(
      Asm->getObjFileLowering().getDwarfStrOffDWOSection());
}

// Emit the .debug_str.dwo section for separated dwarf. This contains the
// string section and is identical in format to traditional .debug_str
// sections.
void DwarfDebug::emitDebugStrDWO() {
  if (useSegmentedStringOffsetsTable())
    emitStringOffsetsTableHeaderDWO();
  assert(useSplitDwarf() && "No split dwarf?");
  MCSection *OffSec = Asm->getObjFileLowering().getDwarfStrOffDWOSection();
  InfoHolder.emitStrings(Asm->getObjFileLowering().getDwarfStrDWOSection(),
                         OffSec, /* UseRelativeOffsets = */ false);
}

MCDwarfDwoLineTable *DwarfDebug::getDwoLineTable(const DwarfCompileUnit &CU) {
  if (!useSplitDwarf())
    return nullptr;
  if (SingleCU)
    SplitTypeUnitFileTable.setCompilationDir(CU.getCUNode()->getDirectory());
  return &SplitTypeUnitFileTable;
}

uint64_t DwarfDebug::makeTypeSignature(StringRef Identifier) {
  MD5 Hash;
  Hash.update(Identifier);
  // ... take the least significant 8 bytes and return those. Our MD5
  // implementation always returns its results in little endian, so we actually
  // need the "high" word.
  MD5::MD5Result Result;
  Hash.final(Result);
  return Result.high();
}

void DwarfDebug::addDwarfTypeUnitType(DwarfCompileUnit &CU,
                                      StringRef Identifier, DIE &RefDie,
                                      const DICompositeType *CTy) {
  // Fast path if we're building some type units and one has already used the
  // address pool we know we're going to throw away all this work anyway, so
  // don't bother building dependent types.
  if (!TypeUnitsUnderConstruction.empty() && AddrPool.hasBeenUsed())
    return;

  auto Ins = TypeSignatures.insert(std::make_pair(CTy, 0));
  if (!Ins.second) {
    CU.addDIETypeSignature(RefDie, Ins.first->second);
    return;
  }

  bool TopLevelType = TypeUnitsUnderConstruction.empty();
  AddrPool.resetUsedFlag();

  auto OwnedUnit = llvm::make_unique<DwarfTypeUnit>(CU, Asm, this, &InfoHolder,
                                                    getDwoLineTable(CU));
  DwarfTypeUnit &NewTU = *OwnedUnit;
  DIE &UnitDie = NewTU.getUnitDie();
  TypeUnitsUnderConstruction.emplace_back(std::move(OwnedUnit), CTy);

  NewTU.addUInt(UnitDie, dwarf::DW_AT_language, dwarf::DW_FORM_data2,
                CU.getLanguage());

  uint64_t Signature = makeTypeSignature(Identifier);
  NewTU.setTypeSignature(Signature);
  Ins.first->second = Signature;

  if (useSplitDwarf())
    NewTU.setSection(Asm->getObjFileLowering().getDwarfTypesDWOSection());
  else {
    CU.applyStmtList(UnitDie);
    NewTU.setSection(Asm->getObjFileLowering().getDwarfTypesSection(Signature));
  }

  // Add DW_AT_str_offsets_base to the type unit DIE, but not for split type
  // units.
  if (useSegmentedStringOffsetsTable() && !useSplitDwarf())
    NewTU.addStringOffsetsStart();

  NewTU.setType(NewTU.createTypeDIE(CTy));

  if (TopLevelType) {
    auto TypeUnitsToAdd = std::move(TypeUnitsUnderConstruction);
    TypeUnitsUnderConstruction.clear();

    // Types referencing entries in the address table cannot be placed in type
    // units.
    if (AddrPool.hasBeenUsed()) {

      // Remove all the types built while building this type.
      // This is pessimistic as some of these types might not be dependent on
      // the type that used an address.
      for (const auto &TU : TypeUnitsToAdd)
        TypeSignatures.erase(TU.second);

      // Construct this type in the CU directly.
      // This is inefficient because all the dependent types will be rebuilt
      // from scratch, including building them in type units, discovering that
      // they depend on addresses, throwing them out and rebuilding them.
      CU.constructTypeDIE(RefDie, cast<DICompositeType>(CTy));
      return;
    }

    // If the type wasn't dependent on fission addresses, finish adding the type
    // and all its dependent types.
    for (auto &TU : TypeUnitsToAdd) {
      InfoHolder.computeSizeAndOffsetsForUnit(TU.first.get());
      InfoHolder.emitUnit(TU.first.get(), useSplitDwarf());
    }
  }
  CU.addDIETypeSignature(RefDie, Signature);
}

// Accelerator table mutators - add each name along with its companion
// DIE to the proper table while ensuring that the name that we're going
// to reference is in the string table. We do this since the names we
// add may not only be identical to the names in the DIE.
void DwarfDebug::addAccelName(StringRef Name, const DIE &Die) {
  if (!useDwarfAccelTables())
    return;
  AccelNames.addName(InfoHolder.getStringPool().getEntry(*Asm, Name), &Die);
}

void DwarfDebug::addAccelObjC(StringRef Name, const DIE &Die) {
  if (!useDwarfAccelTables())
    return;
  AccelObjC.addName(InfoHolder.getStringPool().getEntry(*Asm, Name), &Die);
}

void DwarfDebug::addAccelNamespace(StringRef Name, const DIE &Die) {
  if (!useDwarfAccelTables())
    return;
  AccelNamespace.addName(InfoHolder.getStringPool().getEntry(*Asm, Name), &Die);
}

void DwarfDebug::addAccelType(StringRef Name, const DIE &Die, char Flags) {
  if (!useDwarfAccelTables())
    return;
  AccelTypes.addName(InfoHolder.getStringPool().getEntry(*Asm, Name), &Die);
}

uint16_t DwarfDebug::getDwarfVersion() const {
  return Asm->OutStreamer->getContext().getDwarfVersion();
}
