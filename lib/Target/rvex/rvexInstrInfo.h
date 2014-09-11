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
  const rvexRegisterInfo RI;
public:
  explicit rvexInstrInfo(rvexSubtarget &STM);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  virtual const rvexRegisterInfo &getRegisterInfo() const;

  // ScheduleHazardRecognizer *
  // CreateTargetHazardRecognizer(const TargetMachine *TM,
  //                              const ScheduleDAG *DAG) const;
  
  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const; 
  ScheduleHazardRecognizer *
  CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const; 


public:
  void copyPhysReg(MachineBasicBlock &MBB,
                   MachineBasicBlock::iterator MI, DebugLoc DL,
                   unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  DFAPacketizer *CreateTargetScheduleState(const TargetMachine *TM,
                                          const ScheduleDAG *DAG) const override;

  bool AnalyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned RemoveBranch(MachineBasicBlock &MBB) const override;

private:
  void BuildCondBr(MachineBasicBlock &MBB, MachineBasicBlock *TBB, DebugLoc DL,
                   const SmallVectorImpl<MachineOperand>& Cond) const;

public:
  unsigned InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB,
                        const SmallVectorImpl<MachineOperand> &Cond,
                        DebugLoc DL) const override;

  bool ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  bool isSchedulingBoundary(const MachineInstr *MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  void insertNoop(MachineBasicBlock &MBB,
                   MachineBasicBlock::iterator MI) const override;


};
}

#endif
