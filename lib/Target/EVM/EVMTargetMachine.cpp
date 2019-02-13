//===-- EVMTargetMachine.cpp - Define TargetMachine for EVM -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about EVM target spec.
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMTargetMachine.h"
#include "EVMTargetObjectFile.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

extern "C" void LLVMInitializeEVMTarget() {
  RegisterTargetMachine<EVMTargetMachine> X(getTheEVM32Target());
  RegisterTargetMachine<EVMTargetMachine> Y(getTheEVM64Target());
  auto PR = PassRegistry::getPassRegistry();
  initializeEVMExpandPseudoPass(*PR);
}

static std::string computeDataLayout(const Triple &TT) {
  if (TT.isArch64Bit()) {
    return "e-m:e-p:64:64-i64:64-i128:128-n64-S128";
  } else {
    assert(TT.isArch32Bit() && "only RV32 and RV64 are currently supported");
    return "e-m:e-p:32:32-i64:64-n32-S128";
  }
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::Static;
  return *RM;
}

EVMTargetMachine::EVMTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       Optional<Reloc::Model> RM,
                                       Optional<CodeModel::Model> CM,
                                       CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        getEffectiveRelocModel(TT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(make_unique<EVMELFTargetObjectFile>()),
      Subtarget(TT, CPU, FS, *this) {
  initAsmInfo();
}

namespace {
class EVMPassConfig : public TargetPassConfig {
public:
  EVMPassConfig(EVMTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  EVMTargetMachine &getEVMTargetMachine() const {
    return getTM<EVMTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
  void addPreRegAlloc() override;
};
}

TargetPassConfig *EVMTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new EVMPassConfig(*this, PM);
}

void EVMPassConfig::addIRPasses() {
  addPass(createAtomicExpandPass());
  TargetPassConfig::addIRPasses();
}

bool EVMPassConfig::addInstSelector() {
  addPass(createEVMISelDag(getEVMTargetMachine()));

  return false;
}

void EVMPassConfig::addPreEmitPass() { addPass(&BranchRelaxationPassID); }

void EVMPassConfig::addPreEmitPass2() {
  // Schedule the expansion of AMOs at the last possible moment, avoiding the
  // possibility for other passes to break the requirements for forward
  // progress in the LR/SC block.
  addPass(createEVMExpandPseudoPass());
}

void EVMPassConfig::addPreRegAlloc() {
  addPass(createEVMMergeBaseOffsetOptPass());
}
