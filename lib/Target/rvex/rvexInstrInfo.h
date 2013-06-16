//===-- rvexInstrInfo.h - rvex Instruction Information ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the rvex implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef rvexINSTRUCTIONINFO_H
#define rvexINSTRUCTIONINFO_H

#include "rvex.h"
#include "rvexRegisterInfo.h"
#include "MCTargetDesc/rvexBaseInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetInstrInfo.h"


#define GET_INSTRINFO_HEADER
#include "rvexGenInstrInfo.inc"

namespace llvm {

class rvexInstrInfo : public rvexGenInstrInfo {
  rvexTargetMachine &TM;
  const rvexRegisterInfo RI;
public:
  explicit rvexInstrInfo(rvexTargetMachine &TM);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  virtual const rvexRegisterInfo &getRegisterInfo() const;

  virtual void copyPhysReg(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, DebugLoc DL,
                           unsigned DestReg, unsigned SrcReg,
                           bool KillSrc) const;

public:
  virtual void loadRegFromStackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    unsigned DestReg, int FrameIndex,
                                    const TargetRegisterClass *RC,
                                    const TargetRegisterInfo *TRI) const;
  virtual MachineInstr* emitFrameIndexDebugValue(MachineFunction &MF,
                                                 int FrameIx, uint64_t Offset,
                                                 const MDNode *MDPtr,
                                                 DebugLoc DL) const;

  virtual DFAPacketizer *CreateTargetScheduleState(const TargetMachine *TM,
                                                  const ScheduleDAG *DAG) const;

  virtual bool isSchedulingBoundary(const MachineInstr *MI,
                                    const MachineBasicBlock *MBB,
                                    const MachineFunction &MF) const;
};
}

#endif
