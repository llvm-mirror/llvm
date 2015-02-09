//===-- XCoreTargetObjectFile.h - XCore Object Info -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORETARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_XCORE_XCORETARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

static const unsigned CodeModelLargeSize = 256;

  class XCoreTargetObjectFile : public TargetLoweringObjectFileELF {
   const MCSection *BSSSectionLarge;
   const MCSection *DataSectionLarge;
   const MCSection *ReadOnlySectionLarge;
   const MCSection *DataRelROSectionLarge;
  public:
    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    const MCSection *
      getExplicitSectionGlobal(const GlobalValue *GV,
                               SectionKind Kind, Mangler &Mang,
                               const TargetMachine &TM) const override;

    const MCSection *
      SelectSectionForGlobal(const GlobalValue *GV, SectionKind Kind,
                             Mangler &Mang,
                             const TargetMachine &TM) const override;

    const MCSection *getSectionForConstant(SectionKind Kind,
                                           const Constant *C) const override;
  };
} // end namespace llvm

#endif
