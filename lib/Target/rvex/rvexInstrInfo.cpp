//===-- rvexInstrInfo.cpp - rvex Instruction Information ------------------===//
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

#include "rvex.h"
#include "rvexInstrInfo.h"
#include "rvexTargetMachine.h"
#include "rvexMachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#define GET_INSTRINFO_CTOR
//#define GET_INSTRINFO_ENUM
#include "rvexGenInstrInfo.inc"

namespace llvm {

  extern int rvexDFAStateInputTable[][2];

  extern unsigned int rvexDFAStateEntryTable[];
} // namespace

/*
#include "llvm/CodeGen/DFAPacketizer.h"
namespace llvm {
  DFAPacketizer *rvexGenSubtargetInfo::createDFAPacketizer(const InstrItineraryData *IID) const {
     return new DFAPacketizer(IID, rvexDFAStateInputTable, rvexDFAStateEntryTable);
}

} // End llvm namespace 
*/

#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

rvexInstrInfo::rvexInstrInfo(rvexTargetMachine &tm)
  : rvexGenInstrInfo(rvex::ADJCALLSTACKDOWN, rvex::ADJCALLSTACKUP), 
    TM(tm),
    RI(*TM.getSubtargetImpl(), *this) {}

const rvexRegisterInfo &rvexInstrInfo::getRegisterInfo() const {
  return RI;
}

void rvexInstrInfo::
copyPhysReg(MachineBasicBlock &MBB,
            MachineBasicBlock::iterator I, DebugLoc DL,
            unsigned DestReg, unsigned SrcReg,
            bool KillSrc) const {
  unsigned Opc = 0, ZeroReg = 0;

  if (rvex::CPURegsRegClass.contains(DestReg)) { // Copy to CPU Reg.
    if (rvex::CPURegsRegClass.contains(SrcReg))
      Opc = rvex::ADD, ZeroReg = rvex::R0;
    else if (SrcReg == rvex::HI)
      Opc = rvex::MFHI, SrcReg = 0;
    else if (SrcReg == rvex::LO)
      Opc = rvex::MFLO, SrcReg = 0;
    else if (SrcReg == rvex::B0)
      Opc = rvex::ADD, ZeroReg = rvex::R0;
  }
  else if (rvex::CPURegsRegClass.contains(SrcReg)) { // Copy from CPU Reg.
    if (DestReg == rvex::HI)
      Opc = rvex::MTHI, DestReg = 0;
    else if (DestReg == rvex::LO)
      Opc = rvex::MTLO, DestReg = 0;
    // Only possibility in (DestReg==SW, SrcReg==rvexRegs) is 
    //  cmp $SW, $ZERO, $rc
    //FIXME Moet werken voor alle branch registers
    else if (rvex::BRRegsRegClass.contains(DestReg))
      Opc = rvex::CMPNEQ, ZeroReg = rvex::R0;
  }

  assert(Opc && "Cannot copy registers");

  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc));

  if (DestReg)
    MIB.addReg(DestReg, RegState::Define);

  if (ZeroReg)
    MIB.addReg(ZeroReg);

  if (SrcReg)
    MIB.addReg(SrcReg, getKillRegState(KillSrc));
}

static MachineMemOperand* GetMemOperand(MachineBasicBlock &MBB, int FI,
                                        unsigned Flag) {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);

  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(FI), Flag,
                                 MFI.getObjectSize(FI), Align);
}

//- st SrcReg, MMO(FI)
void rvexInstrInfo::
storeRegToStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned SrcReg, bool isKill, int FI,
                    const TargetRegisterClass *RC,
                    const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOStore);

  unsigned Opc = 0;

  if (rvex::CPURegsRegClass.hasSubClassEq(RC))
    Opc = rvex::ST;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc)).addReg(SrcReg, getKillRegState(isKill))
    .addFrameIndex(FI).addImm(0).addMemOperand(MMO);
}


void rvexInstrInfo::
loadRegFromStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DestReg, int FI,
                     const TargetRegisterClass *RC,
                     const TargetRegisterInfo *TRI) const
{
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOLoad);
  unsigned Opc = 0;

  if (rvex::CPURegsRegClass.hasSubClassEq(RC))
    Opc = rvex::LD;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI).addImm(0)
    .addMemOperand(MMO);
}

DFAPacketizer *rvexInstrInfo::
CreateTargetScheduleState(const TargetMachine *TM,
                          const ScheduleDAG *DAG) const {
  
  const InstrItineraryData *II = TM->getInstrItineraryData();

  //DFAPacketizer *temp = TM->getSubtarget<rvexGenSubtargetInfo>().createDFAPacketizer(II);
  DFAPacketizer *temp = new DFAPacketizer(II, rvexDFAStateInputTable, rvexDFAStateEntryTable);
  
  return temp;
}

bool rvexInstrInfo::isSchedulingBoundary(const MachineInstr *MI,
                                           const MachineBasicBlock *MBB,
                                           const MachineFunction &MF) const {
  //Implementation from HexagonInstrInfo.

  // Debug info is never a scheduling boundary. It's necessary to be explicit
  // due to the special treatment of IT instructions below, otherwise a
  // dbg_value followed by an IT will result in the IT instruction being
  // considered a scheduling hazard, which is wrong. It should be the actual
  // instruction preceding the dbg_value instruction(s), just like it is when
  // debug info is not present.
  if (MI->isDebugValue())
    return false;
 
  // Terminators and labels can't be scheduled around.
  if (MI->getDesc().isTerminator() || MI->isLabel() || MI->isInlineAsm()) {
    return true;
  }

  return false;
}

MachineInstr*
rvexInstrInfo::emitFrameIndexDebugValue(MachineFunction &MF, int FrameIx,
                                        uint64_t Offset, const MDNode *MDPtr,
                                        DebugLoc DL) const {
  MachineInstrBuilder MIB = BuildMI(MF, DL, get(rvex::DBG_VALUE))
    .addFrameIndex(FrameIx).addImm(0).addImm(Offset).addMetadata(MDPtr);
  return &*MIB;
}

