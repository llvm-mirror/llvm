//===-- GraphWriter.cpp - Implements GraphWriter support routines ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements misc. GraphWriter support routines.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/GraphWriter.h"
#include "llvm/Config/config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
using namespace llvm;

static cl::opt<bool> ViewBackground("view-background", cl::Hidden,
  cl::desc("Execute graph viewer in the background. Creates tmp file litter."));

std::string llvm::DOT::EscapeString(const std::string &Label) {
  std::string Str(Label);
  for (unsigned i = 0; i != Str.length(); ++i)
  switch (Str[i]) {
    case '\n':
      Str.insert(Str.begin()+i, '\\');  // Escape character...
      ++i;
      Str[i] = 'n';
      break;
    case '\t':
      Str.insert(Str.begin()+i, ' ');  // Convert to two spaces
      ++i;
      Str[i] = ' ';
      break;
    case '\\':
      if (i+1 != Str.length())
        switch (Str[i+1]) {
          case 'l': continue; // don't disturb \l
          case '|': case '{': case '}':
            Str.erase(Str.begin()+i); continue;
          default: break;
        }
    case '{': case '}':
    case '<': case '>':
    case '|': case '"':
      Str.insert(Str.begin()+i, '\\');  // Escape character...
      ++i;  // don't infinite loop
      break;
  }
  return Str;
}

/// \brief Get a color string for this node number. Simply round-robin selects
/// from a reasonable number of colors.
StringRef llvm::DOT::getColorString(unsigned ColorNumber) {
  static const int NumColors = 20;
  static const char* Colors[NumColors] = {
    "aaaaaa", "aa0000", "00aa00", "aa5500", "0055ff", "aa00aa", "00aaaa",
    "555555", "ff5555", "55ff55", "ffff55", "5555ff", "ff55ff", "55ffff",
    "ffaaaa", "aaffaa", "ffffaa", "aaaaff", "ffaaff", "aaffff"};
  return Colors[ColorNumber % NumColors];
}

std::string llvm::createGraphFilename(const Twine &Name, int &FD) {
  FD = -1;
  SmallString<128> Filename;
  std::error_code EC = sys::fs::createTemporaryFile(Name, "dot", FD, Filename);
  if (EC) {
    errs() << "Error: " << EC.message() << "\n";
    return "";
  }

  errs() << "Writing '" << Filename << "'... ";
  return Filename.str();
}

// Execute the graph viewer. Return true if there were errors.
static bool ExecGraphViewer(StringRef ExecPath, std::vector<const char *> &args,
                            StringRef Filename, bool wait,
                            std::string &ErrMsg) {
  assert(args.back() == nullptr);
  if (wait) {
    if (sys::ExecuteAndWait(ExecPath, args.data(), nullptr, nullptr, 0, 0,
                            &ErrMsg)) {
      errs() << "Error: " << ErrMsg << "\n";
      return true;
    }
    sys::fs::remove(Filename);
    errs() << " done. \n";
  } else {
    sys::ExecuteNoWait(ExecPath, args.data(), nullptr, nullptr, 0, &ErrMsg);
    errs() << "Remember to erase graph file: " << Filename << "\n";
  }
  return false;
}

namespace {
struct GraphSession {
  std::string LogBuffer;
  bool TryFindProgram(StringRef Names, std::string &ProgramPath) {
    raw_string_ostream Log(LogBuffer);
    SmallVector<StringRef, 8> parts;
    Names.split(parts, '|');
    for (auto Name : parts) {
      if (ErrorOr<std::string> P = sys::findProgramByName(Name)) {
        ProgramPath = *P;
        return true;
      }
      Log << "  Tried '" << Name << "'\n";
    }
    return false;
  }
};
} // namespace

static const char *getProgramName(GraphProgram::Name program) {
  switch (program) {
  case GraphProgram::DOT:
    return "dot";
  case GraphProgram::FDP:
    return "fdp";
  case GraphProgram::NEATO:
    return "neato";
  case GraphProgram::TWOPI:
    return "twopi";
  case GraphProgram::CIRCO:
    return "circo";
  }
  llvm_unreachable("bad kind");
}

bool llvm::DisplayGraph(StringRef FilenameRef, bool wait,
                        GraphProgram::Name program) {
  std::string Filename = FilenameRef;
  std::string ErrMsg;
  std::string ViewerPath;
  GraphSession S;

#ifdef __APPLE__
  wait &= !ViewBackground;
  if (S.TryFindProgram("open", ViewerPath)) {
    std::vector<const char *> args;
    args.push_back(ViewerPath.c_str());
    if (wait)
      args.push_back("-W");
    args.push_back(Filename.c_str());
    args.push_back(nullptr);
    errs() << "Trying 'open' program... ";
    if (!ExecGraphViewer(ViewerPath, args, Filename, wait, ErrMsg))
      return false;
  }
#endif
  if (S.TryFindProgram("xdg-open", ViewerPath)) {
    std::vector<const char *> args;
    args.push_back(ViewerPath.c_str());
    args.push_back(Filename.c_str());
    args.push_back(nullptr);
    errs() << "Trying 'xdg-open' program... ";
    if (!ExecGraphViewer(ViewerPath, args, Filename, wait, ErrMsg))
      return false;
  }

  // Graphviz
  if (S.TryFindProgram("Graphviz", ViewerPath)) {
    std::vector<const char *> args;
    args.push_back(ViewerPath.c_str());
    args.push_back(Filename.c_str());
    args.push_back(nullptr);

    errs() << "Running 'Graphviz' program... ";
    return ExecGraphViewer(ViewerPath, args, Filename, wait, ErrMsg);
  }

  // xdot
  if (S.TryFindProgram("xdot|xdot.py", ViewerPath)) {
    std::vector<const char *> args;
    args.push_back(ViewerPath.c_str());
    args.push_back(Filename.c_str());

    args.push_back("-f");
    args.push_back(getProgramName(program));

    args.push_back(nullptr);

    errs() << "Running 'xdot.py' program... ";
    return ExecGraphViewer(ViewerPath, args, Filename, wait, ErrMsg);
  }

  enum ViewerKind {
    VK_None,
    VK_OSXOpen,
    VK_XDGOpen,
    VK_Ghostview,
    VK_CmdStart
  };
  ViewerKind Viewer = VK_None;
#ifdef __APPLE__
  if (!Viewer && S.TryFindProgram("open", ViewerPath))
    Viewer = VK_OSXOpen;
#endif
  if (!Viewer && S.TryFindProgram("gv", ViewerPath))
    Viewer = VK_Ghostview;
  if (!Viewer && S.TryFindProgram("xdg-open", ViewerPath))
    Viewer = VK_XDGOpen;
#ifdef LLVM_ON_WIN32
  if (!Viewer && S.TryFindProgram("cmd", ViewerPath)) {
    Viewer = VK_CmdStart;
  }
#endif

  // PostScript or PDF graph generator + PostScript/PDF viewer
  std::string GeneratorPath;
  if (Viewer &&
      (S.TryFindProgram(getProgramName(program), GeneratorPath) ||
       S.TryFindProgram("dot|fdp|neato|twopi|circo", GeneratorPath))) {
    std::string OutputFilename =
        Filename + (Viewer == VK_CmdStart ? ".pdf" : ".ps");

    std::vector<const char *> args;
    args.push_back(GeneratorPath.c_str());
    if (Viewer == VK_CmdStart)
      args.push_back("-Tpdf");
    else
      args.push_back("-Tps");
    args.push_back("-Nfontname=Courier");
    args.push_back("-Gsize=7.5,10");
    args.push_back(Filename.c_str());
    args.push_back("-o");
    args.push_back(OutputFilename.c_str());
    args.push_back(nullptr);

    errs() << "Running '" << GeneratorPath << "' program... ";

    if (ExecGraphViewer(GeneratorPath, args, Filename, true, ErrMsg))
      return true;

    // The lifetime of StartArg must include the call of ExecGraphViewer
    // because the args are passed as vector of char*.
    std::string StartArg;

    args.clear();
    args.push_back(ViewerPath.c_str());
    switch (Viewer) {
    case VK_OSXOpen:
      args.push_back("-W");
      args.push_back(OutputFilename.c_str());
      break;
    case VK_XDGOpen:
      wait = false;
      args.push_back(OutputFilename.c_str());
      break;
    case VK_Ghostview:
      args.push_back("--spartan");
      args.push_back(OutputFilename.c_str());
      break;
    case VK_CmdStart:
      args.push_back("/S");
      args.push_back("/C");
      StartArg =
          (StringRef("start ") + (wait ? "/WAIT " : "") + OutputFilename).str();
      args.push_back(StartArg.c_str());
      break;
    case VK_None:
      llvm_unreachable("Invalid viewer");
    }
    args.push_back(nullptr);

    ErrMsg.clear();
    return ExecGraphViewer(ViewerPath, args, OutputFilename, wait, ErrMsg);
  }

  // dotty
  if (S.TryFindProgram("dotty", ViewerPath)) {
    std::vector<const char *> args;
    args.push_back(ViewerPath.c_str());
    args.push_back(Filename.c_str());
    args.push_back(nullptr);

// Dotty spawns another app and doesn't wait until it returns
#ifdef LLVM_ON_WIN32
    wait = false;
#endif
    errs() << "Running 'dotty' program... ";
    return ExecGraphViewer(ViewerPath, args, Filename, wait, ErrMsg);
  }

  errs() << "Error: Couldn't find a usable graph viewer program:\n";
  errs() << S.LogBuffer << "\n";
  return true;
}
