//===-- ToolRunner.cpp ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces described in the ToolRunner.h file.
//
//===----------------------------------------------------------------------===//

#include "ToolRunner.h"
#include "llvm/Config/config.h"   // for HAVE_LINK_R
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <sstream>
using namespace llvm;

#define DEBUG_TYPE "toolrunner"

namespace llvm {
  cl::opt<bool>
  SaveTemps("save-temps", cl::init(false), cl::desc("Save temporary files"));
}

namespace {
  cl::opt<std::string>
  RemoteClient("remote-client",
               cl::desc("Remote execution client (rsh/ssh)"));

  cl::opt<std::string>
  RemoteHost("remote-host",
             cl::desc("Remote execution (rsh/ssh) host"));

  cl::opt<std::string>
  RemotePort("remote-port",
             cl::desc("Remote execution (rsh/ssh) port"));

  cl::opt<std::string>
  RemoteUser("remote-user",
             cl::desc("Remote execution (rsh/ssh) user id"));

  cl::opt<std::string>
  RemoteExtra("remote-extra-options",
          cl::desc("Remote execution (rsh/ssh) extra options"));
}

/// RunProgramWithTimeout - This function provides an alternate interface
/// to the sys::Program::ExecuteAndWait interface.
/// @see sys::Program::ExecuteAndWait
static int RunProgramWithTimeout(StringRef ProgramPath,
                                 const char **Args,
                                 StringRef StdInFile,
                                 StringRef StdOutFile,
                                 StringRef StdErrFile,
                                 unsigned NumSeconds = 0,
                                 unsigned MemoryLimit = 0,
                                 std::string *ErrMsg = nullptr) {
  const StringRef *Redirects[3] = { &StdInFile, &StdOutFile, &StdErrFile };
  return sys::ExecuteAndWait(ProgramPath, Args, nullptr, Redirects,
                             NumSeconds, MemoryLimit, ErrMsg);
}

/// RunProgramRemotelyWithTimeout - This function runs the given program
/// remotely using the given remote client and the sys::Program::ExecuteAndWait.
/// Returns the remote program exit code or reports a remote client error if it
/// fails. Remote client is required to return 255 if it failed or program exit
/// code otherwise.
/// @see sys::Program::ExecuteAndWait
static int RunProgramRemotelyWithTimeout(StringRef RemoteClientPath,
                                         const char **Args,
                                         StringRef StdInFile,
                                         StringRef StdOutFile,
                                         StringRef StdErrFile,
                                         unsigned NumSeconds = 0,
                                         unsigned MemoryLimit = 0) {
  const StringRef *Redirects[3] = { &StdInFile, &StdOutFile, &StdErrFile };

  // Run the program remotely with the remote client
  int ReturnCode = sys::ExecuteAndWait(RemoteClientPath, Args, nullptr,
                                       Redirects, NumSeconds, MemoryLimit);

  // Has the remote client fail?
  if (255 == ReturnCode) {
    std::ostringstream OS;
    OS << "\nError running remote client:\n ";
    for (const char **Arg = Args; *Arg; ++Arg)
      OS << " " << *Arg;
    OS << "\n";

    // The error message is in the output file, let's print it out from there.
    std::string StdOutFileName = StdOutFile.str();
    std::ifstream ErrorFile(StdOutFileName.c_str());
    if (ErrorFile) {
      std::copy(std::istreambuf_iterator<char>(ErrorFile),
                std::istreambuf_iterator<char>(),
                std::ostreambuf_iterator<char>(OS));
      ErrorFile.close();
    }

    errs() << OS.str();
  }

  return ReturnCode;
}

static std::string ProcessFailure(StringRef ProgPath, const char** Args,
                                  unsigned Timeout = 0,
                                  unsigned MemoryLimit = 0) {
  std::ostringstream OS;
  OS << "\nError running tool:\n ";
  for (const char **Arg = Args; *Arg; ++Arg)
    OS << " " << *Arg;
  OS << "\n";

  // Rerun the compiler, capturing any error messages to print them.
  SmallString<128> ErrorFilename;
  std::error_code EC = sys::fs::createTemporaryFile(
      "bugpoint.program_error_messages", "", ErrorFilename);
  if (EC) {
    errs() << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }

  RunProgramWithTimeout(ProgPath, Args, "", ErrorFilename.str(),
                        ErrorFilename.str(), Timeout, MemoryLimit);
  // FIXME: check return code ?

  // Print out the error messages generated by CC if possible...
  std::ifstream ErrorFile(ErrorFilename.c_str());
  if (ErrorFile) {
    std::copy(std::istreambuf_iterator<char>(ErrorFile),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(OS));
    ErrorFile.close();
  }

  sys::fs::remove(ErrorFilename.c_str());
  return OS.str();
}

//===---------------------------------------------------------------------===//
// LLI Implementation of AbstractIntepreter interface
//
namespace {
  class LLI : public AbstractInterpreter {
    std::string LLIPath;          // The path to the LLI executable
    std::vector<std::string> ToolArgs; // Args to pass to LLI
  public:
    LLI(const std::string &Path, const std::vector<std::string> *Args)
      : LLIPath(Path) {
      ToolArgs.clear ();
      if (Args) { ToolArgs = *Args; }
    }

    int ExecuteProgram(const std::string &Bitcode,
                       const std::vector<std::string> &Args,
                       const std::string &InputFile,
                       const std::string &OutputFile,
                       std::string *Error,
                       const std::vector<std::string> &CCArgs,
                       const std::vector<std::string> &SharedLibs =
                       std::vector<std::string>(),
                       unsigned Timeout = 0,
                       unsigned MemoryLimit = 0) override;
  };
}

int LLI::ExecuteProgram(const std::string &Bitcode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        std::string *Error,
                        const std::vector<std::string> &CCArgs,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout,
                        unsigned MemoryLimit) {
  std::vector<const char*> LLIArgs;
  LLIArgs.push_back(LLIPath.c_str());
  LLIArgs.push_back("-force-interpreter=true");

  for (std::vector<std::string>::const_iterator i = SharedLibs.begin(),
         e = SharedLibs.end(); i != e; ++i) {
    LLIArgs.push_back("-load");
    LLIArgs.push_back((*i).c_str());
  }

  // Add any extra LLI args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    LLIArgs.push_back(ToolArgs[i].c_str());

  LLIArgs.push_back(Bitcode.c_str());
  // Add optional parameters to the running program from Argv
  for (unsigned i=0, e = Args.size(); i != e; ++i)
    LLIArgs.push_back(Args[i].c_str());
  LLIArgs.push_back(nullptr);

  outs() << "<lli>"; outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i=0, e = LLIArgs.size()-1; i != e; ++i)
          errs() << " " << LLIArgs[i];
        errs() << "\n";
        );
  return RunProgramWithTimeout(LLIPath, &LLIArgs[0],
      InputFile, OutputFile, OutputFile,
      Timeout, MemoryLimit, Error);
}

void AbstractInterpreter::anchor() { }

#if defined(LLVM_ON_UNIX)
const char EXESuffix[] = "";
#elif defined (LLVM_ON_WIN32)
const char EXESuffix[] = "exe";
#endif

/// Prepend the path to the program being executed
/// to \p ExeName, given the value of argv[0] and the address of main()
/// itself. This allows us to find another LLVM tool if it is built in the same
/// directory. An empty string is returned on error; note that this function
/// just mainpulates the path and doesn't check for executability.
/// @brief Find a named executable.
static std::string PrependMainExecutablePath(const std::string &ExeName,
                                             const char *Argv0,
                                             void *MainAddr) {
  // Check the directory that the calling program is in.  We can do
  // this if ProgramPath contains at least one / character, indicating that it
  // is a relative path to the executable itself.
  std::string Main = sys::fs::getMainExecutable(Argv0, MainAddr);
  StringRef Result = sys::path::parent_path(Main);

  if (!Result.empty()) {
    SmallString<128> Storage = Result;
    sys::path::append(Storage, ExeName);
    sys::path::replace_extension(Storage, EXESuffix);
    return Storage.str();
  }

  return Result.str();
}

// LLI create method - Try to find the LLI executable
AbstractInterpreter *AbstractInterpreter::createLLI(const char *Argv0,
                                                    std::string &Message,
                                     const std::vector<std::string> *ToolArgs) {
  std::string LLIPath =
      PrependMainExecutablePath("lli", Argv0, (void *)(intptr_t) & createLLI);
  if (!LLIPath.empty()) {
    Message = "Found lli: " + LLIPath + "\n";
    return new LLI(LLIPath, ToolArgs);
  }

  Message = "Cannot find `lli' in executable directory!\n";
  return nullptr;
}

//===---------------------------------------------------------------------===//
// Custom compiler command implementation of AbstractIntepreter interface
//
// Allows using a custom command for compiling the bitcode, thus allows, for
// example, to compile a bitcode fragment without linking or executing, then
// using a custom wrapper script to check for compiler errors.
namespace {
  class CustomCompiler : public AbstractInterpreter {
    std::string CompilerCommand;
    std::vector<std::string> CompilerArgs;
  public:
    CustomCompiler(
      const std::string &CompilerCmd, std::vector<std::string> CompArgs) :
      CompilerCommand(CompilerCmd), CompilerArgs(CompArgs) {}

    void compileProgram(const std::string &Bitcode,
                        std::string *Error,
                        unsigned Timeout = 0,
                        unsigned MemoryLimit = 0) override;

    int ExecuteProgram(const std::string &Bitcode,
                       const std::vector<std::string> &Args,
                       const std::string &InputFile,
                       const std::string &OutputFile,
                       std::string *Error,
                       const std::vector<std::string> &CCArgs =
                       std::vector<std::string>(),
                       const std::vector<std::string> &SharedLibs =
                       std::vector<std::string>(),
                       unsigned Timeout = 0,
                       unsigned MemoryLimit = 0) override {
      *Error = "Execution not supported with -compile-custom";
      return -1;
    }
  };
}

void CustomCompiler::compileProgram(const std::string &Bitcode,
                                    std::string *Error,
                                    unsigned Timeout,
                                    unsigned MemoryLimit) {

  std::vector<const char*> ProgramArgs;
  ProgramArgs.push_back(CompilerCommand.c_str());

  for (std::size_t i = 0; i < CompilerArgs.size(); ++i)
    ProgramArgs.push_back(CompilerArgs.at(i).c_str());
  ProgramArgs.push_back(Bitcode.c_str());
  ProgramArgs.push_back(nullptr);

  // Add optional parameters to the running program from Argv
  for (unsigned i = 0, e = CompilerArgs.size(); i != e; ++i)
    ProgramArgs.push_back(CompilerArgs[i].c_str());

  if (RunProgramWithTimeout(CompilerCommand, &ProgramArgs[0],
                             "", "", "",
                             Timeout, MemoryLimit, Error))
    *Error = ProcessFailure(CompilerCommand, &ProgramArgs[0],
                           Timeout, MemoryLimit);
}

//===---------------------------------------------------------------------===//
// Custom execution command implementation of AbstractIntepreter interface
//
// Allows using a custom command for executing the bitcode, thus allows,
// for example, to invoke a cross compiler for code generation followed by
// a simulator that executes the generated binary.
namespace {
  class CustomExecutor : public AbstractInterpreter {
    std::string ExecutionCommand;
    std::vector<std::string> ExecutorArgs;
  public:
    CustomExecutor(
      const std::string &ExecutionCmd, std::vector<std::string> ExecArgs) :
      ExecutionCommand(ExecutionCmd), ExecutorArgs(ExecArgs) {}

    int ExecuteProgram(const std::string &Bitcode,
                       const std::vector<std::string> &Args,
                       const std::string &InputFile,
                       const std::string &OutputFile,
                       std::string *Error,
                       const std::vector<std::string> &CCArgs,
                       const std::vector<std::string> &SharedLibs =
                         std::vector<std::string>(),
                       unsigned Timeout = 0,
                       unsigned MemoryLimit = 0) override;
  };
}

int CustomExecutor::ExecuteProgram(const std::string &Bitcode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        std::string *Error,
                        const std::vector<std::string> &CCArgs,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout,
                        unsigned MemoryLimit) {

  std::vector<const char*> ProgramArgs;
  ProgramArgs.push_back(ExecutionCommand.c_str());

  for (std::size_t i = 0; i < ExecutorArgs.size(); ++i)
    ProgramArgs.push_back(ExecutorArgs.at(i).c_str());
  ProgramArgs.push_back(Bitcode.c_str());
  ProgramArgs.push_back(nullptr);

  // Add optional parameters to the running program from Argv
  for (unsigned i = 0, e = Args.size(); i != e; ++i)
    ProgramArgs.push_back(Args[i].c_str());

  return RunProgramWithTimeout(
    ExecutionCommand,
    &ProgramArgs[0], InputFile, OutputFile,
    OutputFile, Timeout, MemoryLimit, Error);
}

// Tokenize the CommandLine to the command and the args to allow
// defining a full command line as the command instead of just the
// executed program. We cannot just pass the whole string after the command
// as a single argument because then program sees only a single
// command line argument (with spaces in it: "foo bar" instead
// of "foo" and "bar").
//
// code borrowed from:
// http://oopweb.com/CPP/Documents/CPPHOWTO/Volume/C++Programming-HOWTO-7.html
static void lexCommand(std::string &Message, const std::string &CommandLine,
                       std::string &CmdPath, std::vector<std::string> &Args) {

  std::string Command = "";
  std::string delimiters = " ";

  std::string::size_type lastPos = CommandLine.find_first_not_of(delimiters, 0);
  std::string::size_type pos = CommandLine.find_first_of(delimiters, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    std::string token = CommandLine.substr(lastPos, pos - lastPos);
    if (Command == "")
       Command = token;
    else
       Args.push_back(token);
    // Skip delimiters.  Note the "not_of"
    lastPos = CommandLine.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = CommandLine.find_first_of(delimiters, lastPos);
  }

  auto Path = sys::findProgramByName(Command);
  if (!Path) {
    Message =
      std::string("Cannot find '") + Command +
      "' in PATH: " + Path.getError().message() + "\n";
    return;
  }
  CmdPath = *Path;

  Message = "Found command in: " + CmdPath + "\n";
}

// Custom execution environment create method, takes the execution command
// as arguments
AbstractInterpreter *AbstractInterpreter::createCustomCompiler(
                    std::string &Message,
                    const std::string &CompileCommandLine) {

  std::string CmdPath;
  std::vector<std::string> Args;
  lexCommand(Message, CompileCommandLine, CmdPath, Args);
  if (CmdPath.empty())
    return nullptr;

  return new CustomCompiler(CmdPath, Args);
}

// Custom execution environment create method, takes the execution command
// as arguments
AbstractInterpreter *AbstractInterpreter::createCustomExecutor(
                    std::string &Message,
                    const std::string &ExecCommandLine) {


  std::string CmdPath;
  std::vector<std::string> Args;
  lexCommand(Message, ExecCommandLine, CmdPath, Args);
  if (CmdPath.empty())
    return nullptr;

  return new CustomExecutor(CmdPath, Args);
}

//===----------------------------------------------------------------------===//
// LLC Implementation of AbstractIntepreter interface
//
CC::FileType LLC::OutputCode(const std::string &Bitcode,
                              std::string &OutputAsmFile, std::string &Error,
                              unsigned Timeout, unsigned MemoryLimit) {
  const char *Suffix = (UseIntegratedAssembler ? ".llc.o" : ".llc.s");

  SmallString<128> UniqueFile;
  std::error_code EC =
      sys::fs::createUniqueFile(Bitcode + "-%%%%%%%" + Suffix, UniqueFile);
  if (EC) {
    errs() << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }
  OutputAsmFile = UniqueFile.str();
  std::vector<const char *> LLCArgs;
  LLCArgs.push_back(LLCPath.c_str());

  // Add any extra LLC args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    LLCArgs.push_back(ToolArgs[i].c_str());

  LLCArgs.push_back("-o");
  LLCArgs.push_back(OutputAsmFile.c_str()); // Output to the Asm file
  LLCArgs.push_back(Bitcode.c_str());      // This is the input bitcode

  if (UseIntegratedAssembler)
    LLCArgs.push_back("-filetype=obj");

  LLCArgs.push_back (nullptr);

  outs() << (UseIntegratedAssembler ? "<llc-ia>" : "<llc>");
  outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i = 0, e = LLCArgs.size()-1; i != e; ++i)
          errs() << " " << LLCArgs[i];
        errs() << "\n";
        );
  if (RunProgramWithTimeout(LLCPath, &LLCArgs[0],
                            "", "", "",
                            Timeout, MemoryLimit))
    Error = ProcessFailure(LLCPath, &LLCArgs[0],
                           Timeout, MemoryLimit);
  return UseIntegratedAssembler ? CC::ObjectFile : CC::AsmFile;
}

void LLC::compileProgram(const std::string &Bitcode, std::string *Error,
                         unsigned Timeout, unsigned MemoryLimit) {
  std::string OutputAsmFile;
  OutputCode(Bitcode, OutputAsmFile, *Error, Timeout, MemoryLimit);
  sys::fs::remove(OutputAsmFile);
}

int LLC::ExecuteProgram(const std::string &Bitcode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        std::string *Error,
                        const std::vector<std::string> &ArgsForCC,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout,
                        unsigned MemoryLimit) {

  std::string OutputAsmFile;
  CC::FileType FileKind = OutputCode(Bitcode, OutputAsmFile, *Error, Timeout,
                                      MemoryLimit);
  FileRemover OutFileRemover(OutputAsmFile, !SaveTemps);

  std::vector<std::string> CCArgs(ArgsForCC);
  CCArgs.insert(CCArgs.end(), SharedLibs.begin(), SharedLibs.end());

  // Assuming LLC worked, compile the result with CC and run it.
  return cc->ExecuteProgram(OutputAsmFile, Args, FileKind,
                             InputFile, OutputFile, Error, CCArgs,
                             Timeout, MemoryLimit);
}

/// createLLC - Try to find the LLC executable
///
LLC *AbstractInterpreter::createLLC(const char *Argv0,
                                    std::string &Message,
                                    const std::string &CCBinary,
                                    const std::vector<std::string> *Args,
                                    const std::vector<std::string> *CCArgs,
                                    bool UseIntegratedAssembler) {
  std::string LLCPath =
      PrependMainExecutablePath("llc", Argv0, (void *)(intptr_t) & createLLC);
  if (LLCPath.empty()) {
    Message = "Cannot find `llc' in executable directory!\n";
    return nullptr;
  }

  CC *cc = CC::create(Message, CCBinary, CCArgs);
  if (!cc) {
    errs() << Message << "\n";
    exit(1);
  }
  Message = "Found llc: " + LLCPath + "\n";
  return new LLC(LLCPath, cc, Args, UseIntegratedAssembler);
}

//===---------------------------------------------------------------------===//
// JIT Implementation of AbstractIntepreter interface
//
namespace {
  class JIT : public AbstractInterpreter {
    std::string LLIPath;          // The path to the LLI executable
    std::vector<std::string> ToolArgs; // Args to pass to LLI
  public:
    JIT(const std::string &Path, const std::vector<std::string> *Args)
      : LLIPath(Path) {
      ToolArgs.clear ();
      if (Args) { ToolArgs = *Args; }
    }

    int ExecuteProgram(const std::string &Bitcode,
                       const std::vector<std::string> &Args,
                       const std::string &InputFile,
                       const std::string &OutputFile,
                       std::string *Error,
                       const std::vector<std::string> &CCArgs =
                         std::vector<std::string>(),
                       const std::vector<std::string> &SharedLibs =
                         std::vector<std::string>(),
                       unsigned Timeout = 0,
                       unsigned MemoryLimit = 0) override;
  };
}

int JIT::ExecuteProgram(const std::string &Bitcode,
                        const std::vector<std::string> &Args,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        std::string *Error,
                        const std::vector<std::string> &CCArgs,
                        const std::vector<std::string> &SharedLibs,
                        unsigned Timeout,
                        unsigned MemoryLimit) {
  // Construct a vector of parameters, incorporating those from the command-line
  std::vector<const char*> JITArgs;
  JITArgs.push_back(LLIPath.c_str());
  JITArgs.push_back("-force-interpreter=false");

  // Add any extra LLI args.
  for (unsigned i = 0, e = ToolArgs.size(); i != e; ++i)
    JITArgs.push_back(ToolArgs[i].c_str());

  for (unsigned i = 0, e = SharedLibs.size(); i != e; ++i) {
    JITArgs.push_back("-load");
    JITArgs.push_back(SharedLibs[i].c_str());
  }
  JITArgs.push_back(Bitcode.c_str());
  // Add optional parameters to the running program from Argv
  for (unsigned i=0, e = Args.size(); i != e; ++i)
    JITArgs.push_back(Args[i].c_str());
  JITArgs.push_back(nullptr);

  outs() << "<jit>"; outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i=0, e = JITArgs.size()-1; i != e; ++i)
          errs() << " " << JITArgs[i];
        errs() << "\n";
        );
  DEBUG(errs() << "\nSending output to " << OutputFile << "\n");
  return RunProgramWithTimeout(LLIPath, &JITArgs[0],
      InputFile, OutputFile, OutputFile,
      Timeout, MemoryLimit, Error);
}

/// createJIT - Try to find the LLI executable
///
AbstractInterpreter *AbstractInterpreter::createJIT(const char *Argv0,
                   std::string &Message, const std::vector<std::string> *Args) {
  std::string LLIPath =
      PrependMainExecutablePath("lli", Argv0, (void *)(intptr_t) & createJIT);
  if (!LLIPath.empty()) {
    Message = "Found lli: " + LLIPath + "\n";
    return new JIT(LLIPath, Args);
  }

  Message = "Cannot find `lli' in executable directory!\n";
  return nullptr;
}

//===---------------------------------------------------------------------===//
// CC abstraction
//

static bool IsARMArchitecture(std::vector<const char*> Args) {
  for (std::vector<const char*>::const_iterator
         I = Args.begin(), E = Args.end(); I != E; ++I) {
    if (StringRef(*I).equals_lower("-arch")) {
      ++I;
      if (I != E && StringRef(*I).startswith_lower("arm"))
        return true;
    }
  }

  return false;
}

int CC::ExecuteProgram(const std::string &ProgramFile,
                        const std::vector<std::string> &Args,
                        FileType fileType,
                        const std::string &InputFile,
                        const std::string &OutputFile,
                        std::string *Error,
                        const std::vector<std::string> &ArgsForCC,
                        unsigned Timeout,
                        unsigned MemoryLimit) {
  std::vector<const char*> CCArgs;

  CCArgs.push_back(CCPath.c_str());

  if (TargetTriple.getArch() == Triple::x86)
    CCArgs.push_back("-m32");

  for (std::vector<std::string>::const_iterator
         I = ccArgs.begin(), E = ccArgs.end(); I != E; ++I)
    CCArgs.push_back(I->c_str());

  // Specify -x explicitly in case the extension is wonky
  if (fileType != ObjectFile) {
    CCArgs.push_back("-x");
    if (fileType == CFile) {
      CCArgs.push_back("c");
      CCArgs.push_back("-fno-strict-aliasing");
    } else {
      CCArgs.push_back("assembler");

      // For ARM architectures we don't want this flag. bugpoint isn't
      // explicitly told what architecture it is working on, so we get
      // it from cc flags
      if (TargetTriple.isOSDarwin() && !IsARMArchitecture(CCArgs))
        CCArgs.push_back("-force_cpusubtype_ALL");
    }
  }

  CCArgs.push_back(ProgramFile.c_str());  // Specify the input filename.

  CCArgs.push_back("-x");
  CCArgs.push_back("none");
  CCArgs.push_back("-o");

  SmallString<128> OutputBinary;
  std::error_code EC =
      sys::fs::createUniqueFile(ProgramFile + "-%%%%%%%.cc.exe", OutputBinary);
  if (EC) {
    errs() << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }
  CCArgs.push_back(OutputBinary.c_str()); // Output to the right file...

  // Add any arguments intended for CC. We locate them here because this is
  // most likely -L and -l options that need to come before other libraries but
  // after the source. Other options won't be sensitive to placement on the
  // command line, so this should be safe.
  for (unsigned i = 0, e = ArgsForCC.size(); i != e; ++i)
    CCArgs.push_back(ArgsForCC[i].c_str());

  CCArgs.push_back("-lm");                // Hard-code the math library...
  CCArgs.push_back("-O2");                // Optimize the program a bit...
#if defined (HAVE_LINK_R)
  CCArgs.push_back("-Wl,-R.");            // Search this dir for .so files
#endif
  if (TargetTriple.getArch() == Triple::sparc)
    CCArgs.push_back("-mcpu=v9");
  CCArgs.push_back(nullptr);                    // NULL terminator

  outs() << "<CC>"; outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i = 0, e = CCArgs.size()-1; i != e; ++i)
          errs() << " " << CCArgs[i];
        errs() << "\n";
        );
  if (RunProgramWithTimeout(CCPath, &CCArgs[0], "", "", "")) {
    *Error = ProcessFailure(CCPath, &CCArgs[0]);
    return -1;
  }

  std::vector<const char*> ProgramArgs;

  // Declared here so that the destructor only runs after
  // ProgramArgs is used.
  std::string Exec;

  if (RemoteClientPath.empty())
    ProgramArgs.push_back(OutputBinary.c_str());
  else {
    ProgramArgs.push_back(RemoteClientPath.c_str());
    ProgramArgs.push_back(RemoteHost.c_str());
    if (!RemoteUser.empty()) {
      ProgramArgs.push_back("-l");
      ProgramArgs.push_back(RemoteUser.c_str());
    }
    if (!RemotePort.empty()) {
      ProgramArgs.push_back("-p");
      ProgramArgs.push_back(RemotePort.c_str());
    }
    if (!RemoteExtra.empty()) {
      ProgramArgs.push_back(RemoteExtra.c_str());
    }

    // Full path to the binary. We need to cd to the exec directory because
    // there is a dylib there that the exec expects to find in the CWD
    char* env_pwd = getenv("PWD");
    Exec = "cd ";
    Exec += env_pwd;
    Exec += "; ./";
    Exec += OutputBinary.c_str();
    ProgramArgs.push_back(Exec.c_str());
  }

  // Add optional parameters to the running program from Argv
  for (unsigned i = 0, e = Args.size(); i != e; ++i)
    ProgramArgs.push_back(Args[i].c_str());
  ProgramArgs.push_back(nullptr);                // NULL terminator

  // Now that we have a binary, run it!
  outs() << "<program>"; outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i = 0, e = ProgramArgs.size()-1; i != e; ++i)
          errs() << " " << ProgramArgs[i];
        errs() << "\n";
        );

  FileRemover OutputBinaryRemover(OutputBinary.str(), !SaveTemps);

  if (RemoteClientPath.empty()) {
    DEBUG(errs() << "<run locally>");
    int ExitCode = RunProgramWithTimeout(OutputBinary.str(), &ProgramArgs[0],
                                         InputFile, OutputFile, OutputFile,
                                         Timeout, MemoryLimit, Error);
    // Treat a signal (usually SIGSEGV) or timeout as part of the program output
    // so that crash-causing miscompilation is handled seamlessly.
    if (ExitCode < -1) {
      std::ofstream outFile(OutputFile.c_str(), std::ios_base::app);
      outFile << *Error << '\n';
      outFile.close();
      Error->clear();
    }
    return ExitCode;
  } else {
    outs() << "<run remotely>"; outs().flush();
    return RunProgramRemotelyWithTimeout(RemoteClientPath,
        &ProgramArgs[0], InputFile, OutputFile,
        OutputFile, Timeout, MemoryLimit);
  }
}

int CC::MakeSharedObject(const std::string &InputFile, FileType fileType,
                          std::string &OutputFile,
                          const std::vector<std::string> &ArgsForCC,
                          std::string &Error) {
  SmallString<128> UniqueFilename;
  std::error_code EC = sys::fs::createUniqueFile(
      InputFile + "-%%%%%%%" + LTDL_SHLIB_EXT, UniqueFilename);
  if (EC) {
    errs() << "Error making unique filename: " << EC.message() << "\n";
    exit(1);
  }
  OutputFile = UniqueFilename.str();

  std::vector<const char*> CCArgs;

  CCArgs.push_back(CCPath.c_str());

  if (TargetTriple.getArch() == Triple::x86)
    CCArgs.push_back("-m32");

  for (std::vector<std::string>::const_iterator
         I = ccArgs.begin(), E = ccArgs.end(); I != E; ++I)
    CCArgs.push_back(I->c_str());

  // Compile the C/asm file into a shared object
  if (fileType != ObjectFile) {
    CCArgs.push_back("-x");
    CCArgs.push_back(fileType == AsmFile ? "assembler" : "c");
  }
  CCArgs.push_back("-fno-strict-aliasing");
  CCArgs.push_back(InputFile.c_str());   // Specify the input filename.
  CCArgs.push_back("-x");
  CCArgs.push_back("none");
  if (TargetTriple.getArch() == Triple::sparc)
    CCArgs.push_back("-G");       // Compile a shared library, `-G' for Sparc
  else if (TargetTriple.isOSDarwin()) {
    // link all source files into a single module in data segment, rather than
    // generating blocks. dynamic_lookup requires that you set
    // MACOSX_DEPLOYMENT_TARGET=10.3 in your env.  FIXME: it would be better for
    // bugpoint to just pass that in the environment of CC.
    CCArgs.push_back("-single_module");
    CCArgs.push_back("-dynamiclib");   // `-dynamiclib' for MacOS X/PowerPC
    CCArgs.push_back("-undefined");
    CCArgs.push_back("dynamic_lookup");
  } else
    CCArgs.push_back("-shared");  // `-shared' for Linux/X86, maybe others

  if (TargetTriple.getArch() == Triple::x86_64)
    CCArgs.push_back("-fPIC");   // Requires shared objs to contain PIC

  if (TargetTriple.getArch() == Triple::sparc)
    CCArgs.push_back("-mcpu=v9");

  CCArgs.push_back("-o");
  CCArgs.push_back(OutputFile.c_str()); // Output to the right filename.
  CCArgs.push_back("-O2");              // Optimize the program a bit.



  // Add any arguments intended for CC. We locate them here because this is
  // most likely -L and -l options that need to come before other libraries but
  // after the source. Other options won't be sensitive to placement on the
  // command line, so this should be safe.
  for (unsigned i = 0, e = ArgsForCC.size(); i != e; ++i)
    CCArgs.push_back(ArgsForCC[i].c_str());
  CCArgs.push_back(nullptr);                    // NULL terminator



  outs() << "<CC>"; outs().flush();
  DEBUG(errs() << "\nAbout to run:\t";
        for (unsigned i = 0, e = CCArgs.size()-1; i != e; ++i)
          errs() << " " << CCArgs[i];
        errs() << "\n";
        );
  if (RunProgramWithTimeout(CCPath, &CCArgs[0], "", "", "")) {
    Error = ProcessFailure(CCPath, &CCArgs[0]);
    return 1;
  }
  return 0;
}

/// create - Try to find the CC executable
///
CC *CC::create(std::string &Message,
                 const std::string &CCBinary,
                 const std::vector<std::string> *Args) {
  auto CCPath = sys::findProgramByName(CCBinary);
  if (!CCPath) {
    Message = "Cannot find `" + CCBinary + "' in PATH: " +
              CCPath.getError().message() + "\n";
    return nullptr;
  }

  std::string RemoteClientPath;
  if (!RemoteClient.empty()) {
    auto Path = sys::findProgramByName(RemoteClient);
    if (!Path) {
      Message = "Cannot find `" + RemoteClient + "' in PATH: " +
                Path.getError().message() + "\n";
      return nullptr;
    }
    RemoteClientPath = *Path;
  }

  Message = "Found CC: " + *CCPath + "\n";
  return new CC(*CCPath, RemoteClientPath, Args);
}
