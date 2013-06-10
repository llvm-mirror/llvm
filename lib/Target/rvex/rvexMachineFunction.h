//===-- rvexMachineFunctionInfo.h - Private data used for rvex ----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the rvex specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef rvex_MACHINE_FUNCTION_INFO_H
#define rvex_MACHINE_FUNCTION_INFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include <utility>

namespace llvm {

/// rvexFunctionInfo - This class is derived from MachineFunction private
/// rvex target-specific information for each MachineFunction.
class rvexFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();
  MachineFunction& MF;

  /// GlobalBaseReg - keeps track of the virtual register initialized for
  /// use as the global base register. This is used for PIC in some PIC
  /// relocation models.
  unsigned GlobalBaseReg;
  int GPFI; // Index of the frame object for restoring $gp
  unsigned MaxCallFrameSize;

public:
  rvexFunctionInfo(MachineFunction& MF)
  : MF(MF), GlobalBaseReg(0), MaxCallFrameSize(0)
  {}

  bool globalBaseRegFixed() const;
  bool globalBaseRegSet() const;
  unsigned getGlobalBaseReg();

  unsigned getMaxCallFrameSize() const { return MaxCallFrameSize; }
};

} // end of namespace llvm

#endif // rvex_MACHINE_FUNCTION_INFO_H

