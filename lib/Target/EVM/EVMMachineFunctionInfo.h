//=- EVMMachineFunctionInfo.h - EVM machine function info -----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares EVM-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_EVMMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_EVM_EVMMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

namespace llvm {

/// EVMMachineFunctionInfo - This class is derived from MachineFunctionInfo
/// and contains private EVM-specific information for each MachineFunction.
class EVMMachineFunctionInfo : public MachineFunctionInfo {
private:
  MachineFunction &MF;
  /// FrameIndex for start of varargs area
  int VarArgsFrameIndex = 0;
  /// Size of the save area used for varargs
  int VarArgsSaveSize = 0;

  unsigned NumStackArgs = 0;

  /// Copied from WebAssembly backend.
  /// A mapping from CodeGen vreg index to a boolean value indicating whether
  /// the given register is considered to be "stackified", meaning it has been
  /// determined or made to meet the stack requirements:
  ///   - single use (per path)
  ///   - single def (per path)
  ///   - defined and used in LIFO order with other stack registers
  BitVector VRegStackified;
  DenseMap<unsigned, unsigned> reg2index;
  unsigned memoryFrameSize;

  unsigned FrameIndexSize;

public:
  EVMMachineFunctionInfo(MachineFunction &MF)
    : MF(MF), memoryFrameSize(0), FrameIndexSize(0)
  {}

  void setNumStackArgs(unsigned size) { NumStackArgs = size; }
  unsigned getNumStackArgs() {return NumStackArgs; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  unsigned getVarArgsSaveSize() const { return VarArgsSaveSize; }
  void setVarArgsSaveSize(int Size) { VarArgsSaveSize = Size; }

  unsigned getFrameIndexSize() const { return FrameIndexSize; }
  void setFrameIndexSize(unsigned newFI) {
    assert(newFI > FrameIndexSize
           && "new frameindex should be greater than previous one.");
    FrameIndexSize = newFI;
  }

  void stackifyVReg(unsigned VReg) {
    assert(MF.getRegInfo().getUniqueVRegDef(VReg));
    auto I = Register::virtReg2Index(VReg);
    if (I >= VRegStackified.size())
      VRegStackified.resize(I + 1);
    VRegStackified.set(I);
  }

  bool isVRegStackified(unsigned VReg) const {
    if (Register::isPhysicalRegister(VReg)) {
      return false;
    }

    auto I = Register::virtReg2Index(VReg);
    if (I >= VRegStackified.size())
      return false;
    return VRegStackified.test(I);
  }

  unsigned get_memory_index(unsigned reg) const {
    assert(reg2index.find(reg) != reg2index.end());
    return reg2index.lookup(reg);
  }

  unsigned allocate_memory_index(unsigned reg) {
    if (reg2index.find(reg) != reg2index.end()) {
      // in non-SSA mode, it is possible to have
      // multiple definitions.
      return get_memory_index(reg);
    }
    unsigned index = reg2index.size();
    reg2index.insert(std::pair<unsigned, unsigned>(reg, index));
    updateMemoryFrameSize(index);
    return index;
  }

  void updateMemoryFrameSize(unsigned s) {
    memoryFrameSize = memoryFrameSize > s ? memoryFrameSize : s;
  }

  unsigned getNumAllocatedIndexInFunction() const {
    int64_t reservedSize = MF.getFrameInfo().getStackSize();
    assert(reservedSize % 32 == 0);
    return memoryFrameSize + reservedSize / 32;
  }


};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_EVM_EVMMACHINEFUNCTIONINFO_H
