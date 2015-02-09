//===-- llvm/Target/MipsTargetObjectFile.h - Mips Object Info ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MIPS_MIPSTARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {

  class MipsTargetObjectFile : public TargetLoweringObjectFileELF {
    const MCSection *SmallDataSection;
    const MCSection *SmallBSSSection;
    const TargetMachine *TM;
  public:

    void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

    /// Return true if this global address should be placed into small data/bss
    /// section.
    bool IsGlobalInSmallSection(const GlobalValue *GV, const TargetMachine &TM,
                                SectionKind Kind) const;
    bool IsGlobalInSmallSection(const GlobalValue *GV,
                                const TargetMachine &TM) const;
    bool IsGlobalInSmallSectionImpl(const GlobalValue *GV,
                                    const TargetMachine &TM) const;

    const MCSection *SelectSectionForGlobal(const GlobalValue *GV,
                                        SectionKind Kind, Mangler &Mang,
                                        const TargetMachine &TM) const override;

    /// Return true if this constant should be placed into small data section.
    bool IsConstantInSmallSection(const Constant *CN,
                                  const TargetMachine &TM) const;

    const MCSection *getSectionForConstant(SectionKind Kind,
                                           const Constant *C) const override;
  };
} // end namespace llvm

#endif
