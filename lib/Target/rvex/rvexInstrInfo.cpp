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
#include "rvexHazardRecognizers.h"

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

// ScheduleHazardRecognizer *rvexInstrInfo::CreateTargetHazardRecognizer(const TargetMachine *TM,
//                              const ScheduleDAG *DAG) const {

//   const InstrItineraryData *II = TM->getInstrItineraryData();
//   return new ScoreboardHazardRecognizer(II, DAG, "pre-RA-sched");

// }

/// CreateTargetPostRAHazardRecognizer - Return the postRA hazard recognizer
/// to use for this target when scheduling the DAG.
ScheduleHazardRecognizer *rvexInstrInfo::CreateTargetPostRAHazardRecognizer(
  const InstrItineraryData *II,
  const ScheduleDAG *DAG) const {

  return new rvexHazardRecognizer(II, DAG, "PostRA");
}

// Default implementation of CreateTargetMIHazardRecognizer.
ScheduleHazardRecognizer *rvexInstrInfo::CreateTargetMIHazardRecognizer(
  const InstrItineraryData *II,
  const ScheduleDAG *DAG) const {

  return new rvexHazardRecognizer(II, DAG, "misched");
}

void rvexInstrInfo::
copyPhysReg(MachineBasicBlock &MBB,
            MachineBasicBlock::iterator I, DebugLoc DL,
            unsigned DestReg, unsigned SrcReg,
            bool KillSrc) const {
  DEBUG(errs() << "copyPhysReg!\n");  
  unsigned Opc = 0, ZeroReg = 0;

  if (rvex::CPURegsRegClass.contains(DestReg)) { // Copy to CPU Reg.
    if (rvex::CPURegsRegClass.contains(SrcReg))
      Opc = rvex::MOV, ZeroReg = rvex::R0;

    else if (rvex::BRRegsRegClass.contains(SrcReg)) {
      Opc = rvex::rvexADDC, ZeroReg = rvex::R0;

      MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Opc));
      MIB2.addReg(DestReg, RegState::Define);
      MIB2.addReg(SrcReg, RegState::Define);
      MIB2.addReg(ZeroReg);
      MIB2.addReg(ZeroReg);
      MIB2.addReg(SrcReg, getKillRegState(KillSrc));

      return;
    }
  }
  else if (rvex::CPURegsRegClass.contains(SrcReg)) { // Copy from CPU Reg.
    // Only possibility in (DestReg==SW, SrcReg==rvexRegs) is 
    //  cmp $SW, $ZERO, $rc
    if (rvex::BRRegsRegClass.contains(DestReg))
      Opc = rvex::CMPNE, ZeroReg = rvex::R0;
  }
  else if (rvex::BRRegsRegClass.contains(SrcReg, DestReg)) {
          unsigned TempReg = rvex::R63;
          Opc = rvex::rvexADDC, ZeroReg = rvex::R0;

          MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Opc));
          MIB2.addReg(TempReg, RegState::Define);
          MIB2.addReg(SrcReg, RegState::Define);
          MIB2.addReg(ZeroReg);
          MIB2.addReg(ZeroReg);
          MIB2.addReg(SrcReg, getKillRegState(KillSrc));

          Opc = rvex::CMPNE, ZeroReg = rvex::R0;
          MachineInstrBuilder MIB3 = BuildMI(MBB, I, DL, get(Opc));
          MIB3.addReg(DestReg, RegState::Define);
          MIB3.addReg(TempReg);
          MIB3.addReg(ZeroReg);          
          
          return;

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

void rvexInstrInfo::
insertNoop(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(rvex::NOP));
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
  if (rvex::BRRegsRegClass.hasSubClassEq(RC))
    Opc = rvex::STW_PRED;
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
  if (rvex::BRRegsRegClass.hasSubClassEq(RC))
    Opc = rvex::LDW_PRED;  
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI).addImm(0)
    .addMemOperand(MMO);
}

//===----------------------------------------------------------------------===//
// Branch Analysis
//===----------------------------------------------------------------------===//

static unsigned GetAnalyzableBrOpc(unsigned Opc) {
  return (Opc == rvex::BR || Opc == rvex::BRF || Opc == rvex::JMP) ? Opc : 0;
}

/// GetOppositeBranchOpc - Return the inverse of the specified
/// opcode, e.g. turning BEQ to BNE.
static unsigned GetOppositeBranchOpc(unsigned Opc)
{
  switch (Opc) {
  default: llvm_unreachable("Illegal opcode!");
  case rvex::BR    : return rvex::BRF;
  case rvex::BRF    : return rvex::BR;
  }
}

static void AnalyzeCondBr(const MachineInstr* Inst, unsigned Opc,
                          MachineBasicBlock *&BB,
                          SmallVectorImpl<MachineOperand>& Cond) {
  assert(GetAnalyzableBrOpc(Opc) && "Not an analyzable branch");
  int NumOp = Inst->getNumExplicitOperands();

  // for both int and fp branches, the last explicit operand is the
  // MBB.
  BB = Inst->getOperand(NumOp-1).getMBB();
  Cond.push_back(MachineOperand::CreateImm(Opc));

  for (int i=0; i<NumOp-1; i++)
    Cond.push_back(Inst->getOperand(i));
}

bool rvexInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const
{
  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();

  // Skip all the debug instructions.
  while (I != REnd && I->isDebugValue())
    ++I;

  if (I == REnd || !isUnpredicatedTerminator(&*I)) {
    // If this block ends with no branches (it just falls through to its succ)
    // just return false, leaving TBB/FBB null.
    TBB = FBB = NULL;
    return false;
  }

  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();

  // Not an analyzable branch (must be an indirect jump).
  if (!GetAnalyzableBrOpc(LastOpc)) {
    return true;
  }

  // Get the second to last instruction in the block.
  unsigned SecondLastOpc = 0;
  MachineInstr *SecondLastInst = NULL;

  if (++I != REnd) {
    SecondLastInst = &*I;
    SecondLastOpc = GetAnalyzableBrOpc(SecondLastInst->getOpcode());

    // Not an analyzable branch (must be an indirect jump).
    if (isUnpredicatedTerminator(SecondLastInst) && !SecondLastOpc) {
      return true;
    }
  }

  // If there is only one terminator instruction, process it.
  if (!SecondLastOpc) {
    // Unconditional branch
    if (LastOpc == rvex::JMP) {
      TBB = LastInst->getOperand(0).getMBB();
      // If the basic block is next, remove the GOTO inst
      if(MBB.isLayoutSuccessor(TBB)) {
        LastInst->eraseFromParent();
      }

      return false;
    }

    // Conditional branch
    AnalyzeCondBr(LastInst, LastOpc, TBB, Cond);
    return false;
  }

  // If we reached here, there are two branches.
  // If there are three terminators, we don't know what sort of block this is.
  if (++I != REnd && isUnpredicatedTerminator(&*I)) {
    return true;
  }

  // If second to last instruction is an unconditional branch,
  // analyze it and remove the last instruction.
  if (SecondLastOpc == rvex::JMP) {
    // Return if the last instruction cannot be removed.
    if (!AllowModify) {
      return true;
    }

    TBB = SecondLastInst->getOperand(0).getMBB();
    LastInst->eraseFromParent();
    return false;
  }

  // Conditional branch followed by an unconditional branch.
  // The last one must be unconditional.
  if (LastOpc != rvex::JMP) {
    return true;
  }

  AnalyzeCondBr(SecondLastInst, SecondLastOpc, TBB, Cond);
  FBB = LastInst->getOperand(0).getMBB();

  return false;
}

void rvexInstrInfo::BuildCondBr(MachineBasicBlock &MBB,
                                MachineBasicBlock *TBB, DebugLoc DL,
                                const SmallVectorImpl<MachineOperand>& Cond)
  const {
  unsigned Opc = Cond[0].getImm();
  const MCInstrDesc &MCID = get(Opc);
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, MCID);

  for (unsigned i = 1; i < Cond.size(); ++i)
    MIB.addReg(Cond[i].getReg());

  MIB.addMBB(TBB);
}

unsigned rvexInstrInfo::
InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
             MachineBasicBlock *FBB,
             const SmallVectorImpl<MachineOperand> &Cond,
             DebugLoc DL) const {
  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");

  // # of condition operands:
  //  Unconditional branches: 0
  //  Floating point branches: 1 (opc)
  //  Int BranchZero: 2 (opc, reg)
  //  Int Branch: 3 (opc, reg0, reg1)
  assert((Cond.size() <= 3) &&
         "# of rvex branch conditions must be <= 3!");

  // Two-way Conditional branch.
  if (FBB) {
    BuildCondBr(MBB, TBB, DL, Cond);
    BuildMI(&MBB, DL, get(rvex::JMP)).addMBB(FBB);
    return 2;
  }

  // One way branch.
  // Unconditional branch.
  if (Cond.empty())
    BuildMI(&MBB, DL, get(rvex::JMP)).addMBB(TBB);
  else // Conditional branch.
    BuildCondBr(MBB, TBB, DL, Cond);
  return 1;
}

unsigned rvexInstrInfo::
RemoveBranch(MachineBasicBlock &MBB) const
{
  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();
  MachineBasicBlock::reverse_iterator FirstBr;
  unsigned removed;

  // Skip all the debug instructions.
  while (I != REnd && I->isDebugValue())
    ++I;

  FirstBr = I;

  // Up to 2 branches are removed.
  // Note that indirect branches are not removed.
  for(removed = 0; I != REnd && removed < 2; ++I, ++removed)
    if (!GetAnalyzableBrOpc(I->getOpcode()))
      break;

  MBB.erase(I.base(), FirstBr.base());

  return removed;
}

/// ReverseBranchCondition - Return the inverse opcode of the
/// specified Branch instruction.
bool rvexInstrInfo::
ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const
{
  assert( (Cond.size() && Cond.size() <= 3) &&
          "Invalid rvex branch condition!");
  Cond[0].setImm(GetOppositeBranchOpc(Cond[0].getImm()));
  return false;
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
  if (MI->getDesc().isTerminator() || MI->isLabel() || MI->isInlineAsm() || MI->isReturn()) {
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



