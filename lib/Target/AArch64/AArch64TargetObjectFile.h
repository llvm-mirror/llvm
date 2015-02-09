//===-- AArch64TargetObjectFile.h - AArch64 Object Info -*- C++ ---------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {
class AArch64TargetMachine;

/// This implementation is used for AArch64 ELF targets (Linux in particular).
class AArch64_ELFTargetObjectFile : public TargetLoweringObjectFileELF {
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;
};

/// AArch64_MachoTargetObjectFile - This TLOF implementation is used for Darwin.
class AArch64_MachoTargetObjectFile : public TargetLoweringObjectFileMachO {
public:
  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding, Mangler &Mang,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV, Mangler &Mang,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;
};

} // end namespace llvm

#endif
