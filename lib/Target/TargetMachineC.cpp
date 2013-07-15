//===-- TargetMachine.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM-C part of TargetMachine.h
//
//===----------------------------------------------------------------------===//

#include "llvm-c/TargetMachine.h"
#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

using namespace llvm;

inline DataLayout *unwrap(LLVMTargetDataRef P) {
  return reinterpret_cast<DataLayout*>(P);
}

inline LLVMTargetDataRef wrap(const DataLayout *P) {
  return reinterpret_cast<LLVMTargetDataRef>(const_cast<DataLayout*>(P));
}

inline TargetLibraryInfo *unwrap(LLVMTargetLibraryInfoRef P) {
  return reinterpret_cast<TargetLibraryInfo*>(P);
}

inline LLVMTargetLibraryInfoRef wrap(const TargetLibraryInfo *P) {
  TargetLibraryInfo *X = const_cast<TargetLibraryInfo*>(P);
  return reinterpret_cast<LLVMTargetLibraryInfoRef>(X);
}

inline TargetMachine *unwrap(LLVMTargetMachineRef P) {
  return reinterpret_cast<TargetMachine*>(P);
}
inline Target *unwrap(LLVMTargetRef P) {
  return reinterpret_cast<Target*>(P);
}
inline LLVMTargetMachineRef wrap(const TargetMachine *P) {
  return
    reinterpret_cast<LLVMTargetMachineRef>(const_cast<TargetMachine*>(P));
}
inline LLVMTargetRef wrap(const Target * P) {
  return reinterpret_cast<LLVMTargetRef>(const_cast<Target*>(P));
}

LLVMTargetRef LLVMGetFirstTarget() {
   const Target* target = &*TargetRegistry::begin();
   return wrap(target);
}
LLVMTargetRef LLVMGetNextTarget(LLVMTargetRef T) {
  return wrap(unwrap(T)->getNext());
}

const char * LLVMGetTargetName(LLVMTargetRef T) {
  return unwrap(T)->getName();
}

const char * LLVMGetTargetDescription(LLVMTargetRef T) {
  return unwrap(T)->getShortDescription();
}

LLVMBool LLVMTargetHasJIT(LLVMTargetRef T) {
  return unwrap(T)->hasJIT();
}

LLVMBool LLVMTargetHasTargetMachine(LLVMTargetRef T) {
  return unwrap(T)->hasTargetMachine();
}

LLVMBool LLVMTargetHasAsmBackend(LLVMTargetRef T) {
  return unwrap(T)->hasMCAsmBackend();
}

LLVMTargetMachineRef LLVMCreateTargetMachine(LLVMTargetRef T, char* Triple,
  char* CPU, char* Features, LLVMCodeGenOptLevel Level, LLVMRelocMode Reloc,
  LLVMCodeModel CodeModel) {
  Reloc::Model RM;
  switch (Reloc){
    case LLVMRelocStatic:
      RM = Reloc::Static;
      break;
    case LLVMRelocPIC:
      RM = Reloc::PIC_;
      break;
    case LLVMRelocDynamicNoPic:
      RM = Reloc::DynamicNoPIC;
      break;
    default:
      RM = Reloc::Default;
      break;
  }

  CodeModel::Model CM = unwrap(CodeModel);

  CodeGenOpt::Level OL;
  switch (Level) {
    case LLVMCodeGenLevelNone:
      OL = CodeGenOpt::None;
      break;
    case LLVMCodeGenLevelLess:
      OL = CodeGenOpt::Less;
      break;
    case LLVMCodeGenLevelAggressive:
      OL = CodeGenOpt::Aggressive;
      break;
    default:
      OL = CodeGenOpt::Default;
      break;
  }

  TargetOptions opt;
  return wrap(unwrap(T)->createTargetMachine(Triple, CPU, Features, opt, RM,
    CM, OL));
}


void LLVMDisposeTargetMachine(LLVMTargetMachineRef T) {
  delete unwrap(T);
}

LLVMTargetRef LLVMGetTargetMachineTarget(LLVMTargetMachineRef T) {
  const Target* target = &(unwrap(T)->getTarget());
  return wrap(target);
}

char* LLVMGetTargetMachineTriple(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetTriple();
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineCPU(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetCPU();
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineFeatureString(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetFeatureString();
  return strdup(StringRep.c_str());
}

LLVMTargetDataRef LLVMGetTargetMachineData(LLVMTargetMachineRef T) {
  return wrap(unwrap(T)->getDataLayout());
}

static LLVMBool LLVMTargetMachineEmit(LLVMTargetMachineRef T, LLVMModuleRef M,
  formatted_raw_ostream &OS, LLVMCodeGenFileType codegen, char **ErrorMessage) {
  TargetMachine* TM = unwrap(T);
  Module* Mod = unwrap(M);

  PassManager pass;

  std::string error;

  const DataLayout* td = TM->getDataLayout();

  if (!td) {
    error = "No DataLayout in TargetMachine";
    *ErrorMessage = strdup(error.c_str());
    return true;
  }
  pass.add(new DataLayout(*td));

  TargetMachine::CodeGenFileType ft;
  switch (codegen) {
    case LLVMAssemblyFile:
      ft = TargetMachine::CGFT_AssemblyFile;
      break;
    default:
      ft = TargetMachine::CGFT_ObjectFile;
      break;
  }
  if (TM->addPassesToEmitFile(pass, OS, ft)) {
    error = "TargetMachine can't emit a file of this type";
    *ErrorMessage = strdup(error.c_str());
    return true;
  }

  pass.run(*Mod);

  OS.flush();
  return false;
}

LLVMBool LLVMTargetMachineEmitToFile(LLVMTargetMachineRef T, LLVMModuleRef M,
  char* Filename, LLVMCodeGenFileType codegen, char** ErrorMessage) {
  std::string error;
  raw_fd_ostream dest(Filename, error, raw_fd_ostream::F_Binary);
  formatted_raw_ostream destf(dest);
  if (!error.empty()) {
    *ErrorMessage = strdup(error.c_str());
    return true;
  }
  bool Result = LLVMTargetMachineEmit(T, M, destf, codegen, ErrorMessage);
  dest.flush();
  return Result;
}

LLVMBool LLVMTargetMachineEmitToMemoryBuffer(LLVMTargetMachineRef T,
  LLVMModuleRef M, LLVMCodeGenFileType codegen, char** ErrorMessage,
  LLVMMemoryBufferRef *OutMemBuf) {
  std::string CodeString;
  raw_string_ostream OStream(CodeString);
  formatted_raw_ostream Out(OStream);
  bool Result = LLVMTargetMachineEmit(T, M, Out, codegen, ErrorMessage);
  OStream.flush();

  std::string &Data = OStream.str();
  *OutMemBuf = LLVMCreateMemoryBufferWithMemoryRangeCopy(Data.c_str(),
                                                     Data.length(), "");
  return Result;
}
