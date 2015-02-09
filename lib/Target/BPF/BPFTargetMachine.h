//===-- BPFTargetMachine.h - Define TargetMachine for BPF --- C++ ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the BPF specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPFTARGETMACHINE_H
#define LLVM_LIB_TARGET_BPF_BPFTARGETMACHINE_H

#include "BPFSubtarget.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class BPFTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  const DataLayout DL;
  BPFSubtarget Subtarget;

public:
  BPFTargetMachine(const Target &T, StringRef TT, StringRef CPU, StringRef FS,
                   const TargetOptions &Options, Reloc::Model RM,
                   CodeModel::Model CM, CodeGenOpt::Level OL);

  const DataLayout *getDataLayout() const override { return &DL; }
  const BPFSubtarget *getSubtargetImpl() const override { return &Subtarget; }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
}

#endif
