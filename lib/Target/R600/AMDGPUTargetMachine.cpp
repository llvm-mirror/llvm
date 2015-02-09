//===-- AMDGPUTargetMachine.cpp - TargetMachine for hw codegen targets-----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief The AMDGPU target machine contains all of the hardware specific
/// information  needed to emit code for R600 and SI GPUs.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetMachine.h"
#include "AMDGPU.h"
#include "AMDGPUTargetTransformInfo.h"
#include "R600ISelLowering.h"
#include "R600InstrInfo.h"
#include "R600MachineScheduler.h"
#include "SIISelLowering.h"
#include "SIInstrInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include <llvm/CodeGen/Passes.h>

using namespace llvm;

extern "C" void LLVMInitializeR600Target() {
  // Register the target
  RegisterTargetMachine<AMDGPUTargetMachine> X(TheAMDGPUTarget);
  RegisterTargetMachine<GCNTargetMachine> Y(TheGCNTarget);
}

static ScheduleDAGInstrs *createR600MachineScheduler(MachineSchedContext *C) {
  return new ScheduleDAGMILive(C, make_unique<R600SchedStrategy>());
}

static MachineSchedRegistry
SchedCustomRegistry("r600", "Run R600's custom scheduler",
                    createR600MachineScheduler);

static std::string computeDataLayout(StringRef TT) {
  Triple Triple(TT);
  std::string Ret = "e-p:32:32";

  if (Triple.getArch() == Triple::amdgcn) {
    // 32-bit private, local, and region pointers. 64-bit global and constant.
    Ret += "-p1:64:64-p2:64:64-p3:32:32-p4:64:64-p5:32:32-p24:64:64";
  }

  Ret += "-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256"
         "-v512:512-v1024:1024-v2048:2048-n32:64";

  return Ret;
}

AMDGPUTargetMachine::AMDGPUTargetMachine(const Target &T, StringRef TT,
                                         StringRef CPU, StringRef FS,
                                         TargetOptions Options, Reloc::Model RM,
                                         CodeModel::Model CM,
                                         CodeGenOpt::Level OptLevel)
    : LLVMTargetMachine(T, TT, CPU, FS, Options, RM, CM, OptLevel),
      DL(computeDataLayout(TT)),
      TLOF(new TargetLoweringObjectFileELF()),
      Subtarget(TT, CPU, FS, *this), IntrinsicInfo() {
  setRequiresStructuredCFG(true);
  initAsmInfo();
}

AMDGPUTargetMachine::~AMDGPUTargetMachine() {
  delete TLOF;
}

namespace {
class AMDGPUPassConfig : public TargetPassConfig {
public:
  AMDGPUPassConfig(AMDGPUTargetMachine *TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  AMDGPUTargetMachine &getAMDGPUTargetMachine() const {
    return getTM<AMDGPUTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();
    if (ST.getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS)
      return createR600MachineScheduler(C);
    return nullptr;
  }

  void addIRPasses() override;
  void addCodeGenPrepare() override;
  bool addPreISel() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // End of anonymous namespace

TargetPassConfig *AMDGPUTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new AMDGPUPassConfig(this, PM);
}

//===----------------------------------------------------------------------===//
// AMDGPU Pass Setup
//===----------------------------------------------------------------------===//

TargetIRAnalysis AMDGPUTargetMachine::getTargetIRAnalysis() {
  return TargetIRAnalysis(
      [this](Function &F) { return TargetTransformInfo(AMDGPUTTIImpl(this)); });
}

void AMDGPUPassConfig::addIRPasses() {
  // Function calls are not supported, so make sure we inline everything.
  addPass(createAMDGPUAlwaysInlinePass());
  addPass(createAlwaysInlinerPass());
  // We need to add the barrier noop pass, otherwise adding the function
  // inlining pass will cause all of the PassConfigs passes to be run
  // one function at a time, which means if we have a nodule with two
  // functions, then we will generate code for the first function
  // without ever running any passes on the second.
  addPass(createBarrierNoopPass());
  TargetPassConfig::addIRPasses();
}

void AMDGPUPassConfig::addCodeGenPrepare() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();
  if (ST.isPromoteAllocaEnabled()) {
    addPass(createAMDGPUPromoteAlloca(ST));
    addPass(createSROAPass());
  }

  TargetPassConfig::addCodeGenPrepare();
}

bool
AMDGPUPassConfig::addPreISel() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();
  addPass(createFlattenCFGPass());
  if (ST.IsIRStructurizerEnabled())
    addPass(createStructurizeCFGPass());
  if (ST.getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS) {
    addPass(createSinkingPass());
    addPass(createSITypeRewriter());
    addPass(createSIAnnotateControlFlowPass());
  } else {
    addPass(createR600TextureIntrinsicsReplacer());
  }
  return false;
}

bool AMDGPUPassConfig::addInstSelector() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();

  addPass(createAMDGPUISelDag(getAMDGPUTargetMachine()));

  if (ST.getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS) {
    addPass(createSILowerI1CopiesPass());
    addPass(createSIFixSGPRCopiesPass(*TM));
    addPass(createSIFoldOperandsPass());
  }

  return false;
}

void AMDGPUPassConfig::addPreRegAlloc() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();

  if (ST.getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS) {
    addPass(createR600VectorRegMerger(*TM));
  } else {
     if (getOptLevel() > CodeGenOpt::None && ST.loadStoreOptEnabled()) {
      // Don't do this with no optimizations since it throws away debug info by
      // merging nonadjacent loads.

      // This should be run after scheduling, but before register allocation. It
      // also need extra copies to the address operand to be eliminated.
      initializeSILoadStoreOptimizerPass(*PassRegistry::getPassRegistry());
      insertPass(&MachineSchedulerID, &SILoadStoreOptimizerID);
    }

    addPass(createSIShrinkInstructionsPass(), false);
    addPass(createSIFixSGPRLiveRangesPass(), false);
  }
}

void AMDGPUPassConfig::addPostRegAlloc() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();

  if (ST.getGeneration() > AMDGPUSubtarget::NORTHERN_ISLANDS) {
    addPass(createSIPrepareScratchRegs(), false);
    addPass(createSIShrinkInstructionsPass(), false);
  }
}

void AMDGPUPassConfig::addPreSched2() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();

  if (ST.getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS)
    addPass(createR600EmitClauseMarkers(), false);
  if (ST.isIfCvtEnabled())
    addPass(&IfConverterID, false);
  if (ST.getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS)
    addPass(createR600ClauseMergePass(*TM), false);
  if (ST.getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS) {
    addPass(createSIInsertWaits(*TM), false);
  }
}

void AMDGPUPassConfig::addPreEmitPass() {
  const AMDGPUSubtarget &ST = TM->getSubtarget<AMDGPUSubtarget>();
  if (ST.getGeneration() <= AMDGPUSubtarget::NORTHERN_ISLANDS) {
    addPass(createAMDGPUCFGStructurizerPass(), false);
    addPass(createR600ExpandSpecialInstrsPass(*TM), false);
    addPass(&FinalizeMachineBundlesID, false);
    addPass(createR600Packetizer(*TM), false);
    addPass(createR600ControlFlowFinalizer(*TM), false);
  } else {
    addPass(createSILowerControlFlowPass(*TM), false);
  }
}


//===----------------------------------------------------------------------===//
// GCN Target Machine (SI+)
//===----------------------------------------------------------------------===//

GCNTargetMachine::GCNTargetMachine(const Target &T, StringRef TT, StringRef FS,
                    StringRef CPU, TargetOptions Options, Reloc::Model RM,
                    CodeModel::Model CM, CodeGenOpt::Level OL) :
    AMDGPUTargetMachine(T, TT, FS, CPU, Options, RM, CM, OL) { }
