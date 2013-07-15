//===-- Mips16InstrInfo.cpp - Mips16 Instruction Information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips16 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Mips16InstrInfo.h"
#include "InstPrinter/MipsInstPrinter.h"
#include "MipsMachineFunction.h"
#include "MipsTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

static cl::opt<bool> NeverUseSaveRestore(
  "mips16-never-use-save-restore",
  cl::init(false),
  cl::desc("For testing ability to adjust stack pointer "
           "without save/restore instruction"),
  cl::Hidden);


Mips16InstrInfo::Mips16InstrInfo(MipsTargetMachine &tm)
  : MipsInstrInfo(tm, Mips::BimmX16),
    RI(*tm.getSubtargetImpl()) {}

const MipsRegisterInfo &Mips16InstrInfo::getRegisterInfo() const {
  return RI;
}

/// isLoadFromStackSlot - If the specified machine instruction is a direct
/// load from a stack slot, return the virtual or physical register number of
/// the destination along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than loading from the stack slot.
unsigned Mips16InstrInfo::
isLoadFromStackSlot(const MachineInstr *MI, int &FrameIndex) const
{
  return 0;
}

/// isStoreToStackSlot - If the specified machine instruction is a direct
/// store to a stack slot, return the virtual or physical register number of
/// the source reg along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than storing to the stack slot.
unsigned Mips16InstrInfo::
isStoreToStackSlot(const MachineInstr *MI, int &FrameIndex) const
{
  return 0;
}

void Mips16InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I, DebugLoc DL,
                                  unsigned DestReg, unsigned SrcReg,
                                  bool KillSrc) const {
  unsigned Opc = 0;

  if (Mips::CPU16RegsRegClass.contains(DestReg) &&
      Mips::CPURegsRegClass.contains(SrcReg))
    Opc = Mips::MoveR3216;
  else if (Mips::CPURegsRegClass.contains(DestReg) &&
           Mips::CPU16RegsRegClass.contains(SrcReg))
    Opc = Mips::Move32R16;
  else if ((SrcReg == Mips::HI) &&
           (Mips::CPU16RegsRegClass.contains(DestReg)))
    Opc = Mips::Mfhi16, SrcReg = 0;

  else if ((SrcReg == Mips::LO) &&
           (Mips::CPU16RegsRegClass.contains(DestReg)))
    Opc = Mips::Mflo16, SrcReg = 0;


  assert(Opc && "Cannot copy registers");

  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc));

  if (DestReg)
    MIB.addReg(DestReg, RegState::Define);

  if (SrcReg)
    MIB.addReg(SrcReg, getKillRegState(KillSrc));
}

void Mips16InstrInfo::
storeRegToStack(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                unsigned SrcReg, bool isKill, int FI,
                const TargetRegisterClass *RC, const TargetRegisterInfo *TRI,
                int64_t Offset) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOStore);
  unsigned Opc = 0;
  if (Mips::CPU16RegsRegClass.hasSubClassEq(RC))
    Opc = Mips::SwRxSpImmX16;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc)).addReg(SrcReg, getKillRegState(isKill))
    .addFrameIndex(FI).addImm(Offset).addMemOperand(MMO);
}

void Mips16InstrInfo::
loadRegFromStack(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                 unsigned DestReg, int FI, const TargetRegisterClass *RC,
                 const TargetRegisterInfo *TRI, int64_t Offset) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOLoad);
  unsigned Opc = 0;

  if (Mips::CPU16RegsRegClass.hasSubClassEq(RC))
    Opc = Mips::LwRxSpImmX16;
  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI).addImm(Offset)
    .addMemOperand(MMO);
}

bool Mips16InstrInfo::expandPostRAPseudo(MachineBasicBlock::iterator MI) const {
  MachineBasicBlock &MBB = *MI->getParent();
  switch(MI->getDesc().getOpcode()) {
  default:
    return false;
  case Mips::RetRA16:
    ExpandRetRA16(MBB, MI, Mips::JrcRa16);
    break;
  }

  MBB.erase(MI);
  return true;
}

/// GetOppositeBranchOpc - Return the inverse of the specified
/// opcode, e.g. turning BEQ to BNE.
unsigned Mips16InstrInfo::getOppositeBranchOpc(unsigned Opc) const {
  switch (Opc) {
  default:  llvm_unreachable("Illegal opcode!");
  case Mips::BeqzRxImmX16: return Mips::BnezRxImmX16;
  case Mips::BnezRxImmX16: return Mips::BeqzRxImmX16;
  case Mips::BteqzT8CmpX16: return Mips::BtnezT8CmpX16;
  case Mips::BteqzT8SltX16: return Mips::BtnezT8SltX16;
  case Mips::BteqzT8SltiX16: return Mips::BtnezT8SltiX16;
  case Mips::BtnezX16: return Mips::BteqzX16;
  case Mips::BtnezT8CmpiX16: return Mips::BteqzT8CmpiX16;
  case Mips::BtnezT8SltuX16: return Mips::BteqzT8SltuX16;
  case Mips::BtnezT8SltiuX16: return Mips::BteqzT8SltiuX16;
  case Mips::BteqzX16: return Mips::BtnezX16;
  case Mips::BteqzT8CmpiX16: return Mips::BtnezT8CmpiX16;
  case Mips::BteqzT8SltuX16: return Mips::BtnezT8SltuX16;
  case Mips::BteqzT8SltiuX16: return Mips::BtnezT8SltiuX16;
  case Mips::BtnezT8CmpX16: return Mips::BteqzT8CmpX16;
  case Mips::BtnezT8SltX16: return Mips::BteqzT8SltX16;
  case Mips::BtnezT8SltiX16: return Mips::BteqzT8SltiX16;
  }
  assert(false && "Implement this function.");
  return 0;
}

// Adjust SP by FrameSize bytes. Save RA, S0, S1
void Mips16InstrInfo::makeFrame(unsigned SP, int64_t FrameSize,
                    MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  if (!NeverUseSaveRestore) {
    if (isUInt<11>(FrameSize))
      BuildMI(MBB, I, DL, get(Mips::SaveRaF16)).addImm(FrameSize);
    else {
      int Base = 2040; // should create template function like isUInt that
                       // returns largest possible n bit unsigned integer
      int64_t Remainder = FrameSize - Base;
      BuildMI(MBB, I, DL, get(Mips::SaveRaF16)). addImm(Base);
      if (isInt<16>(-Remainder))
        BuildAddiuSpImm(MBB, I, -Remainder);
      else
        adjustStackPtrBig(SP, -Remainder, MBB, I, Mips::V0, Mips::V1);
    }

  }
  else {
    //
    // sw ra, -4[sp]
    // sw s1, -8[sp]
    // sw s0, -12[sp]

    MachineInstrBuilder MIB1 = BuildMI(MBB, I, DL, get(Mips::SwRxSpImmX16),
                                       Mips::RA);
    MIB1.addReg(Mips::SP);
    MIB1.addImm(-4);
    MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Mips::SwRxSpImmX16),
                                       Mips::S1);
    MIB2.addReg(Mips::SP);
    MIB2.addImm(-8);
    MachineInstrBuilder MIB3 = BuildMI(MBB, I, DL, get(Mips::SwRxSpImmX16),
                                       Mips::S0);
    MIB3.addReg(Mips::SP);
    MIB3.addImm(-12);
    adjustStackPtrBig(SP, -FrameSize, MBB, I, Mips::V0, Mips::V1);
  }
}

// Adjust SP by FrameSize bytes. Restore RA, S0, S1
void Mips16InstrInfo::restoreFrame(unsigned SP, int64_t FrameSize,
                                   MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  if (!NeverUseSaveRestore) {
    if (isUInt<11>(FrameSize))
      BuildMI(MBB, I, DL, get(Mips::RestoreRaF16)).addImm(FrameSize);
    else {
      int Base = 2040; // should create template function like isUInt that
                       // returns largest possible n bit unsigned integer
      int64_t Remainder = FrameSize - Base;
      if (isInt<16>(Remainder))
        BuildAddiuSpImm(MBB, I, Remainder);
      else
        adjustStackPtrBig(SP, Remainder, MBB, I, Mips::A0, Mips::A1);
      BuildMI(MBB, I, DL, get(Mips::RestoreRaF16)). addImm(Base);
    }
  }
  else {
    adjustStackPtrBig(SP, FrameSize, MBB, I, Mips::A0, Mips::A1);
    // lw ra, -4[sp]
    // lw s1, -8[sp]
    // lw s0, -12[sp]
    MachineInstrBuilder MIB1 = BuildMI(MBB, I, DL, get(Mips::LwRxSpImmX16),
                                       Mips::A0);
    MIB1.addReg(Mips::SP);
    MIB1.addImm(-4);
    MachineInstrBuilder MIB0 = BuildMI(MBB, I, DL, get(Mips::Move32R16),
                                       Mips::RA);
     MIB0.addReg(Mips::A0);
    MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Mips::LwRxSpImmX16),
                                       Mips::S1);
    MIB2.addReg(Mips::SP);
    MIB2.addImm(-8);
    MachineInstrBuilder MIB3 = BuildMI(MBB, I, DL, get(Mips::LwRxSpImmX16),
                                       Mips::S0);
    MIB3.addReg(Mips::SP);
    MIB3.addImm(-12);
  }

}

// Adjust SP by Amount bytes where bytes can be up to 32bit number.
// This can only be called at times that we know that there is at least one free
// register.
// This is clearly safe at prologue and epilogue.
//
void Mips16InstrInfo::adjustStackPtrBig(unsigned SP, int64_t Amount,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned Reg1, unsigned Reg2) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
//  MachineRegisterInfo &RegInfo = MBB.getParent()->getRegInfo();
//  unsigned Reg1 = RegInfo.createVirtualRegister(&Mips::CPU16RegsRegClass);
//  unsigned Reg2 = RegInfo.createVirtualRegister(&Mips::CPU16RegsRegClass);
  //
  // li reg1, constant
  // move reg2, sp
  // add reg1, reg1, reg2
  // move sp, reg1
  //
  //
  MachineInstrBuilder MIB1 = BuildMI(MBB, I, DL, get(Mips::LwConstant32), Reg1);
  MIB1.addImm(Amount);
  MachineInstrBuilder MIB2 = BuildMI(MBB, I, DL, get(Mips::MoveR3216), Reg2);
  MIB2.addReg(Mips::SP, RegState::Kill);
  MachineInstrBuilder MIB3 = BuildMI(MBB, I, DL, get(Mips::AdduRxRyRz16), Reg1);
  MIB3.addReg(Reg1);
  MIB3.addReg(Reg2, RegState::Kill);
  MachineInstrBuilder MIB4 = BuildMI(MBB, I, DL, get(Mips::Move32R16),
                                                     Mips::SP);
  MIB4.addReg(Reg1, RegState::Kill);
}

void Mips16InstrInfo::adjustStackPtrBigUnrestricted(unsigned SP, int64_t Amount,
                    MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator I) const {
   assert(false && "adjust stack pointer amount exceeded");
}

/// Adjust SP by Amount bytes.
void Mips16InstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const {
  if (isInt<16>(Amount))  // need to change to addiu sp, ....and isInt<16>
    BuildAddiuSpImm(MBB, I, Amount);
  else
    adjustStackPtrBigUnrestricted(SP, Amount, MBB, I);
}

/// This function generates the sequence of instructions needed to get the
/// result of adding register REG and immediate IMM.
unsigned
Mips16InstrInfo::loadImmediate(unsigned FrameReg,
                               int64_t Imm, MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator II, DebugLoc DL,
                               unsigned &NewImm) const {
  //
  // given original instruction is:
  // Instr rx, T[offset] where offset is too big.
  //
  // lo = offset & 0xFFFF
  // hi = ((offset >> 16) + (lo >> 15)) & 0xFFFF;
  //
  // let T = temporary register
  // li T, hi
  // shl T, 16
  // add T, Rx, T
  //
  RegScavenger rs;
  int32_t lo = Imm & 0xFFFF;
  int32_t hi = ((Imm >> 16) + (lo >> 15)) & 0xFFFF;
  NewImm = lo;
  unsigned Reg =0;
  unsigned SpReg = 0;
  rs.enterBasicBlock(&MBB);
  rs.forward(II);
  //
  // we use T0 for the first register, if we need to save something away.
  // we use T1 for the second register, if we need to save something away.
  //
  unsigned FirstRegSaved =0, SecondRegSaved=0;
  unsigned FirstRegSavedTo = 0, SecondRegSavedTo = 0;

  Reg = rs.FindUnusedReg(&Mips::CPU16RegsRegClass);
  if (Reg == 0) {
    FirstRegSaved = Reg = Mips::V0;
    FirstRegSavedTo = Mips::T0;
    copyPhysReg(MBB, II, DL, FirstRegSavedTo, FirstRegSaved, true);
  }
  else
    rs.setUsed(Reg);
  BuildMI(MBB, II, DL, get(Mips::LiRxImmX16), Reg).addImm(hi);
  BuildMI(MBB, II, DL, get(Mips::SllX16), Reg).addReg(Reg).
    addImm(16);
  if (FrameReg == Mips::SP) {
    SpReg = rs.FindUnusedReg(&Mips::CPU16RegsRegClass);
    if (SpReg == 0) {
      if (Reg != Mips::V1) {
        SecondRegSaved = SpReg = Mips::V1;
        SecondRegSavedTo = Mips::T1;
      }
      else {
        SecondRegSaved = SpReg = Mips::V0;
        SecondRegSavedTo = Mips::T0;
      }
      copyPhysReg(MBB, II, DL, SecondRegSavedTo, SecondRegSaved, true);
    }
    else
      rs.setUsed(SpReg);

    copyPhysReg(MBB, II, DL, SpReg, Mips::SP, false);
    BuildMI(MBB, II, DL, get(Mips::  AdduRxRyRz16), Reg).addReg(SpReg)
      .addReg(Reg);
  }
  else
    BuildMI(MBB, II, DL, get(Mips::  AdduRxRyRz16), Reg).addReg(FrameReg)
      .addReg(Reg, RegState::Kill);
  if (FirstRegSaved || SecondRegSaved) {
    II = llvm::next(II);
    if (FirstRegSaved)
      copyPhysReg(MBB, II, DL, FirstRegSaved, FirstRegSavedTo, true);
    if (SecondRegSaved)
      copyPhysReg(MBB, II, DL, SecondRegSaved, SecondRegSavedTo, true);
  }
  return Reg;
}

unsigned Mips16InstrInfo::getAnalyzableBrOpc(unsigned Opc) const {
  return (Opc == Mips::BeqzRxImmX16   || Opc == Mips::BimmX16  ||
          Opc == Mips::BnezRxImmX16   || Opc == Mips::BteqzX16 ||
          Opc == Mips::BteqzT8CmpX16  || Opc == Mips::BteqzT8CmpiX16 ||
          Opc == Mips::BteqzT8SltX16  || Opc == Mips::BteqzT8SltuX16  ||
          Opc == Mips::BteqzT8SltiX16 || Opc == Mips::BteqzT8SltiuX16 ||
          Opc == Mips::BtnezX16       || Opc == Mips::BtnezT8CmpX16 ||
          Opc == Mips::BtnezT8CmpiX16 || Opc == Mips::BtnezT8SltX16 ||
          Opc == Mips::BtnezT8SltuX16 || Opc == Mips::BtnezT8SltiX16 ||
          Opc == Mips::BtnezT8SltiuX16 ) ? Opc : 0;
}

void Mips16InstrInfo::ExpandRetRA16(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  unsigned Opc) const {
  BuildMI(MBB, I, I->getDebugLoc(), get(Opc));
}


const MCInstrDesc &Mips16InstrInfo::AddiuSpImm(int64_t Imm) const {
  if (validSpImm8(Imm))
    return get(Mips::AddiuSpImm16);
  else
    return get(Mips::AddiuSpImmX16);
}

void Mips16InstrInfo::BuildAddiuSpImm
  (MachineBasicBlock &MBB, MachineBasicBlock::iterator I, int64_t Imm) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  BuildMI(MBB, I, DL, AddiuSpImm(Imm)).addImm(Imm);
}

const MipsInstrInfo *llvm::createMips16InstrInfo(MipsTargetMachine &TM) {
  return new Mips16InstrInfo(TM);
}
