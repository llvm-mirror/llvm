//===-- llvm/Target/TargetMachine.h - Target Information --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TargetMachine and LLVMTargetMachine classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETMACHINE_H
#define LLVM_TARGET_TARGETMACHINE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"
#include <cassert>
#include <string>

namespace llvm {

class InstrItineraryData;
class GlobalValue;
class Mangler;
class MCAsmInfo;
class MCCodeGenInfo;
class MCContext;
class MCSymbol;
class Target;
class DataLayout;
class TargetLibraryInfo;
class TargetFrameLowering;
class TargetIRAnalysis;
class TargetIntrinsicInfo;
class TargetLowering;
class TargetPassConfig;
class TargetRegisterInfo;
class TargetSelectionDAGInfo;
class TargetSubtargetInfo;
class TargetTransformInfo;
class formatted_raw_ostream;
class raw_ostream;
class TargetLoweringObjectFile;

// The old pass manager infrastructure is hidden in a legacy namespace now.
namespace legacy {
class PassManagerBase;
}
using legacy::PassManagerBase;

//===----------------------------------------------------------------------===//
///
/// TargetMachine - Primary interface to the complete machine description for
/// the target machine.  All target-specific information should be accessible
/// through this interface.
///
class TargetMachine {
  TargetMachine(const TargetMachine &) LLVM_DELETED_FUNCTION;
  void operator=(const TargetMachine &) LLVM_DELETED_FUNCTION;
protected: // Can only create subclasses.
  TargetMachine(const Target &T, StringRef TargetTriple,
                StringRef CPU, StringRef FS, const TargetOptions &Options);

  /// TheTarget - The Target that this machine was created for.
  const Target &TheTarget;

  /// TargetTriple, TargetCPU, TargetFS - Triple string, CPU name, and target
  /// feature strings the TargetMachine instance is created with.
  std::string TargetTriple;
  std::string TargetCPU;
  std::string TargetFS;

  /// CodeGenInfo - Low level target information such as relocation model.
  /// Non-const to allow resetting optimization level per-function.
  MCCodeGenInfo *CodeGenInfo;

  /// AsmInfo - Contains target specific asm information.
  ///
  const MCAsmInfo *AsmInfo;

  unsigned RequireStructuredCFG : 1;

public:
  mutable TargetOptions Options;

  virtual ~TargetMachine();

  const Target &getTarget() const { return TheTarget; }

  StringRef getTargetTriple() const { return TargetTriple; }
  StringRef getTargetCPU() const { return TargetCPU; }
  StringRef getTargetFeatureString() const { return TargetFS; }

  /// getSubtargetImpl - virtual method implemented by subclasses that returns
  /// a reference to that target's TargetSubtargetInfo-derived member variable.
  virtual const TargetSubtargetInfo *getSubtargetImpl() const {
    return nullptr;
  }
  virtual const TargetSubtargetInfo *getSubtargetImpl(const Function &) const {
    return getSubtargetImpl();
  }
  virtual TargetLoweringObjectFile *getObjFileLowering() const {
    return nullptr;
  }

  /// getSubtarget - This method returns a pointer to the specified type of
  /// TargetSubtargetInfo.  In debug builds, it verifies that the object being
  /// returned is of the correct type.
  template<typename STC> const STC &getSubtarget() const {
    return *static_cast<const STC*>(getSubtargetImpl());
  }
  template <typename STC> const STC &getSubtarget(const Function *) const {
    return *static_cast<const STC*>(getSubtargetImpl());
  }

  /// getDataLayout - This method returns a pointer to the DataLayout for
  /// the target. It should be unchanging for every subtarget.
  virtual const DataLayout *getDataLayout() const {
    return nullptr;
  }

  /// \brief Reset the target options based on the function's attributes.
  // FIXME: Remove TargetOptions that affect per-function code generation
  // from TargetMachine.
  void resetTargetOptions(const Function &F) const;

  /// getMCAsmInfo - Return target specific asm information.
  ///
  const MCAsmInfo *getMCAsmInfo() const { return AsmInfo; }

  /// getIntrinsicInfo - If intrinsic information is available, return it.  If
  /// not, return null.
  ///
  virtual const TargetIntrinsicInfo *getIntrinsicInfo() const {
    return nullptr;
  }

  bool requiresStructuredCFG() const { return RequireStructuredCFG; }
  void setRequiresStructuredCFG(bool Value) { RequireStructuredCFG = Value; }

  /// getRelocationModel - Returns the code generation relocation model. The
  /// choices are static, PIC, and dynamic-no-pic, and target default.
  Reloc::Model getRelocationModel() const;

  /// getCodeModel - Returns the code model. The choices are small, kernel,
  /// medium, large, and target default.
  CodeModel::Model getCodeModel() const;

  /// getTLSModel - Returns the TLS model which should be used for the given
  /// global variable.
  TLSModel::Model getTLSModel(const GlobalValue *GV) const;

  /// getOptLevel - Returns the optimization level: None, Less,
  /// Default, or Aggressive.
  CodeGenOpt::Level getOptLevel() const;

  /// \brief Overrides the optimization level.
  void setOptLevel(CodeGenOpt::Level Level) const;

  void setFastISel(bool Enable) { Options.EnableFastISel = Enable; }

  bool shouldPrintMachineCode() const { return Options.PrintMachineCode; }

  /// getAsmVerbosityDefault - Returns the default value of asm verbosity.
  ///
  bool getAsmVerbosityDefault() const ;

  /// setAsmVerbosityDefault - Set the default value of asm verbosity. Default
  /// is false.
  void setAsmVerbosityDefault(bool);

  /// getDataSections - Return true if data objects should be emitted into their
  /// own section, corresponds to -fdata-sections.
  bool getDataSections() const;

  /// getFunctionSections - Return true if functions should be emitted into
  /// their own section, corresponding to -ffunction-sections.
  bool getFunctionSections() const;

  /// setDataSections - Set if the data are emit into separate sections.
  void setDataSections(bool);

  /// setFunctionSections - Set if the functions are emit into separate
  /// sections.
  void setFunctionSections(bool);

  /// \brief Get a \c TargetIRAnalysis appropriate for the target.
  ///
  /// This is used to construct the new pass manager's target IR analysis pass,
  /// set up appropriately for this target machine. Even the old pass manager
  /// uses this to answer queries about the IR.
  virtual TargetIRAnalysis getTargetIRAnalysis();

  /// CodeGenFileType - These enums are meant to be passed into
  /// addPassesToEmitFile to indicate what type of file to emit, and returned by
  /// it to indicate what type of file could actually be made.
  enum CodeGenFileType {
    CGFT_AssemblyFile,
    CGFT_ObjectFile,
    CGFT_Null         // Do not emit any output.
  };

  /// addPassesToEmitFile - Add passes to the specified pass manager to get the
  /// specified file emitted.  Typically this will involve several steps of code
  /// generation.  This method should return true if emission of this file type
  /// is not supported, or false on success.
  virtual bool addPassesToEmitFile(PassManagerBase &,
                                   formatted_raw_ostream &,
                                   CodeGenFileType,
                                   bool /*DisableVerify*/ = true,
                                   AnalysisID /*StartAfter*/ = nullptr,
                                   AnalysisID /*StopAfter*/ = nullptr) {
    return true;
  }

  /// addPassesToEmitMC - Add passes to the specified pass manager to get
  /// machine code emitted with the MCJIT. This method returns true if machine
  /// code is not supported. It fills the MCContext Ctx pointer which can be
  /// used to build custom MCStreamer.
  ///
  virtual bool addPassesToEmitMC(PassManagerBase &,
                                 MCContext *&,
                                 raw_ostream &,
                                 bool /*DisableVerify*/ = true) {
    return true;
  }

  void getNameWithPrefix(SmallVectorImpl<char> &Name, const GlobalValue *GV,
                         Mangler &Mang, bool MayAlwaysUsePrivate = false) const;
  MCSymbol *getSymbol(const GlobalValue *GV, Mangler &Mang) const;
};

/// LLVMTargetMachine - This class describes a target machine that is
/// implemented with the LLVM target-independent code generator.
///
class LLVMTargetMachine : public TargetMachine {
protected: // Can only create subclasses.
  LLVMTargetMachine(const Target &T, StringRef TargetTriple,
                    StringRef CPU, StringRef FS, TargetOptions Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL);

  void initAsmInfo();
public:
  /// \brief Get a TargetIRAnalysis implementation for the target.
  ///
  /// This analysis will produce a TTI result which uses the common code
  /// generator to answer queries about the IR.
  TargetIRAnalysis getTargetIRAnalysis() override;

  /// createPassConfig - Create a pass configuration object to be used by
  /// addPassToEmitX methods for generating a pipeline of CodeGen passes.
  virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);

  /// addPassesToEmitFile - Add passes to the specified pass manager to get the
  /// specified file emitted.  Typically this will involve several steps of code
  /// generation.
  bool addPassesToEmitFile(PassManagerBase &PM, formatted_raw_ostream &Out,
                           CodeGenFileType FileType, bool DisableVerify = true,
                           AnalysisID StartAfter = nullptr,
                           AnalysisID StopAfter = nullptr) override;

  /// addPassesToEmitMC - Add passes to the specified pass manager to get
  /// machine code emitted with the MCJIT. This method returns true if machine
  /// code is not supported. It fills the MCContext Ctx pointer which can be
  /// used to build custom MCStreamer.
  ///
  bool addPassesToEmitMC(PassManagerBase &PM, MCContext *&Ctx,
                         raw_ostream &OS, bool DisableVerify = true) override;
};

} // End llvm namespace

#endif
