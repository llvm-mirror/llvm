//==- AArch64RegisterInfo.h - AArch64 Register Information Impl --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64REGISTERINFO_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64REGISTERINFO_H

#define GET_REGINFO_HEADER
#include "AArch64GenRegisterInfo.inc"

namespace llvm {

class AArch64InstrInfo;
class AArch64Subtarget;
class MachineFunction;
class RegScavenger;
class TargetRegisterClass;

struct AArch64RegisterInfo : public AArch64GenRegisterInfo {
private:
  const AArch64InstrInfo *TII;
  const AArch64Subtarget *STI;

public:
  AArch64RegisterInfo(const AArch64InstrInfo *tii, const AArch64Subtarget *sti);

  bool isReservedReg(const MachineFunction &MF, unsigned Reg) const;

  /// Code Generation virtual methods...
  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction *MF = nullptr) const override;
  const uint32_t *getCallPreservedMask(CallingConv::ID) const override;

  unsigned getCSRFirstUseCost() const override {
    // The cost will be compared against BlockFrequency where entry has the
    // value of 1 << 14. A value of 5 will choose to spill or split really
    // cold path instead of using a callee-saved register.
    return 5;
  }

  // Calls involved in thread-local variable lookup save more registers than
  // normal calls, so they need a different mask to represent this.
  const uint32_t *getTLSCallPreservedMask() const;

  /// getThisReturnPreservedMask - Returns a call preserved mask specific to the
  /// case that 'returned' is on an i64 first argument if the calling convention
  /// is one that can (partially) model this attribute with a preserved mask
  /// (i.e. it is a calling convention that uses the same register for the first
  /// i64 argument and an i64 return value)
  ///
  /// Should return NULL in the case that the calling convention does not have
  /// this property
  const uint32_t *getThisReturnPreservedMask(CallingConv::ID) const;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;
  bool useFPForScavengingIndex(const MachineFunction &MF) const override;
  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI,
                          int64_t Offset) const override;
  void materializeFrameBaseRegister(MachineBasicBlock *MBB, unsigned BaseReg,
                                    int FrameIdx,
                                    int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                         int64_t Offset) const override;
  void eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  bool cannotEliminateFrame(const MachineFunction &MF) const;

  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override;
  bool hasBasePointer(const MachineFunction &MF) const;
  unsigned getBaseRegister() const;

  // Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;
};

} // end namespace llvm

#endif
