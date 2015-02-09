//===--- DIBuilder.cpp - Debug Information Builder ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the DIBuilder.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DIBuilder.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Dwarf.h"

using namespace llvm;
using namespace llvm::dwarf;

namespace {
class HeaderBuilder {
  /// \brief Whether there are any fields yet.
  ///
  /// Note that this is not equivalent to \c Chars.empty(), since \a concat()
  /// may have been called already with an empty string.
  bool IsEmpty;
  SmallVector<char, 256> Chars;

public:
  HeaderBuilder() : IsEmpty(true) {}
  HeaderBuilder(const HeaderBuilder &X) : IsEmpty(X.IsEmpty), Chars(X.Chars) {}
  HeaderBuilder(HeaderBuilder &&X)
      : IsEmpty(X.IsEmpty), Chars(std::move(X.Chars)) {}

  template <class Twineable> HeaderBuilder &concat(Twineable &&X) {
    if (IsEmpty)
      IsEmpty = false;
    else
      Chars.push_back(0);
    Twine(X).toVector(Chars);
    return *this;
  }

  MDString *get(LLVMContext &Context) const {
    return MDString::get(Context, StringRef(Chars.begin(), Chars.size()));
  }

  static HeaderBuilder get(unsigned Tag) {
    return HeaderBuilder().concat("0x" + Twine::utohexstr(Tag));
  }
};
}

DIBuilder::DIBuilder(Module &m, bool AllowUnresolvedNodes)
    : M(m), VMContext(M.getContext()), TempEnumTypes(nullptr),
      TempRetainTypes(nullptr), TempSubprograms(nullptr), TempGVs(nullptr),
      DeclareFn(nullptr), ValueFn(nullptr),
      AllowUnresolvedNodes(AllowUnresolvedNodes) {}

void DIBuilder::trackIfUnresolved(MDNode *N) {
  if (!N)
    return;
  if (N->isResolved())
    return;

  assert(AllowUnresolvedNodes && "Cannot handle unresolved nodes");
  UnresolvedNodes.emplace_back(N);
}

void DIBuilder::finalize() {
  DIArray Enums = getOrCreateArray(AllEnumTypes);
  DIType(TempEnumTypes).replaceAllUsesWith(Enums);

  SmallVector<Metadata *, 16> RetainValues;
  // Declarations and definitions of the same type may be retained. Some
  // clients RAUW these pairs, leaving duplicates in the retained types
  // list. Use a set to remove the duplicates while we transform the
  // TrackingVHs back into Values.
  SmallPtrSet<Metadata *, 16> RetainSet;
  for (unsigned I = 0, E = AllRetainTypes.size(); I < E; I++)
    if (RetainSet.insert(AllRetainTypes[I]).second)
      RetainValues.push_back(AllRetainTypes[I]);
  DIArray RetainTypes = getOrCreateArray(RetainValues);
  DIType(TempRetainTypes).replaceAllUsesWith(RetainTypes);

  DIArray SPs = getOrCreateArray(AllSubprograms);
  DIType(TempSubprograms).replaceAllUsesWith(SPs);
  for (unsigned i = 0, e = SPs.getNumElements(); i != e; ++i) {
    DISubprogram SP(SPs.getElement(i));
    if (MDNode *Temp = SP.getVariablesNodes()) {
      SmallVector<Metadata *, 4> Variables;
      for (Metadata *V : PreservedVariables.lookup(SP))
        Variables.push_back(V);
      DIArray AV = getOrCreateArray(Variables);
      DIType(Temp).replaceAllUsesWith(AV);
    }
  }

  DIArray GVs = getOrCreateArray(AllGVs);
  DIType(TempGVs).replaceAllUsesWith(GVs);

  SmallVector<Metadata *, 16> RetainValuesI;
  for (unsigned I = 0, E = AllImportedModules.size(); I < E; I++)
    RetainValuesI.push_back(AllImportedModules[I]);
  DIArray IMs = getOrCreateArray(RetainValuesI);
  DIType(TempImportedModules).replaceAllUsesWith(IMs);

  // Now that all temp nodes have been replaced or deleted, resolve remaining
  // cycles.
  for (const auto &N : UnresolvedNodes)
    if (N && !N->isResolved())
      N->resolveCycles();
  UnresolvedNodes.clear();

  // Can't handle unresolved nodes anymore.
  AllowUnresolvedNodes = false;
}

/// If N is compile unit return NULL otherwise return N.
static MDNode *getNonCompileUnitScope(MDNode *N) {
  if (DIDescriptor(N).isCompileUnit())
    return nullptr;
  return N;
}

static MDNode *createFilePathPair(LLVMContext &VMContext, StringRef Filename,
                                  StringRef Directory) {
  assert(!Filename.empty() && "Unable to create file without name");
  Metadata *Pair[] = {MDString::get(VMContext, Filename),
                      MDString::get(VMContext, Directory)};
  return MDNode::get(VMContext, Pair);
}

DICompileUnit DIBuilder::createCompileUnit(unsigned Lang, StringRef Filename,
                                           StringRef Directory,
                                           StringRef Producer, bool isOptimized,
                                           StringRef Flags, unsigned RunTimeVer,
                                           StringRef SplitName,
                                           DebugEmissionKind Kind,
                                           bool EmitDebugInfo) {

  assert(((Lang <= dwarf::DW_LANG_Fortran08 && Lang >= dwarf::DW_LANG_C89) ||
          (Lang <= dwarf::DW_LANG_hi_user && Lang >= dwarf::DW_LANG_lo_user)) &&
         "Invalid Language tag");
  assert(!Filename.empty() &&
         "Unable to create compile unit without filename");
  Metadata *TElts[] = {HeaderBuilder::get(DW_TAG_base_type).get(VMContext)};
  TempEnumTypes = MDNode::getTemporary(VMContext, TElts).release();

  TempRetainTypes = MDNode::getTemporary(VMContext, TElts).release();

  TempSubprograms = MDNode::getTemporary(VMContext, TElts).release();

  TempGVs = MDNode::getTemporary(VMContext, TElts).release();

  TempImportedModules = MDNode::getTemporary(VMContext, TElts).release();

  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_compile_unit)
                          .concat(Lang)
                          .concat(Producer)
                          .concat(isOptimized)
                          .concat(Flags)
                          .concat(RunTimeVer)
                          .concat(SplitName)
                          .concat(Kind)
                          .get(VMContext),
                      createFilePathPair(VMContext, Filename, Directory),
                      TempEnumTypes, TempRetainTypes, TempSubprograms, TempGVs,
                      TempImportedModules};

  MDNode *CUNode = MDNode::get(VMContext, Elts);

  // Create a named metadata so that it is easier to find cu in a module.
  // Note that we only generate this when the caller wants to actually
  // emit debug information. When we are only interested in tracking
  // source line locations throughout the backend, we prevent codegen from
  // emitting debug info in the final output by not generating llvm.dbg.cu.
  if (EmitDebugInfo) {
    NamedMDNode *NMD = M.getOrInsertNamedMetadata("llvm.dbg.cu");
    NMD->addOperand(CUNode);
  }

  trackIfUnresolved(CUNode);
  return DICompileUnit(CUNode);
}

static DIImportedEntity
createImportedModule(LLVMContext &C, dwarf::Tag Tag, DIScope Context,
                     Metadata *NS, unsigned Line, StringRef Name,
                     SmallVectorImpl<TrackingMDNodeRef> &AllImportedModules) {
  const MDNode *R;
  Metadata *Elts[] = {HeaderBuilder::get(Tag).concat(Line).concat(Name).get(C),
                      Context, NS};
  R = MDNode::get(C, Elts);
  DIImportedEntity M(R);
  assert(M.Verify() && "Imported module should be valid");
  AllImportedModules.emplace_back(M.get());
  return M;
}

DIImportedEntity DIBuilder::createImportedModule(DIScope Context,
                                                 DINameSpace NS,
                                                 unsigned Line) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_module,
                                Context, NS, Line, StringRef(), AllImportedModules);
}

DIImportedEntity DIBuilder::createImportedModule(DIScope Context,
                                                 DIImportedEntity NS,
                                                 unsigned Line) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_module,
                                Context, NS, Line, StringRef(), AllImportedModules);
}

DIImportedEntity DIBuilder::createImportedDeclaration(DIScope Context,
                                                      DIDescriptor Decl,
                                                      unsigned Line, StringRef Name) {
  // Make sure to use the unique identifier based metadata reference for
  // types that have one.
  Metadata *V =
      Decl.isType() ? static_cast<Metadata *>(DIType(Decl).getRef()) : Decl;
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_declaration,
                                Context, V, Line, Name,
                                AllImportedModules);
}

DIImportedEntity DIBuilder::createImportedDeclaration(DIScope Context,
                                                      DIImportedEntity Imp,
                                                      unsigned Line, StringRef Name) {
  return ::createImportedModule(VMContext, dwarf::DW_TAG_imported_declaration,
                                Context, Imp, Line, Name, AllImportedModules);
}

DIFile DIBuilder::createFile(StringRef Filename, StringRef Directory) {
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_file_type).get(VMContext),
      createFilePathPair(VMContext, Filename, Directory)};
  return DIFile(MDNode::get(VMContext, Elts));
}

DIEnumerator DIBuilder::createEnumerator(StringRef Name, int64_t Val) {
  assert(!Name.empty() && "Unable to create enumerator without name");
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_enumerator)
                          .concat(Name)
                          .concat(Val)
                          .get(VMContext)};
  return DIEnumerator(MDNode::get(VMContext, Elts));
}

DIBasicType DIBuilder::createUnspecifiedType(StringRef Name) {
  assert(!Name.empty() && "Unable to create type without name");
  // Unspecified types are encoded in DIBasicType format. Line number, filename,
  // size, alignment, offset and flags are always empty here.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_unspecified_type)
          .concat(Name)
          .concat(0)
          .concat(0)
          .concat(0)
          .concat(0)
          .concat(0)
          .concat(0)
          .get(VMContext),
      nullptr, // Filename
      nullptr  // Unused
  };
  return DIBasicType(MDNode::get(VMContext, Elts));
}

DIBasicType DIBuilder::createNullPtrType() {
  return createUnspecifiedType("decltype(nullptr)");
}

DIBasicType
DIBuilder::createBasicType(StringRef Name, uint64_t SizeInBits,
                           uint64_t AlignInBits, unsigned Encoding) {
  assert(!Name.empty() && "Unable to create type without name");
  // Basic types are encoded in DIBasicType format. Line number, filename,
  // offset and flags are always empty here.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_base_type)
          .concat(Name)
          .concat(0) // Line
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(0) // Flags
          .concat(Encoding)
          .get(VMContext),
      nullptr, // Filename
      nullptr  // Unused
  };
  return DIBasicType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createQualifiedType(unsigned Tag, DIType FromTy) {
  // Qualified types are encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(Tag)
                          .concat(StringRef()) // Name
                          .concat(0)           // Line
                          .concat(0)           // Size
                          .concat(0)           // Align
                          .concat(0)           // Offset
                          .concat(0)           // Flags
                          .get(VMContext),
                      nullptr, // Filename
                      nullptr, // Unused
                      FromTy.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType
DIBuilder::createPointerType(DIType PointeeTy, uint64_t SizeInBits,
                             uint64_t AlignInBits, StringRef Name) {
  // Pointer types are encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_pointer_type)
                          .concat(Name)
                          .concat(0) // Line
                          .concat(SizeInBits)
                          .concat(AlignInBits)
                          .concat(0) // Offset
                          .concat(0) // Flags
                          .get(VMContext),
                      nullptr, // Filename
                      nullptr, // Unused
                      PointeeTy.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType
DIBuilder::createMemberPointerType(DIType PointeeTy, DIType Base,
                                   uint64_t SizeInBits, uint64_t AlignInBits) {
  // Pointer types are encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_ptr_to_member_type)
                          .concat(StringRef())
                          .concat(0) // Line
                          .concat(SizeInBits) // Size
                          .concat(AlignInBits) // Align
                          .concat(0) // Offset
                          .concat(0) // Flags
                          .get(VMContext),
                      nullptr, // Filename
                      nullptr, // Unused
                      PointeeTy.getRef(), Base.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createReferenceType(unsigned Tag, DIType RTy) {
  assert(RTy.isType() && "Unable to create reference type");
  // References are encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(Tag)
                          .concat(StringRef()) // Name
                          .concat(0)           // Line
                          .concat(0)           // Size
                          .concat(0)           // Align
                          .concat(0)           // Offset
                          .concat(0)           // Flags
                          .get(VMContext),
                      nullptr, // Filename
                      nullptr, // TheCU,
                      RTy.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createTypedef(DIType Ty, StringRef Name, DIFile File,
                                       unsigned LineNo, DIDescriptor Context) {
  // typedefs are encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_typedef)
                          .concat(Name)
                          .concat(LineNo)
                          .concat(0) // Size
                          .concat(0) // Align
                          .concat(0) // Offset
                          .concat(0) // Flags
                          .get(VMContext),
                      File.getFileNode(),
                      DIScope(getNonCompileUnitScope(Context)).getRef(),
                      Ty.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createFriend(DIType Ty, DIType FriendTy) {
  // typedefs are encoded in DIDerivedType format.
  assert(Ty.isType() && "Invalid type!");
  assert(FriendTy.isType() && "Invalid friend type!");
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_friend)
                          .concat(StringRef()) // Name
                          .concat(0)           // Line
                          .concat(0)           // Size
                          .concat(0)           // Align
                          .concat(0)           // Offset
                          .concat(0)           // Flags
                          .get(VMContext),
                      nullptr, Ty.getRef(), FriendTy.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createInheritance(DIType Ty, DIType BaseTy,
                                           uint64_t BaseOffset,
                                           unsigned Flags) {
  assert(Ty.isType() && "Unable to create inheritance");
  // TAG_inheritance is encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_inheritance)
                          .concat(StringRef()) // Name
                          .concat(0)           // Line
                          .concat(0)           // Size
                          .concat(0)           // Align
                          .concat(BaseOffset)
                          .concat(Flags)
                          .get(VMContext),
                      nullptr, Ty.getRef(), BaseTy.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createMemberType(DIDescriptor Scope, StringRef Name,
                                          DIFile File, unsigned LineNumber,
                                          uint64_t SizeInBits,
                                          uint64_t AlignInBits,
                                          uint64_t OffsetInBits, unsigned Flags,
                                          DIType Ty) {
  // TAG_member is encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_member)
                          .concat(Name)
                          .concat(LineNumber)
                          .concat(SizeInBits)
                          .concat(AlignInBits)
                          .concat(OffsetInBits)
                          .concat(Flags)
                          .get(VMContext),
                      File.getFileNode(),
                      DIScope(getNonCompileUnitScope(Scope)).getRef(),
                      Ty.getRef()};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

static Metadata *getConstantOrNull(Constant *C) {
  if (C)
    return ConstantAsMetadata::get(C);
  return nullptr;
}

DIDerivedType DIBuilder::createStaticMemberType(DIDescriptor Scope,
                                                StringRef Name, DIFile File,
                                                unsigned LineNumber, DIType Ty,
                                                unsigned Flags,
                                                llvm::Constant *Val) {
  // TAG_member is encoded in DIDerivedType format.
  Flags |= DIDescriptor::FlagStaticMember;
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_member)
                          .concat(Name)
                          .concat(LineNumber)
                          .concat(0) // Size
                          .concat(0) // Align
                          .concat(0) // Offset
                          .concat(Flags)
                          .get(VMContext),
                      File.getFileNode(),
                      DIScope(getNonCompileUnitScope(Scope)).getRef(),
                      Ty.getRef(), getConstantOrNull(Val)};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIDerivedType DIBuilder::createObjCIVar(StringRef Name, DIFile File,
                                        unsigned LineNumber,
                                        uint64_t SizeInBits,
                                        uint64_t AlignInBits,
                                        uint64_t OffsetInBits, unsigned Flags,
                                        DIType Ty, MDNode *PropertyNode) {
  // TAG_member is encoded in DIDerivedType format.
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_member)
                          .concat(Name)
                          .concat(LineNumber)
                          .concat(SizeInBits)
                          .concat(AlignInBits)
                          .concat(OffsetInBits)
                          .concat(Flags)
                          .get(VMContext),
                      File.getFileNode(), getNonCompileUnitScope(File), Ty,
                      PropertyNode};
  return DIDerivedType(MDNode::get(VMContext, Elts));
}

DIObjCProperty
DIBuilder::createObjCProperty(StringRef Name, DIFile File, unsigned LineNumber,
                              StringRef GetterName, StringRef SetterName,
                              unsigned PropertyAttributes, DIType Ty) {
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_APPLE_property)
                          .concat(Name)
                          .concat(LineNumber)
                          .concat(GetterName)
                          .concat(SetterName)
                          .concat(PropertyAttributes)
                          .get(VMContext),
                      File, Ty};
  return DIObjCProperty(MDNode::get(VMContext, Elts));
}

DITemplateTypeParameter
DIBuilder::createTemplateTypeParameter(DIDescriptor Context, StringRef Name,
                                       DIType Ty, MDNode *File, unsigned LineNo,
                                       unsigned ColumnNo) {
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_template_type_parameter)
                          .concat(Name)
                          .concat(LineNo)
                          .concat(ColumnNo)
                          .get(VMContext),
                      DIScope(getNonCompileUnitScope(Context)).getRef(),
                      Ty.getRef(), File};
  return DITemplateTypeParameter(MDNode::get(VMContext, Elts));
}

static DITemplateValueParameter createTemplateValueParameterHelper(
    LLVMContext &VMContext, unsigned Tag, DIDescriptor Context, StringRef Name,
    DIType Ty, Metadata *MD, MDNode *File, unsigned LineNo, unsigned ColumnNo) {
  Metadata *Elts[] = {
      HeaderBuilder::get(Tag).concat(Name).concat(LineNo).concat(ColumnNo).get(
          VMContext),
      DIScope(getNonCompileUnitScope(Context)).getRef(), Ty.getRef(), MD, File};
  return DITemplateValueParameter(MDNode::get(VMContext, Elts));
}

DITemplateValueParameter
DIBuilder::createTemplateValueParameter(DIDescriptor Context, StringRef Name,
                                        DIType Ty, Constant *Val, MDNode *File,
                                        unsigned LineNo, unsigned ColumnNo) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_template_value_parameter, Context, Name, Ty,
      getConstantOrNull(Val), File, LineNo, ColumnNo);
}

DITemplateValueParameter
DIBuilder::createTemplateTemplateParameter(DIDescriptor Context, StringRef Name,
                                           DIType Ty, StringRef Val,
                                           MDNode *File, unsigned LineNo,
                                           unsigned ColumnNo) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_GNU_template_template_param, Context, Name, Ty,
      MDString::get(VMContext, Val), File, LineNo, ColumnNo);
}

DITemplateValueParameter
DIBuilder::createTemplateParameterPack(DIDescriptor Context, StringRef Name,
                                       DIType Ty, DIArray Val,
                                       MDNode *File, unsigned LineNo,
                                       unsigned ColumnNo) {
  return createTemplateValueParameterHelper(
      VMContext, dwarf::DW_TAG_GNU_template_parameter_pack, Context, Name, Ty,
      Val, File, LineNo, ColumnNo);
}

DICompositeType DIBuilder::createClassType(DIDescriptor Context, StringRef Name,
                                           DIFile File, unsigned LineNumber,
                                           uint64_t SizeInBits,
                                           uint64_t AlignInBits,
                                           uint64_t OffsetInBits,
                                           unsigned Flags, DIType DerivedFrom,
                                           DIArray Elements,
                                           DIType VTableHolder,
                                           MDNode *TemplateParams,
                                           StringRef UniqueIdentifier) {
  assert((!Context || Context.isScope() || Context.isType()) &&
         "createClassType should be called with a valid Context");
  // TAG_class_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_class_type)
          .concat(Name)
          .concat(LineNumber)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(OffsetInBits)
          .concat(Flags)
          .concat(0)
          .get(VMContext),
      File.getFileNode(), DIScope(getNonCompileUnitScope(Context)).getRef(),
      DerivedFrom.getRef(), Elements, VTableHolder.getRef(), TemplateParams,
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  DICompositeType R(MDNode::get(VMContext, Elts));
  assert(R.isCompositeType() &&
         "createClassType should return a DICompositeType");
  if (!UniqueIdentifier.empty())
    retainType(R);
  return R;
}

DICompositeType DIBuilder::createStructType(DIDescriptor Context,
                                            StringRef Name, DIFile File,
                                            unsigned LineNumber,
                                            uint64_t SizeInBits,
                                            uint64_t AlignInBits,
                                            unsigned Flags, DIType DerivedFrom,
                                            DIArray Elements,
                                            unsigned RunTimeLang,
                                            DIType VTableHolder,
                                            StringRef UniqueIdentifier) {
 // TAG_structure_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_structure_type)
          .concat(Name)
          .concat(LineNumber)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0)
          .concat(Flags)
          .concat(RunTimeLang)
          .get(VMContext),
      File.getFileNode(), DIScope(getNonCompileUnitScope(Context)).getRef(),
      DerivedFrom.getRef(), Elements, VTableHolder.getRef(), nullptr,
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  DICompositeType R(MDNode::get(VMContext, Elts));
  assert(R.isCompositeType() &&
         "createStructType should return a DICompositeType");
  if (!UniqueIdentifier.empty())
    retainType(R);
  return R;
}

DICompositeType DIBuilder::createUnionType(DIDescriptor Scope, StringRef Name,
                                           DIFile File, unsigned LineNumber,
                                           uint64_t SizeInBits,
                                           uint64_t AlignInBits, unsigned Flags,
                                           DIArray Elements,
                                           unsigned RunTimeLang,
                                           StringRef UniqueIdentifier) {
  // TAG_union_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_union_type)
          .concat(Name)
          .concat(LineNumber)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(Flags)
          .concat(RunTimeLang)
          .get(VMContext),
      File.getFileNode(), DIScope(getNonCompileUnitScope(Scope)).getRef(),
      nullptr, Elements, nullptr, nullptr,
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  DICompositeType R(MDNode::get(VMContext, Elts));
  if (!UniqueIdentifier.empty())
    retainType(R);
  return R;
}

DISubroutineType DIBuilder::createSubroutineType(DIFile File,
                                                 DITypeArray ParameterTypes,
                                                 unsigned Flags) {
  // TAG_subroutine_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_subroutine_type)
          .concat(StringRef())
          .concat(0)     // Line
          .concat(0)     // Size
          .concat(0)     // Align
          .concat(0)     // Offset
          .concat(Flags) // Flags
          .concat(0)
          .get(VMContext),
      nullptr, nullptr, nullptr, ParameterTypes, nullptr, nullptr,
      nullptr // Type Identifer
  };
  return DISubroutineType(MDNode::get(VMContext, Elts));
}

DICompositeType DIBuilder::createEnumerationType(
    DIDescriptor Scope, StringRef Name, DIFile File, unsigned LineNumber,
    uint64_t SizeInBits, uint64_t AlignInBits, DIArray Elements,
    DIType UnderlyingType, StringRef UniqueIdentifier) {
  // TAG_enumeration_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_enumeration_type)
          .concat(Name)
          .concat(LineNumber)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(0) // Flags
          .concat(0)
          .get(VMContext),
      File.getFileNode(), DIScope(getNonCompileUnitScope(Scope)).getRef(),
      UnderlyingType.getRef(), Elements, nullptr, nullptr,
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  DICompositeType CTy(MDNode::get(VMContext, Elts));
  AllEnumTypes.push_back(CTy);
  if (!UniqueIdentifier.empty())
    retainType(CTy);
  return CTy;
}

DICompositeType DIBuilder::createArrayType(uint64_t Size, uint64_t AlignInBits,
                                           DIType Ty, DIArray Subscripts) {
  // TAG_array_type is encoded in DICompositeType format.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_array_type)
          .concat(StringRef())
          .concat(0) // Line
          .concat(Size)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(0) // Flags
          .concat(0)
          .get(VMContext),
      nullptr, // Filename/Directory,
      nullptr, // Unused
      Ty.getRef(), Subscripts, nullptr, nullptr,
      nullptr // Type Identifer
  };
  return DICompositeType(MDNode::get(VMContext, Elts));
}

DICompositeType DIBuilder::createVectorType(uint64_t Size, uint64_t AlignInBits,
                                            DIType Ty, DIArray Subscripts) {
  // A vector is an array type with the FlagVector flag applied.
  Metadata *Elts[] = {
      HeaderBuilder::get(dwarf::DW_TAG_array_type)
          .concat("")
          .concat(0) // Line
          .concat(Size)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(DIType::FlagVector)
          .concat(0)
          .get(VMContext),
      nullptr, // Filename/Directory,
      nullptr, // Unused
      Ty.getRef(), Subscripts, nullptr, nullptr,
      nullptr // Type Identifer
  };
  return DICompositeType(MDNode::get(VMContext, Elts));
}

static HeaderBuilder setTypeFlagsInHeader(StringRef Header,
                                          unsigned FlagsToSet) {
  DIHeaderFieldIterator I(Header);
  std::advance(I, 6);

  unsigned Flags;
  if (I->getAsInteger(0, Flags))
    Flags = 0;
  Flags |= FlagsToSet;

  return HeaderBuilder()
      .concat(I.getPrefix())
      .concat(Flags)
      .concat(I.getSuffix());
}

static DIType createTypeWithFlags(LLVMContext &Context, DIType Ty,
                                  unsigned FlagsToSet) {
  SmallVector<Metadata *, 9> Elts;
  MDNode *N = Ty;
  assert(N && "Unexpected input DIType!");
  // Update header field.
  Elts.push_back(setTypeFlagsInHeader(Ty.getHeader(), FlagsToSet).get(Context));
  for (unsigned I = 1, E = N->getNumOperands(); I != E; ++I)
    Elts.push_back(N->getOperand(I));

  return DIType(MDNode::get(Context, Elts));
}

DIType DIBuilder::createArtificialType(DIType Ty) {
  if (Ty.isArtificial())
    return Ty;
  return createTypeWithFlags(VMContext, Ty, DIType::FlagArtificial);
}

DIType DIBuilder::createObjectPointerType(DIType Ty) {
  if (Ty.isObjectPointer())
    return Ty;
  unsigned Flags = DIType::FlagObjectPointer | DIType::FlagArtificial;
  return createTypeWithFlags(VMContext, Ty, Flags);
}

void DIBuilder::retainType(DIType T) { AllRetainTypes.emplace_back(T); }

DIBasicType DIBuilder::createUnspecifiedParameter() {
  return DIBasicType();
}

DICompositeType
DIBuilder::createForwardDecl(unsigned Tag, StringRef Name, DIDescriptor Scope,
                             DIFile F, unsigned Line, unsigned RuntimeLang,
                             uint64_t SizeInBits, uint64_t AlignInBits,
                             StringRef UniqueIdentifier) {
  // Create a temporary MDNode.
  Metadata *Elts[] = {
      HeaderBuilder::get(Tag)
          .concat(Name)
          .concat(Line)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(DIDescriptor::FlagFwdDecl)
          .concat(RuntimeLang)
          .get(VMContext),
      F.getFileNode(), DIScope(getNonCompileUnitScope(Scope)).getRef(), nullptr,
      DIArray(), nullptr,
      nullptr, // TemplateParams
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  MDNode *Node = MDNode::get(VMContext, Elts);
  DICompositeType RetTy(Node);
  assert(RetTy.isCompositeType() &&
         "createForwardDecl result should be a DIType");
  if (!UniqueIdentifier.empty())
    retainType(RetTy);
  return RetTy;
}

DICompositeType DIBuilder::createReplaceableForwardDecl(
    unsigned Tag, StringRef Name, DIDescriptor Scope, DIFile F, unsigned Line,
    unsigned RuntimeLang, uint64_t SizeInBits, uint64_t AlignInBits,
    StringRef UniqueIdentifier) {
  // Create a temporary MDNode.
  Metadata *Elts[] = {
      HeaderBuilder::get(Tag)
          .concat(Name)
          .concat(Line)
          .concat(SizeInBits)
          .concat(AlignInBits)
          .concat(0) // Offset
          .concat(DIDescriptor::FlagFwdDecl)
          .concat(RuntimeLang)
          .get(VMContext),
      F.getFileNode(), DIScope(getNonCompileUnitScope(Scope)).getRef(), nullptr,
      DIArray(), nullptr,
      nullptr, // TemplateParams
      UniqueIdentifier.empty() ? nullptr
                               : MDString::get(VMContext, UniqueIdentifier)};
  DICompositeType RetTy(MDNode::getTemporary(VMContext, Elts).release());
  assert(RetTy.isCompositeType() &&
         "createReplaceableForwardDecl result should be a DIType");
  if (!UniqueIdentifier.empty())
    retainType(RetTy);
  return RetTy;
}

DIArray DIBuilder::getOrCreateArray(ArrayRef<Metadata *> Elements) {
  return DIArray(MDNode::get(VMContext, Elements));
}

DITypeArray DIBuilder::getOrCreateTypeArray(ArrayRef<Metadata *> Elements) {
  SmallVector<llvm::Metadata *, 16> Elts;
  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    if (Elements[i] && isa<MDNode>(Elements[i]))
      Elts.push_back(DIType(cast<MDNode>(Elements[i])).getRef());
    else
      Elts.push_back(Elements[i]);
  }
  return DITypeArray(MDNode::get(VMContext, Elts));
}

DISubrange DIBuilder::getOrCreateSubrange(int64_t Lo, int64_t Count) {
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_subrange_type)
                          .concat(Lo)
                          .concat(Count)
                          .get(VMContext)};

  return DISubrange(MDNode::get(VMContext, Elts));
}

static DIGlobalVariable createGlobalVariableHelper(
    LLVMContext &VMContext, DIDescriptor Context, StringRef Name,
    StringRef LinkageName, DIFile F, unsigned LineNumber, DITypeRef Ty,
    bool isLocalToUnit, Constant *Val, MDNode *Decl, bool isDefinition,
    std::function<MDNode *(ArrayRef<Metadata *>)> CreateFunc) {

  MDNode *TheCtx = getNonCompileUnitScope(Context);
  if (DIScope(TheCtx).isCompositeType()) {
    assert(!DICompositeType(TheCtx).getIdentifier() &&
           "Context of a global variable should not be a type with identifier");
  }

  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_variable)
                          .concat(Name)
                          .concat(Name)
                          .concat(LinkageName)
                          .concat(LineNumber)
                          .concat(isLocalToUnit)
                          .concat(isDefinition)
                          .get(VMContext),
                      TheCtx, F, Ty, getConstantOrNull(Val),
                      DIDescriptor(Decl)};

  return DIGlobalVariable(CreateFunc(Elts));
}

DIGlobalVariable DIBuilder::createGlobalVariable(
    DIDescriptor Context, StringRef Name, StringRef LinkageName, DIFile F,
    unsigned LineNumber, DITypeRef Ty, bool isLocalToUnit, Constant *Val,
    MDNode *Decl) {
  return createGlobalVariableHelper(
      VMContext, Context, Name, LinkageName, F, LineNumber, Ty, isLocalToUnit,
      Val, Decl, true, [&](ArrayRef<Metadata *> Elts) -> MDNode *{
        MDNode *Node = MDNode::get(VMContext, Elts);
        AllGVs.push_back(Node);
        return Node;
      });
}

DIGlobalVariable DIBuilder::createTempGlobalVariableFwdDecl(
    DIDescriptor Context, StringRef Name, StringRef LinkageName, DIFile F,
    unsigned LineNumber, DITypeRef Ty, bool isLocalToUnit, Constant *Val,
    MDNode *Decl) {
  return createGlobalVariableHelper(VMContext, Context, Name, LinkageName, F,
                                    LineNumber, Ty, isLocalToUnit, Val, Decl,
                                    false, [&](ArrayRef<Metadata *> Elts) {
    return MDNode::getTemporary(VMContext, Elts).release();
  });
}

DIVariable DIBuilder::createLocalVariable(unsigned Tag, DIDescriptor Scope,
                                          StringRef Name, DIFile File,
                                          unsigned LineNo, DITypeRef Ty,
                                          bool AlwaysPreserve, unsigned Flags,
                                          unsigned ArgNo) {
  DIDescriptor Context(getNonCompileUnitScope(Scope));
  assert((!Context || Context.isScope()) &&
         "createLocalVariable should be called with a valid Context");
  Metadata *Elts[] = {HeaderBuilder::get(Tag)
                          .concat(Name)
                          .concat(LineNo | (ArgNo << 24))
                          .concat(Flags)
                          .get(VMContext),
                      getNonCompileUnitScope(Scope), File, Ty};
  MDNode *Node = MDNode::get(VMContext, Elts);
  if (AlwaysPreserve) {
    // The optimizer may remove local variable. If there is an interest
    // to preserve variable info in such situation then stash it in a
    // named mdnode.
    DISubprogram Fn(getDISubprogram(Scope));
    assert(Fn && "Missing subprogram for local variable");
    PreservedVariables[Fn].emplace_back(Node);
  }
  DIVariable RetVar(Node);
  assert(RetVar.isVariable() &&
         "createLocalVariable should return a valid DIVariable");
  return RetVar;
}

DIExpression DIBuilder::createExpression(ArrayRef<int64_t> Addr) {
  auto Header = HeaderBuilder::get(DW_TAG_expression);
  for (int64_t I : Addr)
    Header.concat(I);
  Metadata *Elts[] = {Header.get(VMContext)};
  return DIExpression(MDNode::get(VMContext, Elts));
}

DIExpression DIBuilder::createPieceExpression(unsigned OffsetInBytes,
                                              unsigned SizeInBytes) {
  int64_t Addr[] = {dwarf::DW_OP_piece, OffsetInBytes, SizeInBytes};
  return createExpression(Addr);
}

DISubprogram DIBuilder::createFunction(DIScopeRef Context, StringRef Name,
                                       StringRef LinkageName, DIFile File,
                                       unsigned LineNo, DICompositeType Ty,
                                       bool isLocalToUnit, bool isDefinition,
                                       unsigned ScopeLine, unsigned Flags,
                                       bool isOptimized, Function *Fn,
                                       MDNode *TParams, MDNode *Decl) {
  // dragonegg does not generate identifier for types, so using an empty map
  // to resolve the context should be fine.
  DITypeIdentifierMap EmptyMap;
  return createFunction(Context.resolve(EmptyMap), Name, LinkageName, File,
                        LineNo, Ty, isLocalToUnit, isDefinition, ScopeLine,
                        Flags, isOptimized, Fn, TParams, Decl);
}

static DISubprogram createFunctionHelper(
    LLVMContext &VMContext, DIDescriptor Context, StringRef Name,
    StringRef LinkageName, DIFile File, unsigned LineNo, DICompositeType Ty,
    bool isLocalToUnit, bool isDefinition, unsigned ScopeLine, unsigned Flags,
    bool isOptimized, Function *Fn, MDNode *TParams, MDNode *Decl, MDNode *Vars,
    std::function<MDNode *(ArrayRef<Metadata *>)> CreateFunc) {
  assert(Ty.getTag() == dwarf::DW_TAG_subroutine_type &&
         "function types should be subroutines");
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_subprogram)
                          .concat(Name)
                          .concat(Name)
                          .concat(LinkageName)
                          .concat(LineNo)
                          .concat(isLocalToUnit)
                          .concat(isDefinition)
                          .concat(0)
                          .concat(0)
                          .concat(Flags)
                          .concat(isOptimized)
                          .concat(ScopeLine)
                          .get(VMContext),
                      File.getFileNode(),
                      DIScope(getNonCompileUnitScope(Context)).getRef(), Ty,
                      nullptr, getConstantOrNull(Fn), TParams, Decl, Vars};

  DISubprogram S(CreateFunc(Elts));
  assert(S.isSubprogram() &&
         "createFunction should return a valid DISubprogram");
  return S;
}


DISubprogram DIBuilder::createFunction(DIDescriptor Context, StringRef Name,
                                       StringRef LinkageName, DIFile File,
                                       unsigned LineNo, DICompositeType Ty,
                                       bool isLocalToUnit, bool isDefinition,
                                       unsigned ScopeLine, unsigned Flags,
                                       bool isOptimized, Function *Fn,
                                       MDNode *TParams, MDNode *Decl) {
  return createFunctionHelper(VMContext, Context, Name, LinkageName, File,
                              LineNo, Ty, isLocalToUnit, isDefinition,
                              ScopeLine, Flags, isOptimized, Fn, TParams, Decl,
                              MDNode::getTemporary(VMContext, None).release(),
                              [&](ArrayRef<Metadata *> Elts) -> MDNode *{
    MDNode *Node = MDNode::get(VMContext, Elts);
    // Create a named metadata so that we
    // do not lose this mdnode.
    if (isDefinition)
      AllSubprograms.push_back(Node);
    return Node;
  });
}

DISubprogram
DIBuilder::createTempFunctionFwdDecl(DIDescriptor Context, StringRef Name,
                                     StringRef LinkageName, DIFile File,
                                     unsigned LineNo, DICompositeType Ty,
                                     bool isLocalToUnit, bool isDefinition,
                                     unsigned ScopeLine, unsigned Flags,
                                     bool isOptimized, Function *Fn,
                                     MDNode *TParams, MDNode *Decl) {
  return createFunctionHelper(VMContext, Context, Name, LinkageName, File,
                              LineNo, Ty, isLocalToUnit, isDefinition,
                              ScopeLine, Flags, isOptimized, Fn, TParams, Decl,
                              nullptr, [&](ArrayRef<Metadata *> Elts) {
    return MDNode::getTemporary(VMContext, Elts).release();
  });
}

DISubprogram DIBuilder::createMethod(DIDescriptor Context, StringRef Name,
                                     StringRef LinkageName, DIFile F,
                                     unsigned LineNo, DICompositeType Ty,
                                     bool isLocalToUnit, bool isDefinition,
                                     unsigned VK, unsigned VIndex,
                                     DIType VTableHolder, unsigned Flags,
                                     bool isOptimized, Function *Fn,
                                     MDNode *TParam) {
  assert(Ty.getTag() == dwarf::DW_TAG_subroutine_type &&
         "function types should be subroutines");
  assert(getNonCompileUnitScope(Context) &&
         "Methods should have both a Context and a context that isn't "
         "the compile unit.");
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_subprogram)
                          .concat(Name)
                          .concat(Name)
                          .concat(LinkageName)
                          .concat(LineNo)
                          .concat(isLocalToUnit)
                          .concat(isDefinition)
                          .concat(VK)
                          .concat(VIndex)
                          .concat(Flags)
                          .concat(isOptimized)
                          .concat(LineNo)
                          // FIXME: Do we want to use different scope/lines?
                          .get(VMContext),
                      F.getFileNode(), DIScope(Context).getRef(), Ty,
                      VTableHolder.getRef(), getConstantOrNull(Fn), TParam,
                      nullptr, nullptr};
  MDNode *Node = MDNode::get(VMContext, Elts);
  if (isDefinition)
    AllSubprograms.push_back(Node);
  DISubprogram S(Node);
  assert(S.isSubprogram() && "createMethod should return a valid DISubprogram");
  return S;
}

DINameSpace DIBuilder::createNameSpace(DIDescriptor Scope, StringRef Name,
                                       DIFile File, unsigned LineNo) {
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_namespace)
                          .concat(Name)
                          .concat(LineNo)
                          .get(VMContext),
                      File.getFileNode(), getNonCompileUnitScope(Scope)};
  DINameSpace R(MDNode::get(VMContext, Elts));
  assert(R.Verify() &&
         "createNameSpace should return a verifiable DINameSpace");
  return R;
}

DILexicalBlockFile DIBuilder::createLexicalBlockFile(DIDescriptor Scope,
                                                     DIFile File,
                                                     unsigned Discriminator) {
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_lexical_block)
                          .concat(Discriminator)
                          .get(VMContext),
                      File.getFileNode(), Scope};
  DILexicalBlockFile R(MDNode::get(VMContext, Elts));
  assert(
      R.Verify() &&
      "createLexicalBlockFile should return a verifiable DILexicalBlockFile");
  return R;
}

DILexicalBlock DIBuilder::createLexicalBlock(DIDescriptor Scope, DIFile File,
                                             unsigned Line, unsigned Col) {
  // FIXME: This isn't thread safe nor the right way to defeat MDNode uniquing.
  // I believe the right way is to have a self-referential element in the node.
  // Also: why do we bother with line/column - they're not used and the
  // documentation (SourceLevelDebugging.rst) claims the line/col are necessary
  // for uniquing, yet then we have this other solution (because line/col were
  // inadequate) anyway. Remove all 3 and replace them with a self-reference.

  // Defeat MDNode uniquing for lexical blocks by using unique id.
  static unsigned int unique_id = 0;
  Metadata *Elts[] = {HeaderBuilder::get(dwarf::DW_TAG_lexical_block)
                          .concat(Line)
                          .concat(Col)
                          .concat(unique_id++)
                          .get(VMContext),
                      File.getFileNode(), getNonCompileUnitScope(Scope)};
  DILexicalBlock R(MDNode::get(VMContext, Elts));
  assert(R.Verify() &&
         "createLexicalBlock should return a verifiable DILexicalBlock");
  return R;
}

static Value *getDbgIntrinsicValueImpl(LLVMContext &VMContext, Value *V) {
  assert(V && "no value passed to dbg intrinsic");
  return MetadataAsValue::get(VMContext, ValueAsMetadata::get(V));
}

Instruction *DIBuilder::insertDeclare(Value *Storage, DIVariable VarInfo,
                                      DIExpression Expr,
                                      Instruction *InsertBefore) {
  assert(VarInfo.isVariable() &&
         "empty or invalid DIVariable passed to dbg.declare");
  if (!DeclareFn)
    DeclareFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_declare);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, Storage),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};
  return CallInst::Create(DeclareFn, Args, "", InsertBefore);
}

Instruction *DIBuilder::insertDeclare(Value *Storage, DIVariable VarInfo,
                                      DIExpression Expr,
                                      BasicBlock *InsertAtEnd) {
  assert(VarInfo.isVariable() &&
         "empty or invalid DIVariable passed to dbg.declare");
  if (!DeclareFn)
    DeclareFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_declare);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, Storage),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};

  // If this block already has a terminator then insert this intrinsic
  // before the terminator.
  if (TerminatorInst *T = InsertAtEnd->getTerminator())
    return CallInst::Create(DeclareFn, Args, "", T);
  else
    return CallInst::Create(DeclareFn, Args, "", InsertAtEnd);
}

Instruction *DIBuilder::insertDbgValueIntrinsic(Value *V, uint64_t Offset,
                                                DIVariable VarInfo,
                                                DIExpression Expr,
                                                Instruction *InsertBefore) {
  assert(V && "no value passed to dbg.value");
  assert(VarInfo.isVariable() &&
         "empty or invalid DIVariable passed to dbg.value");
  if (!ValueFn)
    ValueFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_value);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, V),
                   ConstantInt::get(Type::getInt64Ty(VMContext), Offset),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};
  return CallInst::Create(ValueFn, Args, "", InsertBefore);
}

Instruction *DIBuilder::insertDbgValueIntrinsic(Value *V, uint64_t Offset,
                                                DIVariable VarInfo,
                                                DIExpression Expr,
                                                BasicBlock *InsertAtEnd) {
  assert(V && "no value passed to dbg.value");
  assert(VarInfo.isVariable() &&
         "empty or invalid DIVariable passed to dbg.value");
  if (!ValueFn)
    ValueFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_value);

  trackIfUnresolved(VarInfo);
  trackIfUnresolved(Expr);
  Value *Args[] = {getDbgIntrinsicValueImpl(VMContext, V),
                   ConstantInt::get(Type::getInt64Ty(VMContext), Offset),
                   MetadataAsValue::get(VMContext, VarInfo),
                   MetadataAsValue::get(VMContext, Expr)};
  return CallInst::Create(ValueFn, Args, "", InsertAtEnd);
}

void DIBuilder::replaceVTableHolder(DICompositeType &T, DICompositeType VTableHolder) {
  T.setContainingType(VTableHolder);

  // If this didn't create a self-reference, just return.
  if (T != VTableHolder)
    return;

  // Look for unresolved operands.  T has dropped RAUW support and is already
  // marked resolved, orphaning any cycles underneath it.
  assert(T->isResolved() && "Expected self-reference to be resolved");
  for (const MDOperand &O : T->operands())
    if (auto *N = dyn_cast_or_null<MDNode>(O))
      trackIfUnresolved(N);
}

void DIBuilder::replaceArrays(DICompositeType &T, DIArray Elements,
                              DIArray TParams) {
  T.setArrays(Elements, TParams);

  // If T isn't resolved, there's no problem.
  if (!T->isResolved())
    return;

  // If "T" is resolved, it may be due to a self-reference cycle.  Track the
  // arrays explicitly if they're unresolved, or else the cycles will be
  // orphaned.
  if (Elements)
    trackIfUnresolved(Elements);
  if (TParams)
    trackIfUnresolved(TParams);
}
