//===- ExecutionDriver.cpp - Allow execution of LLVM program --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code used to execute the program utilizing one of the
// various ways of running LLVM bitcode.
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ToolRunner.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>

using namespace llvm;

namespace {
  // OutputType - Allow the user to specify the way code should be run, to test
  // for miscompilation.
  //
  enum OutputType {
    AutoPick, RunLLI, RunJIT, RunLLC, RunLLCIA, LLC_Safe, CompileCustom, Custom
  };

  cl::opt<double>
  AbsTolerance("abs-tolerance", cl::desc("Absolute error tolerated"),
               cl::init(0.0));
  cl::opt<double>
  RelTolerance("rel-tolerance", cl::desc("Relative error tolerated"),
               cl::init(0.0));

  cl::opt<OutputType>
  InterpreterSel(cl::desc("Specify the \"test\" i.e. suspect back-end:"),
                 cl::values(clEnumValN(AutoPick, "auto", "Use best guess"),
                            clEnumValN(RunLLI, "run-int",
                                       "Execute with the interpreter"),
                            clEnumValN(RunJIT, "run-jit", "Execute with JIT"),
                            clEnumValN(RunLLC, "run-llc", "Compile with LLC"),
                            clEnumValN(RunLLCIA, "run-llc-ia",
                                  "Compile with LLC with integrated assembler"),
                            clEnumValN(LLC_Safe, "llc-safe", "Use LLC for all"),
                            clEnumValN(CompileCustom, "compile-custom",
                            "Use -compile-command to define a command to "
                            "compile the bitcode. Useful to avoid linking."),
                            clEnumValN(Custom, "run-custom",
                            "Use -exec-command to define a command to execute "
                            "the bitcode. Useful for cross-compilation."),
                            clEnumValEnd),
                 cl::init(AutoPick));

  cl::opt<OutputType>
  SafeInterpreterSel(cl::desc("Specify \"safe\" i.e. known-good backend:"),
              cl::values(clEnumValN(AutoPick, "safe-auto", "Use best guess"),
                         clEnumValN(RunLLC, "safe-run-llc", "Compile with LLC"),
                         clEnumValN(Custom, "safe-run-custom",
                         "Use -exec-command to define a command to execute "
                         "the bitcode. Useful for cross-compilation."),
                         clEnumValEnd),
                     cl::init(AutoPick));

  cl::opt<std::string>
  SafeInterpreterPath("safe-path",
                   cl::desc("Specify the path to the \"safe\" backend program"),
                   cl::init(""));

  cl::opt<bool>
  AppendProgramExitCode("append-exit-code",
      cl::desc("Append the exit code to the output so it gets diff'd too"),
      cl::init(false));

  cl::opt<std::string>
  InputFile("input", cl::init("/dev/null"),
            cl::desc("Filename to pipe in as stdin (default: /dev/null)"));

  cl::list<std::string>
  AdditionalSOs("additional-so",
                cl::desc("Additional shared objects to load "
                         "into executing programs"));

  cl::list<std::string>
  AdditionalLinkerArgs("Xlinker",
      cl::desc("Additional arguments to pass to the linker"));

  cl::opt<std::string>
  CustomCompileCommand("compile-command", cl::init("llc"),
      cl::desc("Command to compile the bitcode (use with -compile-custom) "
               "(default: llc)"));

  cl::opt<std::string>
  CustomExecCommand("exec-command", cl::init("simulate"),
      cl::desc("Command to execute the bitcode (use with -run-custom) "
               "(default: simulate)"));
}

namespace llvm {
  // Anything specified after the --args option are taken as arguments to the
  // program being debugged.
  cl::list<std::string>
  InputArgv("args", cl::Positional, cl::desc("<program arguments>..."),
            cl::ZeroOrMore, cl::PositionalEatsArgs);

  cl::opt<std::string>
  OutputPrefix("output-prefix", cl::init("bugpoint"),
            cl::desc("Prefix to use for outputs (default: 'bugpoint')"));
}

namespace {
  cl::list<std::string>
  ToolArgv("tool-args", cl::Positional, cl::desc("<tool arguments>..."),
           cl::ZeroOrMore, cl::PositionalEatsArgs);

  cl::list<std::string>
  SafeToolArgv("safe-tool-args", cl::Positional,
               cl::desc("<safe-tool arguments>..."),
               cl::ZeroOrMore, cl::PositionalEatsArgs);

  cl::opt<std::string>
  GCCBinary("gcc", cl::init("gcc"),
              cl::desc("The gcc binary to use. (default 'gcc')"));

  cl::list<std::string>
  GCCToolArgv("gcc-tool-args", cl::Positional,
              cl::desc("<gcc-tool arguments>..."),
              cl::ZeroOrMore, cl::PositionalEatsArgs);
}

//===----------------------------------------------------------------------===//
// BugDriver method implementation
//

/// initializeExecutionEnvironment - This method is used to set up the
/// environment for executing LLVM programs.
///
bool BugDriver::initializeExecutionEnvironment() {
  outs() << "Initializing execution environment: ";

  // Create an instance of the AbstractInterpreter interface as specified on
  // the command line
  SafeInterpreter = 0;
  std::string Message;

  switch (InterpreterSel) {
  case AutoPick:
    if (!Interpreter) {
      InterpreterSel = RunJIT;
      Interpreter = AbstractInterpreter::createJIT(getToolName(), Message,
                                                   &ToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = RunLLC;
      Interpreter = AbstractInterpreter::createLLC(getToolName(), Message,
                                                   GCCBinary, &ToolArgv,
                                                   &GCCToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = RunLLI;
      Interpreter = AbstractInterpreter::createLLI(getToolName(), Message,
                                                   &ToolArgv);
    }
    if (!Interpreter) {
      InterpreterSel = AutoPick;
      Message = "Sorry, I can't automatically select an interpreter!\n";
    }
    break;
  case RunLLI:
    Interpreter = AbstractInterpreter::createLLI(getToolName(), Message,
                                                 &ToolArgv);
    break;
  case RunLLC:
  case RunLLCIA:
  case LLC_Safe:
    Interpreter = AbstractInterpreter::createLLC(getToolName(), Message,
                                                 GCCBinary, &ToolArgv,
                                                 &GCCToolArgv,
                                                 InterpreterSel == RunLLCIA);
    break;
  case RunJIT:
    Interpreter = AbstractInterpreter::createJIT(getToolName(), Message,
                                                 &ToolArgv);
    break;
  case CompileCustom:
    Interpreter =
      AbstractInterpreter::createCustomCompiler(Message, CustomCompileCommand);
    break;
  case Custom:
    Interpreter =
      AbstractInterpreter::createCustomExecutor(Message, CustomExecCommand);
    break;
  }
  if (!Interpreter)
    errs() << Message;
  else // Display informational messages on stdout instead of stderr
    outs() << Message;

  std::string Path = SafeInterpreterPath;
  if (Path.empty())
    Path = getToolName();
  std::vector<std::string> SafeToolArgs = SafeToolArgv;
  switch (SafeInterpreterSel) {
  case AutoPick:
    // In "llc-safe" mode, default to using LLC as the "safe" backend.
    if (!SafeInterpreter &&
        InterpreterSel == LLC_Safe) {
      SafeInterpreterSel = RunLLC;
      SafeToolArgs.push_back("--relocation-model=pic");
      SafeInterpreter = AbstractInterpreter::createLLC(Path.c_str(), Message,
                                                       GCCBinary,
                                                       &SafeToolArgs,
                                                       &GCCToolArgv);
    }

    if (!SafeInterpreter &&
        InterpreterSel != RunLLC &&
        InterpreterSel != RunJIT) {
      SafeInterpreterSel = RunLLC;
      SafeToolArgs.push_back("--relocation-model=pic");
      SafeInterpreter = AbstractInterpreter::createLLC(Path.c_str(), Message,
                                                       GCCBinary,
                                                       &SafeToolArgs,
                                                       &GCCToolArgv);
    }
    if (!SafeInterpreter) {
      SafeInterpreterSel = AutoPick;
      Message = "Sorry, I can't automatically select a safe interpreter!\n";
    }
    break;
  case RunLLC:
  case RunLLCIA:
    SafeToolArgs.push_back("--relocation-model=pic");
    SafeInterpreter = AbstractInterpreter::createLLC(Path.c_str(), Message,
                                                     GCCBinary, &SafeToolArgs,
                                                     &GCCToolArgv,
                                                SafeInterpreterSel == RunLLCIA);
    break;
  case Custom:
    SafeInterpreter =
      AbstractInterpreter::createCustomExecutor(Message, CustomExecCommand);
    break;
  default:
    Message = "Sorry, this back-end is not supported by bugpoint as the "
              "\"safe\" backend right now!\n";
    break;
  }
  if (!SafeInterpreter) { outs() << Message << "\nExiting.\n"; exit(1); }

  gcc = GCC::create(Message, GCCBinary, &GCCToolArgv);
  if (!gcc) { outs() << Message << "\nExiting.\n"; exit(1); }

  // If there was an error creating the selected interpreter, quit with error.
  return Interpreter == 0;
}

/// compileProgram - Try to compile the specified module, returning false and
/// setting Error if an error occurs.  This is used for code generation
/// crash testing.
///
void BugDriver::compileProgram(Module *M, std::string *Error) const {
  // Emit the program to a bitcode file...
  SmallString<128> BitcodeFile;
  int BitcodeFD;
  error_code EC = sys::fs::createUniqueFile(
      OutputPrefix + "-test-program-%%%%%%%.bc", BitcodeFD, BitcodeFile);
  if (EC) {
    errs() << ToolName << ": Error making unique filename: " << EC.message()
           << "\n";
    exit(1);
  }
  if (writeProgramToFile(BitcodeFile.str(), BitcodeFD, M)) {
    errs() << ToolName << ": Error emitting bitcode to file '" << BitcodeFile
           << "'!\n";
    exit(1);
  }

  // Remove the temporary bitcode file when we are done.
  FileRemover BitcodeFileRemover(BitcodeFile.str(), !SaveTemps);

  // Actually compile the program!
  Interpreter->compileProgram(BitcodeFile.str(), Error, Timeout, MemoryLimit);
}


/// executeProgram - This method runs "Program", capturing the output of the
/// program to a file, returning the filename of the file.  A recommended
/// filename may be optionally specified.
///
std::string BugDriver::executeProgram(const Module *Program,
                                      std::string OutputFile,
                                      std::string BitcodeFile,
                                      const std::string &SharedObj,
                                      AbstractInterpreter *AI,
                                      std::string *Error) const {
  if (AI == 0) AI = Interpreter;
  assert(AI && "Interpreter should have been created already!");
  bool CreatedBitcode = false;
  std::string ErrMsg;
  if (BitcodeFile.empty()) {
    // Emit the program to a bitcode file...
    SmallString<128> UniqueFilename;
    int UniqueFD;
    error_code EC = sys::fs::createUniqueFile(
        OutputPrefix + "-test-program-%%%%%%%.bc", UniqueFD, UniqueFilename);
    if (EC) {
      errs() << ToolName << ": Error making unique filename: "
             << EC.message() << "!\n";
      exit(1);
    }
    BitcodeFile = UniqueFilename.str();

    if (writeProgramToFile(BitcodeFile, UniqueFD, Program)) {
      errs() << ToolName << ": Error emitting bitcode to file '"
             << BitcodeFile << "'!\n";
      exit(1);
    }
    CreatedBitcode = true;
  }

  // Remove the temporary bitcode file when we are done.
  std::string BitcodePath(BitcodeFile);
  FileRemover BitcodeFileRemover(BitcodePath,
    CreatedBitcode && !SaveTemps);

  if (OutputFile.empty()) OutputFile = OutputPrefix + "-execution-output-%%%%%%%";

  // Check to see if this is a valid output filename...
  SmallString<128> UniqueFile;
  error_code EC = sys::fs::createUniqueFile(OutputFile, UniqueFile);
  if (EC) {
    errs() << ToolName << ": Error making unique filename: "
           << EC.message() << "\n";
    exit(1);
  }
  OutputFile = UniqueFile.str();

  // Figure out which shared objects to run, if any.
  std::vector<std::string> SharedObjs(AdditionalSOs);
  if (!SharedObj.empty())
    SharedObjs.push_back(SharedObj);

  int RetVal = AI->ExecuteProgram(BitcodeFile, InputArgv, InputFile, OutputFile,
                                  Error, AdditionalLinkerArgs, SharedObjs,
                                  Timeout, MemoryLimit);
  if (!Error->empty())
    return OutputFile;

  if (RetVal == -1) {
    errs() << "<timeout>";
    static bool FirstTimeout = true;
    if (FirstTimeout) {
      outs() << "\n"
 "*** Program execution timed out!  This mechanism is designed to handle\n"
 "    programs stuck in infinite loops gracefully.  The -timeout option\n"
 "    can be used to change the timeout threshold or disable it completely\n"
 "    (with -timeout=0).  This message is only displayed once.\n";
      FirstTimeout = false;
    }
  }

  if (AppendProgramExitCode) {
    std::ofstream outFile(OutputFile.c_str(), std::ios_base::app);
    outFile << "exit " << RetVal << '\n';
    outFile.close();
  }

  // Return the filename we captured the output to.
  return OutputFile;
}

/// executeProgramSafely - Used to create reference output with the "safe"
/// backend, if reference output is not provided.
///
std::string BugDriver::executeProgramSafely(const Module *Program,
                                            std::string OutputFile,
                                            std::string *Error) const {
  return executeProgram(Program, OutputFile, "", "", SafeInterpreter, Error);
}

std::string BugDriver::compileSharedObject(const std::string &BitcodeFile,
                                           std::string &Error) {
  assert(Interpreter && "Interpreter should have been created already!");
  std::string OutputFile;

  // Using the known-good backend.
  GCC::FileType FT = SafeInterpreter->OutputCode(BitcodeFile, OutputFile,
                                                 Error);
  if (!Error.empty())
    return "";

  std::string SharedObjectFile;
  bool Failure = gcc->MakeSharedObject(OutputFile, FT, SharedObjectFile,
                                       AdditionalLinkerArgs, Error);
  if (!Error.empty())
    return "";
  if (Failure)
    exit(1);

  // Remove the intermediate C file
  sys::fs::remove(OutputFile);

  return "./" + SharedObjectFile;
}

/// createReferenceFile - calls compileProgram and then records the output
/// into ReferenceOutputFile. Returns true if reference file created, false
/// otherwise. Note: initializeExecutionEnvironment should be called BEFORE
/// this function.
///
bool BugDriver::createReferenceFile(Module *M, const std::string &Filename) {
  std::string Error;
  compileProgram(Program, &Error);
  if (!Error.empty())
    return false;

  ReferenceOutputFile = executeProgramSafely(Program, Filename, &Error);
  if (!Error.empty()) {
    errs() << Error;
    if (Interpreter != SafeInterpreter) {
      errs() << "*** There is a bug running the \"safe\" backend.  Either"
             << " debug it (for example with the -run-jit bugpoint option,"
             << " if JIT is being used as the \"safe\" backend), or fix the"
             << " error some other way.\n";
    }
    return false;
  }
  outs() << "\nReference output is: " << ReferenceOutputFile << "\n\n";
  return true;
}

/// diffProgram - This method executes the specified module and diffs the
/// output against the file specified by ReferenceOutputFile.  If the output
/// is different, 1 is returned.  If there is a problem with the code
/// generator (e.g., llc crashes), this will set ErrMsg.
///
bool BugDriver::diffProgram(const Module *Program,
                            const std::string &BitcodeFile,
                            const std::string &SharedObject,
                            bool RemoveBitcode,
                            std::string *ErrMsg) const {
  // Execute the program, generating an output file...
  std::string Output(
      executeProgram(Program, "", BitcodeFile, SharedObject, 0, ErrMsg));
  if (!ErrMsg->empty())
    return false;

  std::string Error;
  bool FilesDifferent = false;
  if (int Diff = DiffFilesWithTolerance(ReferenceOutputFile,
                                        Output,
                                        AbsTolerance, RelTolerance, &Error)) {
    if (Diff == 2) {
      errs() << "While diffing output: " << Error << '\n';
      exit(1);
    }
    FilesDifferent = true;
  }
  else {
    // Remove the generated output if there are no differences.
    sys::fs::remove(Output);
  }

  // Remove the bitcode file if we are supposed to.
  if (RemoveBitcode)
    sys::fs::remove(BitcodeFile);
  return FilesDifferent;
}

bool BugDriver::isExecutingJIT() {
  return InterpreterSel == RunJIT;
}

