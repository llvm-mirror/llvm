//===-- BitReader.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/BitReader.h"
#include "llvm-c/Core.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <string>

using namespace llvm;

/* Builds a module from the bitcode in the specified memory buffer, returning a
   reference to the module via the OutModule parameter. Returns 0 on success.
   Optionally returns a human-readable error message via OutMessage. */
LLVMBool LLVMParseBitcode(LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutModule,
                          char **OutMessage) {
  return LLVMParseBitcodeInContext(wrap(&getGlobalContext()), MemBuf, OutModule,
                                   OutMessage);
}

LLVMBool LLVMParseBitcode2(LLVMMemoryBufferRef MemBuf,
                           LLVMModuleRef *OutModule) {
  return LLVMParseBitcodeInContext2(wrap(&getGlobalContext()), MemBuf,
                                    OutModule);
}

static void diagnosticHandler(const DiagnosticInfo &DI, void *C) {
  auto *Message = reinterpret_cast<std::string *>(C);
  raw_string_ostream Stream(*Message);
  DiagnosticPrinterRawOStream DP(Stream);
  DI.print(DP);
}

LLVMBool LLVMParseBitcodeInContext(LLVMContextRef ContextRef,
                                   LLVMMemoryBufferRef MemBuf,
                                   LLVMModuleRef *OutModule,
                                   char **OutMessage) {
  MemoryBufferRef Buf = unwrap(MemBuf)->getMemBufferRef();
  LLVMContext &Ctx = *unwrap(ContextRef);

  LLVMContext::DiagnosticHandlerTy OldDiagnosticHandler =
      Ctx.getDiagnosticHandler();
  void *OldDiagnosticContext = Ctx.getDiagnosticContext();
  std::string Message;
  Ctx.setDiagnosticHandler(diagnosticHandler, &Message, true);

  ErrorOr<std::unique_ptr<Module>> ModuleOrErr = parseBitcodeFile(Buf, Ctx);

  Ctx.setDiagnosticHandler(OldDiagnosticHandler, OldDiagnosticContext, true);

  if (ModuleOrErr.getError()) {
    if (OutMessage)
      *OutMessage = strdup(Message.c_str());
    *OutModule = wrap((Module *)nullptr);
    return 1;
  }

  *OutModule = wrap(ModuleOrErr.get().release());
  return 0;
}

LLVMBool LLVMParseBitcodeInContext2(LLVMContextRef ContextRef,
                                    LLVMMemoryBufferRef MemBuf,
                                    LLVMModuleRef *OutModule) {
  MemoryBufferRef Buf = unwrap(MemBuf)->getMemBufferRef();
  LLVMContext &Ctx = *unwrap(ContextRef);

  ErrorOr<std::unique_ptr<Module>> ModuleOrErr = parseBitcodeFile(Buf, Ctx);
  if (ModuleOrErr.getError()) {
    *OutModule = wrap((Module *)nullptr);
    return 1;
  }

  *OutModule = wrap(ModuleOrErr.get().release());
  return 0;
}

/* Reads a module from the specified path, returning via the OutModule parameter
   a module provider which performs lazy deserialization. Returns 0 on success.
   Optionally returns a human-readable error message via OutMessage. */
LLVMBool LLVMGetBitcodeModuleInContext(LLVMContextRef ContextRef,
                                       LLVMMemoryBufferRef MemBuf,
                                       LLVMModuleRef *OutM, char **OutMessage) {
  LLVMContext &Ctx = *unwrap(ContextRef);
  LLVMContext::DiagnosticHandlerTy OldDiagnosticHandler =
      Ctx.getDiagnosticHandler();
  void *OldDiagnosticContext = Ctx.getDiagnosticContext();

  std::string Message;
  Ctx.setDiagnosticHandler(diagnosticHandler, &Message, true);
  std::unique_ptr<MemoryBuffer> Owner(unwrap(MemBuf));

  ErrorOr<std::unique_ptr<Module>> ModuleOrErr =
      getLazyBitcodeModule(std::move(Owner), Ctx);
  Owner.release();
  Ctx.setDiagnosticHandler(OldDiagnosticHandler, OldDiagnosticContext, true);

  if (ModuleOrErr.getError()) {
    *OutM = wrap((Module *)nullptr);
    if (OutMessage)
      *OutMessage = strdup(Message.c_str());
    return 1;
  }

  *OutM = wrap(ModuleOrErr.get().release());

  return 0;
}

LLVMBool LLVMGetBitcodeModuleInContext2(LLVMContextRef ContextRef,
                                        LLVMMemoryBufferRef MemBuf,
                                        LLVMModuleRef *OutM) {
  LLVMContext &Ctx = *unwrap(ContextRef);
  std::unique_ptr<MemoryBuffer> Owner(unwrap(MemBuf));

  ErrorOr<std::unique_ptr<Module>> ModuleOrErr =
      getLazyBitcodeModule(std::move(Owner), Ctx);
  Owner.release();

  if (ModuleOrErr.getError()) {
    *OutM = wrap((Module *)nullptr);
    return 1;
  }

  *OutM = wrap(ModuleOrErr.get().release());
  return 0;
}

LLVMBool LLVMGetBitcodeModule(LLVMMemoryBufferRef MemBuf, LLVMModuleRef *OutM,
                              char **OutMessage) {
  return LLVMGetBitcodeModuleInContext(LLVMGetGlobalContext(), MemBuf, OutM,
                                       OutMessage);
}

LLVMBool LLVMGetBitcodeModule2(LLVMMemoryBufferRef MemBuf,
                               LLVMModuleRef *OutM) {
  return LLVMGetBitcodeModuleInContext2(LLVMGetGlobalContext(), MemBuf, OutM);
}
