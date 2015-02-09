//===---- MipsABIInfo.cpp - Information about MIPS ABI's ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MipsABIInfo.h"
#include "MipsRegisterInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCTargetOptions.h"

using namespace llvm;

namespace {
static const MCPhysReg O32IntRegs[4] = {Mips::A0, Mips::A1, Mips::A2, Mips::A3};

static const MCPhysReg Mips64IntRegs[8] = {
    Mips::A0_64, Mips::A1_64, Mips::A2_64, Mips::A3_64,
    Mips::T0_64, Mips::T1_64, Mips::T2_64, Mips::T3_64};
}

const ArrayRef<MCPhysReg> MipsABIInfo::GetByValArgRegs() const {
  if (IsO32())
    return makeArrayRef(O32IntRegs);
  if (IsN32() || IsN64())
    return makeArrayRef(Mips64IntRegs);
  llvm_unreachable("Unhandled ABI");
}

const ArrayRef<MCPhysReg> MipsABIInfo::GetVarArgRegs() const {
  if (IsO32())
    return makeArrayRef(O32IntRegs);
  if (IsN32() || IsN64())
    return makeArrayRef(Mips64IntRegs);
  llvm_unreachable("Unhandled ABI");
}

unsigned MipsABIInfo::GetCalleeAllocdArgSizeInBytes(CallingConv::ID CC) const {
  if (IsO32())
    return CC != CallingConv::Fast ? 16 : 0;
  if (IsN32() || IsN64() || IsEABI())
    return 0;
  llvm_unreachable("Unhandled ABI");
}

MipsABIInfo MipsABIInfo::computeTargetABI(Triple TT, StringRef CPU,
                                          const MCTargetOptions &Options) {
  if (Options.getABIName().startswith("o32"))
    return MipsABIInfo::O32();
  else if (Options.getABIName().startswith("n32"))
    return MipsABIInfo::N32();
  else if (Options.getABIName().startswith("n64"))
    return MipsABIInfo::N64();
  else if (Options.getABIName().startswith("eabi"))
    return MipsABIInfo::EABI();
  else if (!Options.getABIName().empty())
    llvm_unreachable("Unknown ABI option for MIPS");

  // FIXME: This shares code with the selectMipsCPU routine that's
  // used and not shared in a couple of other places. This needs unifying
  // at some level.
  if (CPU.empty() || CPU == "generic") {
    if (TT.getArch() == Triple::mips || TT.getArch() == Triple::mipsel)
      CPU = "mips32";
    else
      CPU = "mips64";
  }

  return StringSwitch<MipsABIInfo>(CPU)
      .Case("mips1", MipsABIInfo::O32())
      .Case("mips2", MipsABIInfo::O32())
      .Case("mips32", MipsABIInfo::O32())
      .Case("mips32r2", MipsABIInfo::O32())
      .Case("mips32r6", MipsABIInfo::O32())
      .Case("mips16", MipsABIInfo::O32())
      .Case("mips3", MipsABIInfo::N64())
      .Case("mips4", MipsABIInfo::N64())
      .Case("mips5", MipsABIInfo::N64())
      .Case("mips64", MipsABIInfo::N64())
      .Case("mips64r2", MipsABIInfo::N64())
      .Case("mips64r6", MipsABIInfo::N64())
      .Case("octeon", MipsABIInfo::N64())
      .Default(MipsABIInfo::Unknown());
}
