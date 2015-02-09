//===-- MipsMachineFunctionInfo.h - Private data used for Mips ----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Mips specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSMACHINEFUNCTION_H
#define LLVM_LIB_TARGET_MIPS_MIPSMACHINEFUNCTION_H

#include "Mips16HardFloatInfo.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include <map>
#include <string>
#include <utility>

namespace llvm {

/// \brief A class derived from PseudoSourceValue that represents a GOT entry
/// resolved by lazy-binding.
class MipsCallEntry : public PseudoSourceValue {
public:
  explicit MipsCallEntry(StringRef N);
  explicit MipsCallEntry(const GlobalValue *V);
  bool isConstant(const MachineFrameInfo *) const override;
  bool isAliased(const MachineFrameInfo *) const override;
  bool mayAlias(const MachineFrameInfo *) const override;

private:
  void printCustom(raw_ostream &O) const override;
#ifndef NDEBUG
  std::string Name;
  const GlobalValue *Val;
#endif
};

/// MipsFunctionInfo - This class is derived from MachineFunction private
/// Mips target-specific information for each MachineFunction.
class MipsFunctionInfo : public MachineFunctionInfo {
public:
  MipsFunctionInfo(MachineFunction &MF)
      : MF(MF), SRetReturnReg(0), GlobalBaseReg(0), Mips16SPAliasReg(0),
        VarArgsFrameIndex(0), CallsEhReturn(false), SaveS2(false),
        MoveF64ViaSpillFI(-1) {}

  ~MipsFunctionInfo();

  unsigned getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(unsigned Reg) { SRetReturnReg = Reg; }

  bool globalBaseRegSet() const;
  unsigned getGlobalBaseReg();

  bool mips16SPAliasRegSet() const;
  unsigned getMips16SPAliasReg();

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  bool hasByvalArg() const { return HasByvalArg; }
  void setFormalArgInfo(unsigned Size, bool HasByval) {
    IncomingArgSize = Size;
    HasByvalArg = HasByval;
  }

  unsigned getIncomingArgSize() const { return IncomingArgSize; }

  bool callsEhReturn() const { return CallsEhReturn; }
  void setCallsEhReturn() { CallsEhReturn = true; }

  void createEhDataRegsFI();
  int getEhDataRegFI(unsigned Reg) const { return EhDataRegFI[Reg]; }
  bool isEhDataRegFI(int FI) const;

  /// \brief Create a MachinePointerInfo that has a MipsCallEntr object
  /// representing a GOT entry for an external function.
  MachinePointerInfo callPtrInfo(StringRef Name);

  /// \brief Create a MachinePointerInfo that has a MipsCallEntr object
  /// representing a GOT entry for a global function.
  MachinePointerInfo callPtrInfo(const GlobalValue *Val);

  void setSaveS2() { SaveS2 = true; }
  bool hasSaveS2() const { return SaveS2; }

  int getMoveF64ViaSpillFI(const TargetRegisterClass *RC);

  std::map<const char *, const llvm::Mips16HardFloatInfo::FuncSignature *>
  StubsNeeded;

private:
  virtual void anchor();

  MachineFunction& MF;
  /// SRetReturnReg - Some subtargets require that sret lowering includes
  /// returning the value of the returned struct in a register. This field
  /// holds the virtual register into which the sret argument is passed.
  unsigned SRetReturnReg;

  /// GlobalBaseReg - keeps track of the virtual register initialized for
  /// use as the global base register. This is used for PIC in some PIC
  /// relocation models.
  unsigned GlobalBaseReg;

  /// Mips16SPAliasReg - keeps track of the virtual register initialized for
  /// use as an alias for SP for use in load/store of halfword/byte from/to
  /// the stack
  unsigned Mips16SPAliasReg;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex;

  /// True if function has a byval argument.
  bool HasByvalArg;

  /// Size of incoming argument area.
  unsigned IncomingArgSize;

  /// CallsEhReturn - Whether the function calls llvm.eh.return.
  bool CallsEhReturn;

  /// Frame objects for spilling eh data registers.
  int EhDataRegFI[4];

  // saveS2
  bool SaveS2;

  /// FrameIndex for expanding BuildPairF64 nodes to spill and reload when the
  /// O32 FPXX ABI is enabled. -1 is used to denote invalid index.
  int MoveF64ViaSpillFI;

  /// MipsCallEntry maps.
  StringMap<const MipsCallEntry *> ExternalCallEntries;
  ValueMap<const GlobalValue *, const MipsCallEntry *> GlobalCallEntries;
};

} // end of namespace llvm

#endif
