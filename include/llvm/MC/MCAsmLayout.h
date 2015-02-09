//===- MCAsmLayout.h - Assembly Layout Object -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMLAYOUT_H
#define LLVM_MC_MCASMLAYOUT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class MCAssembler;
class MCFragment;
class MCSectionData;
class MCSymbol;
class MCSymbolData;

/// Encapsulates the layout of an assembly file at a particular point in time.
///
/// Assembly may require computing multiple layouts for a particular assembly
/// file as part of the relaxation process. This class encapsulates the layout
/// at a single point in time in such a way that it is always possible to
/// efficiently compute the exact address of any symbol in the assembly file,
/// even during the relaxation process.
class MCAsmLayout {
public:
  typedef llvm::SmallVectorImpl<MCSectionData*>::const_iterator const_iterator;
  typedef llvm::SmallVectorImpl<MCSectionData*>::iterator iterator;

private:
  MCAssembler &Assembler;

  /// List of sections in layout order.
  llvm::SmallVector<MCSectionData*, 16> SectionOrder;

  /// The last fragment which was laid out, or 0 if nothing has been laid
  /// out. Fragments are always laid out in order, so all fragments with a
  /// lower ordinal will be valid.
  mutable DenseMap<const MCSectionData*, MCFragment*> LastValidFragment;

  /// \brief Make sure that the layout for the given fragment is valid, lazily
  /// computing it if necessary.
  void ensureValid(const MCFragment *F) const;

  /// \brief Is the layout for this fragment valid?
  bool isFragmentValid(const MCFragment *F) const;

  /// \brief Compute the amount of padding required before this fragment to
  /// obey bundling restrictions.
  uint64_t computeBundlePadding(const MCFragment *F,
                                uint64_t FOffset, uint64_t FSize);

public:
  MCAsmLayout(MCAssembler &_Assembler);

  /// Get the assembler object this is a layout for.
  MCAssembler &getAssembler() const { return Assembler; }

  /// \brief Invalidate the fragments starting with F because it has been
  /// resized. The fragment's size should have already been updated, but
  /// its bundle padding will be recomputed.
  void invalidateFragmentsFrom(MCFragment *F);

  /// \brief Perform layout for a single fragment, assuming that the previous
  /// fragment has already been laid out correctly, and the parent section has
  /// been initialized.
  void layoutFragment(MCFragment *Fragment);

  /// @name Section Access (in layout order)
  /// @{

  llvm::SmallVectorImpl<MCSectionData*> &getSectionOrder() {
    return SectionOrder;
  }
  const llvm::SmallVectorImpl<MCSectionData*> &getSectionOrder() const {
    return SectionOrder;
  }

  /// @}
  /// @name Fragment Layout Data
  /// @{

  /// \brief Get the offset of the given fragment inside its containing section.
  uint64_t getFragmentOffset(const MCFragment *F) const;

  /// @}
  /// @name Utility Functions
  /// @{

  /// \brief Get the address space size of the given section, as it effects
  /// layout. This may differ from the size reported by \see getSectionSize() by
  /// not including section tail padding.
  uint64_t getSectionAddressSize(const MCSectionData *SD) const;

  /// \brief Get the data size of the given section, as emitted to the object
  /// file. This may include additional padding, or be 0 for virtual sections.
  uint64_t getSectionFileSize(const MCSectionData *SD) const;

  /// \brief Get the offset of the given symbol, as computed in the current
  /// layout.
  /// \result True on success.
  bool getSymbolOffset(const MCSymbolData *SD, uint64_t &Val) const;

  /// \brief Variant that reports a fatal error if the offset is not computable.
  uint64_t getSymbolOffset(const MCSymbolData *SD) const;

  /// \brief If this symbol is equivalent to A + Constant, return A.
  const MCSymbol *getBaseSymbol(const MCSymbol &Symbol) const;

  /// @}
};

} // end namespace llvm

#endif
