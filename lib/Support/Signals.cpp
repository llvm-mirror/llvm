//===- Signals.cpp - Signal Handling support --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// Unix signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

namespace llvm {
using namespace sys;

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

static ManagedStatic<std::vector<std::pair<void (*)(void *), void *>>>
    CallBacksToRun;
void sys::RunSignalHandlers() {
  if (!CallBacksToRun.isConstructed())
    return;
  for (auto &I : *CallBacksToRun)
    I.first(I.second);
  CallBacksToRun->clear();
}
}

using namespace llvm;

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool);

/// Format a pointer value as hexadecimal. Zero pad it out so its always the
/// same width.
static FormattedNumber format_ptr(void *PC) {
  // Each byte is two hex digits plus 2 for the 0x prefix.
  unsigned PtrWidth = 2 + 2 * sizeof(void *);
  return format_hex((uint64_t)PC, PtrWidth);
}

static bool printSymbolizedStackTrace(void **StackTrace, int Depth,
                                      llvm::raw_ostream &OS)
  LLVM_ATTRIBUTE_USED;

/// Helper that launches llvm-symbolizer and symbolizes a backtrace.
static bool printSymbolizedStackTrace(void **StackTrace, int Depth,
                                      llvm::raw_ostream &OS) {
  // FIXME: Subtract necessary number from StackTrace entries to turn return addresses
  // into actual instruction addresses.
  // Use llvm-symbolizer tool to symbolize the stack traces.
  ErrorOr<std::string> LLVMSymbolizerPathOrErr =
      sys::findProgramByName("llvm-symbolizer");
  if (!LLVMSymbolizerPathOrErr)
    return false;
  const std::string &LLVMSymbolizerPath = *LLVMSymbolizerPathOrErr;
  // We don't know argv0 or the address of main() at this point, but try
  // to guess it anyway (it's possible on some platforms).
  std::string MainExecutableName = sys::fs::getMainExecutable(nullptr, nullptr);
  if (MainExecutableName.empty() ||
      MainExecutableName.find("llvm-symbolizer") != std::string::npos)
    return false;

  BumpPtrAllocator Allocator;
  StringSaver StrPool(Allocator);
  std::vector<const char *> Modules(Depth, nullptr);
  std::vector<intptr_t> Offsets(Depth, 0);
  if (!findModulesAndOffsets(StackTrace, Depth, Modules.data(), Offsets.data(),
                             MainExecutableName.c_str(), StrPool))
    return false;
  int InputFD;
  SmallString<32> InputFile, OutputFile;
  sys::fs::createTemporaryFile("symbolizer-input", "", InputFD, InputFile);
  sys::fs::createTemporaryFile("symbolizer-output", "", OutputFile);
  FileRemover InputRemover(InputFile.c_str());
  FileRemover OutputRemover(OutputFile.c_str());

  {
    raw_fd_ostream Input(InputFD, true);
    for (int i = 0; i < Depth; i++) {
      if (Modules[i])
        Input << Modules[i] << " " << (void*)Offsets[i] << "\n";
    }
  }

  StringRef InputFileStr(InputFile);
  StringRef OutputFileStr(OutputFile);
  StringRef StderrFileStr;
  const StringRef *Redirects[] = {&InputFileStr, &OutputFileStr,
                                  &StderrFileStr};
  const char *Args[] = {"llvm-symbolizer", "--functions=linkage", "--inlining",
#ifdef LLVM_ON_WIN32
                        // Pass --relative-address on Windows so that we don't
                        // have to add ImageBase from PE file.
                        // FIXME: Make this the default for llvm-symbolizer.
                        "--relative-address",
#endif
                        "--demangle", nullptr};
  int RunResult =
      sys::ExecuteAndWait(LLVMSymbolizerPath, Args, nullptr, Redirects);
  if (RunResult != 0)
    return false;

  // This report format is based on the sanitizer stack trace printer.  See
  // sanitizer_stacktrace_printer.cc in compiler-rt.
  auto OutputBuf = MemoryBuffer::getFile(OutputFile.c_str());
  if (!OutputBuf)
    return false;
  StringRef Output = OutputBuf.get()->getBuffer();
  SmallVector<StringRef, 32> Lines;
  Output.split(Lines, "\n");
  auto CurLine = Lines.begin();
  int frame_no = 0;
  for (int i = 0; i < Depth; i++) {
    if (!Modules[i]) {
      OS << '#' << frame_no++ << ' ' << format_ptr(StackTrace[i]) << '\n';
      continue;
    }
    // Read pairs of lines (function name and file/line info) until we
    // encounter empty line.
    for (;;) {
      if (CurLine == Lines.end())
        return false;
      StringRef FunctionName = *CurLine++;
      if (FunctionName.empty())
        break;
      OS << '#' << frame_no++ << ' ' << format_ptr(StackTrace[i]) << ' ';
      if (!FunctionName.startswith("??"))
        OS << FunctionName << ' ';
      if (CurLine == Lines.end())
        return false;
      StringRef FileLineInfo = *CurLine++;
      if (!FileLineInfo.startswith("??"))
        OS << FileLineInfo;
      else
        OS << "(" << Modules[i] << '+' << format_hex(Offsets[i], 0) << ")";
      OS << "\n";
    }
  }
  return true;
}

// Include the platform-specific parts of this class.
#ifdef LLVM_ON_UNIX
#include "Unix/Signals.inc"
#endif
#ifdef LLVM_ON_WIN32
#include "Windows/Signals.inc"
#endif
