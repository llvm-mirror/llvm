//===--------- llvm/DataLayout.h - Data size & alignment info ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines layout properties related to datatype size/offset/alignment
// information.  It uses lazy annotations to cache information about how
// structure types are laid out and used.
//
// This structure should be created once, filled in if the defaults are not
// correct and then passed around by const&.  None of the members functions
// require modification to the object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DATALAYOUT_H
#define LLVM_IR_DATALAYOUT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

class Value;
class Type;
class IntegerType;
class StructType;
class StructLayout;
class GlobalVariable;
class LLVMContext;
template<typename T>
class ArrayRef;

/// Enum used to categorize the alignment types stored by LayoutAlignElem
enum AlignTypeEnum {
  INVALID_ALIGN = 0,                 ///< An invalid alignment
  INTEGER_ALIGN = 'i',               ///< Integer type alignment
  VECTOR_ALIGN = 'v',                ///< Vector type alignment
  FLOAT_ALIGN = 'f',                 ///< Floating point type alignment
  AGGREGATE_ALIGN = 'a',             ///< Aggregate alignment
  STACK_ALIGN = 's'                  ///< Stack objects alignment
};

/// Layout alignment element.
///
/// Stores the alignment data associated with a given alignment type (integer,
/// vector, float) and type bit width.
///
/// @note The unusual order of elements in the structure attempts to reduce
/// padding and make the structure slightly more cache friendly.
struct LayoutAlignElem {
  unsigned AlignType    : 8;  ///< Alignment type (AlignTypeEnum)
  unsigned TypeBitWidth : 24; ///< Type bit width
  unsigned ABIAlign     : 16; ///< ABI alignment for this type/bitw
  unsigned PrefAlign    : 16; ///< Pref. alignment for this type/bitw

  /// Initializer
  static LayoutAlignElem get(AlignTypeEnum align_type, unsigned abi_align,
                             unsigned pref_align, uint32_t bit_width);
  /// Equality predicate
  bool operator==(const LayoutAlignElem &rhs) const;
};

/// Layout pointer alignment element.
///
/// Stores the alignment data associated with a given pointer and address space.
///
/// @note The unusual order of elements in the structure attempts to reduce
/// padding and make the structure slightly more cache friendly.
struct PointerAlignElem {
  unsigned            ABIAlign;       ///< ABI alignment for this type/bitw
  unsigned            PrefAlign;      ///< Pref. alignment for this type/bitw
  uint32_t            TypeBitWidth;   ///< Type bit width
  uint32_t            AddressSpace;   ///< Address space for the pointer type

  /// Initializer
  static PointerAlignElem get(uint32_t addr_space, unsigned abi_align,
                             unsigned pref_align, uint32_t bit_width);
  /// Equality predicate
  bool operator==(const PointerAlignElem &rhs) const;
};


/// DataLayout - This class holds a parsed version of the target data layout
/// string in a module and provides methods for querying it.  The target data
/// layout string is specified *by the target* - a frontend generating LLVM IR
/// is required to generate the right target data for the target being codegen'd
/// to.  If some measure of portability is desired, an empty string may be
/// specified in the module.
class DataLayout : public ImmutablePass {
private:
  bool          LittleEndian;          ///< Defaults to false
  unsigned      StackNaturalAlign;     ///< Stack natural alignment

  SmallVector<unsigned char, 8> LegalIntWidths; ///< Legal Integers.

  /// Alignments - Where the primitive type alignment data is stored.
  ///
  /// @sa init().
  /// @note Could support multiple size pointer alignments, e.g., 32-bit
  /// pointers vs. 64-bit pointers by extending LayoutAlignment, but for now,
  /// we don't.
  SmallVector<LayoutAlignElem, 16> Alignments;
  DenseMap<unsigned, PointerAlignElem> Pointers;

  /// InvalidAlignmentElem - This member is a signal that a requested alignment
  /// type and bit width were not found in the SmallVector.
  static const LayoutAlignElem InvalidAlignmentElem;

  /// InvalidPointerElem - This member is a signal that a requested pointer
  /// type and bit width were not found in the DenseSet.
  static const PointerAlignElem InvalidPointerElem;

  // The StructType -> StructLayout map.
  mutable void *LayoutMap;

  //! Set/initialize target alignments
  void setAlignment(AlignTypeEnum align_type, unsigned abi_align,
                    unsigned pref_align, uint32_t bit_width);
  unsigned getAlignmentInfo(AlignTypeEnum align_type, uint32_t bit_width,
                            bool ABIAlign, Type *Ty) const;

  //! Set/initialize pointer alignments
  void setPointerAlignment(uint32_t addr_space, unsigned abi_align,
      unsigned pref_align, uint32_t bit_width);

  //! Internal helper method that returns requested alignment for type.
  unsigned getAlignment(Type *Ty, bool abi_or_pref) const;

  /// Valid alignment predicate.
  ///
  /// Predicate that tests a LayoutAlignElem reference returned by get() against
  /// InvalidAlignmentElem.
  bool validAlignment(const LayoutAlignElem &align) const {
    return &align != &InvalidAlignmentElem;
  }

  /// Valid pointer predicate.
  ///
  /// Predicate that tests a PointerAlignElem reference returned by get() against
  /// InvalidPointerElem.
  bool validPointer(const PointerAlignElem &align) const {
    return &align != &InvalidPointerElem;
  }

  /// Parses a target data specification string. Assert if the string is
  /// malformed.
  void parseSpecifier(StringRef LayoutDescription);

public:
  /// Default ctor.
  ///
  /// @note This has to exist, because this is a pass, but it should never be
  /// used.
  DataLayout();

  /// Constructs a DataLayout from a specification string. See init().
  explicit DataLayout(StringRef LayoutDescription)
    : ImmutablePass(ID) {
    init(LayoutDescription);
  }

  /// Initialize target data from properties stored in the module.
  explicit DataLayout(const Module *M);

  DataLayout(const DataLayout &DL) :
    ImmutablePass(ID),
    LittleEndian(DL.isLittleEndian()),
    StackNaturalAlign(DL.StackNaturalAlign),
    LegalIntWidths(DL.LegalIntWidths),
    Alignments(DL.Alignments),
    Pointers(DL.Pointers),
    LayoutMap(0)
  { }

  ~DataLayout();  // Not virtual, do not subclass this class

  /// DataLayout is an immutable pass, but holds state.  This allows the pass
  /// manager to clear its mutable state.
  bool doFinalization(Module &M);

  /// Parse a data layout string (with fallback to default values). Ensure that
  /// the data layout pass is registered.
  void init(StringRef LayoutDescription);

  /// Layout endianness...
  bool isLittleEndian() const { return LittleEndian; }
  bool isBigEndian() const { return !LittleEndian; }

  /// getStringRepresentation - Return the string representation of the
  /// DataLayout.  This representation is in the same format accepted by the
  /// string constructor above.
  std::string getStringRepresentation() const;

  /// isLegalInteger - This function returns true if the specified type is
  /// known to be a native integer type supported by the CPU.  For example,
  /// i64 is not native on most 32-bit CPUs and i37 is not native on any known
  /// one.  This returns false if the integer width is not legal.
  ///
  /// The width is specified in bits.
  ///
  bool isLegalInteger(unsigned Width) const {
    for (unsigned i = 0, e = (unsigned)LegalIntWidths.size(); i != e; ++i)
      if (LegalIntWidths[i] == Width)
        return true;
    return false;
  }

  bool isIllegalInteger(unsigned Width) const {
    return !isLegalInteger(Width);
  }

  /// Returns true if the given alignment exceeds the natural stack alignment.
  bool exceedsNaturalStackAlignment(unsigned Align) const {
    return (StackNaturalAlign != 0) && (Align > StackNaturalAlign);
  }

  /// fitsInLegalInteger - This function returns true if the specified type fits
  /// in a native integer type supported by the CPU.  For example, if the CPU
  /// only supports i32 as a native integer type, then i27 fits in a legal
  // integer type but i45 does not.
  bool fitsInLegalInteger(unsigned Width) const {
    for (unsigned i = 0, e = (unsigned)LegalIntWidths.size(); i != e; ++i)
      if (Width <= LegalIntWidths[i])
        return true;
    return false;
  }

  /// Layout pointer alignment
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerABIAlignment(unsigned AS = 0) const {
    DenseMap<unsigned, PointerAlignElem>::const_iterator val = Pointers.find(AS);
    if (val == Pointers.end()) {
      val = Pointers.find(0);
    }
    return val->second.ABIAlign;
  }

  /// Return target's alignment for stack-based pointers
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerPrefAlignment(unsigned AS = 0) const {
    DenseMap<unsigned, PointerAlignElem>::const_iterator val = Pointers.find(AS);
    if (val == Pointers.end()) {
      val = Pointers.find(0);
    }
    return val->second.PrefAlign;
  }
  /// Layout pointer size
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerSize(unsigned AS = 0) const {
    DenseMap<unsigned, PointerAlignElem>::const_iterator val = Pointers.find(AS);
    if (val == Pointers.end()) {
      val = Pointers.find(0);
    }
    return val->second.TypeBitWidth;
  }
  /// Layout pointer size, in bits
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerSizeInBits(unsigned AS = 0) const {
    return getPointerSize(AS) * 8;
  }

  /// Layout pointer size, in bits, based on the type.  If this function is
  /// called with a pointer type, then the type size of the pointer is returned.
  /// If this function is called with a vector of pointers, then the type size
  /// of the pointer is returned.  This should only be called with a pointer or
  /// vector of pointers.
  unsigned getPointerTypeSizeInBits(Type *) const;

  unsigned getPointerTypeSize(Type *Ty) const {
    return getPointerTypeSizeInBits(Ty) / 8;
  }

  /// Size examples:
  ///
  /// Type        SizeInBits  StoreSizeInBits  AllocSizeInBits[*]
  /// ----        ----------  ---------------  ---------------
  ///  i1            1           8                8
  ///  i8            8           8                8
  ///  i19          19          24               32
  ///  i32          32          32               32
  ///  i100        100         104              128
  ///  i128        128         128              128
  ///  Float        32          32               32
  ///  Double       64          64               64
  ///  X86_FP80     80          80               96
  ///
  /// [*] The alloc size depends on the alignment, and thus on the target.
  ///     These values are for x86-32 linux.

  /// getTypeSizeInBits - Return the number of bits necessary to hold the
  /// specified type.  For example, returns 36 for i36 and 80 for x86_fp80.
  /// The type passed must have a size (Type::isSized() must return true).
  uint64_t getTypeSizeInBits(Type *Ty) const;

  /// getTypeStoreSize - Return the maximum number of bytes that may be
  /// overwritten by storing the specified type.  For example, returns 5
  /// for i36 and 10 for x86_fp80.
  uint64_t getTypeStoreSize(Type *Ty) const {
    return (getTypeSizeInBits(Ty)+7)/8;
  }

  /// getTypeStoreSizeInBits - Return the maximum number of bits that may be
  /// overwritten by storing the specified type; always a multiple of 8.  For
  /// example, returns 40 for i36 and 80 for x86_fp80.
  uint64_t getTypeStoreSizeInBits(Type *Ty) const {
    return 8*getTypeStoreSize(Ty);
  }

  /// getTypeAllocSize - Return the offset in bytes between successive objects
  /// of the specified type, including alignment padding.  This is the amount
  /// that alloca reserves for this type.  For example, returns 12 or 16 for
  /// x86_fp80, depending on alignment.
  uint64_t getTypeAllocSize(Type *Ty) const {
    // Round up to the next alignment boundary.
    return RoundUpAlignment(getTypeStoreSize(Ty), getABITypeAlignment(Ty));
  }

  /// getTypeAllocSizeInBits - Return the offset in bits between successive
  /// objects of the specified type, including alignment padding; always a
  /// multiple of 8.  This is the amount that alloca reserves for this type.
  /// For example, returns 96 or 128 for x86_fp80, depending on alignment.
  uint64_t getTypeAllocSizeInBits(Type *Ty) const {
    return 8*getTypeAllocSize(Ty);
  }

  /// getABITypeAlignment - Return the minimum ABI-required alignment for the
  /// specified type.
  unsigned getABITypeAlignment(Type *Ty) const;

  /// getABIIntegerTypeAlignment - Return the minimum ABI-required alignment for
  /// an integer type of the specified bitwidth.
  unsigned getABIIntegerTypeAlignment(unsigned BitWidth) const;

  /// getCallFrameTypeAlignment - Return the minimum ABI-required alignment
  /// for the specified type when it is part of a call frame.
  unsigned getCallFrameTypeAlignment(Type *Ty) const;

  /// getPrefTypeAlignment - Return the preferred stack/global alignment for
  /// the specified type.  This is always at least as good as the ABI alignment.
  unsigned getPrefTypeAlignment(Type *Ty) const;

  /// getPreferredTypeAlignmentShift - Return the preferred alignment for the
  /// specified type, returned as log2 of the value (a shift amount).
  unsigned getPreferredTypeAlignmentShift(Type *Ty) const;

  /// getIntPtrType - Return an integer type with size at least as big as that
  /// of a pointer in the given address space.
  IntegerType *getIntPtrType(LLVMContext &C, unsigned AddressSpace = 0) const;

  /// getIntPtrType - Return an integer (vector of integer) type with size at
  /// least as big as that of a pointer of the given pointer (vector of pointer)
  /// type.
  Type *getIntPtrType(Type *) const;

  /// getSmallestLegalIntType - Return the smallest integer type with size at
  /// least as big as Width bits.
  Type *getSmallestLegalIntType(LLVMContext &C, unsigned Width = 0) const;

  /// getLargestLegalIntType - Return the largest legal integer type, or null if
  /// none are set.
  Type *getLargestLegalIntType(LLVMContext &C) const {
    unsigned LargestSize = getLargestLegalIntTypeSize();
    return (LargestSize == 0) ? 0 : Type::getIntNTy(C, LargestSize);
  }

  /// getLargestLegalIntType - Return the size of largest legal integer type
  /// size, or 0 if none are set.
  unsigned getLargestLegalIntTypeSize() const;

  /// getIndexedOffset - return the offset from the beginning of the type for
  /// the specified indices.  This is used to implement getelementptr.
  uint64_t getIndexedOffset(Type *Ty, ArrayRef<Value *> Indices) const;

  /// getStructLayout - Return a StructLayout object, indicating the alignment
  /// of the struct, its size, and the offsets of its fields.  Note that this
  /// information is lazily cached.
  const StructLayout *getStructLayout(StructType *Ty) const;

  /// getPreferredAlignment - Return the preferred alignment of the specified
  /// global.  This includes an explicitly requested alignment (if the global
  /// has one).
  unsigned getPreferredAlignment(const GlobalVariable *GV) const;

  /// getPreferredAlignmentLog - Return the preferred alignment of the
  /// specified global, returned in log form.  This includes an explicitly
  /// requested alignment (if the global has one).
  unsigned getPreferredAlignmentLog(const GlobalVariable *GV) const;

  /// RoundUpAlignment - Round the specified value up to the next alignment
  /// boundary specified by Alignment.  For example, 7 rounded up to an
  /// alignment boundary of 4 is 8.  8 rounded up to the alignment boundary of 4
  /// is 8 because it is already aligned.
  template <typename UIntTy>
  static UIntTy RoundUpAlignment(UIntTy Val, unsigned Alignment) {
    assert((Alignment & (Alignment-1)) == 0 && "Alignment must be power of 2!");
    return (Val + (Alignment-1)) & ~UIntTy(Alignment-1);
  }

  static char ID; // Pass identification, replacement for typeid
};

/// StructLayout - used to lazily calculate structure layout information for a
/// target machine, based on the DataLayout structure.
///
class StructLayout {
  uint64_t StructSize;
  unsigned StructAlignment;
  unsigned NumElements;
  uint64_t MemberOffsets[1];  // variable sized array!
public:

  uint64_t getSizeInBytes() const {
    return StructSize;
  }

  uint64_t getSizeInBits() const {
    return 8*StructSize;
  }

  unsigned getAlignment() const {
    return StructAlignment;
  }

  /// getElementContainingOffset - Given a valid byte offset into the structure,
  /// return the structure index that contains it.
  ///
  unsigned getElementContainingOffset(uint64_t Offset) const;

  uint64_t getElementOffset(unsigned Idx) const {
    assert(Idx < NumElements && "Invalid element idx!");
    return MemberOffsets[Idx];
  }

  uint64_t getElementOffsetInBits(unsigned Idx) const {
    return getElementOffset(Idx)*8;
  }

private:
  friend class DataLayout;   // Only DataLayout can create this class
  StructLayout(StructType *ST, const DataLayout &DL);
};


// The implementation of this method is provided inline as it is particularly
// well suited to constant folding when called on a specific Type subclass.
inline uint64_t DataLayout::getTypeSizeInBits(Type *Ty) const {
  assert(Ty->isSized() && "Cannot getTypeInfo() on a type that is unsized!");
  switch (Ty->getTypeID()) {
  case Type::LabelTyID:
    return getPointerSizeInBits(0);
  case Type::PointerTyID:
    return getPointerSizeInBits(Ty->getPointerAddressSpace());
  case Type::ArrayTyID: {
    ArrayType *ATy = cast<ArrayType>(Ty);
    return ATy->getNumElements() *
           getTypeAllocSizeInBits(ATy->getElementType());
  }
  case Type::StructTyID:
    // Get the layout annotation... which is lazily created on demand.
    return getStructLayout(cast<StructType>(Ty))->getSizeInBits();
  case Type::IntegerTyID:
    return Ty->getIntegerBitWidth();
  case Type::HalfTyID:
    return 16;
  case Type::FloatTyID:
    return 32;
  case Type::DoubleTyID:
  case Type::X86_MMXTyID:
    return 64;
  case Type::PPC_FP128TyID:
  case Type::FP128TyID:
    return 128;
    // In memory objects this is always aligned to a higher boundary, but
  // only 80 bits contain information.
  case Type::X86_FP80TyID:
    return 80;
  case Type::VectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return VTy->getNumElements() * getTypeSizeInBits(VTy->getElementType());
  }
  default:
    llvm_unreachable("DataLayout::getTypeSizeInBits(): Unsupported type");
  }
}

} // End llvm namespace

#endif
