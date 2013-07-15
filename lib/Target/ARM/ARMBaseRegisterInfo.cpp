//===-- ARMBaseRegisterInfo.cpp - ARM Register Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the base ARM implementation of TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMBaseRegisterInfo.h"
#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMFrameLowering.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#define GET_REGINFO_TARGET_DESC
#include "ARMGenRegisterInfo.inc"

using namespace llvm;

ARMBaseRegisterInfo::ARMBaseRegisterInfo(const ARMSubtarget &sti)
  : ARMGenRegisterInfo(ARM::LR, 0, 0, ARM::PC), STI(sti),
    FramePtr((STI.isTargetDarwin() || STI.isThumb()) ? ARM::R7 : ARM::R11),
    BasePtr(ARM::R6) {
}

const uint16_t*
ARMBaseRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  bool ghcCall = false;
 
  if (MF) {
    const Function *F = MF->getFunction();
    ghcCall = (F ? F->getCallingConv() == CallingConv::GHC : false);
  }
 
  if (ghcCall)
    // GHC set of callee saved regs is empty as all those regs are
    // used for passing STG regs around
    return CSR_NoRegs_SaveList;
  else
    return (STI.isTargetIOS() && !STI.isAAPCS_ABI())
      ? CSR_iOS_SaveList : CSR_AAPCS_SaveList;
}

const uint32_t*
ARMBaseRegisterInfo::getCallPreservedMask(CallingConv::ID CC) const {
  if (CC == CallingConv::GHC)
    // This is academic becase all GHC calls are (supposed to be) tail calls
    return CSR_NoRegs_RegMask;
  return (STI.isTargetIOS() && !STI.isAAPCS_ABI())
    ? CSR_iOS_RegMask : CSR_AAPCS_RegMask;
}

const uint32_t*
ARMBaseRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

const uint32_t*
ARMBaseRegisterInfo::getThisReturnPreservedMask(CallingConv::ID CC) const {
  // This should return a register mask that is the same as that returned by
  // getCallPreservedMask but that additionally preserves the register used for
  // the first i32 argument (which must also be the register used to return a
  // single i32 return value)
  //
  // In case that the calling convention does not use the same register for
  // both or otherwise does not want to enable this optimization, the function
  // should return NULL
  if (CC == CallingConv::GHC)
    // This is academic becase all GHC calls are (supposed to be) tail calls
    return NULL;
  return (STI.isTargetIOS() && !STI.isAAPCS_ABI())
    ? CSR_iOS_ThisReturn_RegMask : CSR_AAPCS_ThisReturn_RegMask;
}

BitVector ARMBaseRegisterInfo::
getReservedRegs(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();

  // FIXME: avoid re-calculating this every time.
  BitVector Reserved(getNumRegs());
  Reserved.set(ARM::SP);
  Reserved.set(ARM::PC);
  Reserved.set(ARM::FPSCR);
  Reserved.set(ARM::APSR_NZCV);
  if (TFI->hasFP(MF))
    Reserved.set(FramePtr);
  if (hasBasePointer(MF))
    Reserved.set(BasePtr);
  // Some targets reserve R9.
  if (STI.isR9Reserved())
    Reserved.set(ARM::R9);
  // Reserve D16-D31 if the subtarget doesn't support them.
  if (!STI.hasVFP3() || STI.hasD16()) {
    assert(ARM::D31 == ARM::D16 + 15);
    for (unsigned i = 0; i != 16; ++i)
      Reserved.set(ARM::D16 + i);
  }
  const TargetRegisterClass *RC  = &ARM::GPRPairRegClass;
  for(TargetRegisterClass::iterator I = RC->begin(), E = RC->end(); I!=E; ++I)
    for (MCSubRegIterator SI(*I, this); SI.isValid(); ++SI)
      if (Reserved.test(*SI)) Reserved.set(*I);

  return Reserved;
}

const TargetRegisterClass*
ARMBaseRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC)
                                                                         const {
  const TargetRegisterClass *Super = RC;
  TargetRegisterClass::sc_iterator I = RC->getSuperClasses();
  do {
    switch (Super->getID()) {
    case ARM::GPRRegClassID:
    case ARM::SPRRegClassID:
    case ARM::DPRRegClassID:
    case ARM::QPRRegClassID:
    case ARM::QQPRRegClassID:
    case ARM::QQQQPRRegClassID:
    case ARM::GPRPairRegClassID:
      return Super;
    }
    Super = *I++;
  } while (Super);
  return RC;
}

const TargetRegisterClass *
ARMBaseRegisterInfo::getPointerRegClass(const MachineFunction &MF, unsigned Kind)
                                                                         const {
  return &ARM::GPRRegClass;
}

const TargetRegisterClass *
ARMBaseRegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (RC == &ARM::CCRRegClass)
    return 0;  // Can't copy CCR registers.
  return RC;
}

unsigned
ARMBaseRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                         MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();

  switch (RC->getID()) {
  default:
    return 0;
  case ARM::tGPRRegClassID:
    return TFI->hasFP(MF) ? 4 : 5;
  case ARM::GPRRegClassID: {
    unsigned FP = TFI->hasFP(MF) ? 1 : 0;
    return 10 - FP - (STI.isR9Reserved() ? 1 : 0);
  }
  case ARM::SPRRegClassID:  // Currently not used as 'rep' register class.
  case ARM::DPRRegClassID:
    return 32 - 10;
  }
}

// Get the other register in a GPRPair.
static unsigned getPairedGPR(unsigned Reg, bool Odd, const MCRegisterInfo *RI) {
  for (MCSuperRegIterator Supers(Reg, RI); Supers.isValid(); ++Supers)
    if (ARM::GPRPairRegClass.contains(*Supers))
      return RI->getSubReg(*Supers, Odd ? ARM::gsub_1 : ARM::gsub_0);
  return 0;
}

// Resolve the RegPairEven / RegPairOdd register allocator hints.
void
ARMBaseRegisterInfo::getRegAllocationHints(unsigned VirtReg,
                                           ArrayRef<MCPhysReg> Order,
                                           SmallVectorImpl<MCPhysReg> &Hints,
                                           const MachineFunction &MF,
                                           const VirtRegMap *VRM) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  std::pair<unsigned, unsigned> Hint = MRI.getRegAllocationHint(VirtReg);

  unsigned Odd;
  switch (Hint.first) {
  case ARMRI::RegPairEven:
    Odd = 0;
    break;
  case ARMRI::RegPairOdd:
    Odd = 1;
    break;
  default:
    TargetRegisterInfo::getRegAllocationHints(VirtReg, Order, Hints, MF, VRM);
    return;
  }

  // This register should preferably be even (Odd == 0) or odd (Odd == 1).
  // Check if the other part of the pair has already been assigned, and provide
  // the paired register as the first hint.
  unsigned PairedPhys = 0;
  if (VRM && VRM->hasPhys(Hint.second)) {
    PairedPhys = getPairedGPR(VRM->getPhys(Hint.second), Odd, this);
    if (PairedPhys && MRI.isReserved(PairedPhys))
      PairedPhys = 0;
  }

  // First prefer the paired physreg.
  if (PairedPhys &&
      std::find(Order.begin(), Order.end(), PairedPhys) != Order.end())
    Hints.push_back(PairedPhys);

  // Then prefer even or odd registers.
  for (unsigned I = 0, E = Order.size(); I != E; ++I) {
    unsigned Reg = Order[I];
    if (Reg == PairedPhys || (getEncodingValue(Reg) & 1) != Odd)
      continue;
    // Don't provide hints that are paired to a reserved register.
    unsigned Paired = getPairedGPR(Reg, !Odd, this);
    if (!Paired || MRI.isReserved(Paired))
      continue;
    Hints.push_back(Reg);
  }
}

void
ARMBaseRegisterInfo::UpdateRegAllocHint(unsigned Reg, unsigned NewReg,
                                        MachineFunction &MF) const {
  MachineRegisterInfo *MRI = &MF.getRegInfo();
  std::pair<unsigned, unsigned> Hint = MRI->getRegAllocationHint(Reg);
  if ((Hint.first == (unsigned)ARMRI::RegPairOdd ||
       Hint.first == (unsigned)ARMRI::RegPairEven) &&
      TargetRegisterInfo::isVirtualRegister(Hint.second)) {
    // If 'Reg' is one of the even / odd register pair and it's now changed
    // (e.g. coalesced) into a different register. The other register of the
    // pair allocation hint must be updated to reflect the relationship
    // change.
    unsigned OtherReg = Hint.second;
    Hint = MRI->getRegAllocationHint(OtherReg);
    if (Hint.second == Reg)
      // Make sure the pair has not already divorced.
      MRI->setRegAllocationHint(OtherReg, Hint.first, NewReg);
  }
}

bool
ARMBaseRegisterInfo::avoidWriteAfterWrite(const TargetRegisterClass *RC) const {
  // CortexA9 has a Write-after-write hazard for NEON registers.
  if (!STI.isLikeA9())
    return false;

  switch (RC->getID()) {
  case ARM::DPRRegClassID:
  case ARM::DPR_8RegClassID:
  case ARM::DPR_VFP2RegClassID:
  case ARM::QPRRegClassID:
  case ARM::QPR_8RegClassID:
  case ARM::QPR_VFP2RegClassID:
  case ARM::SPRRegClassID:
  case ARM::SPR_8RegClassID:
    // Avoid reusing S, D, and Q registers.
    // Don't increase register pressure for QQ and QQQQ.
    return true;
  default:
    return false;
  }
}

bool ARMBaseRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();

  // When outgoing call frames are so large that we adjust the stack pointer
  // around the call, we can no longer use the stack pointer to reach the
  // emergency spill slot.
  if (needsStackRealignment(MF) && !TFI->hasReservedCallFrame(MF))
    return true;

  // Thumb has trouble with negative offsets from the FP. Thumb2 has a limited
  // negative range for ldr/str (255), and thumb1 is positive offsets only.
  // It's going to be better to use the SP or Base Pointer instead. When there
  // are variable sized objects, we can't reference off of the SP, so we
  // reserve a Base Pointer.
  if (AFI->isThumbFunction() && MFI->hasVarSizedObjects()) {
    // Conservatively estimate whether the negative offset from the frame
    // pointer will be sufficient to reach. If a function has a smallish
    // frame, it's less likely to have lots of spills and callee saved
    // space, so it's all more likely to be within range of the frame pointer.
    // If it's wrong, the scavenger will still enable access to work, it just
    // won't be optimal.
    if (AFI->isThumb2Function() && MFI->getLocalFrameSize() < 128)
      return false;
    return true;
  }

  return false;
}

bool ARMBaseRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  const MachineRegisterInfo *MRI = &MF.getRegInfo();
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  // We can't realign the stack if:
  // 1. Dynamic stack realignment is explicitly disabled,
  // 2. This is a Thumb1 function (it's not useful, so we don't bother), or
  // 3. There are VLAs in the function and the base pointer is disabled.
  if (!MF.getTarget().Options.RealignStack)
    return false;
  if (AFI->isThumb1OnlyFunction())
    return false;
  // Stack realignment requires a frame pointer.  If we already started
  // register allocation with frame pointer elimination, it is too late now.
  if (!MRI->canReserveReg(FramePtr))
    return false;
  // We may also need a base pointer if there are dynamic allocas or stack
  // pointer adjustments around calls.
  if (MF.getTarget().getFrameLowering()->hasReservedCallFrame(MF))
    return true;
  // A base pointer is required and allowed.  Check that it isn't too late to
  // reserve it.
  return MRI->canReserveReg(BasePtr);
}

bool ARMBaseRegisterInfo::
needsStackRealignment(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  const Function *F = MF.getFunction();
  unsigned StackAlign = MF.getTarget().getFrameLowering()->getStackAlignment();
  bool requiresRealignment =
    ((MFI->getMaxAlignment() > StackAlign) ||
     F->getAttributes().hasAttribute(AttributeSet::FunctionIndex,
                                     Attribute::StackAlignment));

  return requiresRealignment && canRealignStack(MF);
}

bool ARMBaseRegisterInfo::
cannotEliminateFrame(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  if (MF.getTarget().Options.DisableFramePointerElim(MF) && MFI->adjustsStack())
    return true;
  return MFI->hasVarSizedObjects() || MFI->isFrameAddressTaken()
    || needsStackRealignment(MF);
}

unsigned
ARMBaseRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();

  if (TFI->hasFP(MF))
    return FramePtr;
  return ARM::SP;
}

unsigned ARMBaseRegisterInfo::getEHExceptionRegister() const {
  llvm_unreachable("What is the exception register");
}

unsigned ARMBaseRegisterInfo::getEHHandlerRegister() const {
  llvm_unreachable("What is the exception handler register");
}

/// emitLoadConstPool - Emits a load from constpool to materialize the
/// specified immediate.
void ARMBaseRegisterInfo::
emitLoadConstPool(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator &MBBI,
                  DebugLoc dl,
                  unsigned DestReg, unsigned SubIdx, int Val,
                  ARMCC::CondCodes Pred,
                  unsigned PredReg, unsigned MIFlags) const {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  MachineConstantPool *ConstantPool = MF.getConstantPool();
  const Constant *C =
        ConstantInt::get(Type::getInt32Ty(MF.getFunction()->getContext()), Val);
  unsigned Idx = ConstantPool->getConstantPoolIndex(C, 4);

  BuildMI(MBB, MBBI, dl, TII.get(ARM::LDRcp))
    .addReg(DestReg, getDefRegState(true), SubIdx)
    .addConstantPoolIndex(Idx)
    .addImm(0).addImm(Pred).addReg(PredReg)
    .setMIFlags(MIFlags);
}

bool ARMBaseRegisterInfo::
requiresRegisterScavenging(const MachineFunction &MF) const {
  return true;
}

bool ARMBaseRegisterInfo::
trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  return true;
}

bool ARMBaseRegisterInfo::
requiresFrameIndexScavenging(const MachineFunction &MF) const {
  return true;
}

bool ARMBaseRegisterInfo::
requiresVirtualBaseRegisters(const MachineFunction &MF) const {
  return true;
}

int64_t ARMBaseRegisterInfo::
getFrameIndexInstrOffset(const MachineInstr *MI, int Idx) const {
  const MCInstrDesc &Desc = MI->getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  int64_t InstrOffs = 0;
  int Scale = 1;
  unsigned ImmIdx = 0;
  switch (AddrMode) {
  case ARMII::AddrModeT2_i8:
  case ARMII::AddrModeT2_i12:
  case ARMII::AddrMode_i12:
    InstrOffs = MI->getOperand(Idx+1).getImm();
    Scale = 1;
    break;
  case ARMII::AddrMode5: {
    // VFP address mode.
    const MachineOperand &OffOp = MI->getOperand(Idx+1);
    InstrOffs = ARM_AM::getAM5Offset(OffOp.getImm());
    if (ARM_AM::getAM5Op(OffOp.getImm()) == ARM_AM::sub)
      InstrOffs = -InstrOffs;
    Scale = 4;
    break;
  }
  case ARMII::AddrMode2: {
    ImmIdx = Idx+2;
    InstrOffs = ARM_AM::getAM2Offset(MI->getOperand(ImmIdx).getImm());
    if (ARM_AM::getAM2Op(MI->getOperand(ImmIdx).getImm()) == ARM_AM::sub)
      InstrOffs = -InstrOffs;
    break;
  }
  case ARMII::AddrMode3: {
    ImmIdx = Idx+2;
    InstrOffs = ARM_AM::getAM3Offset(MI->getOperand(ImmIdx).getImm());
    if (ARM_AM::getAM3Op(MI->getOperand(ImmIdx).getImm()) == ARM_AM::sub)
      InstrOffs = -InstrOffs;
    break;
  }
  case ARMII::AddrModeT1_s: {
    ImmIdx = Idx+1;
    InstrOffs = MI->getOperand(ImmIdx).getImm();
    Scale = 4;
    break;
  }
  default:
    llvm_unreachable("Unsupported addressing mode!");
  }

  return InstrOffs * Scale;
}

/// needsFrameBaseReg - Returns true if the instruction's frame index
/// reference would be better served by a base register other than FP
/// or SP. Used by LocalStackFrameAllocation to determine which frame index
/// references it should create new base registers for.
bool ARMBaseRegisterInfo::
needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  for (unsigned i = 0; !MI->getOperand(i).isFI(); ++i) {
    assert(i < MI->getNumOperands() &&"Instr doesn't have FrameIndex operand!");
  }

  // It's the load/store FI references that cause issues, as it can be difficult
  // to materialize the offset if it won't fit in the literal field. Estimate
  // based on the size of the local frame and some conservative assumptions
  // about the rest of the stack frame (note, this is pre-regalloc, so
  // we don't know everything for certain yet) whether this offset is likely
  // to be out of range of the immediate. Return true if so.

  // We only generate virtual base registers for loads and stores, so
  // return false for everything else.
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
  case ARM::LDRi12: case ARM::LDRH: case ARM::LDRBi12:
  case ARM::STRi12: case ARM::STRH: case ARM::STRBi12:
  case ARM::t2LDRi12: case ARM::t2LDRi8:
  case ARM::t2STRi12: case ARM::t2STRi8:
  case ARM::VLDRS: case ARM::VLDRD:
  case ARM::VSTRS: case ARM::VSTRD:
  case ARM::tSTRspi: case ARM::tLDRspi:
    break;
  default:
    return false;
  }

  // Without a virtual base register, if the function has variable sized
  // objects, all fixed-size local references will be via the frame pointer,
  // Approximate the offset and see if it's legal for the instruction.
  // Note that the incoming offset is based on the SP value at function entry,
  // so it'll be negative.
  MachineFunction &MF = *MI->getParent()->getParent();
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Estimate an offset from the frame pointer.
  // Conservatively assume all callee-saved registers get pushed. R4-R6
  // will be earlier than the FP, so we ignore those.
  // R7, LR
  int64_t FPOffset = Offset - 8;
  // ARM and Thumb2 functions also need to consider R8-R11 and D8-D15
  if (!AFI->isThumbFunction() || !AFI->isThumb1OnlyFunction())
    FPOffset -= 80;
  // Estimate an offset from the stack pointer.
  // The incoming offset is relating to the SP at the start of the function,
  // but when we access the local it'll be relative to the SP after local
  // allocation, so adjust our SP-relative offset by that allocation size.
  Offset = -Offset;
  Offset += MFI->getLocalFrameSize();
  // Assume that we'll have at least some spill slots allocated.
  // FIXME: This is a total SWAG number. We should run some statistics
  //        and pick a real one.
  Offset += 128; // 128 bytes of spill slots

  // If there is a frame pointer, try using it.
  // The FP is only available if there is no dynamic realignment. We
  // don't know for sure yet whether we'll need that, so we guess based
  // on whether there are any local variables that would trigger it.
  unsigned StackAlign = TFI->getStackAlignment();
  if (TFI->hasFP(MF) &&
      !((MFI->getLocalFrameMaxAlign() > StackAlign) && canRealignStack(MF))) {
    if (isFrameOffsetLegal(MI, FPOffset))
      return false;
  }
  // If we can reference via the stack pointer, try that.
  // FIXME: This (and the code that resolves the references) can be improved
  //        to only disallow SP relative references in the live range of
  //        the VLA(s). In practice, it's unclear how much difference that
  //        would make, but it may be worth doing.
  if (!MFI->hasVarSizedObjects() && isFrameOffsetLegal(MI, Offset))
    return false;

  // The offset likely isn't legal, we want to allocate a virtual base register.
  return true;
}

/// materializeFrameBaseRegister - Insert defining instruction(s) for BaseReg to
/// be a pointer to FrameIdx at the beginning of the basic block.
void ARMBaseRegisterInfo::
materializeFrameBaseRegister(MachineBasicBlock *MBB,
                             unsigned BaseReg, int FrameIdx,
                             int64_t Offset) const {
  ARMFunctionInfo *AFI = MBB->getParent()->getInfo<ARMFunctionInfo>();
  unsigned ADDriOpc = !AFI->isThumbFunction() ? ARM::ADDri :
    (AFI->isThumb1OnlyFunction() ? ARM::tADDrSPi : ARM::t2ADDri);

  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL;                  // Defaults to "unknown"
  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  const MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const TargetInstrInfo &TII = *MF.getTarget().getInstrInfo();
  const MCInstrDesc &MCID = TII.get(ADDriOpc);
  MRI.constrainRegClass(BaseReg, TII.getRegClass(MCID, 0, this, MF));

  MachineInstrBuilder MIB = AddDefaultPred(BuildMI(*MBB, Ins, DL, MCID, BaseReg)
    .addFrameIndex(FrameIdx).addImm(Offset));

  if (!AFI->isThumb1OnlyFunction())
    AddDefaultCC(MIB);
}

void
ARMBaseRegisterInfo::resolveFrameIndex(MachineBasicBlock::iterator I,
                                       unsigned BaseReg, int64_t Offset) const {
  MachineInstr &MI = *I;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const ARMBaseInstrInfo &TII =
    *static_cast<const ARMBaseInstrInfo*>(MF.getTarget().getInstrInfo());
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  int Off = Offset; // ARM doesn't need the general 64-bit offsets
  unsigned i = 0;

  assert(!AFI->isThumb1OnlyFunction() &&
         "This resolveFrameIndex does not support Thumb1!");

  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  bool Done = false;
  if (!AFI->isThumbFunction())
    Done = rewriteARMFrameIndex(MI, i, BaseReg, Off, TII);
  else {
    assert(AFI->isThumb2Function());
    Done = rewriteT2FrameIndex(MI, i, BaseReg, Off, TII);
  }
  assert (Done && "Unable to resolve frame index!");
  (void)Done;
}

bool ARMBaseRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                             int64_t Offset) const {
  const MCInstrDesc &Desc = MI->getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  unsigned i = 0;

  while (!MI->getOperand(i).isFI()) {
    ++i;
    assert(i < MI->getNumOperands() &&"Instr doesn't have FrameIndex operand!");
  }

  // AddrMode4 and AddrMode6 cannot handle any offset.
  if (AddrMode == ARMII::AddrMode4 || AddrMode == ARMII::AddrMode6)
    return Offset == 0;

  unsigned NumBits = 0;
  unsigned Scale = 1;
  bool isSigned = true;
  switch (AddrMode) {
  case ARMII::AddrModeT2_i8:
  case ARMII::AddrModeT2_i12:
    // i8 supports only negative, and i12 supports only positive, so
    // based on Offset sign, consider the appropriate instruction
    Scale = 1;
    if (Offset < 0) {
      NumBits = 8;
      Offset = -Offset;
    } else {
      NumBits = 12;
    }
    break;
  case ARMII::AddrMode5:
    // VFP address mode.
    NumBits = 8;
    Scale = 4;
    break;
  case ARMII::AddrMode_i12:
  case ARMII::AddrMode2:
    NumBits = 12;
    break;
  case ARMII::AddrMode3:
    NumBits = 8;
    break;
  case ARMII::AddrModeT1_s:
    NumBits = 5;
    Scale = 4;
    isSigned = false;
    break;
  default:
    llvm_unreachable("Unsupported addressing mode!");
  }

  Offset += getFrameIndexInstrOffset(MI, i);
  // Make sure the offset is encodable for instructions that scale the
  // immediate.
  if ((Offset & (Scale-1)) != 0)
    return false;

  if (isSigned && Offset < 0)
    Offset = -Offset;

  unsigned Mask = (1 << NumBits) - 1;
  if ((unsigned)Offset <= Mask * Scale)
    return true;

  return false;
}

void
ARMBaseRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                         int SPAdj, unsigned FIOperandNum,
                                         RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const ARMBaseInstrInfo &TII =
    *static_cast<const ARMBaseInstrInfo*>(MF.getTarget().getInstrInfo());
  const ARMFrameLowering *TFI =
    static_cast<const ARMFrameLowering*>(MF.getTarget().getFrameLowering());
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  assert(!AFI->isThumb1OnlyFunction() &&
         "This eliminateFrameIndex does not support Thumb1!");
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  unsigned FrameReg;

  int Offset = TFI->ResolveFrameIndexReference(MF, FrameIndex, FrameReg, SPAdj);

  // PEI::scavengeFrameVirtualRegs() cannot accurately track SPAdj because the
  // call frame setup/destroy instructions have already been eliminated.  That
  // means the stack pointer cannot be used to access the emergency spill slot
  // when !hasReservedCallFrame().
#ifndef NDEBUG
  if (RS && FrameReg == ARM::SP && RS->isScavengingFrameIndex(FrameIndex)){
    assert(TFI->hasReservedCallFrame(MF) &&
           "Cannot use SP to access the emergency spill slot in "
           "functions without a reserved call frame");
    assert(!MF.getFrameInfo()->hasVarSizedObjects() &&
           "Cannot use SP to access the emergency spill slot in "
           "functions with variable sized frame objects");
  }
#endif // NDEBUG

  assert(!MI.isDebugValue() && "DBG_VALUEs should be handled in target-independent code");

  // Modify MI as necessary to handle as much of 'Offset' as possible
  bool Done = false;
  if (!AFI->isThumbFunction())
    Done = rewriteARMFrameIndex(MI, FIOperandNum, FrameReg, Offset, TII);
  else {
    assert(AFI->isThumb2Function());
    Done = rewriteT2FrameIndex(MI, FIOperandNum, FrameReg, Offset, TII);
  }
  if (Done)
    return;

  // If we get here, the immediate doesn't fit into the instruction.  We folded
  // as much as possible above, handle the rest, providing a register that is
  // SP+LargeImm.
  assert((Offset ||
          (MI.getDesc().TSFlags & ARMII::AddrModeMask) == ARMII::AddrMode4 ||
          (MI.getDesc().TSFlags & ARMII::AddrModeMask) == ARMII::AddrMode6) &&
         "This code isn't needed if offset already handled!");

  unsigned ScratchReg = 0;
  int PIdx = MI.findFirstPredOperandIdx();
  ARMCC::CondCodes Pred = (PIdx == -1)
    ? ARMCC::AL : (ARMCC::CondCodes)MI.getOperand(PIdx).getImm();
  unsigned PredReg = (PIdx == -1) ? 0 : MI.getOperand(PIdx+1).getReg();
  if (Offset == 0)
    // Must be addrmode4/6.
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false, false, false);
  else {
    ScratchReg = MF.getRegInfo().createVirtualRegister(&ARM::GPRRegClass);
    if (!AFI->isThumbFunction())
      emitARMRegPlusImmediate(MBB, II, MI.getDebugLoc(), ScratchReg, FrameReg,
                              Offset, Pred, PredReg, TII);
    else {
      assert(AFI->isThumb2Function());
      emitT2RegPlusImmediate(MBB, II, MI.getDebugLoc(), ScratchReg, FrameReg,
                             Offset, Pred, PredReg, TII);
    }
    // Update the original instruction to use the scratch register.
    MI.getOperand(FIOperandNum).ChangeToRegister(ScratchReg, false, false,true);
  }
}
