//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/Linker.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
using namespace llvm;

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
OutputAssembly("S",
         cl::desc("Write output as LLVM assembly"), cl::Hidden);

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

static cl::opt<bool>
DumpAsm("d", cl::desc("Print assembly as linked"), cl::Hidden);

// LoadFile - Read the specified bitcode file in and return it.  This routine
// searches the link path for the specified file to try to find it...
//
static inline Module *LoadFile(const char *argv0, const std::string &FN,
                               LLVMContext& Context) {
  SMDiagnostic Err;
  if (Verbose) errs() << "Loading '" << FN << "'\n";
  Module* Result = 0;

  Result = ParseIRFile(FN, Err, Context);
  if (Result) return Result;   // Load successful!

  Err.print(argv0, errs());
  return NULL;
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  
  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm linker\n");

  unsigned BaseArg = 0;
  std::string ErrorMessage;

  OwningPtr<Module> Composite(LoadFile(argv[0],
                                       InputFilenames[BaseArg], Context));
  if (Composite.get() == 0) {
    errs() << argv[0] << ": error loading file '"
           << InputFilenames[BaseArg] << "'\n";
    return 1;
  }

  Linker L(Composite.get());
  for (unsigned i = BaseArg+1; i < InputFilenames.size(); ++i) {
    OwningPtr<Module> M(LoadFile(argv[0], InputFilenames[i], Context));
    if (M.get() == 0) {
      errs() << argv[0] << ": error loading file '" <<InputFilenames[i]<< "'\n";
      return 1;
    }

    if (Verbose) errs() << "Linking in '" << InputFilenames[i] << "'\n";

    if (L.linkInModule(M.get(), &ErrorMessage)) {
      errs() << argv[0] << ": link error in '" << InputFilenames[i]
             << "': " << ErrorMessage << "\n";
      return 1;
    }
  }

  if (DumpAsm) errs() << "Here's the assembly:\n" << *Composite;

  std::string ErrorInfo;
  tool_output_file Out(OutputFilename.c_str(), ErrorInfo,
                       raw_fd_ostream::F_Binary);
  if (!ErrorInfo.empty()) {
    errs() << ErrorInfo << '\n';
    return 1;
  }

  if (verifyModule(*Composite)) {
    errs() << argv[0] << ": linked module is broken!\n";
    return 1;
  }

  if (Verbose) errs() << "Writing bitcode...\n";
  if (OutputAssembly) {
    Out.os() << *Composite;
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os(), true))
    WriteBitcodeToFile(Composite.get(), Out.os());

  // Declare success.
  Out.keep();

  return 0;
}
