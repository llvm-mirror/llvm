//===-- SparcTargetMachine.h - Define TargetMachine for Sparc ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Sparc specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_SPARCTARGETMACHINE_H
#define LLVM_LIB_TARGET_SPARC_SPARCTARGETMACHINE_H

#include "SparcInstrInfo.h"
#include "SparcSubtarget.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class SparcTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  const DataLayout DL;
  SparcSubtarget Subtarget;
public:
  SparcTargetMachine(const Target &T, StringRef TT,
                     StringRef CPU, StringRef FS, const TargetOptions &Options,
                     Reloc::Model RM, CodeModel::Model CM,
                     CodeGenOpt::Level OL, bool is64bit);
  ~SparcTargetMachine() override;

  const DataLayout *getDataLayout() const override { return &DL; }
  const SparcSubtarget *getSubtargetImpl() const override { return &Subtarget; }

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

/// SparcV8TargetMachine - Sparc 32-bit target machine
///
class SparcV8TargetMachine : public SparcTargetMachine {
  virtual void anchor();
public:
  SparcV8TargetMachine(const Target &T, StringRef TT,
                       StringRef CPU, StringRef FS,
                       const TargetOptions &Options,
                       Reloc::Model RM, CodeModel::Model CM,
                       CodeGenOpt::Level OL);
};

/// SparcV9TargetMachine - Sparc 64-bit target machine
///
class SparcV9TargetMachine : public SparcTargetMachine {
  virtual void anchor();
public:
  SparcV9TargetMachine(const Target &T, StringRef TT,
                       StringRef CPU, StringRef FS,
                       const TargetOptions &Options,
                       Reloc::Model RM, CodeModel::Model CM,
                       CodeGenOpt::Level OL);
};

} // end namespace llvm

#endif
