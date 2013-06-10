//===-- rvexTargetMachine.h - Define TargetMachine for rvex -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the rvex specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef rvexTARGETMACHINE_H
#define rvexTARGETMACHINE_H

#include "rvexFrameLowering.h"
#include "rvexInstrInfo.h"
#include "rvexISelLowering.h"
#include "rvexSelectionDAGInfo.h"
#include "rvexSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
  class formatted_raw_ostream;

  class rvexTargetMachine : public LLVMTargetMachine {
    rvexSubtarget       Subtarget;
    const DataLayout    DL; // Calculates type size & alignment
    rvexInstrInfo       InstrInfo;	//- Instructions
    rvexTargetLowering  TLInfo;	//- Stack(Frame) and Stack direction
    rvexSelectionDAGInfo TSInfo;	//- Map .bc DAG to backend DAG
    rvexFrameLowering   FrameLowering;  //- Stack(Frame) and Stack direction
    const InstrItineraryData *InstrItins;

  public:
    rvexTargetMachine(const Target &T, StringRef TT,
                      StringRef CPU, StringRef FS, const TargetOptions &Options,
                      Reloc::Model RM, CodeModel::Model CM,
                      CodeGenOpt::Level OL,
                      bool isLittle);

    virtual const rvexInstrInfo   *getInstrInfo()     const
    { return &InstrInfo; }
    virtual const TargetFrameLowering *getFrameLowering()     const
    { return &FrameLowering; }
    virtual const rvexSubtarget   *getSubtargetImpl() const
    { return &Subtarget; }
    virtual const DataLayout *getDataLayout()    const
    { return &DL;}

    virtual const rvexRegisterInfo *getRegisterInfo()  const {
      return &InstrInfo.getRegisterInfo();
    }

    virtual const rvexTargetLowering *getTargetLowering() const {
      return &TLInfo;
    }

    virtual const rvexSelectionDAGInfo* getSelectionDAGInfo() const {
      return &TSInfo;
    }

    virtual const InstrItineraryData *getInstrItineraryData() const {
      return InstrItins;
    }

    // Pass Pipeline Configuration
    virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);
  };

/// rvexebTargetMachine - rvex32 big endian target machine.
///
class rvexebTargetMachine : public rvexTargetMachine {
  virtual void anchor();
public:
  rvexebTargetMachine(const Target &T, StringRef TT,
                      StringRef CPU, StringRef FS, const TargetOptions &Options,
                      Reloc::Model RM, CodeModel::Model CM,
                      CodeGenOpt::Level OL);
};

/// rvexelTargetMachine - rvex32 little endian target machine.
///
class rvexelTargetMachine : public rvexTargetMachine {
  virtual void anchor();
public:
  rvexelTargetMachine(const Target &T, StringRef TT,
                      StringRef CPU, StringRef FS, const TargetOptions &Options,
                      Reloc::Model RM, CodeModel::Model CM,
                      CodeGenOpt::Level OL);
};
} // End llvm namespace

#endif
