//===-- ARMTargetMachine.h - Define TargetMachine for ARM -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARM specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMTARGETMACHINE_H
#define LLVM_LIB_TARGET_ARM_ARMTARGETMACHINE_H

#include "ARMInstrInfo.h"
#include "ARMSubtarget.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class ARMBaseTargetMachine : public LLVMTargetMachine {
public:
  enum ARMABI {
    ARM_ABI_UNKNOWN,
    ARM_ABI_APCS,
    ARM_ABI_AAPCS, // ARM EABI
    ARM_ABI_AAPCS16
  } TargetABI;

protected:
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  ARMSubtarget        Subtarget;
  bool isLittle;
  mutable StringMap<std::unique_ptr<ARMSubtarget>> SubtargetMap;

public:
  ARMBaseTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Reloc::Model RM, CodeModel::Model CM,
                       CodeGenOpt::Level OL, bool isLittle);
  ~ARMBaseTargetMachine() override;

  const ARMSubtarget *getSubtargetImpl() const { return &Subtarget; }
  const ARMSubtarget *getSubtargetImpl(const Function &F) const override;
  bool isLittleEndian() const { return isLittle; }

  /// \brief Get the TargetIRAnalysis for this target.
  TargetIRAnalysis getTargetIRAnalysis() override;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

/// ARMTargetMachine - ARM target machine.
///
class ARMTargetMachine : public ARMBaseTargetMachine {
  virtual void anchor();
 public:
   ARMTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options, Reloc::Model RM,
                    CodeModel::Model CM, CodeGenOpt::Level OL, bool isLittle);
};

/// ARMLETargetMachine - ARM little endian target machine.
///
class ARMLETargetMachine : public ARMTargetMachine {
  void anchor() override;
public:
  ARMLETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     Reloc::Model RM, CodeModel::Model CM,
                     CodeGenOpt::Level OL);
};

/// ARMBETargetMachine - ARM big endian target machine.
///
class ARMBETargetMachine : public ARMTargetMachine {
  void anchor() override;
public:
  ARMBETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     Reloc::Model RM, CodeModel::Model CM,
                     CodeGenOpt::Level OL);
};

/// ThumbTargetMachine - Thumb target machine.
/// Due to the way architectures are handled, this represents both
///   Thumb-1 and Thumb-2.
///
class ThumbTargetMachine : public ARMBaseTargetMachine {
  virtual void anchor();
public:
  ThumbTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     Reloc::Model RM, CodeModel::Model CM, CodeGenOpt::Level OL,
                     bool isLittle);
};

/// ThumbLETargetMachine - Thumb little endian target machine.
///
class ThumbLETargetMachine : public ThumbTargetMachine {
  void anchor() override;
public:
  ThumbLETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Reloc::Model RM, CodeModel::Model CM,
                       CodeGenOpt::Level OL);
};

/// ThumbBETargetMachine - Thumb big endian target machine.
///
class ThumbBETargetMachine : public ThumbTargetMachine {
  void anchor() override;
public:
  ThumbBETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Reloc::Model RM, CodeModel::Model CM,
                       CodeGenOpt::Level OL);
};

} // end namespace llvm

#endif
