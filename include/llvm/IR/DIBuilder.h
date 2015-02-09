//===- DIBuilder.h - Debug Information Builder ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a DIBuilder that is useful for creating debugging
// information entries in LLVM IR form.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIBUILDER_H
#define LLVM_IR_DIBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
  class BasicBlock;
  class Instruction;
  class Function;
  class Module;
  class Value;
  class Constant;
  class LLVMContext;
  class MDNode;
  class StringRef;
  class DIBasicType;
  class DICompileUnit;
  class DICompositeType;
  class DIDerivedType;
  class DIDescriptor;
  class DIFile;
  class DIEnumerator;
  class DIType;
  class DIGlobalVariable;
  class DIImportedEntity;
  class DINameSpace;
  class DIVariable;
  class DISubrange;
  class DILexicalBlockFile;
  class DILexicalBlock;
  class DIScope;
  class DISubprogram;
  class DITemplateTypeParameter;
  class DITemplateValueParameter;
  class DIObjCProperty;

  class DIBuilder {
    Module &M;
    LLVMContext &VMContext;

    MDNode *TempEnumTypes;
    MDNode *TempRetainTypes;
    MDNode *TempSubprograms;
    MDNode *TempGVs;
    MDNode *TempImportedModules;

    Function *DeclareFn;     // llvm.dbg.declare
    Function *ValueFn;       // llvm.dbg.value

    SmallVector<Metadata *, 4> AllEnumTypes;
    /// Track the RetainTypes, since they can be updated later on.
    SmallVector<TrackingMDNodeRef, 4> AllRetainTypes;
    SmallVector<Metadata *, 4> AllSubprograms;
    SmallVector<Metadata *, 4> AllGVs;
    SmallVector<TrackingMDNodeRef, 4> AllImportedModules;

    /// \brief Track nodes that may be unresolved.
    SmallVector<TrackingMDNodeRef, 4> UnresolvedNodes;
    bool AllowUnresolvedNodes;

    /// Each subprogram's preserved local variables.
    DenseMap<MDNode *, std::vector<TrackingMDNodeRef>> PreservedVariables;

    DIBuilder(const DIBuilder &) LLVM_DELETED_FUNCTION;
    void operator=(const DIBuilder &) LLVM_DELETED_FUNCTION;

    /// \brief Create a temporary.
    ///
    /// Create an \a temporary node and track it in \a UnresolvedNodes.
    void trackIfUnresolved(MDNode *N);

  public:
    /// \brief Construct a builder for a module.
    ///
    /// If \c AllowUnresolved, collect unresolved nodes attached to the module
    /// in order to resolve cycles during \a finalize().
    explicit DIBuilder(Module &M, bool AllowUnresolved = true);
    enum DebugEmissionKind { FullDebug=1, LineTablesOnly };

    /// finalize - Construct any deferred debug info descriptors.
    void finalize();

    /// createCompileUnit - A CompileUnit provides an anchor for all debugging
    /// information generated during this instance of compilation.
    /// @param Lang     Source programming language, eg. dwarf::DW_LANG_C99
    /// @param File     File name
    /// @param Dir      Directory
    /// @param Producer Identify the producer of debugging information and code.
    ///                 Usually this is a compiler version string.
    /// @param isOptimized A boolean flag which indicates whether optimization
    ///                    is ON or not.
    /// @param Flags    This string lists command line options. This string is
    ///                 directly embedded in debug info output which may be used
    ///                 by a tool analyzing generated debugging information.
    /// @param RV       This indicates runtime version for languages like
    ///                 Objective-C.
    /// @param SplitName The name of the file that we'll split debug info out
    ///                  into.
    /// @param Kind     The kind of debug information to generate.
    /// @param EmitDebugInfo   A boolean flag which indicates whether debug
    ///                        information should be written to the final
    ///                        output or not. When this is false, debug
    ///                        information annotations will be present in
    ///                        the IL but they are not written to the final
    ///                        assembly or object file. This supports tracking
    ///                        source location information in the back end
    ///                        without actually changing the output (e.g.,
    ///                        when using optimization remarks).
    DICompileUnit createCompileUnit(unsigned Lang, StringRef File,
                                    StringRef Dir, StringRef Producer,
                                    bool isOptimized, StringRef Flags,
                                    unsigned RV,
                                    StringRef SplitName = StringRef(),
                                    DebugEmissionKind Kind = FullDebug,
                                    bool EmitDebugInfo = true);

    /// createFile - Create a file descriptor to hold debugging information
    /// for a file.
    DIFile createFile(StringRef Filename, StringRef Directory);

    /// createEnumerator - Create a single enumerator value.
    DIEnumerator createEnumerator(StringRef Name, int64_t Val);

    /// \brief Create a DWARF unspecified type.
    DIBasicType createUnspecifiedType(StringRef Name);

    /// \brief Create C++11 nullptr type.
    DIBasicType createNullPtrType();

    /// createBasicType - Create debugging information entry for a basic
    /// type.
    /// @param Name        Type name.
    /// @param SizeInBits  Size of the type.
    /// @param AlignInBits Type alignment.
    /// @param Encoding    DWARF encoding code, e.g. dwarf::DW_ATE_float.
    DIBasicType createBasicType(StringRef Name, uint64_t SizeInBits,
                                uint64_t AlignInBits, unsigned Encoding);

    /// createQualifiedType - Create debugging information entry for a qualified
    /// type, e.g. 'const int'.
    /// @param Tag         Tag identifing type, e.g. dwarf::TAG_volatile_type
    /// @param FromTy      Base Type.
    DIDerivedType createQualifiedType(unsigned Tag, DIType FromTy);

    /// createPointerType - Create debugging information entry for a pointer.
    /// @param PointeeTy   Type pointed by this pointer.
    /// @param SizeInBits  Size.
    /// @param AlignInBits Alignment. (optional)
    /// @param Name        Pointer type name. (optional)
    DIDerivedType
    createPointerType(DIType PointeeTy, uint64_t SizeInBits,
                      uint64_t AlignInBits = 0, StringRef Name = StringRef());

    /// \brief Create debugging information entry for a pointer to member.
    /// @param PointeeTy Type pointed to by this pointer.
    /// @param SizeInBits  Size.
    /// @param AlignInBits Alignment. (optional)
    /// @param Class Type for which this pointer points to members of.
    DIDerivedType createMemberPointerType(DIType PointeeTy, DIType Class,
                                          uint64_t SizeInBits,
                                          uint64_t AlignInBits = 0);

    /// createReferenceType - Create debugging information entry for a c++
    /// style reference or rvalue reference type.
    DIDerivedType createReferenceType(unsigned Tag, DIType RTy);

    /// createTypedef - Create debugging information entry for a typedef.
    /// @param Ty          Original type.
    /// @param Name        Typedef name.
    /// @param File        File where this type is defined.
    /// @param LineNo      Line number.
    /// @param Context     The surrounding context for the typedef.
    DIDerivedType createTypedef(DIType Ty, StringRef Name, DIFile File,
                                unsigned LineNo, DIDescriptor Context);

    /// createFriend - Create debugging information entry for a 'friend'.
    DIDerivedType createFriend(DIType Ty, DIType FriendTy);

    /// createInheritance - Create debugging information entry to establish
    /// inheritance relationship between two types.
    /// @param Ty           Original type.
    /// @param BaseTy       Base type. Ty is inherits from base.
    /// @param BaseOffset   Base offset.
    /// @param Flags        Flags to describe inheritance attribute,
    ///                     e.g. private
    DIDerivedType createInheritance(DIType Ty, DIType BaseTy,
                                    uint64_t BaseOffset, unsigned Flags);

    /// createMemberType - Create debugging information entry for a member.
    /// @param Scope        Member scope.
    /// @param Name         Member name.
    /// @param File         File where this member is defined.
    /// @param LineNo       Line number.
    /// @param SizeInBits   Member size.
    /// @param AlignInBits  Member alignment.
    /// @param OffsetInBits Member offset.
    /// @param Flags        Flags to encode member attribute, e.g. private
    /// @param Ty           Parent type.
    DIDerivedType
    createMemberType(DIDescriptor Scope, StringRef Name, DIFile File,
                     unsigned LineNo, uint64_t SizeInBits, uint64_t AlignInBits,
                     uint64_t OffsetInBits, unsigned Flags, DIType Ty);

    /// createStaticMemberType - Create debugging information entry for a
    /// C++ static data member.
    /// @param Scope      Member scope.
    /// @param Name       Member name.
    /// @param File       File where this member is declared.
    /// @param LineNo     Line number.
    /// @param Ty         Type of the static member.
    /// @param Flags      Flags to encode member attribute, e.g. private.
    /// @param Val        Const initializer of the member.
    DIDerivedType createStaticMemberType(DIDescriptor Scope, StringRef Name,
                                         DIFile File, unsigned LineNo,
                                         DIType Ty, unsigned Flags,
                                         llvm::Constant *Val);

    /// createObjCIVar - Create debugging information entry for Objective-C
    /// instance variable.
    /// @param Name         Member name.
    /// @param File         File where this member is defined.
    /// @param LineNo       Line number.
    /// @param SizeInBits   Member size.
    /// @param AlignInBits  Member alignment.
    /// @param OffsetInBits Member offset.
    /// @param Flags        Flags to encode member attribute, e.g. private
    /// @param Ty           Parent type.
    /// @param PropertyNode Property associated with this ivar.
    DIDerivedType createObjCIVar(StringRef Name, DIFile File,
                                 unsigned LineNo, uint64_t SizeInBits,
                                 uint64_t AlignInBits, uint64_t OffsetInBits,
                                 unsigned Flags, DIType Ty,
                                 MDNode *PropertyNode);

    /// createObjCProperty - Create debugging information entry for Objective-C
    /// property.
    /// @param Name         Property name.
    /// @param File         File where this property is defined.
    /// @param LineNumber   Line number.
    /// @param GetterName   Name of the Objective C property getter selector.
    /// @param SetterName   Name of the Objective C property setter selector.
    /// @param PropertyAttributes Objective C property attributes.
    /// @param Ty           Type.
    DIObjCProperty createObjCProperty(StringRef Name,
                                      DIFile File, unsigned LineNumber,
                                      StringRef GetterName,
                                      StringRef SetterName,
                                      unsigned PropertyAttributes,
                                      DIType Ty);

    /// createClassType - Create debugging information entry for a class.
    /// @param Scope        Scope in which this class is defined.
    /// @param Name         class name.
    /// @param File         File where this member is defined.
    /// @param LineNumber   Line number.
    /// @param SizeInBits   Member size.
    /// @param AlignInBits  Member alignment.
    /// @param OffsetInBits Member offset.
    /// @param Flags        Flags to encode member attribute, e.g. private
    /// @param Elements     class members.
    /// @param VTableHolder Debug info of the base class that contains vtable
    ///                     for this type. This is used in
    ///                     DW_AT_containing_type. See DWARF documentation
    ///                     for more info.
    /// @param TemplateParms Template type parameters.
    /// @param UniqueIdentifier A unique identifier for the class.
    DICompositeType createClassType(DIDescriptor Scope, StringRef Name,
                                    DIFile File, unsigned LineNumber,
                                    uint64_t SizeInBits, uint64_t AlignInBits,
                                    uint64_t OffsetInBits, unsigned Flags,
                                    DIType DerivedFrom, DIArray Elements,
                                    DIType VTableHolder = DIType(),
                                    MDNode *TemplateParms = nullptr,
                                    StringRef UniqueIdentifier = StringRef());

    /// createStructType - Create debugging information entry for a struct.
    /// @param Scope        Scope in which this struct is defined.
    /// @param Name         Struct name.
    /// @param File         File where this member is defined.
    /// @param LineNumber   Line number.
    /// @param SizeInBits   Member size.
    /// @param AlignInBits  Member alignment.
    /// @param Flags        Flags to encode member attribute, e.g. private
    /// @param Elements     Struct elements.
    /// @param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// @param UniqueIdentifier A unique identifier for the struct.
    DICompositeType createStructType(DIDescriptor Scope, StringRef Name,
                                     DIFile File, unsigned LineNumber,
                                     uint64_t SizeInBits, uint64_t AlignInBits,
                                     unsigned Flags, DIType DerivedFrom,
                                     DIArray Elements, unsigned RunTimeLang = 0,
                                     DIType VTableHolder = DIType(),
                                     StringRef UniqueIdentifier = StringRef());

    /// createUnionType - Create debugging information entry for an union.
    /// @param Scope        Scope in which this union is defined.
    /// @param Name         Union name.
    /// @param File         File where this member is defined.
    /// @param LineNumber   Line number.
    /// @param SizeInBits   Member size.
    /// @param AlignInBits  Member alignment.
    /// @param Flags        Flags to encode member attribute, e.g. private
    /// @param Elements     Union elements.
    /// @param RunTimeLang  Optional parameter, Objective-C runtime version.
    /// @param UniqueIdentifier A unique identifier for the union.
    DICompositeType createUnionType(
        DIDescriptor Scope, StringRef Name, DIFile File, unsigned LineNumber,
        uint64_t SizeInBits, uint64_t AlignInBits, unsigned Flags,
        DIArray Elements, unsigned RunTimeLang = 0,
        StringRef UniqueIdentifier = StringRef());

    /// createTemplateTypeParameter - Create debugging information for template
    /// type parameter.
    /// @param Scope        Scope in which this type is defined.
    /// @param Name         Type parameter name.
    /// @param Ty           Parameter type.
    /// @param File         File where this type parameter is defined.
    /// @param LineNo       Line number.
    /// @param ColumnNo     Column Number.
    DITemplateTypeParameter
    createTemplateTypeParameter(DIDescriptor Scope, StringRef Name, DIType Ty,
                                MDNode *File = nullptr, unsigned LineNo = 0,
                                unsigned ColumnNo = 0);

    /// createTemplateValueParameter - Create debugging information for template
    /// value parameter.
    /// @param Scope        Scope in which this type is defined.
    /// @param Name         Value parameter name.
    /// @param Ty           Parameter type.
    /// @param Val          Constant parameter value.
    /// @param File         File where this type parameter is defined.
    /// @param LineNo       Line number.
    /// @param ColumnNo     Column Number.
    DITemplateValueParameter
    createTemplateValueParameter(DIDescriptor Scope, StringRef Name, DIType Ty,
                                 Constant *Val, MDNode *File = nullptr,
                                 unsigned LineNo = 0, unsigned ColumnNo = 0);

    /// \brief Create debugging information for a template template parameter.
    /// @param Scope        Scope in which this type is defined.
    /// @param Name         Value parameter name.
    /// @param Ty           Parameter type.
    /// @param Val          The fully qualified name of the template.
    /// @param File         File where this type parameter is defined.
    /// @param LineNo       Line number.
    /// @param ColumnNo     Column Number.
    DITemplateValueParameter
    createTemplateTemplateParameter(DIDescriptor Scope, StringRef Name,
                                    DIType Ty, StringRef Val,
                                    MDNode *File = nullptr, unsigned LineNo = 0,
                                    unsigned ColumnNo = 0);

    /// \brief Create debugging information for a template parameter pack.
    /// @param Scope        Scope in which this type is defined.
    /// @param Name         Value parameter name.
    /// @param Ty           Parameter type.
    /// @param Val          An array of types in the pack.
    /// @param File         File where this type parameter is defined.
    /// @param LineNo       Line number.
    /// @param ColumnNo     Column Number.
    DITemplateValueParameter
    createTemplateParameterPack(DIDescriptor Scope, StringRef Name,
                                DIType Ty, DIArray Val, MDNode *File = nullptr,
                                unsigned LineNo = 0, unsigned ColumnNo = 0);

    /// createArrayType - Create debugging information entry for an array.
    /// @param Size         Array size.
    /// @param AlignInBits  Alignment.
    /// @param Ty           Element type.
    /// @param Subscripts   Subscripts.
    DICompositeType createArrayType(uint64_t Size, uint64_t AlignInBits,
                                    DIType Ty, DIArray Subscripts);

    /// createVectorType - Create debugging information entry for a vector type.
    /// @param Size         Array size.
    /// @param AlignInBits  Alignment.
    /// @param Ty           Element type.
    /// @param Subscripts   Subscripts.
    DICompositeType createVectorType(uint64_t Size, uint64_t AlignInBits,
                                     DIType Ty, DIArray Subscripts);

    /// createEnumerationType - Create debugging information entry for an
    /// enumeration.
    /// @param Scope          Scope in which this enumeration is defined.
    /// @param Name           Union name.
    /// @param File           File where this member is defined.
    /// @param LineNumber     Line number.
    /// @param SizeInBits     Member size.
    /// @param AlignInBits    Member alignment.
    /// @param Elements       Enumeration elements.
    /// @param UnderlyingType Underlying type of a C++11/ObjC fixed enum.
    /// @param UniqueIdentifier A unique identifier for the enum.
    DICompositeType createEnumerationType(DIDescriptor Scope, StringRef Name,
        DIFile File, unsigned LineNumber, uint64_t SizeInBits,
        uint64_t AlignInBits, DIArray Elements, DIType UnderlyingType,
        StringRef UniqueIdentifier = StringRef());

    /// createSubroutineType - Create subroutine type.
    /// @param File            File in which this subroutine is defined.
    /// @param ParameterTypes  An array of subroutine parameter types. This
    ///                        includes return type at 0th index.
    /// @param Flags           E.g.: LValueReference.
    ///                        These flags are used to emit dwarf attributes.
    DISubroutineType createSubroutineType(DIFile File,
                                          DITypeArray ParameterTypes,
                                          unsigned Flags = 0);

    /// createArtificialType - Create a new DIType with "artificial" flag set.
    DIType createArtificialType(DIType Ty);

    /// createObjectPointerType - Create a new DIType with the "object pointer"
    /// flag set.
    DIType createObjectPointerType(DIType Ty);

    /// \brief Create a permanent forward-declared type.
    DICompositeType createForwardDecl(unsigned Tag, StringRef Name,
                                      DIDescriptor Scope, DIFile F,
                                      unsigned Line, unsigned RuntimeLang = 0,
                                      uint64_t SizeInBits = 0,
                                      uint64_t AlignInBits = 0,
                                      StringRef UniqueIdentifier = StringRef());

    /// \brief Create a temporary forward-declared type.
    DICompositeType createReplaceableForwardDecl(
        unsigned Tag, StringRef Name, DIDescriptor Scope, DIFile F,
        unsigned Line, unsigned RuntimeLang = 0, uint64_t SizeInBits = 0,
        uint64_t AlignInBits = 0, StringRef UniqueIdentifier = StringRef());

    /// retainType - Retain DIType in a module even if it is not referenced
    /// through debug info anchors.
    void retainType(DIType T);

    /// createUnspecifiedParameter - Create unspecified parameter type
    /// for a subroutine type.
    DIBasicType createUnspecifiedParameter();

    /// getOrCreateArray - Get a DIArray, create one if required.
    DIArray getOrCreateArray(ArrayRef<Metadata *> Elements);

    /// getOrCreateTypeArray - Get a DITypeArray, create one if required.
    DITypeArray getOrCreateTypeArray(ArrayRef<Metadata *> Elements);

    /// getOrCreateSubrange - Create a descriptor for a value range.  This
    /// implicitly uniques the values returned.
    DISubrange getOrCreateSubrange(int64_t Lo, int64_t Count);


    /// createGlobalVariable - Create a new descriptor for the specified
    /// variable.
    /// @param Context     Variable scope.
    /// @param Name        Name of the variable.
    /// @param LinkageName Mangled  name of the variable.
    /// @param File        File where this variable is defined.
    /// @param LineNo      Line number.
    /// @param Ty          Variable Type.
    /// @param isLocalToUnit Boolean flag indicate whether this variable is
    ///                      externally visible or not.
    /// @param Val         llvm::Value of the variable.
    /// @param Decl        Reference to the corresponding declaration.
    DIGlobalVariable createGlobalVariable(DIDescriptor Context, StringRef Name,
                                          StringRef LinkageName, DIFile File,
                                          unsigned LineNo, DITypeRef Ty,
                                          bool isLocalToUnit,
                                          llvm::Constant *Val,
                                          MDNode *Decl = nullptr);

    /// createTempGlobalVariableFwdDecl - Identical to createGlobalVariable
    /// except that the resulting DbgNode is temporary and meant to be RAUWed.
    DIGlobalVariable createTempGlobalVariableFwdDecl(
        DIDescriptor Context, StringRef Name, StringRef LinkageName,
        DIFile File, unsigned LineNo, DITypeRef Ty, bool isLocalToUnit,
        llvm::Constant *Val, MDNode *Decl = nullptr);

    /// createLocalVariable - Create a new descriptor for the specified
    /// local variable.
    /// @param Tag         Dwarf TAG. Usually DW_TAG_auto_variable or
    ///                    DW_TAG_arg_variable.
    /// @param Scope       Variable scope.
    /// @param Name        Variable name.
    /// @param File        File where this variable is defined.
    /// @param LineNo      Line number.
    /// @param Ty          Variable Type
    /// @param AlwaysPreserve Boolean. Set to true if debug info for this
    ///                       variable should be preserved in optimized build.
    /// @param Flags       Flags, e.g. artificial variable.
    /// @param ArgNo       If this variable is an argument then this argument's
    ///                    number. 1 indicates 1st argument.
    DIVariable createLocalVariable(unsigned Tag, DIDescriptor Scope,
                                   StringRef Name,
                                   DIFile File, unsigned LineNo,
                                   DITypeRef Ty, bool AlwaysPreserve = false,
                                   unsigned Flags = 0,
                                   unsigned ArgNo = 0);

    /// createExpression - Create a new descriptor for the specified
    /// variable which has a complex address expression for its address.
    /// @param Addr        An array of complex address operations.
    DIExpression createExpression(ArrayRef<int64_t> Addr = None);

    /// createPieceExpression - Create a descriptor to describe one part
    /// of aggregate variable that is fragmented across multiple Values.
    ///
    /// @param OffsetInBytes Offset of the piece in bytes.
    /// @param SizeInBytes   Size of the piece in bytes.
    DIExpression createPieceExpression(unsigned OffsetInBytes,
                                       unsigned SizeInBytes);

    /// createFunction - Create a new descriptor for the specified subprogram.
    /// See comments in DISubprogram for descriptions of these fields.
    /// @param Scope         Function scope.
    /// @param Name          Function name.
    /// @param LinkageName   Mangled function name.
    /// @param File          File where this variable is defined.
    /// @param LineNo        Line number.
    /// @param Ty            Function type.
    /// @param isLocalToUnit True if this function is not externally visible.
    /// @param isDefinition  True if this is a function definition.
    /// @param ScopeLine     Set to the beginning of the scope this starts
    /// @param Flags         e.g. is this function prototyped or not.
    ///                      These flags are used to emit dwarf attributes.
    /// @param isOptimized   True if optimization is ON.
    /// @param Fn            llvm::Function pointer.
    /// @param TParam        Function template parameters.
    DISubprogram createFunction(DIDescriptor Scope, StringRef Name,
                                StringRef LinkageName,
                                DIFile File, unsigned LineNo,
                                DICompositeType Ty, bool isLocalToUnit,
                                bool isDefinition,
                                unsigned ScopeLine,
                                unsigned Flags = 0,
                                bool isOptimized = false,
                                Function *Fn = nullptr,
                                MDNode *TParam = nullptr,
                                MDNode *Decl = nullptr);

    /// createTempFunctionFwdDecl - Identical to createFunction,
    /// except that the resulting DbgNode is meant to be RAUWed.
    DISubprogram createTempFunctionFwdDecl(DIDescriptor Scope, StringRef Name,
                                           StringRef LinkageName,
                                           DIFile File, unsigned LineNo,
                                           DICompositeType Ty, bool isLocalToUnit,
                                           bool isDefinition,
                                           unsigned ScopeLine,
                                           unsigned Flags = 0,
                                           bool isOptimized = false,
                                           Function *Fn = nullptr,
                                           MDNode *TParam = nullptr,
                                           MDNode *Decl = nullptr);


    /// FIXME: this is added for dragonegg. Once we update dragonegg
    /// to call resolve function, this will be removed.
    DISubprogram createFunction(DIScopeRef Scope, StringRef Name,
                                StringRef LinkageName,
                                DIFile File, unsigned LineNo,
                                DICompositeType Ty, bool isLocalToUnit,
                                bool isDefinition,
                                unsigned ScopeLine,
                                unsigned Flags = 0,
                                bool isOptimized = false,
                                Function *Fn = nullptr,
                                MDNode *TParam = nullptr,
                                MDNode *Decl = nullptr);

    /// createMethod - Create a new descriptor for the specified C++ method.
    /// See comments in DISubprogram for descriptions of these fields.
    /// @param Scope         Function scope.
    /// @param Name          Function name.
    /// @param LinkageName   Mangled function name.
    /// @param File          File where this variable is defined.
    /// @param LineNo        Line number.
    /// @param Ty            Function type.
    /// @param isLocalToUnit True if this function is not externally visible..
    /// @param isDefinition  True if this is a function definition.
    /// @param Virtuality    Attributes describing virtualness. e.g. pure
    ///                      virtual function.
    /// @param VTableIndex   Index no of this method in virtual table.
    /// @param VTableHolder  Type that holds vtable.
    /// @param Flags         e.g. is this function prototyped or not.
    ///                      This flags are used to emit dwarf attributes.
    /// @param isOptimized   True if optimization is ON.
    /// @param Fn            llvm::Function pointer.
    /// @param TParam        Function template parameters.
    DISubprogram createMethod(DIDescriptor Scope, StringRef Name,
                              StringRef LinkageName,
                              DIFile File, unsigned LineNo,
                              DICompositeType Ty, bool isLocalToUnit,
                              bool isDefinition,
                              unsigned Virtuality = 0, unsigned VTableIndex = 0,
                              DIType VTableHolder = DIType(),
                              unsigned Flags = 0,
                              bool isOptimized = false,
                              Function *Fn = nullptr,
                              MDNode *TParam = nullptr);

    /// createNameSpace - This creates new descriptor for a namespace
    /// with the specified parent scope.
    /// @param Scope       Namespace scope
    /// @param Name        Name of this namespace
    /// @param File        Source file
    /// @param LineNo      Line number
    DINameSpace createNameSpace(DIDescriptor Scope, StringRef Name,
                                DIFile File, unsigned LineNo);


    /// createLexicalBlockFile - This creates a descriptor for a lexical
    /// block with a new file attached. This merely extends the existing
    /// lexical block as it crosses a file.
    /// @param Scope       Lexical block.
    /// @param File        Source file.
    /// @param Discriminator DWARF path discriminator value.
    DILexicalBlockFile createLexicalBlockFile(DIDescriptor Scope, DIFile File,
                                              unsigned Discriminator = 0);

    /// createLexicalBlock - This creates a descriptor for a lexical block
    /// with the specified parent context.
    /// @param Scope         Parent lexical scope.
    /// @param File          Source file.
    /// @param Line          Line number.
    /// @param Col           Column number.
    DILexicalBlock createLexicalBlock(DIDescriptor Scope, DIFile File,
                                      unsigned Line, unsigned Col);

    /// \brief Create a descriptor for an imported module.
    /// @param Context The scope this module is imported into
    /// @param NS The namespace being imported here
    /// @param Line Line number
    DIImportedEntity createImportedModule(DIScope Context, DINameSpace NS,
                                          unsigned Line);

    /// \brief Create a descriptor for an imported module.
    /// @param Context The scope this module is imported into
    /// @param NS An aliased namespace
    /// @param Line Line number
    DIImportedEntity createImportedModule(DIScope Context, DIImportedEntity NS,
                                          unsigned Line);

    /// \brief Create a descriptor for an imported function.
    /// @param Context The scope this module is imported into
    /// @param Decl The declaration (or definition) of a function, type, or
    ///             variable
    /// @param Line Line number
    DIImportedEntity createImportedDeclaration(DIScope Context, DIDescriptor Decl,
                                               unsigned Line,
                                               StringRef Name = StringRef());
    DIImportedEntity createImportedDeclaration(DIScope Context,
                                               DIImportedEntity NS,
                                               unsigned Line,
                                               StringRef Name = StringRef());

    /// insertDeclare - Insert a new llvm.dbg.declare intrinsic call.
    /// @param Storage     llvm::Value of the variable
    /// @param VarInfo     Variable's debug info descriptor.
    /// @param Expr         A complex location expression.
    /// @param InsertAtEnd Location for the new intrinsic.
    Instruction *insertDeclare(llvm::Value *Storage, DIVariable VarInfo,
                               DIExpression Expr, BasicBlock *InsertAtEnd);

    /// insertDeclare - Insert a new llvm.dbg.declare intrinsic call.
    /// @param Storage      llvm::Value of the variable
    /// @param VarInfo      Variable's debug info descriptor.
    /// @param Expr         A complex location expression.
    /// @param InsertBefore Location for the new intrinsic.
    Instruction *insertDeclare(llvm::Value *Storage, DIVariable VarInfo,
                               DIExpression Expr, Instruction *InsertBefore);

    /// insertDbgValueIntrinsic - Insert a new llvm.dbg.value intrinsic call.
    /// @param Val          llvm::Value of the variable
    /// @param Offset       Offset
    /// @param VarInfo      Variable's debug info descriptor.
    /// @param Expr         A complex location expression.
    /// @param InsertAtEnd Location for the new intrinsic.
    Instruction *insertDbgValueIntrinsic(llvm::Value *Val, uint64_t Offset,
                                         DIVariable VarInfo, DIExpression Expr,
                                         BasicBlock *InsertAtEnd);

    /// insertDbgValueIntrinsic - Insert a new llvm.dbg.value intrinsic call.
    /// @param Val          llvm::Value of the variable
    /// @param Offset       Offset
    /// @param VarInfo      Variable's debug info descriptor.
    /// @param Expr         A complex location expression.
    /// @param InsertBefore Location for the new intrinsic.
    Instruction *insertDbgValueIntrinsic(llvm::Value *Val, uint64_t Offset,
                                         DIVariable VarInfo, DIExpression Expr,
                                         Instruction *InsertBefore);

    /// \brief Replace the vtable holder in the given composite type.
    ///
    /// If this creates a self reference, it may orphan some unresolved cycles
    /// in the operands of \c T, so \a DIBuilder needs to track that.
    void replaceVTableHolder(DICompositeType &T, DICompositeType VTableHolder);

    /// \brief Replace arrays on a composite type.
    ///
    /// If \c T is resolved, but the arrays aren't -- which can happen if \c T
    /// has a self-reference -- \a DIBuilder needs to track the array to
    /// resolve cycles.
    void replaceArrays(DICompositeType &T, DIArray Elements,
                       DIArray TParems = DIArray());
  };
} // end namespace llvm

#endif
