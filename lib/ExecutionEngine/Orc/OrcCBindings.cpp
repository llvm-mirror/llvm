//===----------- OrcCBindings.cpp - C bindings for the Orc APIs -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "OrcCBindingsStack.h"
#include "llvm-c/OrcBindings.h"

using namespace llvm;

LLVMOrcJITStackRef LLVMOrcCreateInstance(LLVMTargetMachineRef TM) {
  TargetMachine *TM2(unwrap(TM));

  Triple T(TM2->getTargetTriple());

  auto CompileCallbackMgr = OrcCBindingsStack::createCompileCallbackMgr(T);
  auto IndirectStubsMgrBuilder =
    OrcCBindingsStack::createIndirectStubsMgrBuilder(T);

  OrcCBindingsStack *JITStack =
    new OrcCBindingsStack(*TM2, std::move(CompileCallbackMgr),
			  IndirectStubsMgrBuilder);

  return wrap(JITStack);
}

void LLVMOrcGetMangledSymbol(LLVMOrcJITStackRef JITStack, char **MangledName,
                             const char *SymbolName) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  std::string Mangled = J.mangle(SymbolName);
  *MangledName = new char[Mangled.size() + 1];
  strcpy(*MangledName, Mangled.c_str());
}

void LLVMOrcDisposeMangledSymbol(char *MangledName) {
  delete[] MangledName;
}

LLVMOrcTargetAddress
LLVMOrcCreateLazyCompileCallback(LLVMOrcJITStackRef JITStack,
                                 LLVMOrcLazyCompileCallbackFn Callback,
                                 void *CallbackCtx) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  return J.createLazyCompileCallback(Callback, CallbackCtx);
}

void LLVMOrcCreateIndirectStub(LLVMOrcJITStackRef JITStack,
                               const char *StubName,
                               LLVMOrcTargetAddress InitAddr) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  J.createIndirectStub(StubName, InitAddr);
}

void LLVMOrcSetIndirectStubPointer(LLVMOrcJITStackRef JITStack,
                                   const char *StubName,
                                   LLVMOrcTargetAddress NewAddr) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  J.setIndirectStubPointer(StubName, NewAddr);
}

LLVMOrcModuleHandle
LLVMOrcAddEagerlyCompiledIR(LLVMOrcJITStackRef JITStack, LLVMModuleRef Mod,
                            LLVMOrcSymbolResolverFn SymbolResolver,
                            void *SymbolResolverCtx) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  Module *M(unwrap(Mod));
  return J.addIRModuleEager(M, SymbolResolver, SymbolResolverCtx);
}

LLVMOrcModuleHandle
LLVMOrcAddLazilyCompiledIR(LLVMOrcJITStackRef JITStack, LLVMModuleRef Mod,
                           LLVMOrcSymbolResolverFn SymbolResolver,
                           void *SymbolResolverCtx) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  Module *M(unwrap(Mod));
  return J.addIRModuleLazy(M, SymbolResolver, SymbolResolverCtx);
}

void LLVMOrcRemoveModule(LLVMOrcJITStackRef JITStack, LLVMOrcModuleHandle H) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  J.removeModule(H);
}

LLVMOrcTargetAddress LLVMOrcGetSymbolAddress(LLVMOrcJITStackRef JITStack,
                                             const char *SymbolName) {
  OrcCBindingsStack &J = *unwrap(JITStack);
  auto Sym = J.findSymbol(SymbolName, true);
  return Sym.getAddress();
}

void LLVMOrcDisposeInstance(LLVMOrcJITStackRef JITStack) {
  delete unwrap(JITStack);
}
