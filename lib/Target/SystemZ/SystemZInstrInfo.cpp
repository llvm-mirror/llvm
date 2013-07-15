//===-- SystemZInstrInfo.cpp - SystemZ instruction information ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the SystemZ implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "SystemZInstrInfo.h"
#include "SystemZInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"

#define GET_INSTRINFO_CTOR
#define GET_INSTRMAP_INFO
#include "SystemZGenInstrInfo.inc"

using namespace llvm;

SystemZInstrInfo::SystemZInstrInfo(SystemZTargetMachine &tm)
  : SystemZGenInstrInfo(SystemZ::ADJCALLSTACKDOWN, SystemZ::ADJCALLSTACKUP),
    RI(tm) {
}

// MI is a 128-bit load or store.  Split it into two 64-bit loads or stores,
// each having the opcode given by NewOpcode.
void SystemZInstrInfo::splitMove(MachineBasicBlock::iterator MI,
                                 unsigned NewOpcode) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();

  // Get two load or store instructions.  Use the original instruction for one
  // of them (arbitarily the second here) and create a clone for the other.
  MachineInstr *EarlierMI = MF.CloneMachineInstr(MI);
  MBB->insert(MI, EarlierMI);

  // Set up the two 64-bit registers.
  MachineOperand &HighRegOp = EarlierMI->getOperand(0);
  MachineOperand &LowRegOp = MI->getOperand(0);
  HighRegOp.setReg(RI.getSubReg(HighRegOp.getReg(), SystemZ::subreg_high));
  LowRegOp.setReg(RI.getSubReg(LowRegOp.getReg(), SystemZ::subreg_low));

  // The address in the first (high) instruction is already correct.
  // Adjust the offset in the second (low) instruction.
  MachineOperand &HighOffsetOp = EarlierMI->getOperand(2);
  MachineOperand &LowOffsetOp = MI->getOperand(2);
  LowOffsetOp.setImm(LowOffsetOp.getImm() + 8);

  // Set the opcodes.
  unsigned HighOpcode = getOpcodeForOffset(NewOpcode, HighOffsetOp.getImm());
  unsigned LowOpcode = getOpcodeForOffset(NewOpcode, LowOffsetOp.getImm());
  assert(HighOpcode && LowOpcode && "Both offsets should be in range");

  EarlierMI->setDesc(get(HighOpcode));
  MI->setDesc(get(LowOpcode));
}

// Split ADJDYNALLOC instruction MI.
void SystemZInstrInfo::splitAdjDynAlloc(MachineBasicBlock::iterator MI) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineFrameInfo *MFFrame = MF.getFrameInfo();
  MachineOperand &OffsetMO = MI->getOperand(2);

  uint64_t Offset = (MFFrame->getMaxCallFrameSize() +
                     SystemZMC::CallFrameSize +
                     OffsetMO.getImm());
  unsigned NewOpcode = getOpcodeForOffset(SystemZ::LA, Offset);
  assert(NewOpcode && "No support for huge argument lists yet");
  MI->setDesc(get(NewOpcode));
  OffsetMO.setImm(Offset);
}

// If MI is a simple load or store for a frame object, return the register
// it loads or stores and set FrameIndex to the index of the frame object.
// Return 0 otherwise.
//
// Flag is SimpleBDXLoad for loads and SimpleBDXStore for stores.
static int isSimpleMove(const MachineInstr *MI, int &FrameIndex,
                        unsigned Flag) {
  const MCInstrDesc &MCID = MI->getDesc();
  if ((MCID.TSFlags & Flag) &&
      MI->getOperand(1).isFI() &&
      MI->getOperand(2).getImm() == 0 &&
      MI->getOperand(3).getReg() == 0) {
    FrameIndex = MI->getOperand(1).getIndex();
    return MI->getOperand(0).getReg();
  }
  return 0;
}

unsigned SystemZInstrInfo::isLoadFromStackSlot(const MachineInstr *MI,
                                               int &FrameIndex) const {
  return isSimpleMove(MI, FrameIndex, SystemZII::SimpleBDXLoad);
}

unsigned SystemZInstrInfo::isStoreToStackSlot(const MachineInstr *MI,
                                              int &FrameIndex) const {
  return isSimpleMove(MI, FrameIndex, SystemZII::SimpleBDXStore);
}

bool SystemZInstrInfo::isStackSlotCopy(const MachineInstr *MI,
                                       int &DestFrameIndex,
                                       int &SrcFrameIndex) const {
  // Check for MVC 0(Length,FI1),0(FI2)
  const MachineFrameInfo *MFI = MI->getParent()->getParent()->getFrameInfo();
  if (MI->getOpcode() != SystemZ::MVC ||
      !MI->getOperand(0).isFI() ||
      MI->getOperand(1).getImm() != 0 ||
      !MI->getOperand(3).isFI() ||
      MI->getOperand(4).getImm() != 0)
    return false;

  // Check that Length covers the full slots.
  int64_t Length = MI->getOperand(2).getImm();
  unsigned FI1 = MI->getOperand(0).getIndex();
  unsigned FI2 = MI->getOperand(3).getIndex();
  if (MFI->getObjectSize(FI1) != Length ||
      MFI->getObjectSize(FI2) != Length)
    return false;

  DestFrameIndex = FI1;
  SrcFrameIndex = FI2;
  return true;
}

bool SystemZInstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  // Most of the code and comments here are boilerplate.

  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(I))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!I->isBranch())
      return true;

    // Can't handle indirect branches.
    SystemZII::Branch Branch(getBranchInfo(I));
    if (!Branch.Target->isMBB())
      return true;

    // Punt on compound branches.
    if (Branch.Type != SystemZII::BranchNormal)
      return true;

    if (Branch.CCMask == SystemZ::CCMASK_ANY) {
      // Handle unconditional branches.
      if (!AllowModify) {
        TBB = Branch.Target->getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      while (llvm::next(I) != MBB.end())
        llvm::next(I)->eraseFromParent();

      Cond.clear();
      FBB = 0;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(Branch.Target->getMBB())) {
        TBB = 0;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = Branch.Target->getMBB();
      continue;
    }

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      // FIXME: add X86-style branch swap
      FBB = TBB;
      TBB = Branch.Target->getMBB();
      Cond.push_back(MachineOperand::CreateImm(Branch.CCMask));
      continue;
    }

    // Handle subsequent conditional branches.
    assert(Cond.size() == 1);
    assert(TBB);

    // Only handle the case where all conditional branches branch to the same
    // destination.
    if (TBB != Branch.Target->getMBB())
      return true;

    // If the conditions are the same, we can leave them alone.
    unsigned OldCond = Cond[0].getImm();
    if (OldCond == Branch.CCMask)
      continue;

    // FIXME: Try combining conditions like X86 does.  Should be easy on Z!
  }

  return false;
}

unsigned SystemZInstrInfo::RemoveBranch(MachineBasicBlock &MBB) const {
  // Most of the code and comments here are boilerplate.
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;
    if (!I->isBranch())
      break;
    if (!getBranchInfo(I).Target->isMBB())
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

unsigned
SystemZInstrInfo::InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                               MachineBasicBlock *FBB,
                               const SmallVectorImpl<MachineOperand> &Cond,
                               DebugLoc DL) const {
  // In this function we output 32-bit branches, which should always
  // have enough range.  They can be shortened and relaxed by later code
  // in the pipeline, if desired.

  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "SystemZ branch conditions have one component!");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(SystemZ::J)).addMBB(TBB);
    return 1;
  }

  // Conditional branch.
  unsigned Count = 0;
  unsigned CC = Cond[0].getImm();
  BuildMI(&MBB, DL, get(SystemZ::BRC)).addImm(CC).addMBB(TBB);
  ++Count;

  if (FBB) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(SystemZ::J)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

void
SystemZInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
			      MachineBasicBlock::iterator MBBI, DebugLoc DL,
			      unsigned DestReg, unsigned SrcReg,
			      bool KillSrc) const {
  // Split 128-bit GPR moves into two 64-bit moves.  This handles ADDR128 too.
  if (SystemZ::GR128BitRegClass.contains(DestReg, SrcReg)) {
    copyPhysReg(MBB, MBBI, DL, RI.getSubReg(DestReg, SystemZ::subreg_high),
                RI.getSubReg(SrcReg, SystemZ::subreg_high), KillSrc);
    copyPhysReg(MBB, MBBI, DL, RI.getSubReg(DestReg, SystemZ::subreg_low),
                RI.getSubReg(SrcReg, SystemZ::subreg_low), KillSrc);
    return;
  }

  // Everything else needs only one instruction.
  unsigned Opcode;
  if (SystemZ::GR32BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LR;
  else if (SystemZ::GR64BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LGR;
  else if (SystemZ::FP32BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LER;
  else if (SystemZ::FP64BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LDR;
  else if (SystemZ::FP128BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LXR;
  else
    llvm_unreachable("Impossible reg-to-reg copy");

  BuildMI(MBB, MBBI, DL, get(Opcode), DestReg)
    .addReg(SrcReg, getKillRegState(KillSrc));
}

void
SystemZInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
				      MachineBasicBlock::iterator MBBI,
				      unsigned SrcReg, bool isKill,
				      int FrameIdx,
				      const TargetRegisterClass *RC,
				      const TargetRegisterInfo *TRI) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Callers may expect a single instruction, so keep 128-bit moves
  // together for now and lower them after register allocation.
  unsigned LoadOpcode, StoreOpcode;
  getLoadStoreOpcodes(RC, LoadOpcode, StoreOpcode);
  addFrameReference(BuildMI(MBB, MBBI, DL, get(StoreOpcode))
		    .addReg(SrcReg, getKillRegState(isKill)), FrameIdx);
}

void
SystemZInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
				       MachineBasicBlock::iterator MBBI,
				       unsigned DestReg, int FrameIdx,
				       const TargetRegisterClass *RC,
				       const TargetRegisterInfo *TRI) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Callers may expect a single instruction, so keep 128-bit moves
  // together for now and lower them after register allocation.
  unsigned LoadOpcode, StoreOpcode;
  getLoadStoreOpcodes(RC, LoadOpcode, StoreOpcode);
  addFrameReference(BuildMI(MBB, MBBI, DL, get(LoadOpcode), DestReg),
                    FrameIdx);
}

// Return true if MI is a simple load or store with a 12-bit displacement
// and no index.  Flag is SimpleBDXLoad for loads and SimpleBDXStore for stores.
static bool isSimpleBD12Move(const MachineInstr *MI, unsigned Flag) {
  const MCInstrDesc &MCID = MI->getDesc();
  return ((MCID.TSFlags & Flag) &&
          isUInt<12>(MI->getOperand(2).getImm()) &&
          MI->getOperand(3).getReg() == 0);
}

MachineInstr *
SystemZInstrInfo::foldMemoryOperandImpl(MachineFunction &MF,
                                        MachineInstr *MI,
                                        const SmallVectorImpl<unsigned> &Ops,
                                        int FrameIndex) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  unsigned Size = MFI->getObjectSize(FrameIndex);

  // Eary exit for cases we don't care about
  if (Ops.size() != 1)
    return 0;

  unsigned OpNum = Ops[0];
  assert(Size == MF.getRegInfo()
         .getRegClass(MI->getOperand(OpNum).getReg())->getSize() &&
         "Invalid size combination");

  unsigned Opcode = MI->getOpcode();
  if (Opcode == SystemZ::LGDR || Opcode == SystemZ::LDGR) {
    bool Op0IsGPR = (Opcode == SystemZ::LGDR);
    bool Op1IsGPR = (Opcode == SystemZ::LDGR);
    // If we're spilling the destination of an LDGR or LGDR, store the
    // source register instead.
    if (OpNum == 0) {
      unsigned StoreOpcode = Op1IsGPR ? SystemZ::STG : SystemZ::STD;
      return BuildMI(MF, MI->getDebugLoc(), get(StoreOpcode))
        .addOperand(MI->getOperand(1)).addFrameIndex(FrameIndex)
        .addImm(0).addReg(0);
    }
    // If we're spilling the source of an LDGR or LGDR, load the
    // destination register instead.
    if (OpNum == 1) {
      unsigned LoadOpcode = Op0IsGPR ? SystemZ::LG : SystemZ::LD;
      unsigned Dest = MI->getOperand(0).getReg();
      return BuildMI(MF, MI->getDebugLoc(), get(LoadOpcode), Dest)
        .addFrameIndex(FrameIndex).addImm(0).addReg(0);
    }
  }

  // Look for cases where the source of a simple store or the destination
  // of a simple load is being spilled.  Try to use MVC instead.
  //
  // Although MVC is in practice a fast choice in these cases, it is still
  // logically a bytewise copy.  This means that we cannot use it if the
  // load or store is volatile.  It also means that the transformation is
  // not valid in cases where the two memories partially overlap; however,
  // that is not a problem here, because we know that one of the memories
  // is a full frame index.
  if (OpNum == 0 && MI->hasOneMemOperand()) {
    MachineMemOperand *MMO = *MI->memoperands_begin();
    if (MMO->getSize() == Size && !MMO->isVolatile()) {
      // Handle conversion of loads.
      if (isSimpleBD12Move(MI, SystemZII::SimpleBDXLoad)) {
        return BuildMI(MF, MI->getDebugLoc(), get(SystemZ::MVC))
          .addFrameIndex(FrameIndex).addImm(0).addImm(Size)
          .addOperand(MI->getOperand(1)).addImm(MI->getOperand(2).getImm())
          .addMemOperand(MMO);
      }
      // Handle conversion of stores.
      if (isSimpleBD12Move(MI, SystemZII::SimpleBDXStore)) {
        return BuildMI(MF, MI->getDebugLoc(), get(SystemZ::MVC))
          .addOperand(MI->getOperand(1)).addImm(MI->getOperand(2).getImm())
          .addImm(Size).addFrameIndex(FrameIndex).addImm(0)
          .addMemOperand(MMO);
      }
    }
  }

  // If the spilled operand is the final one, try to change <INSN>R
  // into <INSN>.
  int MemOpcode = SystemZ::getMemOpcode(Opcode);
  if (MemOpcode >= 0) {
    unsigned NumOps = MI->getNumExplicitOperands();
    if (OpNum == NumOps - 1) {
      const MCInstrDesc &MemDesc = get(MemOpcode);
      uint64_t AccessBytes = SystemZII::getAccessSize(MemDesc.TSFlags);
      assert(AccessBytes != 0 && "Size of access should be known");
      assert(AccessBytes <= Size && "Access outside the frame index");
      uint64_t Offset = Size - AccessBytes;
      MachineInstrBuilder MIB = BuildMI(MF, MI->getDebugLoc(), get(MemOpcode));
      for (unsigned I = 0; I < OpNum; ++I)
        MIB.addOperand(MI->getOperand(I));
      MIB.addFrameIndex(FrameIndex).addImm(Offset);
      if (MemDesc.TSFlags & SystemZII::HasIndex)
        MIB.addReg(0);
      return MIB;
    }
  }

  return 0;
}

MachineInstr *
SystemZInstrInfo::foldMemoryOperandImpl(MachineFunction &MF, MachineInstr* MI,
                                        const SmallVectorImpl<unsigned> &Ops,
                                        MachineInstr* LoadMI) const {
  return 0;
}

bool
SystemZInstrInfo::expandPostRAPseudo(MachineBasicBlock::iterator MI) const {
  switch (MI->getOpcode()) {
  case SystemZ::L128:
    splitMove(MI, SystemZ::LG);
    return true;

  case SystemZ::ST128:
    splitMove(MI, SystemZ::STG);
    return true;

  case SystemZ::LX:
    splitMove(MI, SystemZ::LD);
    return true;

  case SystemZ::STX:
    splitMove(MI, SystemZ::STD);
    return true;

  case SystemZ::ADJDYNALLOC:
    splitAdjDynAlloc(MI);
    return true;

  default:
    return false;
  }
}

bool SystemZInstrInfo::
ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid branch condition!");
  Cond[0].setImm(Cond[0].getImm() ^ SystemZ::CCMASK_ANY);
  return false;
}

uint64_t SystemZInstrInfo::getInstSizeInBytes(const MachineInstr *MI) const {
  if (MI->getOpcode() == TargetOpcode::INLINEASM) {
    const MachineFunction *MF = MI->getParent()->getParent();
    const char *AsmStr = MI->getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  return MI->getDesc().getSize();
}

SystemZII::Branch
SystemZInstrInfo::getBranchInfo(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  case SystemZ::BR:
  case SystemZ::J:
  case SystemZ::JG:
    return SystemZII::Branch(SystemZII::BranchNormal, SystemZ::CCMASK_ANY,
                             &MI->getOperand(0));

  case SystemZ::BRC:
  case SystemZ::BRCL:
    return SystemZII::Branch(SystemZII::BranchNormal,
                             MI->getOperand(0).getImm(), &MI->getOperand(1));

  case SystemZ::CIJ:
  case SystemZ::CRJ:
    return SystemZII::Branch(SystemZII::BranchC, MI->getOperand(2).getImm(),
                             &MI->getOperand(3));

  case SystemZ::CGIJ:
  case SystemZ::CGRJ:
    return SystemZII::Branch(SystemZII::BranchCG, MI->getOperand(2).getImm(),
                             &MI->getOperand(3));

  default:
    llvm_unreachable("Unrecognized branch opcode");
  }
}

void SystemZInstrInfo::getLoadStoreOpcodes(const TargetRegisterClass *RC,
                                           unsigned &LoadOpcode,
                                           unsigned &StoreOpcode) const {
  if (RC == &SystemZ::GR32BitRegClass || RC == &SystemZ::ADDR32BitRegClass) {
    LoadOpcode = SystemZ::L;
    StoreOpcode = SystemZ::ST32;
  } else if (RC == &SystemZ::GR64BitRegClass ||
             RC == &SystemZ::ADDR64BitRegClass) {
    LoadOpcode = SystemZ::LG;
    StoreOpcode = SystemZ::STG;
  } else if (RC == &SystemZ::GR128BitRegClass ||
             RC == &SystemZ::ADDR128BitRegClass) {
    LoadOpcode = SystemZ::L128;
    StoreOpcode = SystemZ::ST128;
  } else if (RC == &SystemZ::FP32BitRegClass) {
    LoadOpcode = SystemZ::LE;
    StoreOpcode = SystemZ::STE;
  } else if (RC == &SystemZ::FP64BitRegClass) {
    LoadOpcode = SystemZ::LD;
    StoreOpcode = SystemZ::STD;
  } else if (RC == &SystemZ::FP128BitRegClass) {
    LoadOpcode = SystemZ::LX;
    StoreOpcode = SystemZ::STX;
  } else
    llvm_unreachable("Unsupported regclass to load or store");
}

unsigned SystemZInstrInfo::getOpcodeForOffset(unsigned Opcode,
                                              int64_t Offset) const {
  const MCInstrDesc &MCID = get(Opcode);
  int64_t Offset2 = (MCID.TSFlags & SystemZII::Is128Bit ? Offset + 8 : Offset);
  if (isUInt<12>(Offset) && isUInt<12>(Offset2)) {
    // Get the instruction to use for unsigned 12-bit displacements.
    int Disp12Opcode = SystemZ::getDisp12Opcode(Opcode);
    if (Disp12Opcode >= 0)
      return Disp12Opcode;

    // All address-related instructions can use unsigned 12-bit
    // displacements.
    return Opcode;
  }
  if (isInt<20>(Offset) && isInt<20>(Offset2)) {
    // Get the instruction to use for signed 20-bit displacements.
    int Disp20Opcode = SystemZ::getDisp20Opcode(Opcode);
    if (Disp20Opcode >= 0)
      return Disp20Opcode;

    // Check whether Opcode allows signed 20-bit displacements.
    if (MCID.TSFlags & SystemZII::Has20BitOffset)
      return Opcode;
  }
  return 0;
}

unsigned SystemZInstrInfo::getCompareAndBranch(unsigned Opcode,
                                               const MachineInstr *MI) const {
  switch (Opcode) {
  case SystemZ::CR:
    return SystemZ::CRJ;
  case SystemZ::CGR:
    return SystemZ::CGRJ;
  case SystemZ::CHI:
    return MI && isInt<8>(MI->getOperand(1).getImm()) ? SystemZ::CIJ : 0;
  case SystemZ::CGHI:
    return MI && isInt<8>(MI->getOperand(1).getImm()) ? SystemZ::CGIJ : 0;
  default:
    return 0;
  }
}

void SystemZInstrInfo::loadImmediate(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     unsigned Reg, uint64_t Value) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  unsigned Opcode;
  if (isInt<16>(Value))
    Opcode = SystemZ::LGHI;
  else if (SystemZ::isImmLL(Value))
    Opcode = SystemZ::LLILL;
  else if (SystemZ::isImmLH(Value)) {
    Opcode = SystemZ::LLILH;
    Value >>= 16;
  } else {
    assert(isInt<32>(Value) && "Huge values not handled yet");
    Opcode = SystemZ::LGFI;
  }
  BuildMI(MBB, MBBI, DL, get(Opcode), Reg).addImm(Value);
}
