//===-- rvexSubtarget.cpp - rvex Subtarget Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the rvex specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "rvexSubtarget.h"
#include "rvex.h"
#include "rvexRegisterInfo.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "rvexGenSubtargetInfo.inc"

using namespace llvm;

static cl::opt<bool>
IsLinuxOpt("rvex-islinux-format", cl::Hidden, cl::init(true),
                 cl::desc("Always use linux format."));

void rvexSubtarget::anchor() { }

rvexSubtarget::rvexSubtarget(const std::string &TT, const std::string &CPU,
                             const std::string &FS, bool little) :
  rvexGenSubtargetInfo(TT, CPU, FS),
  rvexABI(UnknownABI), IsLittle(little), IsLinux(IsLinuxOpt)
{
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "rvex32";

  // Parse features string.
  ParseSubtargetFeatures(CPUName, FS);

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUName);

  SchedModel = getSchedModelForCPU(CPUName);

  // Set rvexABI if it hasn't been set yet.
  if (rvexABI == UnknownABI)
    rvexABI = O32;
}

 bool rvexSubtarget::
 enablePostRAScheduler(CodeGenOpt::Level OptLevel,
                       TargetSubtargetInfo::AntiDepBreakMode& Mode,
                       RegClassVector& CriticalPathRCs) const {
   Mode = TargetSubtargetInfo::ANTIDEP_NONE;
   CriticalPathRCs.clear();
   CriticalPathRCs.push_back(&rvex::CPURegsRegClass);
   return OptLevel >= CodeGenOpt::Default;
 }

