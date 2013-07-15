//===-- MipsTargetMachine.cpp - Define TargetMachine for Mips -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Mips target spec.
//
//===----------------------------------------------------------------------===//

#include "MipsTargetMachine.h"
#include "Mips.h"
#include "MipsFrameLowering.h"
#include "MipsInstrInfo.h"
#include "MipsModuleISelDAGToDAG.h"
#include "MipsOs16.h"
#include "MipsSEFrameLowering.h"
#include "MipsSEInstrInfo.h"
#include "MipsSEISelLowering.h"
#include "MipsSEISelDAGToDAG.h"
#include "Mips16FrameLowering.h"
#include "Mips16HardFloat.h"
#include "Mips16InstrInfo.h"
#include "Mips16ISelDAGToDAG.h"
#include "Mips16ISelLowering.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;



extern "C" void LLVMInitializeMipsTarget() {
  // Register the target.
  RegisterTargetMachine<MipsebTargetMachine> X(TheMipsTarget);
  RegisterTargetMachine<MipselTargetMachine> Y(TheMipselTarget);
  RegisterTargetMachine<MipsebTargetMachine> A(TheMips64Target);
  RegisterTargetMachine<MipselTargetMachine> B(TheMips64elTarget);
}

// DataLayout --> Big-endian, 32-bit pointer/ABI/alignment
// The stack is always 8 byte aligned
// On function prologue, the stack is created by decrementing
// its pointer. Once decremented, all references are done with positive
// offset from the stack/frame pointer, using StackGrowsUp enables
// an easier handling.
// Using CodeModel::Large enables different CALL behavior.
MipsTargetMachine::
MipsTargetMachine(const Target &T, StringRef TT,
                  StringRef CPU, StringRef FS, const TargetOptions &Options,
                  Reloc::Model RM, CodeModel::Model CM,
                  CodeGenOpt::Level OL,
                  bool isLittle)
  : LLVMTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL),
    Subtarget(TT, CPU, FS, isLittle, RM, this),
    DL(isLittle ?
               (Subtarget.isABI_N64() ?
                "e-p:64:64:64-i8:8:32-i16:16:32-i64:64:64-f128:128:128-"
                "n32:64-S128" :
                "e-p:32:32:32-i8:8:32-i16:16:32-i64:64:64-n32-S64") :
               (Subtarget.isABI_N64() ?
                "E-p:64:64:64-i8:8:32-i16:16:32-i64:64:64-f128:128:128-"
                "n32:64-S128" :
                "E-p:32:32:32-i8:8:32-i16:16:32-i64:64:64-n32-S64")),
    InstrInfo(MipsInstrInfo::create(*this)),
    FrameLowering(MipsFrameLowering::create(*this, Subtarget)),
    TLInfo(MipsTargetLowering::create(*this)), TSInfo(*this),
    InstrItins(Subtarget.getInstrItineraryData()), JITInfo() {
  initAsmInfo();
}


void MipsTargetMachine::setHelperClassesMips16() {
  InstrInfoSE.swap(InstrInfo);
  FrameLoweringSE.swap(FrameLowering);
  TLInfoSE.swap(TLInfo);
  if (!InstrInfo16) {
    InstrInfo.reset(MipsInstrInfo::create(*this));
    FrameLowering.reset(MipsFrameLowering::create(*this, Subtarget));
    TLInfo.reset(MipsTargetLowering::create(*this));
  } else {
    InstrInfo16.swap(InstrInfo);
    FrameLowering16.swap(FrameLowering);
    TLInfo16.swap(TLInfo);
  }
  assert(TLInfo && "null target lowering 16");
  assert(InstrInfo && "null instr info 16");
  assert(FrameLowering && "null frame lowering 16");
}

void MipsTargetMachine::setHelperClassesMipsSE() {
  InstrInfo16.swap(InstrInfo);
  FrameLowering16.swap(FrameLowering);
  TLInfo16.swap(TLInfo);
  if (!InstrInfoSE) {
    InstrInfo.reset(MipsInstrInfo::create(*this));
    FrameLowering.reset(MipsFrameLowering::create(*this, Subtarget));
    TLInfo.reset(MipsTargetLowering::create(*this));
  } else {
    InstrInfoSE.swap(InstrInfo);
    FrameLoweringSE.swap(FrameLowering);
    TLInfoSE.swap(TLInfo);
  }
  assert(TLInfo && "null target lowering in SE");
  assert(InstrInfo && "null instr info SE");
  assert(FrameLowering && "null frame lowering SE");
}
void MipsebTargetMachine::anchor() { }

MipsebTargetMachine::
MipsebTargetMachine(const Target &T, StringRef TT,
                    StringRef CPU, StringRef FS, const TargetOptions &Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL)
  : MipsTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, false) {}

void MipselTargetMachine::anchor() { }

MipselTargetMachine::
MipselTargetMachine(const Target &T, StringRef TT,
                    StringRef CPU, StringRef FS, const TargetOptions &Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL)
  : MipsTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, true) {}

namespace {
/// Mips Code Generator Pass Configuration Options.
class MipsPassConfig : public TargetPassConfig {
public:
  MipsPassConfig(MipsTargetMachine *TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  MipsTargetMachine &getMipsTargetMachine() const {
    return getTM<MipsTargetMachine>();
  }

  const MipsSubtarget &getMipsSubtarget() const {
    return *getMipsTargetMachine().getSubtargetImpl();
  }

  virtual void addIRPasses();
  virtual bool addInstSelector();
  virtual bool addPreEmitPass();
};
} // namespace

TargetPassConfig *MipsTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MipsPassConfig(this, PM);
}

void MipsPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  if (getMipsSubtarget().os16())
    addPass(createMipsOs16(getMipsTargetMachine()));
  if (getMipsSubtarget().inMips16HardFloat())
    addPass(createMips16HardFloat(getMipsTargetMachine()));
  addPass(createMipsOptimizeMathLibCalls(getMipsTargetMachine()));
}
// Install an instruction selector pass using
// the ISelDag to gen Mips code.
bool MipsPassConfig::addInstSelector() {
  if (getMipsSubtarget().allowMixed16_32()) {
    addPass(createMipsModuleISelDag(getMipsTargetMachine()));
    addPass(createMips16ISelDag(getMipsTargetMachine()));
    addPass(createMipsSEISelDag(getMipsTargetMachine()));
  } else {
    addPass(createMipsISelDag(getMipsTargetMachine()));
  }
  return false;
}

void MipsTargetMachine::addAnalysisPasses(PassManagerBase &PM) {
  if (Subtarget.allowMixed16_32()) {
    DEBUG(errs() << "No ");
    //FIXME: The Basic Target Transform Info
    // pass needs to become a function pass instead of
    // being an immutable pass and then this method as it exists now
    // would be unnecessary.
    PM.add(createNoTargetTransformInfoPass());
  } else
    LLVMTargetMachine::addAnalysisPasses(PM);
  DEBUG(errs() << "Target Transform Info Pass Added\n");
}

// Implemented by targets that want to run passes immediately before
// machine code is emitted. return true if -print-machineinstrs should
// print out the code after the passes.
bool MipsPassConfig::addPreEmitPass() {
  MipsTargetMachine &TM = getMipsTargetMachine();
  const MipsSubtarget &Subtarget = TM.getSubtarget<MipsSubtarget>();
  addPass(createMipsDelaySlotFillerPass(TM));

  if (Subtarget.hasStandardEncoding() ||
      Subtarget.allowMixed16_32())
    addPass(createMipsLongBranchPass(TM));
  if (Subtarget.inMips16Mode() ||
      Subtarget.allowMixed16_32())
    addPass(createMipsConstantIslandPass(TM));

  return true;
}

bool MipsTargetMachine::addCodeEmitter(PassManagerBase &PM,
                                       JITCodeEmitter &JCE) {
  // Machine code emitter pass for Mips.
  PM.add(createMipsJITCodeEmitterPass(*this, JCE));
  return false;
}
