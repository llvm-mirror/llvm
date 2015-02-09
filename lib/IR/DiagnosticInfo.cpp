//===- llvm/Support/DiagnosticInfo.cpp - Diagnostic Definitions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#include "LLVMContextImpl.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Atomic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include <string>

using namespace llvm;

namespace {

/// \brief Regular expression corresponding to the value given in one of the
/// -pass-remarks* command line flags. Passes whose name matches this regexp
/// will emit a diagnostic when calling the associated diagnostic function
/// (emitOptimizationRemark, emitOptimizationRemarkMissed or
/// emitOptimizationRemarkAnalysis).
struct PassRemarksOpt {
  std::shared_ptr<Regex> Pattern;

  void operator=(const std::string &Val) {
    // Create a regexp object to match pass names for emitOptimizationRemark.
    if (!Val.empty()) {
      Pattern = std::make_shared<Regex>(Val);
      std::string RegexError;
      if (!Pattern->isValid(RegexError))
        report_fatal_error("Invalid regular expression '" + Val +
                               "' in -pass-remarks: " + RegexError,
                           false);
    }
  };
};

static PassRemarksOpt PassRemarksOptLoc;
static PassRemarksOpt PassRemarksMissedOptLoc;
static PassRemarksOpt PassRemarksAnalysisOptLoc;

// -pass-remarks
//    Command line flag to enable emitOptimizationRemark()
static cl::opt<PassRemarksOpt, true, cl::parser<std::string>>
PassRemarks("pass-remarks", cl::value_desc("pattern"),
            cl::desc("Enable optimization remarks from passes whose name match "
                     "the given regular expression"),
            cl::Hidden, cl::location(PassRemarksOptLoc), cl::ValueRequired,
            cl::ZeroOrMore);

// -pass-remarks-missed
//    Command line flag to enable emitOptimizationRemarkMissed()
static cl::opt<PassRemarksOpt, true, cl::parser<std::string>> PassRemarksMissed(
    "pass-remarks-missed", cl::value_desc("pattern"),
    cl::desc("Enable missed optimization remarks from passes whose name match "
             "the given regular expression"),
    cl::Hidden, cl::location(PassRemarksMissedOptLoc), cl::ValueRequired,
    cl::ZeroOrMore);

// -pass-remarks-analysis
//    Command line flag to enable emitOptimizationRemarkAnalysis()
static cl::opt<PassRemarksOpt, true, cl::parser<std::string>>
PassRemarksAnalysis(
    "pass-remarks-analysis", cl::value_desc("pattern"),
    cl::desc(
        "Enable optimization analysis remarks from passes whose name match "
        "the given regular expression"),
    cl::Hidden, cl::location(PassRemarksAnalysisOptLoc), cl::ValueRequired,
    cl::ZeroOrMore);
}

int llvm::getNextAvailablePluginDiagnosticKind() {
  static sys::cas_flag PluginKindID = DK_FirstPluginKind;
  return (int)sys::AtomicIncrement(&PluginKindID);
}

DiagnosticInfoInlineAsm::DiagnosticInfoInlineAsm(const Instruction &I,
                                                 const Twine &MsgStr,
                                                 DiagnosticSeverity Severity)
    : DiagnosticInfo(DK_InlineAsm, Severity), LocCookie(0), MsgStr(MsgStr),
      Instr(&I) {
  if (const MDNode *SrcLoc = I.getMetadata("srcloc")) {
    if (SrcLoc->getNumOperands() != 0)
      if (const auto *CI =
              mdconst::dyn_extract<ConstantInt>(SrcLoc->getOperand(0)))
        LocCookie = CI->getZExtValue();
  }
}

void DiagnosticInfoInlineAsm::print(DiagnosticPrinter &DP) const {
  DP << getMsgStr();
  if (getLocCookie())
    DP << " at line " << getLocCookie();
}

void DiagnosticInfoStackSize::print(DiagnosticPrinter &DP) const {
  DP << "stack size limit exceeded (" << getStackSize() << ") in "
     << getFunction();
}

void DiagnosticInfoDebugMetadataVersion::print(DiagnosticPrinter &DP) const {
  DP << "ignoring debug info with an invalid version (" << getMetadataVersion()
     << ") in " << getModule();
}

void DiagnosticInfoSampleProfile::print(DiagnosticPrinter &DP) const {
  if (getFileName() && getLineNum() > 0)
    DP << getFileName() << ":" << getLineNum() << ": ";
  else if (getFileName())
    DP << getFileName() << ": ";
  DP << getMsg();
}

bool DiagnosticInfoOptimizationBase::isLocationAvailable() const {
  return getDebugLoc().isUnknown() == false;
}

void DiagnosticInfoOptimizationBase::getLocation(StringRef *Filename,
                                                 unsigned *Line,
                                                 unsigned *Column) const {
  DILocation DIL(getDebugLoc().getAsMDNode(getFunction().getContext()));
  *Filename = DIL.getFilename();
  *Line = DIL.getLineNumber();
  *Column = DIL.getColumnNumber();
}

const std::string DiagnosticInfoOptimizationBase::getLocationStr() const {
  StringRef Filename("<unknown>");
  unsigned Line = 0;
  unsigned Column = 0;
  if (isLocationAvailable())
    getLocation(&Filename, &Line, &Column);
  return Twine(Filename + ":" + Twine(Line) + ":" + Twine(Column)).str();
}

void DiagnosticInfoOptimizationBase::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getMsg();
}

bool DiagnosticInfoOptimizationRemark::isEnabled() const {
  return PassRemarksOptLoc.Pattern &&
         PassRemarksOptLoc.Pattern->match(getPassName());
}

bool DiagnosticInfoOptimizationRemarkMissed::isEnabled() const {
  return PassRemarksMissedOptLoc.Pattern &&
         PassRemarksMissedOptLoc.Pattern->match(getPassName());
}

bool DiagnosticInfoOptimizationRemarkAnalysis::isEnabled() const {
  return PassRemarksAnalysisOptLoc.Pattern &&
         PassRemarksAnalysisOptLoc.Pattern->match(getPassName());
}

void llvm::emitOptimizationRemark(LLVMContext &Ctx, const char *PassName,
                                  const Function &Fn, const DebugLoc &DLoc,
                                  const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationRemark(PassName, Fn, DLoc, Msg));
}

void llvm::emitOptimizationRemarkMissed(LLVMContext &Ctx, const char *PassName,
                                        const Function &Fn,
                                        const DebugLoc &DLoc,
                                        const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationRemarkMissed(PassName, Fn, DLoc, Msg));
}

void llvm::emitOptimizationRemarkAnalysis(LLVMContext &Ctx,
                                          const char *PassName,
                                          const Function &Fn,
                                          const DebugLoc &DLoc,
                                          const Twine &Msg) {
  Ctx.diagnose(
      DiagnosticInfoOptimizationRemarkAnalysis(PassName, Fn, DLoc, Msg));
}

bool DiagnosticInfoOptimizationFailure::isEnabled() const {
  // Only print warnings.
  return getSeverity() == DS_Warning;
}

void llvm::emitLoopVectorizeWarning(LLVMContext &Ctx, const Function &Fn,
                                    const DebugLoc &DLoc, const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationFailure(
      Fn, DLoc, Twine("loop not vectorized: " + Msg)));
}

void llvm::emitLoopInterleaveWarning(LLVMContext &Ctx, const Function &Fn,
                                     const DebugLoc &DLoc, const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationFailure(
      Fn, DLoc, Twine("loop not interleaved: " + Msg)));
}
