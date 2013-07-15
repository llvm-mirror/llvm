//===-- PPCRegisterInfo.cpp - PowerPC Register Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "reginfo"
#include "PPCRegisterInfo.h"
#include "PPC.h"
#include "PPCFrameLowering.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

#define GET_REGINFO_TARGET_DESC
#include "PPCGenRegisterInfo.inc"

using namespace llvm;

PPCRegisterInfo::PPCRegisterInfo(const PPCSubtarget &ST)
  : PPCGenRegisterInfo(ST.isPPC64() ? PPC::LR8 : PPC::LR,
                       ST.isPPC64() ? 0 : 1,
                       ST.isPPC64() ? 0 : 1),
    Subtarget(ST) {
  ImmToIdxMap[PPC::LD]   = PPC::LDX;    ImmToIdxMap[PPC::STD]  = PPC::STDX;
  ImmToIdxMap[PPC::LBZ]  = PPC::LBZX;   ImmToIdxMap[PPC::STB]  = PPC::STBX;
  ImmToIdxMap[PPC::LHZ]  = PPC::LHZX;   ImmToIdxMap[PPC::LHA]  = PPC::LHAX;
  ImmToIdxMap[PPC::LWZ]  = PPC::LWZX;   ImmToIdxMap[PPC::LWA]  = PPC::LWAX;
  ImmToIdxMap[PPC::LFS]  = PPC::LFSX;   ImmToIdxMap[PPC::LFD]  = PPC::LFDX;
  ImmToIdxMap[PPC::STH]  = PPC::STHX;   ImmToIdxMap[PPC::STW]  = PPC::STWX;
  ImmToIdxMap[PPC::STFS] = PPC::STFSX;  ImmToIdxMap[PPC::STFD] = PPC::STFDX;
  ImmToIdxMap[PPC::ADDI] = PPC::ADD4;

  // 64-bit
  ImmToIdxMap[PPC::LHA8] = PPC::LHAX8; ImmToIdxMap[PPC::LBZ8] = PPC::LBZX8;
  ImmToIdxMap[PPC::LHZ8] = PPC::LHZX8; ImmToIdxMap[PPC::LWZ8] = PPC::LWZX8;
  ImmToIdxMap[PPC::STB8] = PPC::STBX8; ImmToIdxMap[PPC::STH8] = PPC::STHX8;
  ImmToIdxMap[PPC::STW8] = PPC::STWX8; ImmToIdxMap[PPC::STDU] = PPC::STDUX;
  ImmToIdxMap[PPC::ADDI8] = PPC::ADD8;
}

/// getPointerRegClass - Return the register class to use to hold pointers.
/// This is used for addressing modes.
const TargetRegisterClass *
PPCRegisterInfo::getPointerRegClass(const MachineFunction &MF, unsigned Kind)
                                                                       const {
  // Note that PPCInstrInfo::FoldImmediate also directly uses this Kind value
  // when it checks for ZERO folding.
  if (Kind == 1) {
    if (Subtarget.isPPC64())
      return &PPC::G8RC_NOX0RegClass;
    return &PPC::GPRC_NOR0RegClass;
  }

  if (Subtarget.isPPC64())
    return &PPC::G8RCRegClass;
  return &PPC::GPRCRegClass;
}

const uint16_t*
PPCRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (Subtarget.isDarwinABI())
    return Subtarget.isPPC64() ? (Subtarget.hasAltivec() ?
                                  CSR_Darwin64_Altivec_SaveList :
                                  CSR_Darwin64_SaveList) :
                                 (Subtarget.hasAltivec() ?
                                  CSR_Darwin32_Altivec_SaveList :
                                  CSR_Darwin32_SaveList);

  return Subtarget.isPPC64() ? (Subtarget.hasAltivec() ?
                                CSR_SVR464_Altivec_SaveList :
                                CSR_SVR464_SaveList) :
                               (Subtarget.hasAltivec() ?
                                CSR_SVR432_Altivec_SaveList :
                                CSR_SVR432_SaveList);
}

const uint32_t*
PPCRegisterInfo::getCallPreservedMask(CallingConv::ID CC) const {
  if (Subtarget.isDarwinABI())
    return Subtarget.isPPC64() ? (Subtarget.hasAltivec() ?
                                  CSR_Darwin64_Altivec_RegMask :
                                  CSR_Darwin64_RegMask) :
                                 (Subtarget.hasAltivec() ?
                                  CSR_Darwin32_Altivec_RegMask :
                                  CSR_Darwin32_RegMask);

  return Subtarget.isPPC64() ? (Subtarget.hasAltivec() ?
                                CSR_SVR464_Altivec_RegMask :
                                CSR_SVR464_RegMask) :
                               (Subtarget.hasAltivec() ?
                                CSR_SVR432_Altivec_RegMask :
                                CSR_SVR432_RegMask);
}

const uint32_t*
PPCRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

BitVector PPCRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const PPCFrameLowering *PPCFI =
    static_cast<const PPCFrameLowering*>(MF.getTarget().getFrameLowering());

  // The ZERO register is not really a register, but the representation of r0
  // when used in instructions that treat r0 as the constant 0.
  Reserved.set(PPC::ZERO);
  Reserved.set(PPC::ZERO8);

  // The FP register is also not really a register, but is the representation
  // of the frame pointer register used by ISD::FRAMEADDR.
  Reserved.set(PPC::FP);
  Reserved.set(PPC::FP8);

  // The counter registers must be reserved so that counter-based loops can
  // be correctly formed (and the mtctr instructions are not DCE'd).
  Reserved.set(PPC::CTR);
  Reserved.set(PPC::CTR8);

  Reserved.set(PPC::R1);
  Reserved.set(PPC::LR);
  Reserved.set(PPC::LR8);
  Reserved.set(PPC::RM);

  if (!Subtarget.isDarwinABI() || !Subtarget.hasAltivec())
    Reserved.set(PPC::VRSAVE);

  // The SVR4 ABI reserves r2 and r13
  if (Subtarget.isSVR4ABI()) {
    Reserved.set(PPC::R2);  // System-reserved register
    Reserved.set(PPC::R13); // Small Data Area pointer register
  }
  
  // On PPC64, r13 is the thread pointer. Never allocate this register.
  if (Subtarget.isPPC64()) {
    Reserved.set(PPC::R13);

    Reserved.set(PPC::X1);
    Reserved.set(PPC::X13);

    if (PPCFI->needsFP(MF))
      Reserved.set(PPC::X31);

    // The 64-bit SVR4 ABI reserves r2 for the TOC pointer.
    if (Subtarget.isSVR4ABI()) {
      Reserved.set(PPC::X2);
    }
  }

  if (PPCFI->needsFP(MF))
    Reserved.set(PPC::R31);

  // Reserve Altivec registers when Altivec is unavailable.
  if (!Subtarget.hasAltivec())
    for (TargetRegisterClass::iterator I = PPC::VRRCRegClass.begin(),
         IE = PPC::VRRCRegClass.end(); I != IE; ++I)
      Reserved.set(*I);

  return Reserved;
}

unsigned
PPCRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                         MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();
  const unsigned DefaultSafety = 1;

  switch (RC->getID()) {
  default:
    return 0;
  case PPC::G8RC_NOX0RegClassID:
  case PPC::GPRC_NOR0RegClassID: 
  case PPC::G8RCRegClassID:
  case PPC::GPRCRegClassID: {
    unsigned FP = TFI->hasFP(MF) ? 1 : 0;
    return 32 - FP - DefaultSafety;
  }
  case PPC::F8RCRegClassID:
  case PPC::F4RCRegClassID:
  case PPC::VRRCRegClassID:
    return 32 - DefaultSafety;
  case PPC::CRRCRegClassID:
    return 8 - DefaultSafety;
  }
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

/// lowerDynamicAlloc - Generate the code for allocating an object in the
/// current frame.  The sequence of code with be in the general form
///
///   addi   R0, SP, \#frameSize ; get the address of the previous frame
///   stwxu  R0, SP, Rnegsize   ; add and update the SP with the negated size
///   addi   Rnew, SP, \#maxCalFrameSize ; get the top of the allocation
///
void PPCRegisterInfo::lowerDynamicAlloc(MachineBasicBlock::iterator II) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo *MFI = MF.getFrameInfo();
  // Get the instruction info.
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  // Determine whether 64-bit pointers are used.
  bool LP64 = Subtarget.isPPC64();
  DebugLoc dl = MI.getDebugLoc();

  // Get the maximum call stack size.
  unsigned maxCallFrameSize = MFI->getMaxCallFrameSize();
  // Get the total frame size.
  unsigned FrameSize = MFI->getStackSize();
  
  // Get stack alignments.
  unsigned TargetAlign = MF.getTarget().getFrameLowering()->getStackAlignment();
  unsigned MaxAlign = MFI->getMaxAlignment();
  if (MaxAlign > TargetAlign)
    report_fatal_error("Dynamic alloca with large aligns not supported");

  // Determine the previous frame's address.  If FrameSize can't be
  // represented as 16 bits or we need special alignment, then we load the
  // previous frame's address from 0(SP).  Why not do an addis of the hi? 
  // Because R0 is our only safe tmp register and addi/addis treat R0 as zero. 
  // Constructing the constant and adding would take 3 instructions. 
  // Fortunately, a frame greater than 32K is rare.
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  
  if (MaxAlign < TargetAlign && isInt<16>(FrameSize)) {
    BuildMI(MBB, II, dl, TII.get(PPC::ADDI), Reg)
      .addReg(PPC::R31)
      .addImm(FrameSize);
  } else if (LP64) {
    BuildMI(MBB, II, dl, TII.get(PPC::LD), Reg)
      .addImm(0)
      .addReg(PPC::X1);
  } else {
    BuildMI(MBB, II, dl, TII.get(PPC::LWZ), Reg)
      .addImm(0)
      .addReg(PPC::R1);
  }
  
  // Grow the stack and update the stack pointer link, then determine the
  // address of new allocated space.
  if (LP64) {
    BuildMI(MBB, II, dl, TII.get(PPC::STDUX), PPC::X1)
      .addReg(Reg, RegState::Kill)
      .addReg(PPC::X1)
      .addReg(MI.getOperand(1).getReg());
    if (!MI.getOperand(1).isKill())
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), MI.getOperand(0).getReg())
        .addReg(PPC::X1)
        .addImm(maxCallFrameSize);
    else
      // Implicitly kill the register.
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), MI.getOperand(0).getReg())
        .addReg(PPC::X1)
        .addImm(maxCallFrameSize)
        .addReg(MI.getOperand(1).getReg(), RegState::ImplicitKill);
  } else {
    BuildMI(MBB, II, dl, TII.get(PPC::STWUX), PPC::R1)
      .addReg(Reg, RegState::Kill)
      .addReg(PPC::R1)
      .addReg(MI.getOperand(1).getReg());

    if (!MI.getOperand(1).isKill())
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI), MI.getOperand(0).getReg())
        .addReg(PPC::R1)
        .addImm(maxCallFrameSize);
    else
      // Implicitly kill the register.
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI), MI.getOperand(0).getReg())
        .addReg(PPC::R1)
        .addImm(maxCallFrameSize)
        .addReg(MI.getOperand(1).getReg(), RegState::ImplicitKill);
  }
  
  // Discard the DYNALLOC instruction.
  MBB.erase(II);
}

/// lowerCRSpilling - Generate the code for spilling a CR register. Instead of
/// reserving a whole register (R0), we scrounge for one here. This generates
/// code like this:
///
///   mfcr rA                  ; Move the conditional register into GPR rA.
///   rlwinm rA, rA, SB, 0, 31 ; Shift the bits left so they are in CR0's slot.
///   stw rA, FI               ; Store rA to the frame.
///
void PPCRegisterInfo::lowerCRSpilling(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_CR <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = Subtarget.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned SrcReg = MI.getOperand(0).getReg();

  // We need to store the CR in the low 4-bits of the saved value. First, issue
  // an MFOCRF to save all of the CRBits and, if needed, kill the SrcReg.
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), Reg)
          .addReg(SrcReg, getKillRegState(MI.getOperand(0).isKill()));
    
  // If the saved register wasn't CR0, shift the bits left so that they are in
  // CR0's slot.
  if (SrcReg != PPC::CR0) {
    unsigned Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    // rlwinm rA, rA, ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
      .addReg(Reg1, RegState::Kill)
      .addImm(getEncodingValue(SrcReg) * 4)
      .addImm(0)
      .addImm(31);
  }

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::STW8 : PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_CR <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = Subtarget.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  unsigned DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg) &&
    "RESTORE_CR does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LWZ8 : PPC::LWZ),
                              Reg), FrameIndex);

  // If the reloaded register isn't CR0, shift the bits right so that they are
  // in the right CR's slot.
  if (DestReg != PPC::CR0) {
    unsigned Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    unsigned ShiftBits = getEncodingValue(DestReg)*4;
    // rlwinm r11, r11, 32-ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
             .addReg(Reg1, RegState::Kill).addImm(32-ShiftBits).addImm(0)
             .addImm(31);
  }

  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MTOCRF8 : PPC::MTOCRF), DestReg)
             .addReg(Reg, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerVRSAVESpilling(MachineBasicBlock::iterator II,
                                          unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_VRSAVE <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(GPRC);
  unsigned SrcReg = MI.getOperand(0).getReg();

  BuildMI(MBB, II, dl, TII.get(PPC::MFVRSAVEv), Reg)
          .addReg(SrcReg, getKillRegState(MI.getOperand(0).isKill()));
    
  addFrameReference(BuildMI(MBB, II, dl, TII.get(PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerVRSAVERestore(MachineBasicBlock::iterator II,
                                         unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_VRSAVE <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  unsigned Reg = MF.getRegInfo().createVirtualRegister(GPRC);
  unsigned DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg) &&
    "RESTORE_VRSAVE does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(PPC::LWZ),
                              Reg), FrameIndex);

  BuildMI(MBB, II, dl, TII.get(PPC::MTVRSAVEv), DestReg)
             .addReg(Reg, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

bool
PPCRegisterInfo::hasReservedSpillSlot(const MachineFunction &MF,
				      unsigned Reg, int &FrameIdx) const {

  // For the nonvolatile condition registers (CR2, CR3, CR4) in an SVR4
  // ABI, return true to prevent allocating an additional frame slot.
  // For 64-bit, the CR save area is at SP+8; the value of FrameIdx = 0
  // is arbitrary and will be subsequently ignored.  For 32-bit, we have
  // previously created the stack slot if needed, so return its FrameIdx.
  if (Subtarget.isSVR4ABI() && PPC::CR2 <= Reg && Reg <= PPC::CR4) {
    if (Subtarget.isPPC64())
      FrameIdx = 0;
    else {
      const PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
      FrameIdx = FI->getCRSpillFrameIndex();
    }
    return true;
  }
  return false;
}

// Figure out if the offset in the instruction must be a multiple of 4.
// This is true for instructions like "STD".
static bool usesIXAddr(const MachineInstr &MI) {
  unsigned OpC = MI.getOpcode();

  switch (OpC) {
  default:
    return false;
  case PPC::LWA:
  case PPC::LD:
  case PPC::STD:
    return true;
  }
}

// Return the OffsetOperandNo given the FIOperandNum (and the instruction).
static unsigned getOffsetONFromFION(const MachineInstr &MI,
                                    unsigned FIOperandNum) {
  // Take into account whether it's an add or mem instruction
  unsigned OffsetOperandNo = (FIOperandNum == 2) ? 1 : 2;
  if (MI.isInlineAsm())
    OffsetOperandNo = FIOperandNum-1;

  return OffsetOperandNo;
}

void
PPCRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                     int SPAdj, unsigned FIOperandNum,
                                     RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the instruction info.
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  // Get the frame info.
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();
  DebugLoc dl = MI.getDebugLoc();

  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);

  // Get the frame index.
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Get the frame pointer save index.  Users of this index are primarily
  // DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int FPSI = FI->getFramePointerSaveIndex();
  // Get the instruction opcode.
  unsigned OpC = MI.getOpcode();
  
  // Special case for dynamic alloca.
  if (FPSI && FrameIndex == FPSI &&
      (OpC == PPC::DYNALLOC || OpC == PPC::DYNALLOC8)) {
    lowerDynamicAlloc(II);
    return;
  }

  // Special case for pseudo-ops SPILL_CR and RESTORE_CR, etc.
  if (OpC == PPC::SPILL_CR) {
    lowerCRSpilling(II, FrameIndex);
    return;
  } else if (OpC == PPC::RESTORE_CR) {
    lowerCRRestore(II, FrameIndex);
    return;
  } else if (OpC == PPC::SPILL_VRSAVE) {
    lowerVRSAVESpilling(II, FrameIndex);
    return;
  } else if (OpC == PPC::RESTORE_VRSAVE) {
    lowerVRSAVERestore(II, FrameIndex);
    return;
  }

  // Replace the FrameIndex with base register with GPR1 (SP) or GPR31 (FP).

  bool is64Bit = Subtarget.isPPC64();
  MI.getOperand(FIOperandNum).ChangeToRegister(TFI->hasFP(MF) ?
                                              (is64Bit ? PPC::X31 : PPC::R31) :
                                                (is64Bit ? PPC::X1 : PPC::R1),
                                              false);

  // Figure out if the offset in the instruction is shifted right two bits.
  bool isIXAddr = usesIXAddr(MI);

  // If the instruction is not present in ImmToIdxMap, then it has no immediate
  // form (and must be r+r).
  bool noImmForm = !MI.isInlineAsm() && !ImmToIdxMap.count(OpC);

  // Now add the frame object offset to the offset from r1.
  int Offset = MFI->getObjectOffset(FrameIndex);
  Offset += MI.getOperand(OffsetOperandNo).getImm();

  // If we're not using a Frame Pointer that has been set to the value of the
  // SP before having the stack size subtracted from it, then add the stack size
  // to Offset to get the correct offset.
  // Naked functions have stack size 0, although getStackSize may not reflect that
  // because we didn't call all the pieces that compute it for naked functions.
  if (!MF.getFunction()->getAttributes().
        hasAttribute(AttributeSet::FunctionIndex, Attribute::Naked))
    Offset += MFI->getStackSize();

  // If we can, encode the offset directly into the instruction.  If this is a
  // normal PPC "ri" instruction, any 16-bit value can be safely encoded.  If
  // this is a PPC64 "ix" instruction, only a 16-bit value with the low two bits
  // clear can be encoded.  This is extremely uncommon, because normally you
  // only "std" to a stack slot that is at least 4-byte aligned, but it can
  // happen in invalid code.
  assert(OpC != PPC::DBG_VALUE &&
         "This should be handle in a target independent way");
  if (!noImmForm && isInt<16>(Offset) && (!isIXAddr || (Offset & 3) == 0)) {
    MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);
    return;
  }

  // The offset doesn't fit into a single register, scavenge one to build the
  // offset in.

  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  const TargetRegisterClass *RC = is64Bit ? G8RC : GPRC;
  unsigned SRegHi = MF.getRegInfo().createVirtualRegister(RC),
           SReg = MF.getRegInfo().createVirtualRegister(RC);

  // Insert a set of rA with the full offset value before the ld, st, or add
  BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LIS8 : PPC::LIS), SRegHi)
    .addImm(Offset >> 16);
  BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::ORI8 : PPC::ORI), SReg)
    .addReg(SRegHi, RegState::Kill)
    .addImm(Offset);

  // Convert into indexed form of the instruction:
  // 
  //   sth 0:rA, 1:imm 2:(rB) ==> sthx 0:rA, 2:rB, 1:r0
  //   addi 0:rA 1:rB, 2, imm ==> add 0:rA, 1:rB, 2:r0
  unsigned OperandBase;

  if (noImmForm)
    OperandBase = 1;
  else if (OpC != TargetOpcode::INLINEASM) {
    assert(ImmToIdxMap.count(OpC) &&
           "No indexed form of load or store available!");
    unsigned NewOpcode = ImmToIdxMap.find(OpC)->second;
    MI.setDesc(TII.get(NewOpcode));
    OperandBase = 1;
  } else {
    OperandBase = OffsetOperandNo;
  }

  unsigned StackReg = MI.getOperand(FIOperandNum).getReg();
  MI.getOperand(OperandBase).ChangeToRegister(StackReg, false);
  MI.getOperand(OperandBase + 1).ChangeToRegister(SReg, false, false, true);
}

unsigned PPCRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();

  if (!Subtarget.isPPC64())
    return TFI->hasFP(MF) ? PPC::R31 : PPC::R1;
  else
    return TFI->hasFP(MF) ? PPC::X31 : PPC::X1;
}

unsigned PPCRegisterInfo::getEHExceptionRegister() const {
  return !Subtarget.isPPC64() ? PPC::R3 : PPC::X3;
}

unsigned PPCRegisterInfo::getEHHandlerRegister() const {
  return !Subtarget.isPPC64() ? PPC::R4 : PPC::X4;
}

/// Returns true if the instruction's frame index
/// reference would be better served by a base register other than FP
/// or SP. Used by LocalStackFrameAllocation to determine which frame index
/// references it should create new base registers for.
bool PPCRegisterInfo::
needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  assert(Offset < 0 && "Local offset must be negative");

  unsigned FIOperandNum = 0;
  while (!MI->getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  unsigned OffsetOperandNo = getOffsetONFromFION(*MI, FIOperandNum);
  Offset += MI->getOperand(OffsetOperandNo).getImm();

  // It's the load/store FI references that cause issues, as it can be difficult
  // to materialize the offset if it won't fit in the literal field. Estimate
  // based on the size of the local frame and some conservative assumptions
  // about the rest of the stack frame (note, this is pre-regalloc, so
  // we don't know everything for certain yet) whether this offset is likely
  // to be out of range of the immediate. Return true if so.

  // We only generate virtual base registers for loads and stores that have
  // an r+i form. Return false for everything else.
  unsigned OpC = MI->getOpcode();
  if (!ImmToIdxMap.count(OpC))
    return false;

  // Don't generate a new virtual base register just to add zero to it.
  if ((OpC == PPC::ADDI || OpC == PPC::ADDI8) &&
      MI->getOperand(2).getImm() == 0)
    return false;

  MachineBasicBlock &MBB = *MI->getParent();
  MachineFunction &MF = *MBB.getParent();

  const PPCFrameLowering *PPCFI =
    static_cast<const PPCFrameLowering*>(MF.getTarget().getFrameLowering());
  unsigned StackEst =
    PPCFI->determineFrameLayout(MF, false, true);

  // If we likely don't need a stack frame, then we probably don't need a
  // virtual base register either.
  if (!StackEst)
    return false;

  // Estimate an offset from the stack pointer.
  // The incoming offset is relating to the SP at the start of the function,
  // but when we access the local it'll be relative to the SP after local
  // allocation, so adjust our SP-relative offset by that allocation size.
  Offset += StackEst;

  // The frame pointer will point to the end of the stack, so estimate the
  // offset as the difference between the object offset and the FP location.
  return !isFrameOffsetLegal(MI, Offset);
}

/// Insert defining instruction(s) for BaseReg to
/// be a pointer to FrameIdx at the beginning of the basic block.
void PPCRegisterInfo::
materializeFrameBaseRegister(MachineBasicBlock *MBB,
                             unsigned BaseReg, int FrameIdx,
                             int64_t Offset) const {
  unsigned ADDriOpc = Subtarget.isPPC64() ? PPC::ADDI8 : PPC::ADDI;

  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL;                  // Defaults to "unknown"
  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  const MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  const MCInstrDesc &MCID = TII.get(ADDriOpc);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  MRI.constrainRegClass(BaseReg, TII.getRegClass(MCID, 0, this, MF));

  BuildMI(*MBB, Ins, DL, MCID, BaseReg)
    .addFrameIndex(FrameIdx).addImm(Offset);
}

void
PPCRegisterInfo::resolveFrameIndex(MachineBasicBlock::iterator I,
                                   unsigned BaseReg, int64_t Offset) const {
  MachineInstr &MI = *I;

  unsigned FIOperandNum = 0;
  while (!MI.getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI.getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);
  Offset += MI.getOperand(OffsetOperandNo).getImm();
  MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);
}

bool PPCRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                         int64_t Offset) const {
  return MI->getOpcode() == PPC::DBG_VALUE || // DBG_VALUE is always Reg+Imm
         (isInt<16>(Offset) && (!usesIXAddr(*MI) || (Offset & 3) == 0));
}

