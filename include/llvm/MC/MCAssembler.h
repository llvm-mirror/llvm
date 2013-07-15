//===- MCAssembler.h - Object File Generation -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASSEMBLER_H
#define LLVM_MC_MCASSEMBLER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataTypes.h"
#include <vector> // FIXME: Shouldn't be needed.

namespace llvm {
class raw_ostream;
class MCAsmLayout;
class MCAssembler;
class MCContext;
class MCCodeEmitter;
class MCExpr;
class MCFragment;
class MCObjectWriter;
class MCSection;
class MCSectionData;
class MCSymbol;
class MCSymbolData;
class MCValue;
class MCAsmBackend;

class MCFragment : public ilist_node<MCFragment> {
  friend class MCAsmLayout;

  MCFragment(const MCFragment&) LLVM_DELETED_FUNCTION;
  void operator=(const MCFragment&) LLVM_DELETED_FUNCTION;

public:
  enum FragmentType {
    FT_Align,
    FT_Data,
    FT_CompactEncodedInst,
    FT_Fill,
    FT_Relaxable,
    FT_Org,
    FT_Dwarf,
    FT_DwarfFrame,
    FT_LEB
  };

private:
  FragmentType Kind;

  /// Parent - The data for the section this fragment is in.
  MCSectionData *Parent;

  /// Atom - The atom this fragment is in, as represented by it's defining
  /// symbol. Atom's are only used by backends which set
  /// \see MCAsmBackend::hasReliableSymbolDifference().
  MCSymbolData *Atom;

  /// @name Assembler Backend Data
  /// @{
  //
  // FIXME: This could all be kept private to the assembler implementation.

  /// Offset - The offset of this fragment in its section. This is ~0 until
  /// initialized.
  uint64_t Offset;

  /// LayoutOrder - The layout order of this fragment.
  unsigned LayoutOrder;

  /// @}

protected:
  MCFragment(FragmentType _Kind, MCSectionData *_Parent = 0);

public:
  // Only for sentinel.
  MCFragment();
  virtual ~MCFragment();

  FragmentType getKind() const { return Kind; }

  MCSectionData *getParent() const { return Parent; }
  void setParent(MCSectionData *Value) { Parent = Value; }

  MCSymbolData *getAtom() const { return Atom; }
  void setAtom(MCSymbolData *Value) { Atom = Value; }

  unsigned getLayoutOrder() const { return LayoutOrder; }
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  /// \brief Does this fragment have instructions emitted into it? By default
  /// this is false, but specific fragment types may set it to true.
  virtual bool hasInstructions() const { return false; }

  /// \brief Should this fragment be placed at the end of an aligned bundle?
  virtual bool alignToBundleEnd() const { return false; }
  virtual void setAlignToBundleEnd(bool V) { }

  /// \brief Get the padding size that must be inserted before this fragment.
  /// Used for bundling. By default, no padding is inserted.
  /// Note that padding size is restricted to 8 bits. This is an optimization
  /// to reduce the amount of space used for each fragment. In practice, larger
  /// padding should never be required.
  virtual uint8_t getBundlePadding() const {
    return 0;
  }

  /// \brief Set the padding size for this fragment. By default it's a no-op,
  /// and only some fragments have a meaningful implementation.
  virtual void setBundlePadding(uint8_t N) {
  }

  void dump();
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data.
///
class MCEncodedFragment : public MCFragment {
  virtual void anchor();

  uint8_t BundlePadding;
public:
  MCEncodedFragment(MCFragment::FragmentType FType, MCSectionData *SD = 0)
    : MCFragment(FType, SD), BundlePadding(0)
  {
  }
  virtual ~MCEncodedFragment();

  virtual SmallVectorImpl<char> &getContents() = 0;
  virtual const SmallVectorImpl<char> &getContents() const = 0;

  virtual uint8_t getBundlePadding() const {
    return BundlePadding;
  }

  virtual void setBundlePadding(uint8_t N) {
    BundlePadding = N;
  }

  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    switch (Kind) {
      default:
        return false;
      case MCFragment::FT_Relaxable:
      case MCFragment::FT_CompactEncodedInst:
      case MCFragment::FT_Data:
        return true;
    }
  }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data and also have fixups registered.
///
class MCEncodedFragmentWithFixups : public MCEncodedFragment {
  virtual void anchor();

public:
  MCEncodedFragmentWithFixups(MCFragment::FragmentType FType,
                              MCSectionData *SD = 0)
    : MCEncodedFragment(FType, SD)
  {
  }

  virtual ~MCEncodedFragmentWithFixups();

  typedef SmallVectorImpl<MCFixup>::const_iterator const_fixup_iterator;
  typedef SmallVectorImpl<MCFixup>::iterator fixup_iterator;

  virtual SmallVectorImpl<MCFixup> &getFixups() = 0;
  virtual const SmallVectorImpl<MCFixup> &getFixups() const = 0;

  virtual fixup_iterator fixup_begin() = 0;
  virtual const_fixup_iterator fixup_begin() const  = 0;
  virtual fixup_iterator fixup_end() = 0;
  virtual const_fixup_iterator fixup_end() const = 0;

  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    return Kind == MCFragment::FT_Relaxable || Kind == MCFragment::FT_Data;
  }
};

/// Fragment for data and encoded instructions.
///
class MCDataFragment : public MCEncodedFragmentWithFixups {
  virtual void anchor();

  /// \brief Does this fragment contain encoded instructions anywhere in it?
  bool HasInstructions;

  /// \brief Should this fragment be aligned to the end of a bundle?
  bool AlignToBundleEnd;

  SmallVector<char, 32> Contents;

  /// Fixups - The list of fixups in this fragment.
  SmallVector<MCFixup, 4> Fixups;
public:
  MCDataFragment(MCSectionData *SD = 0)
    : MCEncodedFragmentWithFixups(FT_Data, SD),
      HasInstructions(false), AlignToBundleEnd(false)
  {
  }

  virtual SmallVectorImpl<char> &getContents() { return Contents; }
  virtual const SmallVectorImpl<char> &getContents() const { return Contents; }

  SmallVectorImpl<MCFixup> &getFixups() {
    return Fixups;
  }

  const SmallVectorImpl<MCFixup> &getFixups() const {
    return Fixups;
  }

  virtual bool hasInstructions() const { return HasInstructions; }
  virtual void setHasInstructions(bool V) { HasInstructions = V; }

  virtual bool alignToBundleEnd() const { return AlignToBundleEnd; }
  virtual void setAlignToBundleEnd(bool V) { AlignToBundleEnd = V; }

  fixup_iterator fixup_begin() { return Fixups.begin(); }
  const_fixup_iterator fixup_begin() const { return Fixups.begin(); }

  fixup_iterator fixup_end() {return Fixups.end();}
  const_fixup_iterator fixup_end() const {return Fixups.end();}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Data;
  }
};

/// This is a compact (memory-size-wise) fragment for holding an encoded
/// instruction (non-relaxable) that has no fixups registered. When applicable,
/// it can be used instead of MCDataFragment and lead to lower memory
/// consumption.
///
class MCCompactEncodedInstFragment : public MCEncodedFragment {
  virtual void anchor();

  /// \brief Should this fragment be aligned to the end of a bundle?
  bool AlignToBundleEnd;

  SmallVector<char, 4> Contents;
public:
  MCCompactEncodedInstFragment(MCSectionData *SD = 0)
    : MCEncodedFragment(FT_CompactEncodedInst, SD), AlignToBundleEnd(false)
  {
  }

  virtual bool hasInstructions() const {
    return true;
  }

  virtual SmallVectorImpl<char> &getContents() { return Contents; }
  virtual const SmallVectorImpl<char> &getContents() const { return Contents; }

  virtual bool alignToBundleEnd() const { return AlignToBundleEnd; }
  virtual void setAlignToBundleEnd(bool V) { AlignToBundleEnd = V; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CompactEncodedInst;
  }
};

/// A relaxable fragment holds on to its MCInst, since it may need to be
/// relaxed during the assembler layout and relaxation stage.
///
class MCRelaxableFragment : public MCEncodedFragmentWithFixups {
  virtual void anchor();

  /// Inst - The instruction this is a fragment for.
  MCInst Inst;

  /// Contents - Binary data for the currently encoded instruction.
  SmallVector<char, 8> Contents;

  /// Fixups - The list of fixups in this fragment.
  SmallVector<MCFixup, 1> Fixups;

public:
  MCRelaxableFragment(const MCInst &_Inst, MCSectionData *SD = 0)
    : MCEncodedFragmentWithFixups(FT_Relaxable, SD), Inst(_Inst) {
  }

  virtual SmallVectorImpl<char> &getContents() { return Contents; }
  virtual const SmallVectorImpl<char> &getContents() const { return Contents; }

  const MCInst &getInst() const { return Inst; }
  void setInst(const MCInst& Value) { Inst = Value; }

  SmallVectorImpl<MCFixup> &getFixups() {
    return Fixups;
  }

  const SmallVectorImpl<MCFixup> &getFixups() const {
    return Fixups;
  }

  virtual bool hasInstructions() const { return true; }

  fixup_iterator fixup_begin() { return Fixups.begin(); }
  const_fixup_iterator fixup_begin() const { return Fixups.begin(); }

  fixup_iterator fixup_end() {return Fixups.end();}
  const_fixup_iterator fixup_end() const {return Fixups.end();}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Relaxable;
  }
};

class MCAlignFragment : public MCFragment {
  virtual void anchor();

  /// Alignment - The alignment to ensure, in bytes.
  unsigned Alignment;

  /// Value - Value to use for filling padding bytes.
  int64_t Value;

  /// ValueSize - The size of the integer (in bytes) of \p Value.
  unsigned ValueSize;

  /// MaxBytesToEmit - The maximum number of bytes to emit; if the alignment
  /// cannot be satisfied in this width then this fragment is ignored.
  unsigned MaxBytesToEmit;

  /// EmitNops - Flag to indicate that (optimal) NOPs should be emitted instead
  /// of using the provided value. The exact interpretation of this flag is
  /// target dependent.
  bool EmitNops : 1;

public:
  MCAlignFragment(unsigned _Alignment, int64_t _Value, unsigned _ValueSize,
                  unsigned _MaxBytesToEmit, MCSectionData *SD = 0)
    : MCFragment(FT_Align, SD), Alignment(_Alignment),
      Value(_Value),ValueSize(_ValueSize),
      MaxBytesToEmit(_MaxBytesToEmit), EmitNops(false) {}

  /// @name Accessors
  /// @{

  unsigned getAlignment() const { return Alignment; }

  int64_t getValue() const { return Value; }

  unsigned getValueSize() const { return ValueSize; }

  unsigned getMaxBytesToEmit() const { return MaxBytesToEmit; }

  bool hasEmitNops() const { return EmitNops; }
  void setEmitNops(bool Value) { EmitNops = Value; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Align;
  }
};

class MCFillFragment : public MCFragment {
  virtual void anchor();

  /// Value - Value to use for filling bytes.
  int64_t Value;

  /// ValueSize - The size (in bytes) of \p Value to use when filling, or 0 if
  /// this is a virtual fill fragment.
  unsigned ValueSize;

  /// Size - The number of bytes to insert.
  uint64_t Size;

public:
  MCFillFragment(int64_t _Value, unsigned _ValueSize, uint64_t _Size,
                 MCSectionData *SD = 0)
    : MCFragment(FT_Fill, SD),
      Value(_Value), ValueSize(_ValueSize), Size(_Size) {
    assert((!ValueSize || (Size % ValueSize) == 0) &&
           "Fill size must be a multiple of the value size!");
  }

  /// @name Accessors
  /// @{

  int64_t getValue() const { return Value; }

  unsigned getValueSize() const { return ValueSize; }

  uint64_t getSize() const { return Size; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Fill;
  }
};

class MCOrgFragment : public MCFragment {
  virtual void anchor();

  /// Offset - The offset this fragment should start at.
  const MCExpr *Offset;

  /// Value - Value to use for filling bytes.
  int8_t Value;

public:
  MCOrgFragment(const MCExpr &_Offset, int8_t _Value, MCSectionData *SD = 0)
    : MCFragment(FT_Org, SD),
      Offset(&_Offset), Value(_Value) {}

  /// @name Accessors
  /// @{

  const MCExpr &getOffset() const { return *Offset; }

  uint8_t getValue() const { return Value; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Org;
  }
};

class MCLEBFragment : public MCFragment {
  virtual void anchor();

  /// Value - The value this fragment should contain.
  const MCExpr *Value;

  /// IsSigned - True if this is a sleb128, false if uleb128.
  bool IsSigned;

  SmallString<8> Contents;
public:
  MCLEBFragment(const MCExpr &Value_, bool IsSigned_, MCSectionData *SD = 0)
    : MCFragment(FT_LEB, SD),
      Value(&Value_), IsSigned(IsSigned_) { Contents.push_back(0); }

  /// @name Accessors
  /// @{

  const MCExpr &getValue() const { return *Value; }

  bool isSigned() const { return IsSigned; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_LEB;
  }
};

class MCDwarfLineAddrFragment : public MCFragment {
  virtual void anchor();

  /// LineDelta - the value of the difference between the two line numbers
  /// between two .loc dwarf directives.
  int64_t LineDelta;

  /// AddrDelta - The expression for the difference of the two symbols that
  /// make up the address delta between two .loc dwarf directives.
  const MCExpr *AddrDelta;

  SmallString<8> Contents;

public:
  MCDwarfLineAddrFragment(int64_t _LineDelta, const MCExpr &_AddrDelta,
                      MCSectionData *SD = 0)
    : MCFragment(FT_Dwarf, SD),
      LineDelta(_LineDelta), AddrDelta(&_AddrDelta) { Contents.push_back(0); }

  /// @name Accessors
  /// @{

  int64_t getLineDelta() const { return LineDelta; }

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Dwarf;
  }
};

class MCDwarfCallFrameFragment : public MCFragment {
  virtual void anchor();

  /// AddrDelta - The expression for the difference of the two symbols that
  /// make up the address delta between two .cfi_* dwarf directives.
  const MCExpr *AddrDelta;

  SmallString<8> Contents;

public:
  MCDwarfCallFrameFragment(const MCExpr &_AddrDelta,  MCSectionData *SD = 0)
    : MCFragment(FT_DwarfFrame, SD),
      AddrDelta(&_AddrDelta) { Contents.push_back(0); }

  /// @name Accessors
  /// @{

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_DwarfFrame;
  }
};

// FIXME: Should this be a separate class, or just merged into MCSection? Since
// we anticipate the fast path being through an MCAssembler, the only reason to
// keep it out is for API abstraction.
class MCSectionData : public ilist_node<MCSectionData> {
  friend class MCAsmLayout;

  MCSectionData(const MCSectionData&) LLVM_DELETED_FUNCTION;
  void operator=(const MCSectionData&) LLVM_DELETED_FUNCTION;

public:
  typedef iplist<MCFragment> FragmentListType;

  typedef FragmentListType::const_iterator const_iterator;
  typedef FragmentListType::iterator iterator;

  typedef FragmentListType::const_reverse_iterator const_reverse_iterator;
  typedef FragmentListType::reverse_iterator reverse_iterator;

  /// \brief Express the state of bundle locked groups while emitting code.
  enum BundleLockStateType {
    NotBundleLocked,
    BundleLocked,
    BundleLockedAlignToEnd
  };
private:
  FragmentListType Fragments;
  const MCSection *Section;

  /// Ordinal - The section index in the assemblers section list.
  unsigned Ordinal;

  /// LayoutOrder - The index of this section in the layout order.
  unsigned LayoutOrder;

  /// Alignment - The maximum alignment seen in this section.
  unsigned Alignment;

  /// \brief Keeping track of bundle-locked state.
  BundleLockStateType BundleLockState; 

  /// \brief We've seen a bundle_lock directive but not its first instruction
  /// yet.
  bool BundleGroupBeforeFirstInst;

  /// @name Assembler Backend Data
  /// @{
  //
  // FIXME: This could all be kept private to the assembler implementation.

  /// HasInstructions - Whether this section has had instructions emitted into
  /// it.
  unsigned HasInstructions : 1;

  /// Mapping from subsection number to insertion point for subsection numbers
  /// below that number.
  SmallVector<std::pair<unsigned, MCFragment *>, 1> SubsectionFragmentMap;

  /// @}

public:
  // Only for use as sentinel.
  MCSectionData();
  MCSectionData(const MCSection &Section, MCAssembler *A = 0);

  const MCSection &getSection() const { return *Section; }

  unsigned getAlignment() const { return Alignment; }
  void setAlignment(unsigned Value) { Alignment = Value; }

  bool hasInstructions() const { return HasInstructions; }
  void setHasInstructions(bool Value) { HasInstructions = Value; }

  unsigned getOrdinal() const { return Ordinal; }
  void setOrdinal(unsigned Value) { Ordinal = Value; }

  unsigned getLayoutOrder() const { return LayoutOrder; }
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  /// @name Fragment Access
  /// @{

  const FragmentListType &getFragmentList() const { return Fragments; }
  FragmentListType &getFragmentList() { return Fragments; }

  iterator begin() { return Fragments.begin(); }
  const_iterator begin() const { return Fragments.begin(); }

  iterator end() { return Fragments.end(); }
  const_iterator end() const { return Fragments.end(); }

  reverse_iterator rbegin() { return Fragments.rbegin(); }
  const_reverse_iterator rbegin() const { return Fragments.rbegin(); }

  reverse_iterator rend() { return Fragments.rend(); }
  const_reverse_iterator rend() const { return Fragments.rend(); }

  size_t size() const { return Fragments.size(); }

  bool empty() const { return Fragments.empty(); }

  iterator getSubsectionInsertionPoint(unsigned Subsection);

  bool isBundleLocked() const {
    return BundleLockState != NotBundleLocked;
  }

  BundleLockStateType getBundleLockState() const {
    return BundleLockState;
  }

  void setBundleLockState(BundleLockStateType NewState) {
    BundleLockState = NewState;
  }

  bool isBundleGroupBeforeFirstInst() const {
    return BundleGroupBeforeFirstInst;
  }

  void setBundleGroupBeforeFirstInst(bool IsFirst) {
    BundleGroupBeforeFirstInst = IsFirst;
  }

  void dump();

  /// @}
};

// FIXME: Same concerns as with SectionData.
class MCSymbolData : public ilist_node<MCSymbolData> {
public:
  const MCSymbol *Symbol;

  /// Fragment - The fragment this symbol's value is relative to, if any.
  MCFragment *Fragment;

  /// Offset - The offset to apply to the fragment address to form this symbol's
  /// value.
  uint64_t Offset;

  /// IsExternal - True if this symbol is visible outside this translation
  /// unit.
  unsigned IsExternal : 1;

  /// IsPrivateExtern - True if this symbol is private extern.
  unsigned IsPrivateExtern : 1;

  /// CommonSize - The size of the symbol, if it is 'common', or 0.
  //
  // FIXME: Pack this in with other fields? We could put it in offset, since a
  // common symbol can never get a definition.
  uint64_t CommonSize;

  /// SymbolSize - An expression describing how to calculate the size of
  /// a symbol. If a symbol has no size this field will be NULL.
  const MCExpr *SymbolSize;

  /// CommonAlign - The alignment of the symbol, if it is 'common'.
  //
  // FIXME: Pack this in with other fields?
  unsigned CommonAlign;

  /// Flags - The Flags field is used by object file implementations to store
  /// additional per symbol information which is not easily classified.
  uint32_t Flags;

  /// Index - Index field, for use by the object file implementation.
  uint64_t Index;

public:
  // Only for use as sentinel.
  MCSymbolData();
  MCSymbolData(const MCSymbol &_Symbol, MCFragment *_Fragment, uint64_t _Offset,
               MCAssembler *A = 0);

  /// @name Accessors
  /// @{

  const MCSymbol &getSymbol() const { return *Symbol; }

  MCFragment *getFragment() const { return Fragment; }
  void setFragment(MCFragment *Value) { Fragment = Value; }

  uint64_t getOffset() const { return Offset; }
  void setOffset(uint64_t Value) { Offset = Value; }

  /// @}
  /// @name Symbol Attributes
  /// @{

  bool isExternal() const { return IsExternal; }
  void setExternal(bool Value) { IsExternal = Value; }

  bool isPrivateExtern() const { return IsPrivateExtern; }
  void setPrivateExtern(bool Value) { IsPrivateExtern = Value; }

  /// isCommon - Is this a 'common' symbol.
  bool isCommon() const { return CommonSize != 0; }

  /// setCommon - Mark this symbol as being 'common'.
  ///
  /// \param Size - The size of the symbol.
  /// \param Align - The alignment of the symbol.
  void setCommon(uint64_t Size, unsigned Align) {
    CommonSize = Size;
    CommonAlign = Align;
  }

  /// getCommonSize - Return the size of a 'common' symbol.
  uint64_t getCommonSize() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return CommonSize;
  }

  void setSize(const MCExpr *SS) {
    SymbolSize = SS;
  }

  const MCExpr *getSize() const {
    return SymbolSize;
  }


  /// getCommonAlignment - Return the alignment of a 'common' symbol.
  unsigned getCommonAlignment() const {
    assert(isCommon() && "Not a 'common' symbol!");
    return CommonAlign;
  }

  /// getFlags - Get the (implementation defined) symbol flags.
  uint32_t getFlags() const { return Flags; }

  /// setFlags - Set the (implementation defined) symbol flags.
  void setFlags(uint32_t Value) { Flags = Value; }

  /// modifyFlags - Modify the flags via a mask
  void modifyFlags(uint32_t Value, uint32_t Mask) {
    Flags = (Flags & ~Mask) | Value;
  }

  /// getIndex - Get the (implementation defined) index.
  uint64_t getIndex() const { return Index; }

  /// setIndex - Set the (implementation defined) index.
  void setIndex(uint64_t Value) { Index = Value; }

  /// @}

  void dump();
};

// FIXME: This really doesn't belong here. See comments below.
struct IndirectSymbolData {
  MCSymbol *Symbol;
  MCSectionData *SectionData;
};

// FIXME: Ditto this. Purely so the Streamer and the ObjectWriter can talk
// to one another.
struct DataRegionData {
  // This enum should be kept in sync w/ the mach-o definition in
  // llvm/Object/MachOFormat.h.
  enum KindTy { Data = 1, JumpTable8, JumpTable16, JumpTable32 } Kind;
  MCSymbol *Start;
  MCSymbol *End;
};

class MCAssembler {
  friend class MCAsmLayout;

public:
  typedef iplist<MCSectionData> SectionDataListType;
  typedef iplist<MCSymbolData> SymbolDataListType;

  typedef SectionDataListType::const_iterator const_iterator;
  typedef SectionDataListType::iterator iterator;

  typedef SymbolDataListType::const_iterator const_symbol_iterator;
  typedef SymbolDataListType::iterator symbol_iterator;

  typedef std::vector<IndirectSymbolData>::const_iterator
    const_indirect_symbol_iterator;
  typedef std::vector<IndirectSymbolData>::iterator indirect_symbol_iterator;

  typedef std::vector<DataRegionData>::const_iterator
    const_data_region_iterator;
  typedef std::vector<DataRegionData>::iterator data_region_iterator;

private:
  MCAssembler(const MCAssembler&) LLVM_DELETED_FUNCTION;
  void operator=(const MCAssembler&) LLVM_DELETED_FUNCTION;

  MCContext &Context;

  MCAsmBackend &Backend;

  MCCodeEmitter &Emitter;

  MCObjectWriter &Writer;

  raw_ostream &OS;

  iplist<MCSectionData> Sections;

  iplist<MCSymbolData> Symbols;

  /// The map of sections to their associated assembler backend data.
  //
  // FIXME: Avoid this indirection?
  DenseMap<const MCSection*, MCSectionData*> SectionMap;

  /// The map of symbols to their associated assembler backend data.
  //
  // FIXME: Avoid this indirection?
  DenseMap<const MCSymbol*, MCSymbolData*> SymbolMap;

  std::vector<IndirectSymbolData> IndirectSymbols;

  std::vector<DataRegionData> DataRegions;

  /// The list of linker options to propagate into the object file.
  std::vector<std::vector<std::string> > LinkerOptions;

  /// The set of function symbols for which a .thumb_func directive has
  /// been seen.
  //
  // FIXME: We really would like this in target specific code rather than
  // here. Maybe when the relocation stuff moves to target specific,
  // this can go with it? The streamer would need some target specific
  // refactoring too.
  SmallPtrSet<const MCSymbol*, 64> ThumbFuncs;

  /// \brief The bundle alignment size currently set in the assembler.
  ///
  /// By default it's 0, which means bundling is disabled.
  unsigned BundleAlignSize;

  unsigned RelaxAll : 1;
  unsigned NoExecStack : 1;
  unsigned SubsectionsViaSymbols : 1;

  /// ELF specific e_header flags
  // It would be good if there were an MCELFAssembler class to hold this.
  // ELF header flags are used both by the integrated and standalone assemblers.
  // Access to the flags is necessary in cases where assembler directives affect
  // which flags to be set.
  unsigned ELFHeaderEFlags;
private:
  /// Evaluate a fixup to a relocatable expression and the value which should be
  /// placed into the fixup.
  ///
  /// \param Layout The layout to use for evaluation.
  /// \param Fixup The fixup to evaluate.
  /// \param DF The fragment the fixup is inside.
  /// \param Target [out] On return, the relocatable expression the fixup
  /// evaluates to.
  /// \param Value [out] On return, the value of the fixup as currently laid
  /// out.
  /// \return Whether the fixup value was fully resolved. This is true if the
  /// \p Value result is fixed, otherwise the value may change due to
  /// relocation.
  bool evaluateFixup(const MCAsmLayout &Layout,
                     const MCFixup &Fixup, const MCFragment *DF,
                     MCValue &Target, uint64_t &Value) const;

  /// Check whether a fixup can be satisfied, or whether it needs to be relaxed
  /// (increased in size, in order to hold its value correctly).
  bool fixupNeedsRelaxation(const MCFixup &Fixup, const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const;

  /// Check whether the given fragment needs relaxation.
  bool fragmentNeedsRelaxation(const MCRelaxableFragment *IF,
                               const MCAsmLayout &Layout) const;

  /// \brief Perform one layout iteration and return true if any offsets
  /// were adjusted.
  bool layoutOnce(MCAsmLayout &Layout);

  /// \brief Perform one layout iteration of the given section and return true
  /// if any offsets were adjusted.
  bool layoutSectionOnce(MCAsmLayout &Layout, MCSectionData &SD);

  bool relaxInstruction(MCAsmLayout &Layout, MCRelaxableFragment &IF);

  bool relaxLEB(MCAsmLayout &Layout, MCLEBFragment &IF);

  bool relaxDwarfLineAddr(MCAsmLayout &Layout, MCDwarfLineAddrFragment &DF);
  bool relaxDwarfCallFrameFragment(MCAsmLayout &Layout,
                                   MCDwarfCallFrameFragment &DF);

  /// finishLayout - Finalize a layout, including fragment lowering.
  void finishLayout(MCAsmLayout &Layout);

  uint64_t handleFixup(const MCAsmLayout &Layout,
                       MCFragment &F, const MCFixup &Fixup);

public:
  /// Compute the effective fragment size assuming it is laid out at the given
  /// \p SectionAddress and \p FragmentOffset.
  uint64_t computeFragmentSize(const MCAsmLayout &Layout,
                               const MCFragment &F) const;

  /// Find the symbol which defines the atom containing the given symbol, or
  /// null if there is no such symbol.
  const MCSymbolData *getAtom(const MCSymbolData *Symbol) const;

  /// Check whether a particular symbol is visible to the linker and is required
  /// in the symbol table, or whether it can be discarded by the assembler. This
  /// also effects whether the assembler treats the label as potentially
  /// defining a separate atom.
  bool isSymbolLinkerVisible(const MCSymbol &SD) const;

  /// Emit the section contents using the given object writer.
  void writeSectionData(const MCSectionData *Section,
                        const MCAsmLayout &Layout) const;

  /// Check whether a given symbol has been flagged with .thumb_func.
  bool isThumbFunc(const MCSymbol *Func) const {
    return ThumbFuncs.count(Func);
  }

  /// Flag a function symbol as the target of a .thumb_func directive.
  void setIsThumbFunc(const MCSymbol *Func) { ThumbFuncs.insert(Func); }

  /// ELF e_header flags
  unsigned getELFHeaderEFlags() const {return ELFHeaderEFlags;}
  void setELFHeaderEFlags(unsigned Flags) { ELFHeaderEFlags = Flags;}

public:
  /// Construct a new assembler instance.
  ///
  /// \param OS The stream to output to.
  //
  // FIXME: How are we going to parameterize this? Two obvious options are stay
  // concrete and require clients to pass in a target like object. The other
  // option is to make this abstract, and have targets provide concrete
  // implementations as we do with AsmParser.
  MCAssembler(MCContext &Context_, MCAsmBackend &Backend_,
              MCCodeEmitter &Emitter_, MCObjectWriter &Writer_,
              raw_ostream &OS);
  ~MCAssembler();

  /// Reuse an assembler instance
  ///
  void reset();

  MCContext &getContext() const { return Context; }

  MCAsmBackend &getBackend() const { return Backend; }

  MCCodeEmitter &getEmitter() const { return Emitter; }

  MCObjectWriter &getWriter() const { return Writer; }

  /// Finish - Do final processing and write the object to the output stream.
  /// \p Writer is used for custom object writer (as the MCJIT does),
  /// if not specified it is automatically created from backend.
  void Finish();

  // FIXME: This does not belong here.
  bool getSubsectionsViaSymbols() const {
    return SubsectionsViaSymbols;
  }
  void setSubsectionsViaSymbols(bool Value) {
    SubsectionsViaSymbols = Value;
  }

  bool getRelaxAll() const { return RelaxAll; }
  void setRelaxAll(bool Value) { RelaxAll = Value; }

  bool getNoExecStack() const { return NoExecStack; }
  void setNoExecStack(bool Value) { NoExecStack = Value; }

  bool isBundlingEnabled() const {
    return BundleAlignSize != 0;
  }

  unsigned getBundleAlignSize() const {
    return BundleAlignSize;
  }

  void setBundleAlignSize(unsigned Size) {
    assert((Size == 0 || !(Size & (Size - 1))) && 
           "Expect a power-of-two bundle align size");
    BundleAlignSize = Size;
  }

  /// @name Section List Access
  /// @{

  const SectionDataListType &getSectionList() const { return Sections; }
  SectionDataListType &getSectionList() { return Sections; }

  iterator begin() { return Sections.begin(); }
  const_iterator begin() const { return Sections.begin(); }

  iterator end() { return Sections.end(); }
  const_iterator end() const { return Sections.end(); }

  size_t size() const { return Sections.size(); }

  /// @}
  /// @name Symbol List Access
  /// @{

  const SymbolDataListType &getSymbolList() const { return Symbols; }
  SymbolDataListType &getSymbolList() { return Symbols; }

  symbol_iterator symbol_begin() { return Symbols.begin(); }
  const_symbol_iterator symbol_begin() const { return Symbols.begin(); }

  symbol_iterator symbol_end() { return Symbols.end(); }
  const_symbol_iterator symbol_end() const { return Symbols.end(); }

  size_t symbol_size() const { return Symbols.size(); }

  /// @}
  /// @name Indirect Symbol List Access
  /// @{

  // FIXME: This is a total hack, this should not be here. Once things are
  // factored so that the streamer has direct access to the .o writer, it can
  // disappear.
  std::vector<IndirectSymbolData> &getIndirectSymbols() {
    return IndirectSymbols;
  }

  indirect_symbol_iterator indirect_symbol_begin() {
    return IndirectSymbols.begin();
  }
  const_indirect_symbol_iterator indirect_symbol_begin() const {
    return IndirectSymbols.begin();
  }

  indirect_symbol_iterator indirect_symbol_end() {
    return IndirectSymbols.end();
  }
  const_indirect_symbol_iterator indirect_symbol_end() const {
    return IndirectSymbols.end();
  }

  size_t indirect_symbol_size() const { return IndirectSymbols.size(); }

  /// @}
  /// @name Linker Option List Access
  /// @{

  std::vector<std::vector<std::string> > &getLinkerOptions() {
    return LinkerOptions;
  }

  /// @}
  /// @name Data Region List Access
  /// @{

  // FIXME: This is a total hack, this should not be here. Once things are
  // factored so that the streamer has direct access to the .o writer, it can
  // disappear.
  std::vector<DataRegionData> &getDataRegions() {
    return DataRegions;
  }

  data_region_iterator data_region_begin() {
    return DataRegions.begin();
  }
  const_data_region_iterator data_region_begin() const {
    return DataRegions.begin();
  }

  data_region_iterator data_region_end() {
    return DataRegions.end();
  }
  const_data_region_iterator data_region_end() const {
    return DataRegions.end();
  }

  size_t data_region_size() const { return DataRegions.size(); }

  /// @}
  /// @name Backend Data Access
  /// @{

  MCSectionData &getSectionData(const MCSection &Section) const {
    MCSectionData *Entry = SectionMap.lookup(&Section);
    assert(Entry && "Missing section data!");
    return *Entry;
  }

  MCSectionData &getOrCreateSectionData(const MCSection &Section,
                                        bool *Created = 0) {
    MCSectionData *&Entry = SectionMap[&Section];

    if (Created) *Created = !Entry;
    if (!Entry)
      Entry = new MCSectionData(Section, this);

    return *Entry;
  }

  MCSymbolData &getSymbolData(const MCSymbol &Symbol) const {
    MCSymbolData *Entry = SymbolMap.lookup(&Symbol);
    assert(Entry && "Missing symbol data!");
    return *Entry;
  }

  MCSymbolData &getOrCreateSymbolData(const MCSymbol &Symbol,
                                      bool *Created = 0) {
    MCSymbolData *&Entry = SymbolMap[&Symbol];

    if (Created) *Created = !Entry;
    if (!Entry)
      Entry = new MCSymbolData(Symbol, 0, 0, this);

    return *Entry;
  }

  /// @}

  void dump();
};

} // end namespace llvm

#endif
