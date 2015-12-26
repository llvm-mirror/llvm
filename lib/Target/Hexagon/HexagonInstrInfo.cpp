//===-- HexagonInstrInfo.cpp - Hexagon Instruction Information ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "Hexagon.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>

using namespace llvm;

#define DEBUG_TYPE "hexagon-instrinfo"

#define GET_INSTRINFO_CTOR_DTOR
#define GET_INSTRMAP_INFO
#include "HexagonGenInstrInfo.inc"
#include "HexagonGenDFAPacketizer.inc"

using namespace llvm;

cl::opt<bool> ScheduleInlineAsm("hexagon-sched-inline-asm", cl::Hidden,
  cl::init(false), cl::desc("Do not consider inline-asm a scheduling/"
                            "packetization boundary."));

static cl::opt<bool> EnableBranchPrediction("hexagon-enable-branch-prediction",
  cl::Hidden, cl::init(true), cl::desc("Enable branch prediction"));

static cl::opt<bool> DisableNVSchedule("disable-hexagon-nv-schedule",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable schedule adjustment for new value stores."));

static cl::opt<bool> EnableTimingClassLatency(
  "enable-timing-class-latency", cl::Hidden, cl::init(false),
  cl::desc("Enable timing class latency"));

static cl::opt<bool> EnableALUForwarding(
  "enable-alu-forwarding", cl::Hidden, cl::init(true),
  cl::desc("Enable vec alu forwarding"));

static cl::opt<bool> EnableACCForwarding(
  "enable-acc-forwarding", cl::Hidden, cl::init(true),
  cl::desc("Enable vec acc forwarding"));

static cl::opt<bool> BranchRelaxAsmLarge("branch-relax-asm-large",
  cl::init(true), cl::Hidden, cl::ZeroOrMore, cl::desc("branch relax asm"));

///
/// Constants for Hexagon instructions.
///
const int Hexagon_MEMV_OFFSET_MAX_128B = 2047;  // #s7
const int Hexagon_MEMV_OFFSET_MIN_128B = -2048; // #s7
const int Hexagon_MEMV_OFFSET_MAX = 1023;  // #s6
const int Hexagon_MEMV_OFFSET_MIN = -1024; // #s6
const int Hexagon_MEMW_OFFSET_MAX = 4095;
const int Hexagon_MEMW_OFFSET_MIN = -4096;
const int Hexagon_MEMD_OFFSET_MAX = 8191;
const int Hexagon_MEMD_OFFSET_MIN = -8192;
const int Hexagon_MEMH_OFFSET_MAX = 2047;
const int Hexagon_MEMH_OFFSET_MIN = -2048;
const int Hexagon_MEMB_OFFSET_MAX = 1023;
const int Hexagon_MEMB_OFFSET_MIN = -1024;
const int Hexagon_ADDI_OFFSET_MAX = 32767;
const int Hexagon_ADDI_OFFSET_MIN = -32768;
const int Hexagon_MEMD_AUTOINC_MAX = 56;
const int Hexagon_MEMD_AUTOINC_MIN = -64;
const int Hexagon_MEMW_AUTOINC_MAX = 28;
const int Hexagon_MEMW_AUTOINC_MIN = -32;
const int Hexagon_MEMH_AUTOINC_MAX = 14;
const int Hexagon_MEMH_AUTOINC_MIN = -16;
const int Hexagon_MEMB_AUTOINC_MAX = 7;
const int Hexagon_MEMB_AUTOINC_MIN = -8;
const int Hexagon_MEMV_AUTOINC_MAX = 192;
const int Hexagon_MEMV_AUTOINC_MIN = -256;
const int Hexagon_MEMV_AUTOINC_MAX_128B = 384;
const int Hexagon_MEMV_AUTOINC_MIN_128B = -512;

// Pin the vtable to this file.
void HexagonInstrInfo::anchor() {}

HexagonInstrInfo::HexagonInstrInfo(HexagonSubtarget &ST)
    : HexagonGenInstrInfo(Hexagon::ADJCALLSTACKDOWN, Hexagon::ADJCALLSTACKUP),
      RI() {}


static bool isIntRegForSubInst(unsigned Reg) {
  return (Reg >= Hexagon::R0 && Reg <= Hexagon::R7) ||
         (Reg >= Hexagon::R16 && Reg <= Hexagon::R23);
}


static bool isDblRegForSubInst(unsigned Reg, const HexagonRegisterInfo &HRI) {
  return isIntRegForSubInst(HRI.getSubReg(Reg, Hexagon::subreg_loreg)) &&
         isIntRegForSubInst(HRI.getSubReg(Reg, Hexagon::subreg_hireg));
}


/// Calculate number of instructions excluding the debug instructions.
static unsigned nonDbgMICount(MachineBasicBlock::const_instr_iterator MIB,
                              MachineBasicBlock::const_instr_iterator MIE) {
  unsigned Count = 0;
  for (; MIB != MIE; ++MIB) {
    if (!MIB->isDebugValue())
      ++Count;
  }
  return Count;
}


/// Find the hardware loop instruction used to set-up the specified loop.
/// On Hexagon, we have two instructions used to set-up the hardware loop
/// (LOOP0, LOOP1) with corresponding endloop (ENDLOOP0, ENDLOOP1) instructions
/// to indicate the end of a loop.
static MachineInstr *findLoopInstr(MachineBasicBlock *BB, int EndLoopOp,
      SmallPtrSet<MachineBasicBlock *, 8> &Visited) {
  int LOOPi;
  int LOOPr;
  if (EndLoopOp == Hexagon::ENDLOOP0) {
    LOOPi = Hexagon::J2_loop0i;
    LOOPr = Hexagon::J2_loop0r;
  } else { // EndLoopOp == Hexagon::EndLOOP1
    LOOPi = Hexagon::J2_loop1i;
    LOOPr = Hexagon::J2_loop1r;
  }

  // The loop set-up instruction will be in a predecessor block
  for (MachineBasicBlock::pred_iterator PB = BB->pred_begin(),
         PE = BB->pred_end(); PB != PE; ++PB) {
    // If this has been visited, already skip it.
    if (!Visited.insert(*PB).second)
      continue;
    if (*PB == BB)
      continue;
    for (MachineBasicBlock::reverse_instr_iterator I = (*PB)->instr_rbegin(),
           E = (*PB)->instr_rend(); I != E; ++I) {
      int Opc = I->getOpcode();
      if (Opc == LOOPi || Opc == LOOPr)
        return &*I;
      // We've reached a different loop, which means the loop0 has been removed.
      if (Opc == EndLoopOp)
        return 0;
    }
    // Check the predecessors for the LOOP instruction.
    MachineInstr *loop = findLoopInstr(*PB, EndLoopOp, Visited);
    if (loop)
      return loop;
  }
  return 0;
}


/// Gather register def/uses from MI.
/// This treats possible (predicated) defs as actually happening ones
/// (conservatively).
static inline void parseOperands(const MachineInstr *MI,
      SmallVector<unsigned, 4> &Defs, SmallVector<unsigned, 8> &Uses) {
  Defs.clear();
  Uses.clear();

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);

    if (!MO.isReg())
      continue;

    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;

    if (MO.isUse())
      Uses.push_back(MO.getReg());

    if (MO.isDef())
      Defs.push_back(MO.getReg());
  }
}


// Position dependent, so check twice for swap.
static bool isDuplexPairMatch(unsigned Ga, unsigned Gb) {
  switch (Ga) {
  case HexagonII::HSIG_None:
  default:
    return false;
  case HexagonII::HSIG_L1:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_L2:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_S1:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_S1 || Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_S2:
    return (Gb == HexagonII::HSIG_L1 || Gb == HexagonII::HSIG_L2 ||
            Gb == HexagonII::HSIG_S1 || Gb == HexagonII::HSIG_S2 ||
            Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_A:
    return (Gb == HexagonII::HSIG_A);
  case HexagonII::HSIG_Compound:
    return (Gb == HexagonII::HSIG_Compound);
  }
  return false;
}



/// isLoadFromStackSlot - If the specified machine instruction is a direct
/// load from a stack slot, return the virtual or physical register number of
/// the destination along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than loading from the stack slot.
unsigned HexagonInstrInfo::isLoadFromStackSlot(const MachineInstr *MI,
                                               int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case Hexagon::L2_loadri_io:
  case Hexagon::L2_loadrd_io:
  case Hexagon::L2_loadrh_io:
  case Hexagon::L2_loadrb_io:
  case Hexagon::L2_loadrub_io:
    if (MI->getOperand(2).isFI() &&
        MI->getOperand(1).isImm() && (MI->getOperand(1).getImm() == 0)) {
      FrameIndex = MI->getOperand(2).getIndex();
      return MI->getOperand(0).getReg();
    }
    break;
  }
  return 0;
}


/// isStoreToStackSlot - If the specified machine instruction is a direct
/// store to a stack slot, return the virtual or physical register number of
/// the source reg along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than storing to the stack slot.
unsigned HexagonInstrInfo::isStoreToStackSlot(const MachineInstr *MI,
                                              int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case Hexagon::S2_storeri_io:
  case Hexagon::S2_storerd_io:
  case Hexagon::S2_storerh_io:
  case Hexagon::S2_storerb_io:
    if (MI->getOperand(2).isFI() &&
        MI->getOperand(1).isImm() && (MI->getOperand(1).getImm() == 0)) {
      FrameIndex = MI->getOperand(0).getIndex();
      return MI->getOperand(2).getReg();
    }
    break;
  }
  return 0;
}


/// This function can analyze one/two way branching only and should (mostly) be
/// called by target independent side.
/// First entry is always the opcode of the branching instruction, except when
/// the Cond vector is supposed to be empty, e.g., when AnalyzeBranch fails, a
/// BB with only unconditional jump. Subsequent entries depend upon the opcode,
/// e.g. Jump_c p will have
/// Cond[0] = Jump_c
/// Cond[1] = p
/// HW-loop ENDLOOP:
/// Cond[0] = ENDLOOP
/// Cond[1] = MBB
/// New value jump:
/// Cond[0] = Hexagon::CMPEQri_f_Jumpnv_t_V4 -- specific opcode
/// Cond[1] = R
/// Cond[2] = Imm
///
bool HexagonInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  Cond.clear();

  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::instr_iterator I = MBB.instr_end();
  if (I == MBB.instr_begin())
    return false;

  // A basic block may looks like this:
  //
  //  [   insn
  //     EH_LABEL
  //      insn
  //      insn
  //      insn
  //     EH_LABEL
  //      insn     ]
  //
  // It has two succs but does not have a terminator
  // Don't know how to handle it.
  do {
    --I;
    if (I->isEHLabel())
      // Don't analyze EH branches.
      return true;
  } while (I != MBB.instr_begin());

  I = MBB.instr_end();
  --I;

  while (I->isDebugValue()) {
    if (I == MBB.instr_begin())
      return false;
    --I;
  }

  bool JumpToBlock = I->getOpcode() == Hexagon::J2_jump &&
                     I->getOperand(0).isMBB();
  // Delete the J2_jump if it's equivalent to a fall-through.
  if (AllowModify && JumpToBlock &&
      MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
    DEBUG(dbgs()<< "\nErasing the jump to successor block\n";);
    I->eraseFromParent();
    I = MBB.instr_end();
    if (I == MBB.instr_begin())
      return false;
    --I;
  }
  if (!isUnpredicatedTerminator(&*I))
    return false;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;
  MachineInstr *SecondLastInst = nullptr;
  // Find one more terminator if present.
  for (;;) {
    if (&*I != LastInst && !I->isBundle() && isUnpredicatedTerminator(&*I)) {
      if (!SecondLastInst)
        SecondLastInst = &*I;
      else
        // This is a third branch.
        return true;
    }
    if (I == MBB.instr_begin())
      break;
    --I;
  }

  int LastOpcode = LastInst->getOpcode();
  int SecLastOpcode = SecondLastInst ? SecondLastInst->getOpcode() : 0;
  // If the branch target is not a basic block, it could be a tail call.
  // (It is, if the target is a function.)
  if (LastOpcode == Hexagon::J2_jump && !LastInst->getOperand(0).isMBB())
    return true;
  if (SecLastOpcode == Hexagon::J2_jump &&
      !SecondLastInst->getOperand(0).isMBB())
    return true;

  bool LastOpcodeHasJMP_c = PredOpcodeHasJMP_c(LastOpcode);
  bool LastOpcodeHasNVJump = isNewValueJump(LastInst);

  // If there is only one terminator instruction, process it.
  if (LastInst && !SecondLastInst) {
    if (LastOpcode == Hexagon::J2_jump) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }
    if (isEndLoopN(LastOpcode)) {
      TBB = LastInst->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(LastInst->getOpcode()));
      Cond.push_back(LastInst->getOperand(0));
      return false;
    }
    if (LastOpcodeHasJMP_c) {
      TBB = LastInst->getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(LastInst->getOpcode()));
      Cond.push_back(LastInst->getOperand(0));
      return false;
    }
    // Only supporting rr/ri versions of new-value jumps.
    if (LastOpcodeHasNVJump && (LastInst->getNumExplicitOperands() == 3)) {
      TBB = LastInst->getOperand(2).getMBB();
      Cond.push_back(MachineOperand::CreateImm(LastInst->getOpcode()));
      Cond.push_back(LastInst->getOperand(0));
      Cond.push_back(LastInst->getOperand(1));
      return false;
    }
    DEBUG(dbgs() << "\nCant analyze BB#" << MBB.getNumber()
                 << " with one jump\n";);
    // Otherwise, don't know what this is.
    return true;
  }

  bool SecLastOpcodeHasJMP_c = PredOpcodeHasJMP_c(SecLastOpcode);
  bool SecLastOpcodeHasNVJump = isNewValueJump(SecondLastInst);
  if (SecLastOpcodeHasJMP_c && (LastOpcode == Hexagon::J2_jump)) {
    TBB =  SecondLastInst->getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(SecondLastInst->getOpcode()));
    Cond.push_back(SecondLastInst->getOperand(0));
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // Only supporting rr/ri versions of new-value jumps.
  if (SecLastOpcodeHasNVJump &&
      (SecondLastInst->getNumExplicitOperands() == 3) &&
      (LastOpcode == Hexagon::J2_jump)) {
    TBB = SecondLastInst->getOperand(2).getMBB();
    Cond.push_back(MachineOperand::CreateImm(SecondLastInst->getOpcode()));
    Cond.push_back(SecondLastInst->getOperand(0));
    Cond.push_back(SecondLastInst->getOperand(1));
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two Hexagon:JMPs, handle it.  The second one is not
  // executed, so remove it.
  if (SecLastOpcode == Hexagon::J2_jump && LastOpcode == Hexagon::J2_jump) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    I = LastInst->getIterator();
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // If the block ends with an ENDLOOP, and J2_jump, handle it.
  if (isEndLoopN(SecLastOpcode) && LastOpcode == Hexagon::J2_jump) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(SecondLastInst->getOpcode()));
    Cond.push_back(SecondLastInst->getOperand(0));
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }
  DEBUG(dbgs() << "\nCant analyze BB#" << MBB.getNumber()
               << " with two jumps";);
  // Otherwise, can't handle this.
  return true;
}


unsigned HexagonInstrInfo::RemoveBranch(MachineBasicBlock &MBB) const {
  DEBUG(dbgs() << "\nRemoving branches out of BB#" << MBB.getNumber());
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;
    // Only removing branches from end of MBB.
    if (!I->isBranch())
      return Count;
    if (Count && (I->getOpcode() == Hexagon::J2_jump))
      llvm_unreachable("Malformed basic block: unconditional branch not last");
    MBB.erase(&MBB.back());
    I = MBB.end();
    ++Count;
  }
  return Count;
}


unsigned HexagonInstrInfo::InsertBranch(MachineBasicBlock &MBB,
      MachineBasicBlock *TBB, MachineBasicBlock *FBB,
      ArrayRef<MachineOperand> Cond, DebugLoc DL) const {
  unsigned BOpc   = Hexagon::J2_jump;
  unsigned BccOpc = Hexagon::J2_jumpt;
  assert(validateBranchCond(Cond) && "Invalid branching condition");
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");

  // Check if ReverseBranchCondition has asked to reverse this branch
  // If we want to reverse the branch an odd number of times, we want
  // J2_jumpf.
  if (!Cond.empty() && Cond[0].isImm())
    BccOpc = Cond[0].getImm();

  if (!FBB) {
    if (Cond.empty()) {
      // Due to a bug in TailMerging/CFG Optimization, we need to add a
      // special case handling of a predicated jump followed by an
      // unconditional jump. If not, Tail Merging and CFG Optimization go
      // into an infinite loop.
      MachineBasicBlock *NewTBB, *NewFBB;
      SmallVector<MachineOperand, 4> Cond;
      MachineInstr *Term = MBB.getFirstTerminator();
      if (Term != MBB.end() && isPredicated(Term) &&
          !AnalyzeBranch(MBB, NewTBB, NewFBB, Cond, false)) {
        MachineBasicBlock *NextBB = &*++MBB.getIterator();
        if (NewTBB == NextBB) {
          ReverseBranchCondition(Cond);
          RemoveBranch(MBB);
          return InsertBranch(MBB, TBB, nullptr, Cond, DL);
        }
      }
      BuildMI(&MBB, DL, get(BOpc)).addMBB(TBB);
    } else if (isEndLoopN(Cond[0].getImm())) {
      int EndLoopOp = Cond[0].getImm();
      assert(Cond[1].isMBB());
      // Since we're adding an ENDLOOP, there better be a LOOP instruction.
      // Check for it, and change the BB target if needed.
      SmallPtrSet<MachineBasicBlock *, 8> VisitedBBs;
      MachineInstr *Loop = findLoopInstr(TBB, EndLoopOp, VisitedBBs);
      assert(Loop != 0 && "Inserting an ENDLOOP without a LOOP");
      Loop->getOperand(0).setMBB(TBB);
      // Add the ENDLOOP after the finding the LOOP0.
      BuildMI(&MBB, DL, get(EndLoopOp)).addMBB(TBB);
    } else if (isNewValueJump(Cond[0].getImm())) {
      assert((Cond.size() == 3) && "Only supporting rr/ri version of nvjump");
      // New value jump
      // (ins IntRegs:$src1, IntRegs:$src2, brtarget:$offset)
      // (ins IntRegs:$src1, u5Imm:$src2, brtarget:$offset)
      unsigned Flags1 = getUndefRegState(Cond[1].isUndef());
      DEBUG(dbgs() << "\nInserting NVJump for BB#" << MBB.getNumber(););
      if (Cond[2].isReg()) {
        unsigned Flags2 = getUndefRegState(Cond[2].isUndef());
        BuildMI(&MBB, DL, get(BccOpc)).addReg(Cond[1].getReg(), Flags1).
          addReg(Cond[2].getReg(), Flags2).addMBB(TBB);
      } else if(Cond[2].isImm()) {
        BuildMI(&MBB, DL, get(BccOpc)).addReg(Cond[1].getReg(), Flags1).
          addImm(Cond[2].getImm()).addMBB(TBB);
      } else
        llvm_unreachable("Invalid condition for branching");
    } else {
      assert((Cond.size() == 2) && "Malformed cond vector");
      const MachineOperand &RO = Cond[1];
      unsigned Flags = getUndefRegState(RO.isUndef());
      BuildMI(&MBB, DL, get(BccOpc)).addReg(RO.getReg(), Flags).addMBB(TBB);
    }
    return 1;
  }
  assert((!Cond.empty()) &&
         "Cond. cannot be empty when multiple branchings are required");
  assert((!isNewValueJump(Cond[0].getImm())) &&
         "NV-jump cannot be inserted with another branch");
  // Special case for hardware loops.  The condition is a basic block.
  if (isEndLoopN(Cond[0].getImm())) {
    int EndLoopOp = Cond[0].getImm();
    assert(Cond[1].isMBB());
    // Since we're adding an ENDLOOP, there better be a LOOP instruction.
    // Check for it, and change the BB target if needed.
    SmallPtrSet<MachineBasicBlock *, 8> VisitedBBs;
    MachineInstr *Loop = findLoopInstr(TBB, EndLoopOp, VisitedBBs);
    assert(Loop != 0 && "Inserting an ENDLOOP without a LOOP");
    Loop->getOperand(0).setMBB(TBB);
    // Add the ENDLOOP after the finding the LOOP0.
    BuildMI(&MBB, DL, get(EndLoopOp)).addMBB(TBB);
  } else {
    const MachineOperand &RO = Cond[1];
    unsigned Flags = getUndefRegState(RO.isUndef());
    BuildMI(&MBB, DL, get(BccOpc)).addReg(RO.getReg(), Flags).addMBB(TBB);
  }
  BuildMI(&MBB, DL, get(BOpc)).addMBB(FBB);

  return 2;
}


bool HexagonInstrInfo::isProfitableToIfCvt(MachineBasicBlock &MBB,
      unsigned NumCycles, unsigned ExtraPredCycles,
      BranchProbability Probability) const {
  return nonDbgBBSize(&MBB) <= 3;
}


bool HexagonInstrInfo::isProfitableToIfCvt(MachineBasicBlock &TMBB,
      unsigned NumTCycles, unsigned ExtraTCycles, MachineBasicBlock &FMBB,
      unsigned NumFCycles, unsigned ExtraFCycles, BranchProbability Probability)
      const {
  return nonDbgBBSize(&TMBB) <= 3 && nonDbgBBSize(&FMBB) <= 3;
}


bool HexagonInstrInfo::isProfitableToDupForIfCvt(MachineBasicBlock &MBB,
      unsigned NumInstrs, BranchProbability Probability) const {
  return NumInstrs <= 4;
}


void HexagonInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator I, DebugLoc DL, unsigned DestReg,
      unsigned SrcReg, bool KillSrc) const {
  auto &HRI = getRegisterInfo();
  if (Hexagon::IntRegsRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::A2_tfr), DestReg).addReg(SrcReg);
    return;
  }
  if (Hexagon::DoubleRegsRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::A2_tfrp), DestReg).addReg(SrcReg);
    return;
  }
  if (Hexagon::PredRegsRegClass.contains(SrcReg, DestReg)) {
    // Map Pd = Ps to Pd = or(Ps, Ps).
    BuildMI(MBB, I, DL, get(Hexagon::C2_or),
            DestReg).addReg(SrcReg).addReg(SrcReg);
    return;
  }
  if (Hexagon::DoubleRegsRegClass.contains(DestReg) &&
      Hexagon::IntRegsRegClass.contains(SrcReg)) {
    // We can have an overlap between single and double reg: r1:0 = r0.
    if(SrcReg == RI.getSubReg(DestReg, Hexagon::subreg_loreg)) {
        // r1:0 = r0
        BuildMI(MBB, I, DL, get(Hexagon::A2_tfrsi), (RI.getSubReg(DestReg,
                Hexagon::subreg_hireg))).addImm(0);
    } else {
        // r1:0 = r1 or no overlap.
        BuildMI(MBB, I, DL, get(Hexagon::A2_tfr), (RI.getSubReg(DestReg,
                Hexagon::subreg_loreg))).addReg(SrcReg);
        BuildMI(MBB, I, DL, get(Hexagon::A2_tfrsi), (RI.getSubReg(DestReg,
                Hexagon::subreg_hireg))).addImm(0);
    }
    return;
  }
  if (Hexagon::CtrRegsRegClass.contains(DestReg) &&
      Hexagon::IntRegsRegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::A2_tfrrcr), DestReg).addReg(SrcReg);
    return;
  }
  if (Hexagon::PredRegsRegClass.contains(SrcReg) &&
      Hexagon::IntRegsRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::C2_tfrpr), DestReg).
      addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::IntRegsRegClass.contains(SrcReg) &&
      Hexagon::PredRegsRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::C2_tfrrp), DestReg).
      addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::PredRegsRegClass.contains(SrcReg) &&
      Hexagon::IntRegsRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::C2_tfrpr), DestReg).
      addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::VectorRegsRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::V6_vassign), DestReg).
      addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::VecDblRegsRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::V6_vcombine), DestReg).
      addReg(HRI.getSubReg(SrcReg, Hexagon::subreg_hireg),
             getKillRegState(KillSrc)).
      addReg(HRI.getSubReg(SrcReg, Hexagon::subreg_loreg),
             getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::VecPredRegsRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::V6_pred_and), DestReg).
      addReg(SrcReg).
      addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (Hexagon::VecPredRegsRegClass.contains(SrcReg) &&
    Hexagon::VectorRegsRegClass.contains(DestReg)) {
    llvm_unreachable("Unimplemented pred to vec");
    return;
  }
  if (Hexagon::VecPredRegsRegClass.contains(DestReg) &&
      Hexagon::VectorRegsRegClass.contains(SrcReg)) {
    llvm_unreachable("Unimplemented vec to pred");
    return;
  }
  if (Hexagon::VecPredRegs128BRegClass.contains(SrcReg, DestReg)) {
    BuildMI(MBB, I, DL, get(Hexagon::V6_pred_and),
      HRI.getSubReg(DestReg, Hexagon::subreg_hireg)).
      addReg(HRI.getSubReg(SrcReg, Hexagon::subreg_hireg),
             getKillRegState(KillSrc));
    BuildMI(MBB, I, DL, get(Hexagon::V6_pred_and),
      HRI.getSubReg(DestReg, Hexagon::subreg_loreg)).
      addReg(HRI.getSubReg(SrcReg, Hexagon::subreg_loreg),
             getKillRegState(KillSrc));
    return;
  }

#ifndef NDEBUG
  // Show the invalid registers to ease debugging.
  dbgs() << "Invalid registers for copy in BB#" << MBB.getNumber()
         << ": " << PrintReg(DestReg, &HRI)
         << " = " << PrintReg(SrcReg, &HRI) << '\n';
#endif
  llvm_unreachable("Unimplemented");
}


void HexagonInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator I, unsigned SrcReg, bool isKill, int FI,
      const TargetRegisterClass *RC, const TargetRegisterInfo *TRI) const {
  DebugLoc DL = MBB.findDebugLoc(I);
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), Align);

  if (Hexagon::IntRegsRegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, I, DL, get(Hexagon::S2_storeri_io))
          .addFrameIndex(FI).addImm(0)
          .addReg(SrcReg, getKillRegState(isKill)).addMemOperand(MMO);
  } else if (Hexagon::DoubleRegsRegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, I, DL, get(Hexagon::S2_storerd_io))
          .addFrameIndex(FI).addImm(0)
          .addReg(SrcReg, getKillRegState(isKill)).addMemOperand(MMO);
  } else if (Hexagon::PredRegsRegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, I, DL, get(Hexagon::STriw_pred))
          .addFrameIndex(FI).addImm(0)
          .addReg(SrcReg, getKillRegState(isKill)).addMemOperand(MMO);
  } else {
    llvm_unreachable("Unimplemented");
  }
}


void HexagonInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator I, unsigned DestReg, int FI,
      const TargetRegisterClass *RC, const TargetRegisterInfo *TRI) const {
  DebugLoc DL = MBB.findDebugLoc(I);
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), Align);
  if (RC == &Hexagon::IntRegsRegClass) {
    BuildMI(MBB, I, DL, get(Hexagon::L2_loadri_io), DestReg)
          .addFrameIndex(FI).addImm(0).addMemOperand(MMO);
  } else if (RC == &Hexagon::DoubleRegsRegClass) {
    BuildMI(MBB, I, DL, get(Hexagon::L2_loadrd_io), DestReg)
          .addFrameIndex(FI).addImm(0).addMemOperand(MMO);
  } else if (RC == &Hexagon::PredRegsRegClass) {
    BuildMI(MBB, I, DL, get(Hexagon::LDriw_pred), DestReg)
          .addFrameIndex(FI).addImm(0).addMemOperand(MMO);
  } else {
    llvm_unreachable("Can't store this register to stack slot");
  }
}


/// expandPostRAPseudo - This function is called for all pseudo instructions
/// that remain after register allocation. Many pseudo instructions are
/// created to help register allocation. This is the place to convert them
/// into real instructions. The target can edit MI in place, or it can insert
/// new instructions and erase MI. The function should return true if
/// anything was changed.
bool HexagonInstrInfo::expandPostRAPseudo(MachineBasicBlock::iterator MI)
      const {
  const HexagonRegisterInfo &HRI = getRegisterInfo();
  MachineRegisterInfo &MRI = MI->getParent()->getParent()->getRegInfo();
  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned Opc = MI->getOpcode();
  const unsigned VecOffset = 1;
  bool Is128B = false;

  switch (Opc) {
    case Hexagon::ALIGNA:
      BuildMI(MBB, MI, DL, get(Hexagon::A2_andir), MI->getOperand(0).getReg())
          .addReg(HRI.getFrameRegister())
          .addImm(-MI->getOperand(1).getImm());
      MBB.erase(MI);
      return true;
    case Hexagon::HEXAGON_V6_vassignp_128B:
    case Hexagon::HEXAGON_V6_vassignp: {
      unsigned SrcReg = MI->getOperand(1).getReg();
      unsigned DstReg = MI->getOperand(0).getReg();
      if (SrcReg != DstReg)
        copyPhysReg(MBB, MI, DL, DstReg, SrcReg, MI->getOperand(1).isKill());
      MBB.erase(MI);
      return true;
    }
    case Hexagon::HEXAGON_V6_lo_128B:
    case Hexagon::HEXAGON_V6_lo: {
      unsigned SrcReg = MI->getOperand(1).getReg();
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned SrcSubLo = HRI.getSubReg(SrcReg, Hexagon::subreg_loreg);
      copyPhysReg(MBB, MI, DL, DstReg, SrcSubLo, MI->getOperand(1).isKill());
      MBB.erase(MI);
      MRI.clearKillFlags(SrcSubLo);
      return true;
    }
    case Hexagon::HEXAGON_V6_hi_128B:
    case Hexagon::HEXAGON_V6_hi: {
      unsigned SrcReg = MI->getOperand(1).getReg();
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned SrcSubHi = HRI.getSubReg(SrcReg, Hexagon::subreg_hireg);
      copyPhysReg(MBB, MI, DL, DstReg, SrcSubHi, MI->getOperand(1).isKill());
      MBB.erase(MI);
      MRI.clearKillFlags(SrcSubHi);
      return true;
    }
    case Hexagon::STrivv_indexed_128B:
      Is128B = true;
    case Hexagon::STrivv_indexed: {
      unsigned SrcReg = MI->getOperand(2).getReg();
      unsigned SrcSubHi = HRI.getSubReg(SrcReg, Hexagon::subreg_hireg);
      unsigned SrcSubLo = HRI.getSubReg(SrcReg, Hexagon::subreg_loreg);
      unsigned NewOpcd = Is128B ? Hexagon::V6_vS32b_ai_128B
                                : Hexagon::V6_vS32b_ai;
      unsigned Offset = Is128B ? VecOffset << 7 : VecOffset << 6;
      MachineInstr *MI1New = BuildMI(MBB, MI, DL, get(NewOpcd))
          .addOperand(MI->getOperand(0))
          .addImm(MI->getOperand(1).getImm())
          .addReg(SrcSubLo)
          .setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
      MI1New->getOperand(0).setIsKill(false);
      BuildMI(MBB, MI, DL, get(NewOpcd))
        .addOperand(MI->getOperand(0))
        // The Vectors are indexed in multiples of vector size.
        .addImm(MI->getOperand(1).getImm()+Offset)
        .addReg(SrcSubHi)
        .setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
      MBB.erase(MI);
      return true;
    }
    case Hexagon::LDrivv_pseudo_V6_128B:
    case Hexagon::LDrivv_indexed_128B:
      Is128B = true;
    case Hexagon::LDrivv_pseudo_V6:
    case Hexagon::LDrivv_indexed: {
      unsigned NewOpcd = Is128B ? Hexagon::V6_vL32b_ai_128B
                                : Hexagon::V6_vL32b_ai;
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned Offset = Is128B ? VecOffset << 7 : VecOffset << 6;
      MachineInstr *MI1New =
          BuildMI(MBB, MI, DL, get(NewOpcd),
                  HRI.getSubReg(DstReg, Hexagon::subreg_loreg))
              .addOperand(MI->getOperand(1))
              .addImm(MI->getOperand(2).getImm());
      MI1New->getOperand(1).setIsKill(false);
      BuildMI(MBB, MI, DL, get(NewOpcd),
              HRI.getSubReg(DstReg, Hexagon::subreg_hireg))
          .addOperand(MI->getOperand(1))
          // The Vectors are indexed in multiples of vector size.
          .addImm(MI->getOperand(2).getImm() + Offset)
          .setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
      MBB.erase(MI);
      return true;
    }
    case Hexagon::LDriv_pseudo_V6_128B:
      Is128B = true;
    case Hexagon::LDriv_pseudo_V6: {
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned NewOpc = Is128B ? Hexagon::V6_vL32b_ai_128B
                               : Hexagon::V6_vL32b_ai;
      int32_t Off = MI->getOperand(2).getImm();
      int32_t Idx = Off;
      BuildMI(MBB, MI, DL, get(NewOpc), DstReg)
        .addOperand(MI->getOperand(1))
        .addImm(Idx)
        .setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
      MBB.erase(MI);
      return true;
    }
    case Hexagon::STriv_pseudo_V6_128B:
      Is128B = true;
    case Hexagon::STriv_pseudo_V6: {
      unsigned NewOpc = Is128B ? Hexagon::V6_vS32b_ai_128B
                               : Hexagon::V6_vS32b_ai;
      int32_t Off = MI->getOperand(1).getImm();
      int32_t Idx = Is128B ? (Off >> 7) : (Off >> 6);
      BuildMI(MBB, MI, DL, get(NewOpc))
        .addOperand(MI->getOperand(0))
        .addImm(Idx)
        .addOperand(MI->getOperand(2))
        .setMemRefs(MI->memoperands_begin(), MI->memoperands_end());
      MBB.erase(MI);
      return true;
    }
    case Hexagon::TFR_PdTrue: {
      unsigned Reg = MI->getOperand(0).getReg();
      BuildMI(MBB, MI, DL, get(Hexagon::C2_orn), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
      MBB.erase(MI);
      return true;
    }
    case Hexagon::TFR_PdFalse: {
      unsigned Reg = MI->getOperand(0).getReg();
      BuildMI(MBB, MI, DL, get(Hexagon::C2_andn), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
      MBB.erase(MI);
      return true;
    }
    case Hexagon::VMULW: {
      // Expand a 64-bit vector multiply into 2 32-bit scalar multiplies.
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned Src1Reg = MI->getOperand(1).getReg();
      unsigned Src2Reg = MI->getOperand(2).getReg();
      unsigned Src1SubHi = HRI.getSubReg(Src1Reg, Hexagon::subreg_hireg);
      unsigned Src1SubLo = HRI.getSubReg(Src1Reg, Hexagon::subreg_loreg);
      unsigned Src2SubHi = HRI.getSubReg(Src2Reg, Hexagon::subreg_hireg);
      unsigned Src2SubLo = HRI.getSubReg(Src2Reg, Hexagon::subreg_loreg);
      BuildMI(MBB, MI, MI->getDebugLoc(), get(Hexagon::M2_mpyi),
              HRI.getSubReg(DstReg, Hexagon::subreg_hireg)).addReg(Src1SubHi)
          .addReg(Src2SubHi);
      BuildMI(MBB, MI, MI->getDebugLoc(), get(Hexagon::M2_mpyi),
              HRI.getSubReg(DstReg, Hexagon::subreg_loreg)).addReg(Src1SubLo)
          .addReg(Src2SubLo);
      MBB.erase(MI);
      MRI.clearKillFlags(Src1SubHi);
      MRI.clearKillFlags(Src1SubLo);
      MRI.clearKillFlags(Src2SubHi);
      MRI.clearKillFlags(Src2SubLo);
      return true;
    }
    case Hexagon::VMULW_ACC: {
      // Expand 64-bit vector multiply with addition into 2 scalar multiplies.
      unsigned DstReg = MI->getOperand(0).getReg();
      unsigned Src1Reg = MI->getOperand(1).getReg();
      unsigned Src2Reg = MI->getOperand(2).getReg();
      unsigned Src3Reg = MI->getOperand(3).getReg();
      unsigned Src1SubHi = HRI.getSubReg(Src1Reg, Hexagon::subreg_hireg);
      unsigned Src1SubLo = HRI.getSubReg(Src1Reg, Hexagon::subreg_loreg);
      unsigned Src2SubHi = HRI.getSubReg(Src2Reg, Hexagon::subreg_hireg);
      unsigned Src2SubLo = HRI.getSubReg(Src2Reg, Hexagon::subreg_loreg);
      unsigned Src3SubHi = HRI.getSubReg(Src3Reg, Hexagon::subreg_hireg);
      unsigned Src3SubLo = HRI.getSubReg(Src3Reg, Hexagon::subreg_loreg);
      BuildMI(MBB, MI, MI->getDebugLoc(), get(Hexagon::M2_maci),
              HRI.getSubReg(DstReg, Hexagon::subreg_hireg)).addReg(Src1SubHi)
          .addReg(Src2SubHi).addReg(Src3SubHi);
      BuildMI(MBB, MI, MI->getDebugLoc(), get(Hexagon::M2_maci),
              HRI.getSubReg(DstReg, Hexagon::subreg_loreg)).addReg(Src1SubLo)
          .addReg(Src2SubLo).addReg(Src3SubLo);
      MBB.erase(MI);
      MRI.clearKillFlags(Src1SubHi);
      MRI.clearKillFlags(Src1SubLo);
      MRI.clearKillFlags(Src2SubHi);
      MRI.clearKillFlags(Src2SubLo);
      MRI.clearKillFlags(Src3SubHi);
      MRI.clearKillFlags(Src3SubLo);
      return true;
    }
    case Hexagon::MUX64_rr: {
      const MachineOperand &Op0 = MI->getOperand(0);
      const MachineOperand &Op1 = MI->getOperand(1);
      const MachineOperand &Op2 = MI->getOperand(2);
      const MachineOperand &Op3 = MI->getOperand(3);
      unsigned Rd = Op0.getReg();
      unsigned Pu = Op1.getReg();
      unsigned Rs = Op2.getReg();
      unsigned Rt = Op3.getReg();
      DebugLoc DL = MI->getDebugLoc();
      unsigned K1 = getKillRegState(Op1.isKill());
      unsigned K2 = getKillRegState(Op2.isKill());
      unsigned K3 = getKillRegState(Op3.isKill());
      if (Rd != Rs)
        BuildMI(MBB, MI, DL, get(Hexagon::A2_tfrpt), Rd)
          .addReg(Pu, (Rd == Rt) ? K1 : 0)
          .addReg(Rs, K2);
      if (Rd != Rt)
        BuildMI(MBB, MI, DL, get(Hexagon::A2_tfrpf), Rd)
          .addReg(Pu, K1)
          .addReg(Rt, K3);
      MBB.erase(MI);
      return true;
    }
    case Hexagon::TCRETURNi:
      MI->setDesc(get(Hexagon::J2_jump));
      return true;
    case Hexagon::TCRETURNr:
      MI->setDesc(get(Hexagon::J2_jumpr));
      return true;
    case Hexagon::TFRI_f:
    case Hexagon::TFRI_cPt_f:
    case Hexagon::TFRI_cNotPt_f: {
      unsigned Opx = (Opc == Hexagon::TFRI_f) ? 1 : 2;
      APFloat FVal = MI->getOperand(Opx).getFPImm()->getValueAPF();
      APInt IVal = FVal.bitcastToAPInt();
      MI->RemoveOperand(Opx);
      unsigned NewOpc = (Opc == Hexagon::TFRI_f)     ? Hexagon::A2_tfrsi   :
                        (Opc == Hexagon::TFRI_cPt_f) ? Hexagon::C2_cmoveit :
                                                       Hexagon::C2_cmoveif;
      MI->setDesc(get(NewOpc));
      MI->addOperand(MachineOperand::CreateImm(IVal.getZExtValue()));
      return true;
    }
  }

  return false;
}


// We indicate that we want to reverse the branch by
// inserting the reversed branching opcode.
bool HexagonInstrInfo::ReverseBranchCondition(
      SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.empty())
    return true;
  assert(Cond[0].isImm() && "First entry in the cond vector not imm-val");
  unsigned opcode = Cond[0].getImm();
  //unsigned temp;
  assert(get(opcode).isBranch() && "Should be a branching condition.");
  if (isEndLoopN(opcode))
    return true;
  unsigned NewOpcode = getInvertedPredicatedOpcode(opcode);
  Cond[0].setImm(NewOpcode);
  return false;
}


void HexagonInstrInfo::insertNoop(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(Hexagon::A2_nop));
}


// Returns true if an instruction is predicated irrespective of the predicate
// sense. For example, all of the following will return true.
// if (p0) R1 = add(R2, R3)
// if (!p0) R1 = add(R2, R3)
// if (p0.new) R1 = add(R2, R3)
// if (!p0.new) R1 = add(R2, R3)
// Note: New-value stores are not included here as in the current
// implementation, we don't need to check their predicate sense.
bool HexagonInstrInfo::isPredicated(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::PredicatedPos) & HexagonII::PredicatedMask;
}


bool HexagonInstrInfo::PredicateInstruction(MachineInstr *MI,
      ArrayRef<MachineOperand> Cond) const {
  if (Cond.empty() || isNewValueJump(Cond[0].getImm()) ||
      isEndLoopN(Cond[0].getImm())) {
    DEBUG(dbgs() << "\nCannot predicate:"; MI->dump(););
    return false;
  }
  int Opc = MI->getOpcode();
  assert (isPredicable(MI) && "Expected predicable instruction");
  bool invertJump = predOpcodeHasNot(Cond);

  // We have to predicate MI "in place", i.e. after this function returns,
  // MI will need to be transformed into a predicated form. To avoid com-
  // plicated manipulations with the operands (handling tied operands,
  // etc.), build a new temporary instruction, then overwrite MI with it.

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned PredOpc = getCondOpcode(Opc, invertJump);
  MachineInstrBuilder T = BuildMI(B, MI, DL, get(PredOpc));
  unsigned NOp = 0, NumOps = MI->getNumOperands();
  while (NOp < NumOps) {
    MachineOperand &Op = MI->getOperand(NOp);
    if (!Op.isReg() || !Op.isDef() || Op.isImplicit())
      break;
    T.addOperand(Op);
    NOp++;
  }

  unsigned PredReg, PredRegPos, PredRegFlags;
  bool GotPredReg = getPredReg(Cond, PredReg, PredRegPos, PredRegFlags);
  (void)GotPredReg;
  assert(GotPredReg);
  T.addReg(PredReg, PredRegFlags);
  while (NOp < NumOps)
    T.addOperand(MI->getOperand(NOp++));

  MI->setDesc(get(PredOpc));
  while (unsigned n = MI->getNumOperands())
    MI->RemoveOperand(n-1);
  for (unsigned i = 0, n = T->getNumOperands(); i < n; ++i)
    MI->addOperand(T->getOperand(i));

  MachineBasicBlock::instr_iterator TI = T->getIterator();
  B.erase(TI);

  MachineRegisterInfo &MRI = B.getParent()->getRegInfo();
  MRI.clearKillFlags(PredReg);
  return true;
}


bool HexagonInstrInfo::SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
      ArrayRef<MachineOperand> Pred2) const {
  // TODO: Fix this
  return false;
}


bool HexagonInstrInfo::DefinesPredicate(MachineInstr *MI,
                                   std::vector<MachineOperand> &Pred) const {
  auto &HRI = getRegisterInfo();
  for (unsigned oper = 0; oper < MI->getNumOperands(); ++oper) {
    MachineOperand MO = MI->getOperand(oper);
    if (MO.isReg() && MO.isDef()) {
      const TargetRegisterClass* RC = HRI.getMinimalPhysRegClass(MO.getReg());
      if (RC == &Hexagon::PredRegsRegClass) {
        Pred.push_back(MO);
        return true;
      }
    }
  }
  return false;
}

bool HexagonInstrInfo::isPredicable(MachineInstr *MI) const {
  bool isPred = MI->getDesc().isPredicable();

  if (!isPred)
    return false;

  const int Opc = MI->getOpcode();
  int NumOperands = MI->getNumOperands();

  // Keep a flag for upto 4 operands in the instructions, to indicate if
  // that operand has been constant extended.
  bool OpCExtended[4];
  if (NumOperands > 4)
    NumOperands = 4;

  for (int i = 0; i < NumOperands; i++)
    OpCExtended[i] = (isOperandExtended(MI, i) && isConstExtended(MI));

  switch(Opc) {
  case Hexagon::A2_tfrsi:
    return (isOperandExtended(MI, 1) && isConstExtended(MI)) ||
           isInt<12>(MI->getOperand(1).getImm());

  case Hexagon::S2_storerd_io:
    return isShiftedUInt<6,3>(MI->getOperand(1).getImm());

  case Hexagon::S2_storeri_io:
  case Hexagon::S2_storerinew_io:
    return isShiftedUInt<6,2>(MI->getOperand(1).getImm());

  case Hexagon::S2_storerh_io:
  case Hexagon::S2_storerhnew_io:
    return isShiftedUInt<6,1>(MI->getOperand(1).getImm());

  case Hexagon::S2_storerb_io:
  case Hexagon::S2_storerbnew_io:
    return isUInt<6>(MI->getOperand(1).getImm());

  case Hexagon::L2_loadrd_io:
    return isShiftedUInt<6,3>(MI->getOperand(2).getImm());

  case Hexagon::L2_loadri_io:
    return isShiftedUInt<6,2>(MI->getOperand(2).getImm());

  case Hexagon::L2_loadrh_io:
  case Hexagon::L2_loadruh_io:
    return isShiftedUInt<6,1>(MI->getOperand(2).getImm());

  case Hexagon::L2_loadrb_io:
  case Hexagon::L2_loadrub_io:
    return isUInt<6>(MI->getOperand(2).getImm());

  case Hexagon::L2_loadrd_pi:
    return isShiftedInt<4,3>(MI->getOperand(3).getImm());

  case Hexagon::L2_loadri_pi:
    return isShiftedInt<4,2>(MI->getOperand(3).getImm());

  case Hexagon::L2_loadrh_pi:
  case Hexagon::L2_loadruh_pi:
    return isShiftedInt<4,1>(MI->getOperand(3).getImm());

  case Hexagon::L2_loadrb_pi:
  case Hexagon::L2_loadrub_pi:
    return isInt<4>(MI->getOperand(3).getImm());

  case Hexagon::S4_storeirb_io:
  case Hexagon::S4_storeirh_io:
  case Hexagon::S4_storeiri_io:
    return (OpCExtended[1] || isUInt<6>(MI->getOperand(1).getImm())) &&
           (OpCExtended[2] || isInt<6>(MI->getOperand(2).getImm()));

  case Hexagon::A2_addi:
    return isInt<8>(MI->getOperand(2).getImm());

  case Hexagon::A2_aslh:
  case Hexagon::A2_asrh:
  case Hexagon::A2_sxtb:
  case Hexagon::A2_sxth:
  case Hexagon::A2_zxtb:
  case Hexagon::A2_zxth:
    return true;
  }

  return true;
}


bool HexagonInstrInfo::isSchedulingBoundary(const MachineInstr *MI,
      const MachineBasicBlock *MBB, const MachineFunction &MF) const {
  // Debug info is never a scheduling boundary. It's necessary to be explicit
  // due to the special treatment of IT instructions below, otherwise a
  // dbg_value followed by an IT will result in the IT instruction being
  // considered a scheduling hazard, which is wrong. It should be the actual
  // instruction preceding the dbg_value instruction(s), just like it is
  // when debug info is not present.
  if (MI->isDebugValue())
    return false;

  // Throwing call is a boundary.
  if (MI->isCall()) {
    // If any of the block's successors is a landing pad, this could be a
    // throwing call.
    for (auto I : MBB->successors())
      if (I->isEHPad())
        return true;
  }

  // Don't mess around with no return calls.
  if (MI->getOpcode() == Hexagon::CALLv3nr)
    return true;

  // Terminators and labels can't be scheduled around.
  if (MI->getDesc().isTerminator() || MI->isPosition())
    return true;

  if (MI->isInlineAsm() && !ScheduleInlineAsm)
      return true;

  return false;
}


/// Measure the specified inline asm to determine an approximation of its
/// length.
/// Comments (which run till the next SeparatorString or newline) do not
/// count as an instruction.
/// Any other non-whitespace text is considered an instruction, with
/// multiple instructions separated by SeparatorString or newlines.
/// Variable-length instructions are not handled here; this function
/// may be overloaded in the target code to do that.
/// Hexagon counts the number of ##'s and adjust for that many
/// constant exenders.
unsigned HexagonInstrInfo::getInlineAsmLength(const char *Str,
      const MCAsmInfo &MAI) const {
  StringRef AStr(Str);
  // Count the number of instructions in the asm.
  bool atInsnStart = true;
  unsigned Length = 0;
  for (; *Str; ++Str) {
    if (*Str == '\n' || strncmp(Str, MAI.getSeparatorString(),
                                strlen(MAI.getSeparatorString())) == 0)
      atInsnStart = true;
    if (atInsnStart && !std::isspace(static_cast<unsigned char>(*Str))) {
      Length += MAI.getMaxInstLength();
      atInsnStart = false;
    }
    if (atInsnStart && strncmp(Str, MAI.getCommentString(),
                               strlen(MAI.getCommentString())) == 0)
      atInsnStart = false;
  }

  // Add to size number of constant extenders seen * 4.
  StringRef Occ("##");
  Length += AStr.count(Occ)*4;
  return Length;
}


ScheduleHazardRecognizer*
HexagonInstrInfo::CreateTargetPostRAHazardRecognizer(
      const InstrItineraryData *II, const ScheduleDAG *DAG) const {
  return TargetInstrInfo::CreateTargetPostRAHazardRecognizer(II, DAG);
}


/// \brief For a comparison instruction, return the source registers in
/// \p SrcReg and \p SrcReg2 if having two register operands, and the value it
/// compares against in CmpValue. Return true if the comparison instruction
/// can be analyzed.
bool HexagonInstrInfo::analyzeCompare(const MachineInstr *MI,
      unsigned &SrcReg, unsigned &SrcReg2, int &Mask, int &Value) const {
  unsigned Opc = MI->getOpcode();

  // Set mask and the first source register.
  switch (Opc) {
    case Hexagon::C2_cmpeq:
    case Hexagon::C2_cmpeqp:
    case Hexagon::C2_cmpgt:
    case Hexagon::C2_cmpgtp:
    case Hexagon::C2_cmpgtu:
    case Hexagon::C2_cmpgtup:
    case Hexagon::C4_cmpneq:
    case Hexagon::C4_cmplte:
    case Hexagon::C4_cmplteu:
    case Hexagon::C2_cmpeqi:
    case Hexagon::C2_cmpgti:
    case Hexagon::C2_cmpgtui:
    case Hexagon::C4_cmpneqi:
    case Hexagon::C4_cmplteui:
    case Hexagon::C4_cmpltei:
      SrcReg = MI->getOperand(1).getReg();
      Mask = ~0;
      break;
    case Hexagon::A4_cmpbeq:
    case Hexagon::A4_cmpbgt:
    case Hexagon::A4_cmpbgtu:
    case Hexagon::A4_cmpbeqi:
    case Hexagon::A4_cmpbgti:
    case Hexagon::A4_cmpbgtui:
      SrcReg = MI->getOperand(1).getReg();
      Mask = 0xFF;
      break;
    case Hexagon::A4_cmpheq:
    case Hexagon::A4_cmphgt:
    case Hexagon::A4_cmphgtu:
    case Hexagon::A4_cmpheqi:
    case Hexagon::A4_cmphgti:
    case Hexagon::A4_cmphgtui:
      SrcReg = MI->getOperand(1).getReg();
      Mask = 0xFFFF;
      break;
  }

  // Set the value/second source register.
  switch (Opc) {
    case Hexagon::C2_cmpeq:
    case Hexagon::C2_cmpeqp:
    case Hexagon::C2_cmpgt:
    case Hexagon::C2_cmpgtp:
    case Hexagon::C2_cmpgtu:
    case Hexagon::C2_cmpgtup:
    case Hexagon::A4_cmpbeq:
    case Hexagon::A4_cmpbgt:
    case Hexagon::A4_cmpbgtu:
    case Hexagon::A4_cmpheq:
    case Hexagon::A4_cmphgt:
    case Hexagon::A4_cmphgtu:
    case Hexagon::C4_cmpneq:
    case Hexagon::C4_cmplte:
    case Hexagon::C4_cmplteu:
      SrcReg2 = MI->getOperand(2).getReg();
      return true;

    case Hexagon::C2_cmpeqi:
    case Hexagon::C2_cmpgtui:
    case Hexagon::C2_cmpgti:
    case Hexagon::C4_cmpneqi:
    case Hexagon::C4_cmplteui:
    case Hexagon::C4_cmpltei:
    case Hexagon::A4_cmpbeqi:
    case Hexagon::A4_cmpbgti:
    case Hexagon::A4_cmpbgtui:
    case Hexagon::A4_cmpheqi:
    case Hexagon::A4_cmphgti:
    case Hexagon::A4_cmphgtui:
      SrcReg2 = 0;
      Value = MI->getOperand(2).getImm();
      return true;
  }

  return false;
}


unsigned HexagonInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
      const MachineInstr *MI, unsigned *PredCost) const {
  return getInstrTimingClassLatency(ItinData, MI);
}


DFAPacketizer *HexagonInstrInfo::CreateTargetScheduleState(
    const TargetSubtargetInfo &STI) const {
  const InstrItineraryData *II = STI.getInstrItineraryData();
  return static_cast<const HexagonSubtarget&>(STI).createDFAPacketizer(II);
}


// Inspired by this pair:
//  %R13<def> = L2_loadri_io %R29, 136; mem:LD4[FixedStack0]
//  S2_storeri_io %R29, 132, %R1<kill>; flags:  mem:ST4[FixedStack1]
// Currently AA considers the addresses in these instructions to be aliasing.
bool HexagonInstrInfo::areMemAccessesTriviallyDisjoint(MachineInstr *MIa,
      MachineInstr *MIb, AliasAnalysis *AA) const {
  int OffsetA = 0, OffsetB = 0;
  unsigned SizeA = 0, SizeB = 0;

  if (MIa->hasUnmodeledSideEffects() || MIb->hasUnmodeledSideEffects() ||
      MIa->hasOrderedMemoryRef() || MIa->hasOrderedMemoryRef())
    return false;

  // Instructions that are pure loads, not loads and stores like memops are not
  // dependent.
  if (MIa->mayLoad() && !isMemOp(MIa) && MIb->mayLoad() && !isMemOp(MIb))
    return true;

  // Get base, offset, and access size in MIa.
  unsigned BaseRegA = getBaseAndOffset(MIa, OffsetA, SizeA);
  if (!BaseRegA || !SizeA)
    return false;

  // Get base, offset, and access size in MIb.
  unsigned BaseRegB = getBaseAndOffset(MIb, OffsetB, SizeB);
  if (!BaseRegB || !SizeB)
    return false;

  if (BaseRegA != BaseRegB)
    return false;

  // This is a mem access with the same base register and known offsets from it.
  // Reason about it.
  if (OffsetA > OffsetB) {
    uint64_t offDiff = (uint64_t)((int64_t)OffsetA - (int64_t)OffsetB);
    return (SizeB <= offDiff);
  } else if (OffsetA < OffsetB) {
    uint64_t offDiff = (uint64_t)((int64_t)OffsetB - (int64_t)OffsetA);
    return (SizeA <= offDiff);
  }

  return false;
}


unsigned HexagonInstrInfo::createVR(MachineFunction* MF, MVT VT) const {
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetRegisterClass *TRC;
  if (VT == MVT::i1) {
    TRC = &Hexagon::PredRegsRegClass;
  } else if (VT == MVT::i32 || VT == MVT::f32) {
    TRC = &Hexagon::IntRegsRegClass;
  } else if (VT == MVT::i64 || VT == MVT::f64) {
    TRC = &Hexagon::DoubleRegsRegClass;
  } else {
    llvm_unreachable("Cannot handle this register class");
  }

  unsigned NewReg = MRI.createVirtualRegister(TRC);
  return NewReg;
}


bool HexagonInstrInfo::isAbsoluteSet(const MachineInstr* MI) const {
  return (getAddrMode(MI) == HexagonII::AbsoluteSet);
}


bool HexagonInstrInfo::isAccumulator(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return((F >> HexagonII::AccumulatorPos) & HexagonII::AccumulatorMask);
}


bool HexagonInstrInfo::isComplex(const MachineInstr *MI) const {
  const MachineFunction *MF = MI->getParent()->getParent();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const HexagonInstrInfo *QII = (const HexagonInstrInfo *) TII;

  if (!(isTC1(MI))
      && !(QII->isTC2Early(MI))
      && !(MI->getDesc().mayLoad())
      && !(MI->getDesc().mayStore())
      && (MI->getDesc().getOpcode() != Hexagon::S2_allocframe)
      && (MI->getDesc().getOpcode() != Hexagon::L2_deallocframe)
      && !(QII->isMemOp(MI))
      && !(MI->isBranch())
      && !(MI->isReturn())
      && !MI->isCall())
    return true;

  return false;
}


// Return true if the instruction is a compund branch instruction.
bool HexagonInstrInfo::isCompoundBranchInstr(const MachineInstr *MI) const {
  return (getType(MI) == HexagonII::TypeCOMPOUND && MI->isBranch());
}


bool HexagonInstrInfo::isCondInst(const MachineInstr *MI) const {
  return (MI->isBranch() && isPredicated(MI)) ||
         isConditionalTransfer(MI) ||
         isConditionalALU32(MI)    ||
         isConditionalLoad(MI)     ||
         // Predicated stores which don't have a .new on any operands.
         (MI->mayStore() && isPredicated(MI) && !isNewValueStore(MI) &&
          !isPredicatedNew(MI));
}


bool HexagonInstrInfo::isConditionalALU32(const MachineInstr* MI) const {
  switch (MI->getOpcode()) {
    case Hexagon::A2_paddf:
    case Hexagon::A2_paddfnew:
    case Hexagon::A2_paddif:
    case Hexagon::A2_paddifnew:
    case Hexagon::A2_paddit:
    case Hexagon::A2_padditnew:
    case Hexagon::A2_paddt:
    case Hexagon::A2_paddtnew:
    case Hexagon::A2_pandf:
    case Hexagon::A2_pandfnew:
    case Hexagon::A2_pandt:
    case Hexagon::A2_pandtnew:
    case Hexagon::A2_porf:
    case Hexagon::A2_porfnew:
    case Hexagon::A2_port:
    case Hexagon::A2_portnew:
    case Hexagon::A2_psubf:
    case Hexagon::A2_psubfnew:
    case Hexagon::A2_psubt:
    case Hexagon::A2_psubtnew:
    case Hexagon::A2_pxorf:
    case Hexagon::A2_pxorfnew:
    case Hexagon::A2_pxort:
    case Hexagon::A2_pxortnew:
    case Hexagon::A4_paslhf:
    case Hexagon::A4_paslhfnew:
    case Hexagon::A4_paslht:
    case Hexagon::A4_paslhtnew:
    case Hexagon::A4_pasrhf:
    case Hexagon::A4_pasrhfnew:
    case Hexagon::A4_pasrht:
    case Hexagon::A4_pasrhtnew:
    case Hexagon::A4_psxtbf:
    case Hexagon::A4_psxtbfnew:
    case Hexagon::A4_psxtbt:
    case Hexagon::A4_psxtbtnew:
    case Hexagon::A4_psxthf:
    case Hexagon::A4_psxthfnew:
    case Hexagon::A4_psxtht:
    case Hexagon::A4_psxthtnew:
    case Hexagon::A4_pzxtbf:
    case Hexagon::A4_pzxtbfnew:
    case Hexagon::A4_pzxtbt:
    case Hexagon::A4_pzxtbtnew:
    case Hexagon::A4_pzxthf:
    case Hexagon::A4_pzxthfnew:
    case Hexagon::A4_pzxtht:
    case Hexagon::A4_pzxthtnew:
    case Hexagon::C2_ccombinewf:
    case Hexagon::C2_ccombinewt:
      return true;
  }
  return false;
}


// FIXME - Function name and it's functionality don't match.
// It should be renamed to hasPredNewOpcode()
bool HexagonInstrInfo::isConditionalLoad(const MachineInstr* MI) const {
  if (!MI->getDesc().mayLoad() || !isPredicated(MI))
    return false;

  int PNewOpcode = Hexagon::getPredNewOpcode(MI->getOpcode());
  // Instruction with valid predicated-new opcode can be promoted to .new.
  return PNewOpcode >= 0;
}


// Returns true if an instruction is a conditional store.
//
// Note: It doesn't include conditional new-value stores as they can't be
// converted to .new predicate.
bool HexagonInstrInfo::isConditionalStore(const MachineInstr* MI) const {
  switch (MI->getOpcode()) {
    default: return false;
    case Hexagon::S4_storeirbt_io:
    case Hexagon::S4_storeirbf_io:
    case Hexagon::S4_pstorerbt_rr:
    case Hexagon::S4_pstorerbf_rr:
    case Hexagon::S2_pstorerbt_io:
    case Hexagon::S2_pstorerbf_io:
    case Hexagon::S2_pstorerbt_pi:
    case Hexagon::S2_pstorerbf_pi:
    case Hexagon::S2_pstorerdt_io:
    case Hexagon::S2_pstorerdf_io:
    case Hexagon::S4_pstorerdt_rr:
    case Hexagon::S4_pstorerdf_rr:
    case Hexagon::S2_pstorerdt_pi:
    case Hexagon::S2_pstorerdf_pi:
    case Hexagon::S2_pstorerht_io:
    case Hexagon::S2_pstorerhf_io:
    case Hexagon::S4_storeirht_io:
    case Hexagon::S4_storeirhf_io:
    case Hexagon::S4_pstorerht_rr:
    case Hexagon::S4_pstorerhf_rr:
    case Hexagon::S2_pstorerht_pi:
    case Hexagon::S2_pstorerhf_pi:
    case Hexagon::S2_pstorerit_io:
    case Hexagon::S2_pstorerif_io:
    case Hexagon::S4_storeirit_io:
    case Hexagon::S4_storeirif_io:
    case Hexagon::S4_pstorerit_rr:
    case Hexagon::S4_pstorerif_rr:
    case Hexagon::S2_pstorerit_pi:
    case Hexagon::S2_pstorerif_pi:

    // V4 global address store before promoting to dot new.
    case Hexagon::S4_pstorerdt_abs:
    case Hexagon::S4_pstorerdf_abs:
    case Hexagon::S4_pstorerbt_abs:
    case Hexagon::S4_pstorerbf_abs:
    case Hexagon::S4_pstorerht_abs:
    case Hexagon::S4_pstorerhf_abs:
    case Hexagon::S4_pstorerit_abs:
    case Hexagon::S4_pstorerif_abs:
      return true;

    // Predicated new value stores (i.e. if (p0) memw(..)=r0.new) are excluded
    // from the "Conditional Store" list. Because a predicated new value store
    // would NOT be promoted to a double dot new store.
    // This function returns yes for those stores that are predicated but not
    // yet promoted to predicate dot new instructions.
  }
}


bool HexagonInstrInfo::isConditionalTransfer(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
    case Hexagon::A2_tfrt:
    case Hexagon::A2_tfrf:
    case Hexagon::C2_cmoveit:
    case Hexagon::C2_cmoveif:
    case Hexagon::A2_tfrtnew:
    case Hexagon::A2_tfrfnew:
    case Hexagon::C2_cmovenewit:
    case Hexagon::C2_cmovenewif:
    case Hexagon::A2_tfrpt:
    case Hexagon::A2_tfrpf:
      return true;

    default:
      return false;
  }
  return false;
}


// TODO: In order to have isExtendable for fpimm/f32Ext, we need to handle
// isFPImm and later getFPImm as well.
bool HexagonInstrInfo::isConstExtended(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  unsigned isExtended = (F >> HexagonII::ExtendedPos) & HexagonII::ExtendedMask;
  if (isExtended) // Instruction must be extended.
    return true;

  unsigned isExtendable =
    (F >> HexagonII::ExtendablePos) & HexagonII::ExtendableMask;
  if (!isExtendable)
    return false;

  if (MI->isCall())
    return false;

  short ExtOpNum = getCExtOpNum(MI);
  const MachineOperand &MO = MI->getOperand(ExtOpNum);
  // Use MO operand flags to determine if MO
  // has the HMOTF_ConstExtended flag set.
  if (MO.getTargetFlags() && HexagonII::HMOTF_ConstExtended)
    return true;
  // If this is a Machine BB address we are talking about, and it is
  // not marked as extended, say so.
  if (MO.isMBB())
    return false;

  // We could be using an instruction with an extendable immediate and shoehorn
  // a global address into it. If it is a global address it will be constant
  // extended. We do this for COMBINE.
  // We currently only handle isGlobal() because it is the only kind of
  // object we are going to end up with here for now.
  // In the future we probably should add isSymbol(), etc.
  if (MO.isGlobal() || MO.isSymbol() || MO.isBlockAddress() ||
      MO.isJTI() || MO.isCPI())
    return true;

  // If the extendable operand is not 'Immediate' type, the instruction should
  // have 'isExtended' flag set.
  assert(MO.isImm() && "Extendable operand must be Immediate type");

  int MinValue = getMinValue(MI);
  int MaxValue = getMaxValue(MI);
  int ImmValue = MO.getImm();

  return (ImmValue < MinValue || ImmValue > MaxValue);
}


bool HexagonInstrInfo::isDeallocRet(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::L4_return :
  case Hexagon::L4_return_t :
  case Hexagon::L4_return_f :
  case Hexagon::L4_return_tnew_pnt :
  case Hexagon::L4_return_fnew_pnt :
  case Hexagon::L4_return_tnew_pt :
  case Hexagon::L4_return_fnew_pt :
   return true;
  }
  return false;
}


// Return true when ConsMI uses a register defined by ProdMI.
bool HexagonInstrInfo::isDependent(const MachineInstr *ProdMI,
      const MachineInstr *ConsMI) const {
  const MCInstrDesc &ProdMCID = ProdMI->getDesc();
  if (!ProdMCID.getNumDefs())
    return false;

  auto &HRI = getRegisterInfo();

  SmallVector<unsigned, 4> DefsA;
  SmallVector<unsigned, 4> DefsB;
  SmallVector<unsigned, 8> UsesA;
  SmallVector<unsigned, 8> UsesB;

  parseOperands(ProdMI, DefsA, UsesA);
  parseOperands(ConsMI, DefsB, UsesB);

  for (auto &RegA : DefsA)
    for (auto &RegB : UsesB) {
      // True data dependency.
      if (RegA == RegB)
        return true;

      if (Hexagon::DoubleRegsRegClass.contains(RegA))
        for (MCSubRegIterator SubRegs(RegA, &HRI); SubRegs.isValid(); ++SubRegs)
          if (RegB == *SubRegs)
            return true;

      if (Hexagon::DoubleRegsRegClass.contains(RegB))
        for (MCSubRegIterator SubRegs(RegB, &HRI); SubRegs.isValid(); ++SubRegs)
          if (RegA == *SubRegs)
            return true;
    }

  return false;
}


// Returns true if the instruction is alread a .cur.
bool HexagonInstrInfo::isDotCurInst(const MachineInstr* MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::V6_vL32b_cur_pi:
  case Hexagon::V6_vL32b_cur_ai:
  case Hexagon::V6_vL32b_cur_pi_128B:
  case Hexagon::V6_vL32b_cur_ai_128B:
    return true;
  }
  return false;
}


// Returns true, if any one of the operands is a dot new
// insn, whether it is predicated dot new or register dot new.
bool HexagonInstrInfo::isDotNewInst(const MachineInstr* MI) const {
  if (isNewValueInst(MI) ||
     (isPredicated(MI) && isPredicatedNew(MI)))
    return true;

  return false;
}


/// Symmetrical. See if these two instructions are fit for duplex pair.
bool HexagonInstrInfo::isDuplexPair(const MachineInstr *MIa,
      const MachineInstr *MIb) const {
  HexagonII::SubInstructionGroup MIaG = getDuplexCandidateGroup(MIa);
  HexagonII::SubInstructionGroup MIbG = getDuplexCandidateGroup(MIb);
  return (isDuplexPairMatch(MIaG, MIbG) || isDuplexPairMatch(MIbG, MIaG));
}


bool HexagonInstrInfo::isEarlySourceInstr(const MachineInstr *MI) const {
  if (!MI)
    return false;

  if (MI->mayLoad() || MI->mayStore() || MI->isCompare())
    return true;

  // Multiply
  unsigned SchedClass = MI->getDesc().getSchedClass();
  if (SchedClass == Hexagon::Sched::M_tc_3or4x_SLOT23)
    return true;
  return false;
}


bool HexagonInstrInfo::isEndLoopN(unsigned Opcode) const {
  return (Opcode == Hexagon::ENDLOOP0 ||
          Opcode == Hexagon::ENDLOOP1);
}


bool HexagonInstrInfo::isExpr(unsigned OpType) const {
  switch(OpType) {
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_BlockAddress:
    return true;
  default:
    return false;
  }
}


bool HexagonInstrInfo::isExtendable(const MachineInstr *MI) const {
  const MCInstrDesc &MID = MI->getDesc();
  const uint64_t F = MID.TSFlags;
  if ((F >> HexagonII::ExtendablePos) & HexagonII::ExtendableMask)
    return true;

  // TODO: This is largely obsolete now. Will need to be removed
  // in consecutive patches.
  switch(MI->getOpcode()) {
    // TFR_FI Remains a special case.
    case Hexagon::TFR_FI:
      return true;
    default:
      return false;
  }
  return  false;
}


// This returns true in two cases:
// - The OP code itself indicates that this is an extended instruction.
// - One of MOs has been marked with HMOTF_ConstExtended flag.
bool HexagonInstrInfo::isExtended(const MachineInstr *MI) const {
  // First check if this is permanently extended op code.
  const uint64_t F = MI->getDesc().TSFlags;
  if ((F >> HexagonII::ExtendedPos) & HexagonII::ExtendedMask)
    return true;
  // Use MO operand flags to determine if one of MI's operands
  // has HMOTF_ConstExtended flag set.
  for (MachineInstr::const_mop_iterator I = MI->operands_begin(),
       E = MI->operands_end(); I != E; ++I) {
    if (I->getTargetFlags() && HexagonII::HMOTF_ConstExtended)
      return true;
  }
  return  false;
}


bool HexagonInstrInfo::isFloat(const MachineInstr *MI) const {
  unsigned Opcode = MI->getOpcode();
  const uint64_t F = get(Opcode).TSFlags;
  return (F >> HexagonII::FPPos) & HexagonII::FPMask;
}


// No V60 HVX VMEM with A_INDIRECT.
bool HexagonInstrInfo::isHVXMemWithAIndirect(const MachineInstr *I,
      const MachineInstr *J) const {
  if (!isV60VectorInstruction(I))
    return false;
  if (!I->mayLoad() && !I->mayStore())
    return false;
  return J->isIndirectBranch() || isIndirectCall(J) || isIndirectL4Return(J);
}


bool HexagonInstrInfo::isIndirectCall(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::J2_callr :
  case Hexagon::J2_callrf :
  case Hexagon::J2_callrt :
    return true;
  }
  return false;
}


bool HexagonInstrInfo::isIndirectL4Return(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::L4_return :
  case Hexagon::L4_return_t :
  case Hexagon::L4_return_f :
  case Hexagon::L4_return_fnew_pnt :
  case Hexagon::L4_return_fnew_pt :
  case Hexagon::L4_return_tnew_pnt :
  case Hexagon::L4_return_tnew_pt :
    return true;
  }
  return false;
}


bool HexagonInstrInfo::isJumpR(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::J2_jumpr :
  case Hexagon::J2_jumprt :
  case Hexagon::J2_jumprf :
  case Hexagon::J2_jumprtnewpt :
  case Hexagon::J2_jumprfnewpt  :
  case Hexagon::J2_jumprtnew :
  case Hexagon::J2_jumprfnew :
    return true;
  }
  return false;
}


// Return true if a given MI can accomodate given offset.
// Use abs estimate as oppose to the exact number.
// TODO: This will need to be changed to use MC level
// definition of instruction extendable field size.
bool HexagonInstrInfo::isJumpWithinBranchRange(const MachineInstr *MI,
      unsigned offset) const {
  // This selection of jump instructions matches to that what
  // AnalyzeBranch can parse, plus NVJ.
  if (isNewValueJump(MI)) // r9:2
    return isInt<11>(offset);

  switch (MI->getOpcode()) {
  // Still missing Jump to address condition on register value.
  default:
    return false;
  case Hexagon::J2_jump: // bits<24> dst; // r22:2
  case Hexagon::J2_call:
  case Hexagon::CALLv3nr:
    return isInt<24>(offset);
  case Hexagon::J2_jumpt: //bits<17> dst; // r15:2
  case Hexagon::J2_jumpf:
  case Hexagon::J2_jumptnew:
  case Hexagon::J2_jumptnewpt:
  case Hexagon::J2_jumpfnew:
  case Hexagon::J2_jumpfnewpt:
  case Hexagon::J2_callt:
  case Hexagon::J2_callf:
    return isInt<17>(offset);
  case Hexagon::J2_loop0i:
  case Hexagon::J2_loop0iext:
  case Hexagon::J2_loop0r:
  case Hexagon::J2_loop0rext:
  case Hexagon::J2_loop1i:
  case Hexagon::J2_loop1iext:
  case Hexagon::J2_loop1r:
  case Hexagon::J2_loop1rext:
    return isInt<9>(offset);
  // TODO: Add all the compound branches here. Can we do this in Relation model?
  case Hexagon::J4_cmpeqi_tp0_jump_nt:
  case Hexagon::J4_cmpeqi_tp1_jump_nt:
    return isInt<11>(offset);
  }
}


bool HexagonInstrInfo::isLateInstrFeedsEarlyInstr(const MachineInstr *LRMI,
      const MachineInstr *ESMI) const {
  if (!LRMI || !ESMI)
    return false;

  bool isLate = isLateResultInstr(LRMI);
  bool isEarly = isEarlySourceInstr(ESMI);

  DEBUG(dbgs() << "V60" <<  (isLate ? "-LR  " : " --  "));
  DEBUG(LRMI->dump());
  DEBUG(dbgs() << "V60" <<  (isEarly ? "-ES  " : " --  "));
  DEBUG(ESMI->dump());

  if (isLate && isEarly) {
    DEBUG(dbgs() << "++Is Late Result feeding Early Source\n");
    return true;
  }

  return false;
}


bool HexagonInstrInfo::isLateResultInstr(const MachineInstr *MI) const {
  if (!MI)
    return false;

  switch (MI->getOpcode()) {
  case TargetOpcode::EXTRACT_SUBREG:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::SUBREG_TO_REG:
  case TargetOpcode::REG_SEQUENCE:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::COPY:
  case TargetOpcode::INLINEASM:
  case TargetOpcode::PHI:
    return false;
  default:
    break;
  }

  unsigned SchedClass = MI->getDesc().getSchedClass();

  switch (SchedClass) {
  case Hexagon::Sched::ALU32_2op_tc_1_SLOT0123:
  case Hexagon::Sched::ALU32_3op_tc_1_SLOT0123:
  case Hexagon::Sched::ALU32_ADDI_tc_1_SLOT0123:
  case Hexagon::Sched::ALU64_tc_1_SLOT23:
  case Hexagon::Sched::EXTENDER_tc_1_SLOT0123:
  case Hexagon::Sched::S_2op_tc_1_SLOT23:
  case Hexagon::Sched::S_3op_tc_1_SLOT23:
  case Hexagon::Sched::V2LDST_tc_ld_SLOT01:
  case Hexagon::Sched::V2LDST_tc_st_SLOT0:
  case Hexagon::Sched::V2LDST_tc_st_SLOT01:
  case Hexagon::Sched::V4LDST_tc_ld_SLOT01:
  case Hexagon::Sched::V4LDST_tc_st_SLOT0:
  case Hexagon::Sched::V4LDST_tc_st_SLOT01:
    return false;
  }
  return true;
}


bool HexagonInstrInfo::isLateSourceInstr(const MachineInstr *MI) const {
  if (!MI)
    return false;

  // Instructions with iclass A_CVI_VX and attribute A_CVI_LATE uses a multiply
  // resource, but all operands can be received late like an ALU instruction.
  return MI->getDesc().getSchedClass() == Hexagon::Sched::CVI_VX_LATE;
}


bool HexagonInstrInfo::isLoopN(const MachineInstr *MI) const {
  unsigned Opcode = MI->getOpcode();
  return Opcode == Hexagon::J2_loop0i    ||
         Opcode == Hexagon::J2_loop0r    ||
         Opcode == Hexagon::J2_loop0iext ||
         Opcode == Hexagon::J2_loop0rext ||
         Opcode == Hexagon::J2_loop1i    ||
         Opcode == Hexagon::J2_loop1r    ||
         Opcode == Hexagon::J2_loop1iext ||
         Opcode == Hexagon::J2_loop1rext;
}


bool HexagonInstrInfo::isMemOp(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
    default: return false;
    case Hexagon::L4_iadd_memopw_io :
    case Hexagon::L4_isub_memopw_io :
    case Hexagon::L4_add_memopw_io :
    case Hexagon::L4_sub_memopw_io :
    case Hexagon::L4_and_memopw_io :
    case Hexagon::L4_or_memopw_io :
    case Hexagon::L4_iadd_memoph_io :
    case Hexagon::L4_isub_memoph_io :
    case Hexagon::L4_add_memoph_io :
    case Hexagon::L4_sub_memoph_io :
    case Hexagon::L4_and_memoph_io :
    case Hexagon::L4_or_memoph_io :
    case Hexagon::L4_iadd_memopb_io :
    case Hexagon::L4_isub_memopb_io :
    case Hexagon::L4_add_memopb_io :
    case Hexagon::L4_sub_memopb_io :
    case Hexagon::L4_and_memopb_io :
    case Hexagon::L4_or_memopb_io :
    case Hexagon::L4_ior_memopb_io:
    case Hexagon::L4_ior_memoph_io:
    case Hexagon::L4_ior_memopw_io:
    case Hexagon::L4_iand_memopb_io:
    case Hexagon::L4_iand_memoph_io:
    case Hexagon::L4_iand_memopw_io:
    return true;
  }
  return false;
}


bool HexagonInstrInfo::isNewValue(const MachineInstr* MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::NewValuePos) & HexagonII::NewValueMask;
}


bool HexagonInstrInfo::isNewValue(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  return (F >> HexagonII::NewValuePos) & HexagonII::NewValueMask;
}


bool HexagonInstrInfo::isNewValueInst(const MachineInstr *MI) const {
  return isNewValueJump(MI) || isNewValueStore(MI);
}


bool HexagonInstrInfo::isNewValueJump(const MachineInstr *MI) const {
  return isNewValue(MI) && MI->isBranch();
}


bool HexagonInstrInfo::isNewValueJump(unsigned Opcode) const {
  return isNewValue(Opcode) && get(Opcode).isBranch() && isPredicated(Opcode);
}


bool HexagonInstrInfo::isNewValueStore(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::NVStorePos) & HexagonII::NVStoreMask;
}


bool HexagonInstrInfo::isNewValueStore(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  return (F >> HexagonII::NVStorePos) & HexagonII::NVStoreMask;
}


// Returns true if a particular operand is extendable for an instruction.
bool HexagonInstrInfo::isOperandExtended(const MachineInstr *MI,
    unsigned OperandNum) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return ((F >> HexagonII::ExtendableOpPos) & HexagonII::ExtendableOpMask)
          == OperandNum;
}


bool HexagonInstrInfo::isPostIncrement(const MachineInstr* MI) const {
  return getAddrMode(MI) == HexagonII::PostInc;
}


bool HexagonInstrInfo::isPredicatedNew(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  assert(isPredicated(MI));
  return (F >> HexagonII::PredicatedNewPos) & HexagonII::PredicatedNewMask;
}


bool HexagonInstrInfo::isPredicatedNew(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  assert(isPredicated(Opcode));
  return (F >> HexagonII::PredicatedNewPos) & HexagonII::PredicatedNewMask;
}


bool HexagonInstrInfo::isPredicatedTrue(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return !((F >> HexagonII::PredicatedFalsePos) &
           HexagonII::PredicatedFalseMask);
}


bool HexagonInstrInfo::isPredicatedTrue(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  // Make sure that the instruction is predicated.
  assert((F>> HexagonII::PredicatedPos) & HexagonII::PredicatedMask);
  return !((F >> HexagonII::PredicatedFalsePos) &
           HexagonII::PredicatedFalseMask);
}


bool HexagonInstrInfo::isPredicated(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  return (F >> HexagonII::PredicatedPos) & HexagonII::PredicatedMask;
}


bool HexagonInstrInfo::isPredicateLate(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  return ~(F >> HexagonII::PredicateLatePos) & HexagonII::PredicateLateMask;
}


bool HexagonInstrInfo::isPredictedTaken(unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  assert(get(Opcode).isBranch() &&
         (isPredicatedNew(Opcode) || isNewValue(Opcode)));
  return (F >> HexagonII::TakenPos) & HexagonII::TakenMask;
}


bool HexagonInstrInfo::isSaveCalleeSavedRegsCall(const MachineInstr *MI) const {
  return MI->getOpcode() == Hexagon::SAVE_REGISTERS_CALL_V4 ||
         MI->getOpcode() == Hexagon::SAVE_REGISTERS_CALL_V4_EXT;
}


bool HexagonInstrInfo::isSolo(const MachineInstr* MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::SoloPos) & HexagonII::SoloMask;
}


bool HexagonInstrInfo::isSpillPredRegOp(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case Hexagon::STriw_pred :
  case Hexagon::LDriw_pred :
    return true;
  default:
    return false;
  }
}


// Returns true when SU has a timing class TC1.
bool HexagonInstrInfo::isTC1(const MachineInstr *MI) const {
  unsigned SchedClass = MI->getDesc().getSchedClass();
  switch (SchedClass) {
  case Hexagon::Sched::ALU32_2op_tc_1_SLOT0123:
  case Hexagon::Sched::ALU32_3op_tc_1_SLOT0123:
  case Hexagon::Sched::ALU32_ADDI_tc_1_SLOT0123:
  case Hexagon::Sched::ALU64_tc_1_SLOT23:
  case Hexagon::Sched::EXTENDER_tc_1_SLOT0123:
  //case Hexagon::Sched::M_tc_1_SLOT23:
  case Hexagon::Sched::S_2op_tc_1_SLOT23:
  case Hexagon::Sched::S_3op_tc_1_SLOT23:
    return true;

  default:
    return false;
  }
}


bool HexagonInstrInfo::isTC2(const MachineInstr *MI) const {
  unsigned SchedClass = MI->getDesc().getSchedClass();
  switch (SchedClass) {
  case Hexagon::Sched::ALU32_3op_tc_2_SLOT0123:
  case Hexagon::Sched::ALU64_tc_2_SLOT23:
  case Hexagon::Sched::CR_tc_2_SLOT3:
  case Hexagon::Sched::M_tc_2_SLOT23:
  case Hexagon::Sched::S_2op_tc_2_SLOT23:
  case Hexagon::Sched::S_3op_tc_2_SLOT23:
    return true;

  default:
    return false;
  }
}


bool HexagonInstrInfo::isTC2Early(const MachineInstr *MI) const {
  unsigned SchedClass = MI->getDesc().getSchedClass();
  switch (SchedClass) {
  case Hexagon::Sched::ALU32_2op_tc_2early_SLOT0123:
  case Hexagon::Sched::ALU32_3op_tc_2early_SLOT0123:
  case Hexagon::Sched::ALU64_tc_2early_SLOT23:
  case Hexagon::Sched::CR_tc_2early_SLOT23:
  case Hexagon::Sched::CR_tc_2early_SLOT3:
  case Hexagon::Sched::J_tc_2early_SLOT0123:
  case Hexagon::Sched::J_tc_2early_SLOT2:
  case Hexagon::Sched::J_tc_2early_SLOT23:
  case Hexagon::Sched::S_2op_tc_2early_SLOT23:
  case Hexagon::Sched::S_3op_tc_2early_SLOT23:
    return true;

  default:
    return false;
  }
}


bool HexagonInstrInfo::isTC4x(const MachineInstr *MI) const {
  if (!MI)
    return false;

  unsigned SchedClass = MI->getDesc().getSchedClass();
  return SchedClass == Hexagon::Sched::M_tc_3or4x_SLOT23;
}


bool HexagonInstrInfo::isV60VectorInstruction(const MachineInstr *MI) const {
  if (!MI)
    return false;

  const uint64_t V = getType(MI);
  return HexagonII::TypeCVI_FIRST <= V && V <= HexagonII::TypeCVI_LAST;
}


// Check if the Offset is a valid auto-inc imm by Load/Store Type.
//
bool HexagonInstrInfo::isValidAutoIncImm(const EVT VT, const int Offset) const {
  if (VT == MVT::v16i32 || VT == MVT::v8i64 ||
      VT == MVT::v32i16 || VT == MVT::v64i8) {
      return (Offset >= Hexagon_MEMV_AUTOINC_MIN &&
              Offset <= Hexagon_MEMV_AUTOINC_MAX &&
              (Offset & 0x3f) == 0);
  }
  // 128B
  if (VT == MVT::v32i32 || VT == MVT::v16i64 ||
      VT == MVT::v64i16 || VT == MVT::v128i8) {
      return (Offset >= Hexagon_MEMV_AUTOINC_MIN_128B &&
              Offset <= Hexagon_MEMV_AUTOINC_MAX_128B &&
              (Offset & 0x7f) == 0);
  }
  if (VT == MVT::i64) {
      return (Offset >= Hexagon_MEMD_AUTOINC_MIN &&
              Offset <= Hexagon_MEMD_AUTOINC_MAX &&
              (Offset & 0x7) == 0);
  }
  if (VT == MVT::i32) {
      return (Offset >= Hexagon_MEMW_AUTOINC_MIN &&
              Offset <= Hexagon_MEMW_AUTOINC_MAX &&
              (Offset & 0x3) == 0);
  }
  if (VT == MVT::i16) {
      return (Offset >= Hexagon_MEMH_AUTOINC_MIN &&
              Offset <= Hexagon_MEMH_AUTOINC_MAX &&
              (Offset & 0x1) == 0);
  }
  if (VT == MVT::i8) {
      return (Offset >= Hexagon_MEMB_AUTOINC_MIN &&
              Offset <= Hexagon_MEMB_AUTOINC_MAX);
  }
  llvm_unreachable("Not an auto-inc opc!");
}


bool HexagonInstrInfo::isValidOffset(unsigned Opcode, int Offset,
      bool Extend) const {
  // This function is to check whether the "Offset" is in the correct range of
  // the given "Opcode". If "Offset" is not in the correct range, "A2_addi" is
  // inserted to calculate the final address. Due to this reason, the function
  // assumes that the "Offset" has correct alignment.
  // We used to assert if the offset was not properly aligned, however,
  // there are cases where a misaligned pointer recast can cause this
  // problem, and we need to allow for it. The front end warns of such
  // misaligns with respect to load size.

  switch (Opcode) {
  case Hexagon::STriq_pred_V6:
  case Hexagon::STriq_pred_vec_V6:
  case Hexagon::STriv_pseudo_V6:
  case Hexagon::STrivv_pseudo_V6:
  case Hexagon::LDriq_pred_V6:
  case Hexagon::LDriq_pred_vec_V6:
  case Hexagon::LDriv_pseudo_V6:
  case Hexagon::LDrivv_pseudo_V6:
  case Hexagon::LDrivv_indexed:
  case Hexagon::STrivv_indexed:
  case Hexagon::V6_vL32b_ai:
  case Hexagon::V6_vS32b_ai:
  case Hexagon::V6_vL32Ub_ai:
  case Hexagon::V6_vS32Ub_ai:
    return (Offset >= Hexagon_MEMV_OFFSET_MIN) &&
      (Offset <= Hexagon_MEMV_OFFSET_MAX);

  case Hexagon::STriq_pred_V6_128B:
  case Hexagon::STriq_pred_vec_V6_128B:
  case Hexagon::STriv_pseudo_V6_128B:
  case Hexagon::STrivv_pseudo_V6_128B:
  case Hexagon::LDriq_pred_V6_128B:
  case Hexagon::LDriq_pred_vec_V6_128B:
  case Hexagon::LDriv_pseudo_V6_128B:
  case Hexagon::LDrivv_pseudo_V6_128B:
  case Hexagon::LDrivv_indexed_128B:
  case Hexagon::STrivv_indexed_128B:
  case Hexagon::V6_vL32b_ai_128B:
  case Hexagon::V6_vS32b_ai_128B:
  case Hexagon::V6_vL32Ub_ai_128B:
  case Hexagon::V6_vS32Ub_ai_128B:
    return (Offset >= Hexagon_MEMV_OFFSET_MIN_128B) &&
      (Offset <= Hexagon_MEMV_OFFSET_MAX_128B);

  case Hexagon::J2_loop0i:
  case Hexagon::J2_loop1i:
    return isUInt<10>(Offset);
  }

  if (Extend)
    return true;

  switch (Opcode) {
  case Hexagon::L2_loadri_io:
  case Hexagon::S2_storeri_io:
    return (Offset >= Hexagon_MEMW_OFFSET_MIN) &&
      (Offset <= Hexagon_MEMW_OFFSET_MAX);

  case Hexagon::L2_loadrd_io:
  case Hexagon::S2_storerd_io:
    return (Offset >= Hexagon_MEMD_OFFSET_MIN) &&
      (Offset <= Hexagon_MEMD_OFFSET_MAX);

  case Hexagon::L2_loadrh_io:
  case Hexagon::L2_loadruh_io:
  case Hexagon::S2_storerh_io:
    return (Offset >= Hexagon_MEMH_OFFSET_MIN) &&
      (Offset <= Hexagon_MEMH_OFFSET_MAX);

  case Hexagon::L2_loadrb_io:
  case Hexagon::L2_loadrub_io:
  case Hexagon::S2_storerb_io:
    return (Offset >= Hexagon_MEMB_OFFSET_MIN) &&
      (Offset <= Hexagon_MEMB_OFFSET_MAX);

  case Hexagon::A2_addi:
    return (Offset >= Hexagon_ADDI_OFFSET_MIN) &&
      (Offset <= Hexagon_ADDI_OFFSET_MAX);

  case Hexagon::L4_iadd_memopw_io :
  case Hexagon::L4_isub_memopw_io :
  case Hexagon::L4_add_memopw_io :
  case Hexagon::L4_sub_memopw_io :
  case Hexagon::L4_and_memopw_io :
  case Hexagon::L4_or_memopw_io :
    return (0 <= Offset && Offset <= 255);

  case Hexagon::L4_iadd_memoph_io :
  case Hexagon::L4_isub_memoph_io :
  case Hexagon::L4_add_memoph_io :
  case Hexagon::L4_sub_memoph_io :
  case Hexagon::L4_and_memoph_io :
  case Hexagon::L4_or_memoph_io :
    return (0 <= Offset && Offset <= 127);

  case Hexagon::L4_iadd_memopb_io :
  case Hexagon::L4_isub_memopb_io :
  case Hexagon::L4_add_memopb_io :
  case Hexagon::L4_sub_memopb_io :
  case Hexagon::L4_and_memopb_io :
  case Hexagon::L4_or_memopb_io :
    return (0 <= Offset && Offset <= 63);

  // LDri_pred and STriw_pred are pseudo operations, so it has to take offset of
  // any size. Later pass knows how to handle it.
  case Hexagon::STriw_pred:
  case Hexagon::LDriw_pred:
    return true;

  case Hexagon::TFR_FI:
  case Hexagon::TFR_FIA:
  case Hexagon::INLINEASM:
    return true;

  case Hexagon::L2_ploadrbt_io:
  case Hexagon::L2_ploadrbf_io:
  case Hexagon::L2_ploadrubt_io:
  case Hexagon::L2_ploadrubf_io:
  case Hexagon::S2_pstorerbt_io:
  case Hexagon::S2_pstorerbf_io:
  case Hexagon::S4_storeirb_io:
  case Hexagon::S4_storeirbt_io:
  case Hexagon::S4_storeirbf_io:
    return isUInt<6>(Offset);

  case Hexagon::L2_ploadrht_io:
  case Hexagon::L2_ploadrhf_io:
  case Hexagon::L2_ploadruht_io:
  case Hexagon::L2_ploadruhf_io:
  case Hexagon::S2_pstorerht_io:
  case Hexagon::S2_pstorerhf_io:
  case Hexagon::S4_storeirh_io:
  case Hexagon::S4_storeirht_io:
  case Hexagon::S4_storeirhf_io:
    return isShiftedUInt<6,1>(Offset);

  case Hexagon::L2_ploadrit_io:
  case Hexagon::L2_ploadrif_io:
  case Hexagon::S2_pstorerit_io:
  case Hexagon::S2_pstorerif_io:
  case Hexagon::S4_storeiri_io:
  case Hexagon::S4_storeirit_io:
  case Hexagon::S4_storeirif_io:
    return isShiftedUInt<6,2>(Offset);

  case Hexagon::L2_ploadrdt_io:
  case Hexagon::L2_ploadrdf_io:
  case Hexagon::S2_pstorerdt_io:
  case Hexagon::S2_pstorerdf_io:
    return isShiftedUInt<6,3>(Offset);
  } // switch

  llvm_unreachable("No offset range is defined for this opcode. "
                   "Please define it in the above switch statement!");
}


bool HexagonInstrInfo::isVecAcc(const MachineInstr *MI) const {
  return MI && isV60VectorInstruction(MI) && isAccumulator(MI);
}


bool HexagonInstrInfo::isVecALU(const MachineInstr *MI) const {
  if (!MI)
    return false;
  const uint64_t F = get(MI->getOpcode()).TSFlags;
  const uint64_t V = ((F >> HexagonII::TypePos) & HexagonII::TypeMask);
  return
    V == HexagonII::TypeCVI_VA         ||
    V == HexagonII::TypeCVI_VA_DV;
}


bool HexagonInstrInfo::isVecUsableNextPacket(const MachineInstr *ProdMI,
      const MachineInstr *ConsMI) const {
  if (EnableACCForwarding && isVecAcc(ProdMI) && isVecAcc(ConsMI))
    return true;

  if (EnableALUForwarding && (isVecALU(ConsMI) || isLateSourceInstr(ConsMI)))
    return true;

  if (mayBeNewStore(ConsMI))
    return true;

  return false;
}


/// \brief Can these instructions execute at the same time in a bundle.
bool HexagonInstrInfo::canExecuteInBundle(const MachineInstr *First,
      const MachineInstr *Second) const {
  if (DisableNVSchedule)
    return false;
  if (mayBeNewStore(Second)) {
    // Make sure the definition of the first instruction is the value being
    // stored.
    const MachineOperand &Stored =
      Second->getOperand(Second->getNumOperands() - 1);
    if (!Stored.isReg())
      return false;
    for (unsigned i = 0, e = First->getNumOperands(); i < e; ++i) {
      const MachineOperand &Op = First->getOperand(i);
      if (Op.isReg() && Op.isDef() && Op.getReg() == Stored.getReg())
        return true;
    }
  }
  return false;
}


bool HexagonInstrInfo::hasEHLabel(const MachineBasicBlock *B) const {
  for (auto &I : *B)
    if (I.isEHLabel())
      return true;
  return false;
}


// Returns true if an instruction can be converted into a non-extended
// equivalent instruction.
bool HexagonInstrInfo::hasNonExtEquivalent(const MachineInstr *MI) const {
  short NonExtOpcode;
  // Check if the instruction has a register form that uses register in place
  // of the extended operand, if so return that as the non-extended form.
  if (Hexagon::getRegForm(MI->getOpcode()) >= 0)
    return true;

  if (MI->getDesc().mayLoad() || MI->getDesc().mayStore()) {
    // Check addressing mode and retrieve non-ext equivalent instruction.

    switch (getAddrMode(MI)) {
    case HexagonII::Absolute :
      // Load/store with absolute addressing mode can be converted into
      // base+offset mode.
      NonExtOpcode = Hexagon::getBaseWithImmOffset(MI->getOpcode());
      break;
    case HexagonII::BaseImmOffset :
      // Load/store with base+offset addressing mode can be converted into
      // base+register offset addressing mode. However left shift operand should
      // be set to 0.
      NonExtOpcode = Hexagon::getBaseWithRegOffset(MI->getOpcode());
      break;
    case HexagonII::BaseLongOffset:
      NonExtOpcode = Hexagon::getRegShlForm(MI->getOpcode());
      break;
    default:
      return false;
    }
    if (NonExtOpcode < 0)
      return false;
    return true;
  }
  return false;
}


bool HexagonInstrInfo::hasPseudoInstrPair(const MachineInstr *MI) const {
  return Hexagon::getRealHWInstr(MI->getOpcode(),
                                 Hexagon::InstrType_Pseudo) >= 0;
}


bool HexagonInstrInfo::hasUncondBranch(const MachineBasicBlock *B)
      const {
  MachineBasicBlock::const_iterator I = B->getFirstTerminator(), E = B->end();
  while (I != E) {
    if (I->isBarrier())
      return true;
    ++I;
  }
  return false;
}


// Returns true, if a LD insn can be promoted to a cur load.
bool HexagonInstrInfo::mayBeCurLoad(const MachineInstr *MI) const {
  auto &HST = MI->getParent()->getParent()->getSubtarget<HexagonSubtarget>();
  const uint64_t F = MI->getDesc().TSFlags;
  return ((F >> HexagonII::mayCVLoadPos) & HexagonII::mayCVLoadMask) &&
         HST.hasV60TOps();
}


// Returns true, if a ST insn can be promoted to a new-value store.
bool HexagonInstrInfo::mayBeNewStore(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::mayNVStorePos) & HexagonII::mayNVStoreMask;
}


bool HexagonInstrInfo::producesStall(const MachineInstr *ProdMI,
      const MachineInstr *ConsMI) const {
  // There is no stall when ProdMI is not a V60 vector.
  if (!isV60VectorInstruction(ProdMI))
    return false;

  // There is no stall when ProdMI and ConsMI are not dependent.
  if (!isDependent(ProdMI, ConsMI))
    return false;

  // When Forward Scheduling is enabled, there is no stall if ProdMI and ConsMI
  // are scheduled in consecutive packets.
  if (isVecUsableNextPacket(ProdMI, ConsMI))
    return false;

  return true;
}


bool HexagonInstrInfo::producesStall(const MachineInstr *MI,
      MachineBasicBlock::const_instr_iterator BII) const {
  // There is no stall when I is not a V60 vector.
  if (!isV60VectorInstruction(MI))
    return false;

  MachineBasicBlock::const_instr_iterator MII = BII;
  MachineBasicBlock::const_instr_iterator MIE = MII->getParent()->instr_end();

  if (!(*MII).isBundle()) {
    const MachineInstr *J = &*MII;
    if (!isV60VectorInstruction(J))
      return false;
    else if (isVecUsableNextPacket(J, MI))
      return false;
    return true;
  }

  for (++MII; MII != MIE && MII->isInsideBundle(); ++MII) {
    const MachineInstr *J = &*MII;
    if (producesStall(J, MI))
      return true;
  }
  return false;
}


bool HexagonInstrInfo::predCanBeUsedAsDotNew(const MachineInstr *MI,
      unsigned PredReg) const {
  for (unsigned opNum = 0; opNum < MI->getNumOperands(); opNum++) {
    const MachineOperand &MO = MI->getOperand(opNum);
    if (MO.isReg() && MO.isDef() && MO.isImplicit() && (MO.getReg() == PredReg))
      return false; // Predicate register must be explicitly defined.
  }

  // Hexagon Programmer's Reference says that decbin, memw_locked, and
  // memd_locked cannot be used as .new as well,
  // but we don't seem to have these instructions defined.
  return MI->getOpcode() != Hexagon::A4_tlbmatch;
}


bool HexagonInstrInfo::PredOpcodeHasJMP_c(unsigned Opcode) const {
  return (Opcode == Hexagon::J2_jumpt)      ||
         (Opcode == Hexagon::J2_jumpf)      ||
         (Opcode == Hexagon::J2_jumptnew)   ||
         (Opcode == Hexagon::J2_jumpfnew)   ||
         (Opcode == Hexagon::J2_jumptnewpt) ||
         (Opcode == Hexagon::J2_jumpfnewpt);
}


bool HexagonInstrInfo::predOpcodeHasNot(ArrayRef<MachineOperand> Cond) const {
  if (Cond.empty() || !isPredicated(Cond[0].getImm()))
    return false;
  return !isPredicatedTrue(Cond[0].getImm());
}


unsigned HexagonInstrInfo::getAddrMode(const MachineInstr* MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::AddrModePos) & HexagonII::AddrModeMask;
}


// Returns the base register in a memory access (load/store). The offset is
// returned in Offset and the access size is returned in AccessSize.
unsigned HexagonInstrInfo::getBaseAndOffset(const MachineInstr *MI,
      int &Offset, unsigned &AccessSize) const {
  // Return if it is not a base+offset type instruction or a MemOp.
  if (getAddrMode(MI) != HexagonII::BaseImmOffset &&
      getAddrMode(MI) != HexagonII::BaseLongOffset &&
      !isMemOp(MI) && !isPostIncrement(MI))
    return 0;

  // Since it is a memory access instruction, getMemAccessSize() should never
  // return 0.
  assert (getMemAccessSize(MI) &&
          "BaseImmOffset or BaseLongOffset or MemOp without accessSize");

  // Return Values of getMemAccessSize() are
  // 0 - Checked in the assert above.
  // 1, 2, 3, 4 & 7, 8 - The statement below is correct for all these.
  // MemAccessSize is represented as 1+log2(N) where N is size in bits.
  AccessSize = (1U << (getMemAccessSize(MI) - 1));

  unsigned basePos = 0, offsetPos = 0;
  if (!getBaseAndOffsetPosition(MI, basePos, offsetPos))
    return 0;

  // Post increment updates its EA after the mem access,
  // so we need to treat its offset as zero.
  if (isPostIncrement(MI))
    Offset = 0;
  else {
    Offset = MI->getOperand(offsetPos).getImm();
  }

  return MI->getOperand(basePos).getReg();
}


/// Return the position of the base and offset operands for this instruction.
bool HexagonInstrInfo::getBaseAndOffsetPosition(const MachineInstr *MI,
      unsigned &BasePos, unsigned &OffsetPos) const {
  // Deal with memops first.
  if (isMemOp(MI)) {
    assert (MI->getOperand(0).isReg() && MI->getOperand(1).isImm() &&
            "Bad Memop.");
    BasePos = 0;
    OffsetPos = 1;
  } else if (MI->mayStore()) {
    BasePos = 0;
    OffsetPos = 1;
  } else if (MI->mayLoad()) {
    BasePos = 1;
    OffsetPos = 2;
  } else
    return false;

  if (isPredicated(MI)) {
    BasePos++;
    OffsetPos++;
  }
  if (isPostIncrement(MI)) {
    BasePos++;
    OffsetPos++;
  }

  if (!MI->getOperand(BasePos).isReg() || !MI->getOperand(OffsetPos).isImm())
    return false;

  return true;
}


// Inserts branching instructions in reverse order of their occurence.
// e.g. jump_t t1 (i1)
// jump t2        (i2)
// Jumpers = {i2, i1}
SmallVector<MachineInstr*, 2> HexagonInstrInfo::getBranchingInstrs(
      MachineBasicBlock& MBB) const {
  SmallVector<MachineInstr*, 2> Jumpers;
  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::instr_iterator I = MBB.instr_end();
  if (I == MBB.instr_begin())
    return Jumpers;

  // A basic block may looks like this:
  //
  //  [   insn
  //     EH_LABEL
  //      insn
  //      insn
  //      insn
  //     EH_LABEL
  //      insn     ]
  //
  // It has two succs but does not have a terminator
  // Don't know how to handle it.
  do {
    --I;
    if (I->isEHLabel())
      return Jumpers;
  } while (I != MBB.instr_begin());

  I = MBB.instr_end();
  --I;

  while (I->isDebugValue()) {
    if (I == MBB.instr_begin())
      return Jumpers;
    --I;
  }
  if (!isUnpredicatedTerminator(&*I))
    return Jumpers;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;
  Jumpers.push_back(LastInst);
  MachineInstr *SecondLastInst = nullptr;
  // Find one more terminator if present.
  do {
    if (&*I != LastInst && !I->isBundle() && isUnpredicatedTerminator(&*I)) {
      if (!SecondLastInst) {
        SecondLastInst = &*I;
        Jumpers.push_back(SecondLastInst);
      } else // This is a third branch.
        return Jumpers;
    }
    if (I == MBB.instr_begin())
      break;
    --I;
  } while (true);
  return Jumpers;
}


// Returns Operand Index for the constant extended instruction.
unsigned HexagonInstrInfo::getCExtOpNum(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::ExtendableOpPos) & HexagonII::ExtendableOpMask;
}

// See if instruction could potentially be a duplex candidate.
// If so, return its group. Zero otherwise.
HexagonII::CompoundGroup HexagonInstrInfo::getCompoundCandidateGroup(
      const MachineInstr *MI) const {
  unsigned DstReg, SrcReg, Src1Reg, Src2Reg;

  switch (MI->getOpcode()) {
  default:
    return HexagonII::HCG_None;
  //
  // Compound pairs.
  // "p0=cmp.eq(Rs16,Rt16); if (p0.new) jump:nt #r9:2"
  // "Rd16=#U6 ; jump #r9:2"
  // "Rd16=Rs16 ; jump #r9:2"
  //
  case Hexagon::C2_cmpeq:
  case Hexagon::C2_cmpgt:
  case Hexagon::C2_cmpgtu:
    DstReg = MI->getOperand(0).getReg();
    Src1Reg = MI->getOperand(1).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (Hexagon::PredRegsRegClass.contains(DstReg) &&
        (Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        isIntRegForSubInst(Src1Reg) && isIntRegForSubInst(Src2Reg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::C2_cmpeqi:
  case Hexagon::C2_cmpgti:
  case Hexagon::C2_cmpgtui:
    // P0 = cmp.eq(Rs,#u2)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (Hexagon::PredRegsRegClass.contains(DstReg) &&
        (Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        isIntRegForSubInst(SrcReg) && MI->getOperand(2).isImm() &&
        ((isUInt<5>(MI->getOperand(2).getImm())) ||
         (MI->getOperand(2).getImm() == -1)))
      return HexagonII::HCG_A;
    break;
  case Hexagon::A2_tfr:
    // Rd = Rs
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::A2_tfrsi:
    // Rd = #u6
    // Do not test for #u6 size since the const is getting extended
    // regardless and compound could be formed.
    DstReg = MI->getOperand(0).getReg();
    if (isIntRegForSubInst(DstReg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::S2_tstbit_i:
    DstReg = MI->getOperand(0).getReg();
    Src1Reg = MI->getOperand(1).getReg();
    if (Hexagon::PredRegsRegClass.contains(DstReg) &&
        (Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        MI->getOperand(2).isImm() &&
        isIntRegForSubInst(Src1Reg) && (MI->getOperand(2).getImm() == 0))
      return HexagonII::HCG_A;
    break;
  // The fact that .new form is used pretty much guarantees
  // that predicate register will match. Nevertheless,
  // there could be some false positives without additional
  // checking.
  case Hexagon::J2_jumptnew:
  case Hexagon::J2_jumpfnew:
  case Hexagon::J2_jumptnewpt:
  case Hexagon::J2_jumpfnewpt:
    Src1Reg = MI->getOperand(0).getReg();
    if (Hexagon::PredRegsRegClass.contains(Src1Reg) &&
        (Hexagon::P0 == Src1Reg || Hexagon::P1 == Src1Reg))
      return HexagonII::HCG_B;
    break;
  // Transfer and jump:
  // Rd=#U6 ; jump #r9:2
  // Rd=Rs ; jump #r9:2
  // Do not test for jump range here.
  case Hexagon::J2_jump:
  case Hexagon::RESTORE_DEALLOC_RET_JMP_V4:
    return HexagonII::HCG_C;
    break;
  }

  return HexagonII::HCG_None;
}


// Returns -1 when there is no opcode found.
unsigned HexagonInstrInfo::getCompoundOpcode(const MachineInstr *GA,
      const MachineInstr *GB) const {
  assert(getCompoundCandidateGroup(GA) == HexagonII::HCG_A);
  assert(getCompoundCandidateGroup(GB) == HexagonII::HCG_B);
  if ((GA->getOpcode() != Hexagon::C2_cmpeqi) ||
      (GB->getOpcode() != Hexagon::J2_jumptnew))
    return -1;
  unsigned DestReg = GA->getOperand(0).getReg();
  if (!GB->readsRegister(DestReg))
    return -1;
  if (DestReg == Hexagon::P0)
    return Hexagon::J4_cmpeqi_tp0_jump_nt;
  if (DestReg == Hexagon::P1)
    return Hexagon::J4_cmpeqi_tp1_jump_nt;
  return -1;
}


int HexagonInstrInfo::getCondOpcode(int Opc, bool invertPredicate) const {
  enum Hexagon::PredSense inPredSense;
  inPredSense = invertPredicate ? Hexagon::PredSense_false :
                                  Hexagon::PredSense_true;
  int CondOpcode = Hexagon::getPredOpcode(Opc, inPredSense);
  if (CondOpcode >= 0) // Valid Conditional opcode/instruction
    return CondOpcode;

  // This switch case will be removed once all the instructions have been
  // modified to use relation maps.
  switch(Opc) {
  case Hexagon::TFRI_f:
    return !invertPredicate ? Hexagon::TFRI_cPt_f :
                              Hexagon::TFRI_cNotPt_f;
  }

  llvm_unreachable("Unexpected predicable instruction");
}


// Return the cur value instruction for a given store.
int HexagonInstrInfo::getDotCurOp(const MachineInstr* MI) const {
  switch (MI->getOpcode()) {
  default: llvm_unreachable("Unknown .cur type");
  case Hexagon::V6_vL32b_pi:
    return Hexagon::V6_vL32b_cur_pi;
  case Hexagon::V6_vL32b_ai:
    return Hexagon::V6_vL32b_cur_ai;
  //128B
  case Hexagon::V6_vL32b_pi_128B:
    return Hexagon::V6_vL32b_cur_pi_128B;
  case Hexagon::V6_vL32b_ai_128B:
    return Hexagon::V6_vL32b_cur_ai_128B;
  }
  return 0;
}



// The diagram below shows the steps involved in the conversion of a predicated
// store instruction to its .new predicated new-value form.
//
//               p.new NV store [ if(p0.new)memw(R0+#0)=R2.new ]
//                ^           ^
//               /             \ (not OK. it will cause new-value store to be
//              /               X conditional on p0.new while R2 producer is
//             /                 \ on p0)
//            /                   \.
//     p.new store                 p.old NV store
// [if(p0.new)memw(R0+#0)=R2]    [if(p0)memw(R0+#0)=R2.new]
//            ^                  ^
//             \                /
//              \              /
//               \            /
//                 p.old store
//             [if (p0)memw(R0+#0)=R2]
//
//
// The following set of instructions further explains the scenario where
// conditional new-value store becomes invalid when promoted to .new predicate
// form.
//
// { 1) if (p0) r0 = add(r1, r2)
//   2) p0 = cmp.eq(r3, #0) }
//
//   3) if (p0) memb(r1+#0) = r0  --> this instruction can't be grouped with
// the first two instructions because in instr 1, r0 is conditional on old value
// of p0 but its use in instr 3 is conditional on p0 modified by instr 2 which
// is not valid for new-value stores.
// Predicated new value stores (i.e. if (p0) memw(..)=r0.new) are excluded
// from the "Conditional Store" list. Because a predicated new value store
// would NOT be promoted to a double dot new store. See diagram below:
// This function returns yes for those stores that are predicated but not
// yet promoted to predicate dot new instructions.
//
//                          +---------------------+
//                    /-----| if (p0) memw(..)=r0 |---------\~
//                   ||     +---------------------+         ||
//          promote  ||       /\       /\                   ||  promote
//                   ||      /||\     /||\                  ||
//                  \||/    demote     ||                  \||/
//                   \/       ||       ||                   \/
//       +-------------------------+   ||   +-------------------------+
//       | if (p0.new) memw(..)=r0 |   ||   | if (p0) memw(..)=r0.new |
//       +-------------------------+   ||   +-------------------------+
//                        ||           ||         ||
//                        ||         demote      \||/
//                      promote        ||         \/ NOT possible
//                        ||           ||         /\~
//                       \||/          ||        /||\~
//                        \/           ||         ||
//                      +-----------------------------+
//                      | if (p0.new) memw(..)=r0.new |
//                      +-----------------------------+
//                           Double Dot New Store
//
// Returns the most basic instruction for the .new predicated instructions and
// new-value stores.
// For example, all of the following instructions will be converted back to the
// same instruction:
// 1) if (p0.new) memw(R0+#0) = R1.new  --->
// 2) if (p0) memw(R0+#0)= R1.new      -------> if (p0) memw(R0+#0) = R1
// 3) if (p0.new) memw(R0+#0) = R1      --->
//
// To understand the translation of instruction 1 to its original form, consider
// a packet with 3 instructions.
// { p0 = cmp.eq(R0,R1)
//   if (p0.new) R2 = add(R3, R4)
//   R5 = add (R3, R1)
// }
// if (p0) memw(R5+#0) = R2 <--- trying to include it in the previous packet
//
// This instruction can be part of the previous packet only if both p0 and R2
// are promoted to .new values. This promotion happens in steps, first
// predicate register is promoted to .new and in the next iteration R2 is
// promoted. Therefore, in case of dependence check failure (due to R5) during
// next iteration, it should be converted back to its most basic form.


// Return the new value instruction for a given store.
int HexagonInstrInfo::getDotNewOp(const MachineInstr* MI) const {
  int NVOpcode = Hexagon::getNewValueOpcode(MI->getOpcode());
  if (NVOpcode >= 0) // Valid new-value store instruction.
    return NVOpcode;

  switch (MI->getOpcode()) {
  default: llvm_unreachable("Unknown .new type");
  case Hexagon::S4_storerb_ur:
    return Hexagon::S4_storerbnew_ur;

  case Hexagon::S2_storerb_pci:
    return Hexagon::S2_storerb_pci;

  case Hexagon::S2_storeri_pci:
    return Hexagon::S2_storeri_pci;

  case Hexagon::S2_storerh_pci:
    return Hexagon::S2_storerh_pci;

  case Hexagon::S2_storerd_pci:
    return Hexagon::S2_storerd_pci;

  case Hexagon::S2_storerf_pci:
    return Hexagon::S2_storerf_pci;

  case Hexagon::V6_vS32b_ai:
    return Hexagon::V6_vS32b_new_ai;

  case Hexagon::V6_vS32b_pi:
    return Hexagon::V6_vS32b_new_pi;

  // 128B
  case Hexagon::V6_vS32b_ai_128B:
    return Hexagon::V6_vS32b_new_ai_128B;

  case Hexagon::V6_vS32b_pi_128B:
    return Hexagon::V6_vS32b_new_pi_128B;
  }
  return 0;
}

// Returns the opcode to use when converting MI, which is a conditional jump,
// into a conditional instruction which uses the .new value of the predicate.
// We also use branch probabilities to add a hint to the jump.
int HexagonInstrInfo::getDotNewPredJumpOp(const MachineInstr *MI,
      const MachineBranchProbabilityInfo *MBPI) const {
  // We assume that block can have at most two successors.
  bool taken = false;
  const MachineBasicBlock *Src = MI->getParent();
  const MachineOperand *BrTarget = &MI->getOperand(1);
  const MachineBasicBlock *Dst = BrTarget->getMBB();

  const BranchProbability Prediction = MBPI->getEdgeProbability(Src, Dst);
  if (Prediction >= BranchProbability(1,2))
    taken = true;

  switch (MI->getOpcode()) {
  case Hexagon::J2_jumpt:
    return taken ? Hexagon::J2_jumptnewpt : Hexagon::J2_jumptnew;
  case Hexagon::J2_jumpf:
    return taken ? Hexagon::J2_jumpfnewpt : Hexagon::J2_jumpfnew;

  default:
    llvm_unreachable("Unexpected jump instruction.");
  }
}


// Return .new predicate version for an instruction.
int HexagonInstrInfo::getDotNewPredOp(const MachineInstr *MI,
      const MachineBranchProbabilityInfo *MBPI) const {
  int NewOpcode = Hexagon::getPredNewOpcode(MI->getOpcode());
  if (NewOpcode >= 0) // Valid predicate new instruction
    return NewOpcode;

  switch (MI->getOpcode()) {
  // Condtional Jumps
  case Hexagon::J2_jumpt:
  case Hexagon::J2_jumpf:
    return getDotNewPredJumpOp(MI, MBPI);

  default:
    assert(0 && "Unknown .new type");
  }
  return 0;
}


int HexagonInstrInfo::getDotOldOp(const int opc) const {
  int NewOp = opc;
  if (isPredicated(NewOp) && isPredicatedNew(NewOp)) { // Get predicate old form
    NewOp = Hexagon::getPredOldOpcode(NewOp);
    assert(NewOp >= 0 &&
           "Couldn't change predicate new instruction to its old form.");
  }

  if (isNewValueStore(NewOp)) { // Convert into non-new-value format
    NewOp = Hexagon::getNonNVStore(NewOp);
    assert(NewOp >= 0 && "Couldn't change new-value store to its old form.");
  }
  return NewOp;
}


// See if instruction could potentially be a duplex candidate.
// If so, return its group. Zero otherwise.
HexagonII::SubInstructionGroup HexagonInstrInfo::getDuplexCandidateGroup(
      const MachineInstr *MI) const {
  unsigned DstReg, SrcReg, Src1Reg, Src2Reg;
  auto &HRI = getRegisterInfo();

  switch (MI->getOpcode()) {
  default:
    return HexagonII::HSIG_None;
  //
  // Group L1:
  //
  // Rd = memw(Rs+#u4:2)
  // Rd = memub(Rs+#u4:0)
  case Hexagon::L2_loadri_io:
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    // Special case this one from Group L2.
    // Rd = memw(r29+#u5:2)
    if (isIntRegForSubInst(DstReg)) {
      if (Hexagon::IntRegsRegClass.contains(SrcReg) &&
          HRI.getStackRegister() == SrcReg &&
          MI->getOperand(2).isImm() &&
          isShiftedUInt<5,2>(MI->getOperand(2).getImm()))
        return HexagonII::HSIG_L2;
      // Rd = memw(Rs+#u4:2)
      if (isIntRegForSubInst(SrcReg) &&
          (MI->getOperand(2).isImm() &&
          isShiftedUInt<4,2>(MI->getOperand(2).getImm())))
        return HexagonII::HSIG_L1;
    }
    break;
  case Hexagon::L2_loadrub_io:
    // Rd = memub(Rs+#u4:0)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg) &&
        MI->getOperand(2).isImm() && isUInt<4>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_L1;
    break;
  //
  // Group L2:
  //
  // Rd = memh/memuh(Rs+#u3:1)
  // Rd = memb(Rs+#u3:0)
  // Rd = memw(r29+#u5:2) - Handled above.
  // Rdd = memd(r29+#u5:3)
  // deallocframe
  // [if ([!]p0[.new])] dealloc_return
  // [if ([!]p0[.new])] jumpr r31
  case Hexagon::L2_loadrh_io:
  case Hexagon::L2_loadruh_io:
    // Rd = memh/memuh(Rs+#u3:1)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg) &&
        MI->getOperand(2).isImm() &&
        isShiftedUInt<3,1>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_L2;
    break;
  case Hexagon::L2_loadrb_io:
    // Rd = memb(Rs+#u3:0)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg) &&
        MI->getOperand(2).isImm() &&
        isUInt<3>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_L2;
    break;
  case Hexagon::L2_loadrd_io:
    // Rdd = memd(r29+#u5:3)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isDblRegForSubInst(DstReg, HRI) &&
        Hexagon::IntRegsRegClass.contains(SrcReg) &&
        HRI.getStackRegister() == SrcReg &&
        MI->getOperand(2).isImm() &&
        isShiftedUInt<5,3>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_L2;
    break;
  // dealloc_return is not documented in Hexagon Manual, but marked
  // with A_SUBINSN attribute in iset_v4classic.py.
  case Hexagon::RESTORE_DEALLOC_RET_JMP_V4:
  case Hexagon::L4_return:
  case Hexagon::L2_deallocframe:
    return HexagonII::HSIG_L2;
  case Hexagon::EH_RETURN_JMPR:
  case Hexagon::JMPret :
    // jumpr r31
    // Actual form JMPR %PC<imp-def>, %R31<imp-use>, %R0<imp-use,internal>.
    DstReg = MI->getOperand(0).getReg();
    if (Hexagon::IntRegsRegClass.contains(DstReg) && (Hexagon::R31 == DstReg))
      return HexagonII::HSIG_L2;
    break;
  case Hexagon::JMPrett:
  case Hexagon::JMPretf:
  case Hexagon::JMPrettnewpt:
  case Hexagon::JMPretfnewpt :
  case Hexagon::JMPrettnew :
  case Hexagon::JMPretfnew :
    DstReg = MI->getOperand(1).getReg();
    SrcReg = MI->getOperand(0).getReg();
    // [if ([!]p0[.new])] jumpr r31
    if ((Hexagon::PredRegsRegClass.contains(SrcReg) &&
        (Hexagon::P0 == SrcReg)) &&
        (Hexagon::IntRegsRegClass.contains(DstReg) && (Hexagon::R31 == DstReg)))
      return HexagonII::HSIG_L2;
     break;
  case Hexagon::L4_return_t :
  case Hexagon::L4_return_f :
  case Hexagon::L4_return_tnew_pnt :
  case Hexagon::L4_return_fnew_pnt :
  case Hexagon::L4_return_tnew_pt :
  case Hexagon::L4_return_fnew_pt :
    // [if ([!]p0[.new])] dealloc_return
    SrcReg = MI->getOperand(0).getReg();
    if (Hexagon::PredRegsRegClass.contains(SrcReg) && (Hexagon::P0 == SrcReg))
      return HexagonII::HSIG_L2;
    break;
  //
  // Group S1:
  //
  // memw(Rs+#u4:2) = Rt
  // memb(Rs+#u4:0) = Rt
  case Hexagon::S2_storeri_io:
    // Special case this one from Group S2.
    // memw(r29+#u5:2) = Rt
    Src1Reg = MI->getOperand(0).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (Hexagon::IntRegsRegClass.contains(Src1Reg) &&
        isIntRegForSubInst(Src2Reg) &&
        HRI.getStackRegister() == Src1Reg && MI->getOperand(1).isImm() &&
        isShiftedUInt<5,2>(MI->getOperand(1).getImm()))
      return HexagonII::HSIG_S2;
    // memw(Rs+#u4:2) = Rt
    if (isIntRegForSubInst(Src1Reg) && isIntRegForSubInst(Src2Reg) &&
        MI->getOperand(1).isImm() &&
        isShiftedUInt<4,2>(MI->getOperand(1).getImm()))
      return HexagonII::HSIG_S1;
    break;
  case Hexagon::S2_storerb_io:
    // memb(Rs+#u4:0) = Rt
    Src1Reg = MI->getOperand(0).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (isIntRegForSubInst(Src1Reg) && isIntRegForSubInst(Src2Reg) &&
        MI->getOperand(1).isImm() && isUInt<4>(MI->getOperand(1).getImm()))
      return HexagonII::HSIG_S1;
    break;
  //
  // Group S2:
  //
  // memh(Rs+#u3:1) = Rt
  // memw(r29+#u5:2) = Rt
  // memd(r29+#s6:3) = Rtt
  // memw(Rs+#u4:2) = #U1
  // memb(Rs+#u4) = #U1
  // allocframe(#u5:3)
  case Hexagon::S2_storerh_io:
    // memh(Rs+#u3:1) = Rt
    Src1Reg = MI->getOperand(0).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (isIntRegForSubInst(Src1Reg) && isIntRegForSubInst(Src2Reg) &&
        MI->getOperand(1).isImm() &&
        isShiftedUInt<3,1>(MI->getOperand(1).getImm()))
      return HexagonII::HSIG_S1;
    break;
  case Hexagon::S2_storerd_io:
    // memd(r29+#s6:3) = Rtt
    Src1Reg = MI->getOperand(0).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (isDblRegForSubInst(Src2Reg, HRI) &&
        Hexagon::IntRegsRegClass.contains(Src1Reg) &&
        HRI.getStackRegister() == Src1Reg && MI->getOperand(1).isImm() &&
        isShiftedInt<6,3>(MI->getOperand(1).getImm()))
      return HexagonII::HSIG_S2;
    break;
  case Hexagon::S4_storeiri_io:
    // memw(Rs+#u4:2) = #U1
    Src1Reg = MI->getOperand(0).getReg();
    if (isIntRegForSubInst(Src1Reg) && MI->getOperand(1).isImm() &&
        isShiftedUInt<4,2>(MI->getOperand(1).getImm()) &&
        MI->getOperand(2).isImm() && isUInt<1>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_S2;
    break;
  case Hexagon::S4_storeirb_io:
    // memb(Rs+#u4) = #U1
    Src1Reg = MI->getOperand(0).getReg();
    if (isIntRegForSubInst(Src1Reg) && MI->getOperand(1).isImm() &&
        isUInt<4>(MI->getOperand(1).getImm()) && MI->getOperand(2).isImm() &&
        MI->getOperand(2).isImm() && isUInt<1>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_S2;
    break;
  case Hexagon::S2_allocframe:
    if (MI->getOperand(0).isImm() &&
        isShiftedUInt<5,3>(MI->getOperand(0).getImm()))
      return HexagonII::HSIG_S1;
    break;
  //
  // Group A:
  //
  // Rx = add(Rx,#s7)
  // Rd = Rs
  // Rd = #u6
  // Rd = #-1
  // if ([!]P0[.new]) Rd = #0
  // Rd = add(r29,#u6:2)
  // Rx = add(Rx,Rs)
  // P0 = cmp.eq(Rs,#u2)
  // Rdd = combine(#0,Rs)
  // Rdd = combine(Rs,#0)
  // Rdd = combine(#u2,#U2)
  // Rd = add(Rs,#1)
  // Rd = add(Rs,#-1)
  // Rd = sxth/sxtb/zxtb/zxth(Rs)
  // Rd = and(Rs,#1)
  case Hexagon::A2_addi:
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg)) {
      // Rd = add(r29,#u6:2)
      if (Hexagon::IntRegsRegClass.contains(SrcReg) &&
        HRI.getStackRegister() == SrcReg && MI->getOperand(2).isImm() &&
        isShiftedUInt<6,2>(MI->getOperand(2).getImm()))
        return HexagonII::HSIG_A;
      // Rx = add(Rx,#s7)
      if ((DstReg == SrcReg) && MI->getOperand(2).isImm() &&
          isInt<7>(MI->getOperand(2).getImm()))
        return HexagonII::HSIG_A;
      // Rd = add(Rs,#1)
      // Rd = add(Rs,#-1)
      if (isIntRegForSubInst(SrcReg) && MI->getOperand(2).isImm() &&
          ((MI->getOperand(2).getImm() == 1) ||
          (MI->getOperand(2).getImm() == -1)))
        return HexagonII::HSIG_A;
    }
    break;
  case Hexagon::A2_add:
    // Rx = add(Rx,Rs)
    DstReg = MI->getOperand(0).getReg();
    Src1Reg = MI->getOperand(1).getReg();
    Src2Reg = MI->getOperand(2).getReg();
    if (isIntRegForSubInst(DstReg) && (DstReg == Src1Reg) &&
        isIntRegForSubInst(Src2Reg))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A2_andir:
    // Same as zxtb.
    // Rd16=and(Rs16,#255)
    // Rd16=and(Rs16,#1)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg) &&
        MI->getOperand(2).isImm() &&
        ((MI->getOperand(2).getImm() == 1) ||
        (MI->getOperand(2).getImm() == 255)))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A2_tfr:
    // Rd = Rs
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A2_tfrsi:
    // Rd = #u6
    // Do not test for #u6 size since the const is getting extended
    // regardless and compound could be formed.
    // Rd = #-1
    DstReg = MI->getOperand(0).getReg();
    if (isIntRegForSubInst(DstReg))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::C2_cmoveit:
  case Hexagon::C2_cmovenewit:
  case Hexagon::C2_cmoveif:
  case Hexagon::C2_cmovenewif:
    // if ([!]P0[.new]) Rd = #0
    // Actual form:
    // %R16<def> = C2_cmovenewit %P0<internal>, 0, %R16<imp-use,undef>;
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) &&
        Hexagon::PredRegsRegClass.contains(SrcReg) && Hexagon::P0 == SrcReg &&
        MI->getOperand(2).isImm() && MI->getOperand(2).getImm() == 0)
      return HexagonII::HSIG_A;
    break;
  case Hexagon::C2_cmpeqi:
    // P0 = cmp.eq(Rs,#u2)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (Hexagon::PredRegsRegClass.contains(DstReg) &&
        Hexagon::P0 == DstReg && isIntRegForSubInst(SrcReg) &&
        MI->getOperand(2).isImm() && isUInt<2>(MI->getOperand(2).getImm()))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A2_combineii:
  case Hexagon::A4_combineii:
    // Rdd = combine(#u2,#U2)
    DstReg = MI->getOperand(0).getReg();
    if (isDblRegForSubInst(DstReg, HRI) &&
        ((MI->getOperand(1).isImm() && isUInt<2>(MI->getOperand(1).getImm())) ||
        (MI->getOperand(1).isGlobal() &&
        isUInt<2>(MI->getOperand(1).getOffset()))) &&
        ((MI->getOperand(2).isImm() && isUInt<2>(MI->getOperand(2).getImm())) ||
        (MI->getOperand(2).isGlobal() &&
        isUInt<2>(MI->getOperand(2).getOffset()))))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A4_combineri:
    // Rdd = combine(Rs,#0)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isDblRegForSubInst(DstReg, HRI) && isIntRegForSubInst(SrcReg) &&
        ((MI->getOperand(2).isImm() && MI->getOperand(2).getImm() == 0) ||
        (MI->getOperand(2).isGlobal() && MI->getOperand(2).getOffset() == 0)))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A4_combineir:
    // Rdd = combine(#0,Rs)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(2).getReg();
    if (isDblRegForSubInst(DstReg, HRI) && isIntRegForSubInst(SrcReg) &&
        ((MI->getOperand(1).isImm() && MI->getOperand(1).getImm() == 0) ||
        (MI->getOperand(1).isGlobal() && MI->getOperand(1).getOffset() == 0)))
      return HexagonII::HSIG_A;
    break;
  case Hexagon::A2_sxtb:
  case Hexagon::A2_sxth:
  case Hexagon::A2_zxtb:
  case Hexagon::A2_zxth:
    // Rd = sxth/sxtb/zxtb/zxth(Rs)
    DstReg = MI->getOperand(0).getReg();
    SrcReg = MI->getOperand(1).getReg();
    if (isIntRegForSubInst(DstReg) && isIntRegForSubInst(SrcReg))
      return HexagonII::HSIG_A;
    break;
  }

  return HexagonII::HSIG_None;
}


short HexagonInstrInfo::getEquivalentHWInstr(const MachineInstr *MI) const {
  return Hexagon::getRealHWInstr(MI->getOpcode(), Hexagon::InstrType_Real);
}


// Return first non-debug instruction in the basic block.
MachineInstr *HexagonInstrInfo::getFirstNonDbgInst(MachineBasicBlock *BB)
      const {
  for (auto MII = BB->instr_begin(), End = BB->instr_end(); MII != End; MII++) {
    MachineInstr *MI = &*MII;
    if (MI->isDebugValue())
      continue;
    return MI;
  }
  return nullptr;
}


unsigned HexagonInstrInfo::getInstrTimingClassLatency(
      const InstrItineraryData *ItinData, const MachineInstr *MI) const {
  // Default to one cycle for no itinerary. However, an "empty" itinerary may
  // still have a MinLatency property, which getStageLatency checks.
  if (!ItinData)
    return getInstrLatency(ItinData, MI);

  // Get the latency embedded in the itinerary. If we're not using timing class
  // latencies or if we using BSB scheduling, then restrict the maximum latency
  // to 1 (that is, either 0 or 1).
  if (MI->isTransient())
    return 0;
  unsigned Latency = ItinData->getStageLatency(MI->getDesc().getSchedClass());
  if (!EnableTimingClassLatency ||
      MI->getParent()->getParent()->getSubtarget<HexagonSubtarget>().
      useBSBScheduling())
    if (Latency > 1)
      Latency = 1;
  return Latency;
}


// inverts the predication logic.
// p -> NotP
// NotP -> P
bool HexagonInstrInfo::getInvertedPredSense(
      SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.empty())
    return false;
  unsigned Opc = getInvertedPredicatedOpcode(Cond[0].getImm());
  Cond[0].setImm(Opc);
  return true;
}


unsigned HexagonInstrInfo::getInvertedPredicatedOpcode(const int Opc) const {
  int InvPredOpcode;
  InvPredOpcode = isPredicatedTrue(Opc) ? Hexagon::getFalsePredOpcode(Opc)
                                        : Hexagon::getTruePredOpcode(Opc);
  if (InvPredOpcode >= 0) // Valid instruction with the inverted predicate.
    return InvPredOpcode;

  llvm_unreachable("Unexpected predicated instruction");
}


// Returns the max value that doesn't need to be extended.
int HexagonInstrInfo::getMaxValue(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  unsigned isSigned = (F >> HexagonII::ExtentSignedPos)
                    & HexagonII::ExtentSignedMask;
  unsigned bits =  (F >> HexagonII::ExtentBitsPos)
                    & HexagonII::ExtentBitsMask;

  if (isSigned) // if value is signed
    return ~(-1U << (bits - 1));
  else
    return ~(-1U << bits);
}


unsigned HexagonInstrInfo::getMemAccessSize(const MachineInstr* MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::MemAccessSizePos) & HexagonII::MemAccesSizeMask;
}


// Returns the min value that doesn't need to be extended.
int HexagonInstrInfo::getMinValue(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  unsigned isSigned = (F >> HexagonII::ExtentSignedPos)
                    & HexagonII::ExtentSignedMask;
  unsigned bits =  (F >> HexagonII::ExtentBitsPos)
                    & HexagonII::ExtentBitsMask;

  if (isSigned) // if value is signed
    return -1U << (bits - 1);
  else
    return 0;
}


// Returns opcode of the non-extended equivalent instruction.
short HexagonInstrInfo::getNonExtOpcode(const MachineInstr *MI) const {
  // Check if the instruction has a register form that uses register in place
  // of the extended operand, if so return that as the non-extended form.
  short NonExtOpcode = Hexagon::getRegForm(MI->getOpcode());
    if (NonExtOpcode >= 0)
      return NonExtOpcode;

  if (MI->getDesc().mayLoad() || MI->getDesc().mayStore()) {
    // Check addressing mode and retrieve non-ext equivalent instruction.
    switch (getAddrMode(MI)) {
    case HexagonII::Absolute :
      return Hexagon::getBaseWithImmOffset(MI->getOpcode());
    case HexagonII::BaseImmOffset :
      return Hexagon::getBaseWithRegOffset(MI->getOpcode());
    case HexagonII::BaseLongOffset:
      return Hexagon::getRegShlForm(MI->getOpcode());

    default:
      return -1;
    }
  }
  return -1;
}


bool HexagonInstrInfo::getPredReg(ArrayRef<MachineOperand> Cond,
      unsigned &PredReg, unsigned &PredRegPos, unsigned &PredRegFlags) const {
  if (Cond.empty())
    return false;
  assert(Cond.size() == 2);
  if (isNewValueJump(Cond[0].getImm()) || Cond[1].isMBB()) {
     DEBUG(dbgs() << "No predregs for new-value jumps/endloop");
     return false;
  }
  PredReg = Cond[1].getReg();
  PredRegPos = 1;
  // See IfConversion.cpp why we add RegState::Implicit | RegState::Undef
  PredRegFlags = 0;
  if (Cond[1].isImplicit())
    PredRegFlags = RegState::Implicit;
  if (Cond[1].isUndef())
    PredRegFlags |= RegState::Undef;
  return true;
}


short HexagonInstrInfo::getPseudoInstrPair(const MachineInstr *MI) const {
  return Hexagon::getRealHWInstr(MI->getOpcode(), Hexagon::InstrType_Pseudo);
}


short HexagonInstrInfo::getRegForm(const MachineInstr *MI) const {
  return Hexagon::getRegForm(MI->getOpcode());
}


// Return the number of bytes required to encode the instruction.
// Hexagon instructions are fixed length, 4 bytes, unless they
// use a constant extender, which requires another 4 bytes.
// For debug instructions and prolog labels, return 0.
unsigned HexagonInstrInfo::getSize(const MachineInstr *MI) const {
  if (MI->isDebugValue() || MI->isPosition())
    return 0;

  unsigned Size = MI->getDesc().getSize();
  if (!Size)
    // Assume the default insn size in case it cannot be determined
    // for whatever reason.
    Size = HEXAGON_INSTR_SIZE;

  if (isConstExtended(MI) || isExtended(MI))
    Size += HEXAGON_INSTR_SIZE;

  // Try and compute number of instructions in asm.
  if (BranchRelaxAsmLarge && MI->getOpcode() == Hexagon::INLINEASM) {
    const MachineBasicBlock &MBB = *MI->getParent();
    const MachineFunction *MF = MBB.getParent();
    const MCAsmInfo *MAI = MF->getTarget().getMCAsmInfo();

    // Count the number of register definitions to find the asm string.
    unsigned NumDefs = 0;
    for (; MI->getOperand(NumDefs).isReg() && MI->getOperand(NumDefs).isDef();
         ++NumDefs)
      assert(NumDefs != MI->getNumOperands()-2 && "No asm string?");

    assert(MI->getOperand(NumDefs).isSymbol() && "No asm string?");
    // Disassemble the AsmStr and approximate number of instructions.
    const char *AsmStr = MI->getOperand(NumDefs).getSymbolName();
    Size = getInlineAsmLength(AsmStr, *MAI);
  }

  return Size;
}


uint64_t HexagonInstrInfo::getType(const MachineInstr* MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
  return (F >> HexagonII::TypePos) & HexagonII::TypeMask;
}


unsigned HexagonInstrInfo::getUnits(const MachineInstr* MI) const {
  const TargetSubtargetInfo &ST = MI->getParent()->getParent()->getSubtarget();
  const InstrItineraryData &II = *ST.getInstrItineraryData();
  const InstrStage &IS = *II.beginStage(MI->getDesc().getSchedClass());

  return IS.getUnits();
}


unsigned HexagonInstrInfo::getValidSubTargets(const unsigned Opcode) const {
  const uint64_t F = get(Opcode).TSFlags;
  return (F >> HexagonII::validSubTargetPos) & HexagonII::validSubTargetMask;
}


// Calculate size of the basic block without debug instructions.
unsigned HexagonInstrInfo::nonDbgBBSize(const MachineBasicBlock *BB) const {
  return nonDbgMICount(BB->instr_begin(), BB->instr_end());
}


unsigned HexagonInstrInfo::nonDbgBundleSize(
      MachineBasicBlock::const_iterator BundleHead) const {
  assert(BundleHead->isBundle() && "Not a bundle header");
  auto MII = BundleHead.getInstrIterator();
  // Skip the bundle header.
  return nonDbgMICount(++MII, getBundleEnd(BundleHead));
}


/// immediateExtend - Changes the instruction in place to one using an immediate
/// extender.
void HexagonInstrInfo::immediateExtend(MachineInstr *MI) const {
  assert((isExtendable(MI)||isConstExtended(MI)) &&
                               "Instruction must be extendable");
  // Find which operand is extendable.
  short ExtOpNum = getCExtOpNum(MI);
  MachineOperand &MO = MI->getOperand(ExtOpNum);
  // This needs to be something we understand.
  assert((MO.isMBB() || MO.isImm()) &&
         "Branch with unknown extendable field type");
  // Mark given operand as extended.
  MO.addTargetFlag(HexagonII::HMOTF_ConstExtended);
}


bool HexagonInstrInfo::invertAndChangeJumpTarget(
      MachineInstr* MI, MachineBasicBlock* NewTarget) const {
  DEBUG(dbgs() << "\n[invertAndChangeJumpTarget] to BB#"
               << NewTarget->getNumber(); MI->dump(););
  assert(MI->isBranch());
  unsigned NewOpcode = getInvertedPredicatedOpcode(MI->getOpcode());
  int TargetPos = MI->getNumOperands() - 1;
  // In general branch target is the last operand,
  // but some implicit defs added at the end might change it.
  while ((TargetPos > -1) && !MI->getOperand(TargetPos).isMBB())
    --TargetPos;
  assert((TargetPos >= 0) && MI->getOperand(TargetPos).isMBB());
  MI->getOperand(TargetPos).setMBB(NewTarget);
  if (EnableBranchPrediction && isPredicatedNew(MI)) {
    NewOpcode = reversePrediction(NewOpcode);
  }
  MI->setDesc(get(NewOpcode));
  return true;
}


void HexagonInstrInfo::genAllInsnTimingClasses(MachineFunction &MF) const {
  /* +++ The code below is used to generate complete set of Hexagon Insn +++ */
  MachineFunction::iterator A = MF.begin();
  MachineBasicBlock &B = *A;
  MachineBasicBlock::iterator I = B.begin();
  MachineInstr *MI = &*I;
  DebugLoc DL = MI->getDebugLoc();
  MachineInstr *NewMI;

  for (unsigned insn = TargetOpcode::GENERIC_OP_END+1;
       insn < Hexagon::INSTRUCTION_LIST_END; ++insn) {
    NewMI = BuildMI(B, MI, DL, get(insn));
    DEBUG(dbgs() << "\n" << getName(NewMI->getOpcode()) <<
          "  Class: " << NewMI->getDesc().getSchedClass());
    NewMI->eraseFromParent();
  }
  /* --- The code above is used to generate complete set of Hexagon Insn --- */
}


// inverts the predication logic.
// p -> NotP
// NotP -> P
bool HexagonInstrInfo::reversePredSense(MachineInstr* MI) const {
  DEBUG(dbgs() << "\nTrying to reverse pred. sense of:"; MI->dump());
  MI->setDesc(get(getInvertedPredicatedOpcode(MI->getOpcode())));
  return true;
}


// Reverse the branch prediction.
unsigned HexagonInstrInfo::reversePrediction(unsigned Opcode) const {
  int PredRevOpcode = -1;
  if (isPredictedTaken(Opcode))
    PredRevOpcode = Hexagon::notTakenBranchPrediction(Opcode);
  else
    PredRevOpcode = Hexagon::takenBranchPrediction(Opcode);
  assert(PredRevOpcode > 0);
  return PredRevOpcode;
}


// TODO: Add more rigorous validation.
bool HexagonInstrInfo::validateBranchCond(const ArrayRef<MachineOperand> &Cond)
      const {
  return Cond.empty() || (Cond[0].isImm() && (Cond.size() != 1));
}

