//===-- X86RegisterInfo.cpp - X86 Register Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetRegisterInfo class.
// This file is responsible for the frame pointer elimination optimization
// on X86.
//
//===----------------------------------------------------------------------===//

#include "X86RegisterInfo.h"
#include "X86FrameLowering.h"
#include "X86InstrBuilder.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "X86GenRegisterInfo.inc"

static cl::opt<bool>
EnableBasePointer("x86-use-base-pointer", cl::Hidden, cl::init(true),
          cl::desc("Enable use of a base pointer for complex stack frames"));

X86RegisterInfo::X86RegisterInfo(const Triple &TT)
    : X86GenRegisterInfo((TT.isArch64Bit() ? X86::RIP : X86::EIP),
                         X86_MC::getDwarfRegFlavour(TT, false),
                         X86_MC::getDwarfRegFlavour(TT, true),
                         (TT.isArch64Bit() ? X86::RIP : X86::EIP)) {
  X86_MC::InitLLVM2SEHRegisterMapping(this);

  // Cache some information.
  Is64Bit = TT.isArch64Bit();
  IsWin64 = Is64Bit && TT.isOSWindows();

  // Use a callee-saved register as the base pointer.  These registers must
  // not conflict with any ABI requirements.  For example, in 32-bit mode PIC
  // requires GOT in the EBX register before function calls via PLT GOT pointer.
  if (Is64Bit) {
    SlotSize = 8;
    // This matches the simplified 32-bit pointer code in the data layout
    // computation.
    // FIXME: Should use the data layout?
    bool Use64BitReg = TT.getEnvironment() != Triple::GNUX32;
    StackPtr = Use64BitReg ? X86::RSP : X86::ESP;
    FramePtr = Use64BitReg ? X86::RBP : X86::EBP;
    BasePtr = Use64BitReg ? X86::RBX : X86::EBX;
  } else {
    SlotSize = 4;
    StackPtr = X86::ESP;
    FramePtr = X86::EBP;
    BasePtr = X86::ESI;
  }
}

bool
X86RegisterInfo::trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  // ExeDepsFixer and PostRAScheduler require liveness.
  return true;
}

int
X86RegisterInfo::getSEHRegNum(unsigned i) const {
  return getEncodingValue(i);
}

const TargetRegisterClass *
X86RegisterInfo::getSubClassWithSubReg(const TargetRegisterClass *RC,
                                       unsigned Idx) const {
  // The sub_8bit sub-register index is more constrained in 32-bit mode.
  // It behaves just like the sub_8bit_hi index.
  if (!Is64Bit && Idx == X86::sub_8bit)
    Idx = X86::sub_8bit_hi;

  // Forward to TableGen's default version.
  return X86GenRegisterInfo::getSubClassWithSubReg(RC, Idx);
}

const TargetRegisterClass *
X86RegisterInfo::getMatchingSuperRegClass(const TargetRegisterClass *A,
                                          const TargetRegisterClass *B,
                                          unsigned SubIdx) const {
  // The sub_8bit sub-register index is more constrained in 32-bit mode.
  if (!Is64Bit && SubIdx == X86::sub_8bit) {
    A = X86GenRegisterInfo::getSubClassWithSubReg(A, X86::sub_8bit_hi);
    if (!A)
      return nullptr;
  }
  return X86GenRegisterInfo::getMatchingSuperRegClass(A, B, SubIdx);
}

const TargetRegisterClass *
X86RegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &MF) const {
  // Don't allow super-classes of GR8_NOREX.  This class is only used after
  // extracting sub_8bit_hi sub-registers.  The H sub-registers cannot be copied
  // to the full GR8 register class in 64-bit mode, so we cannot allow the
  // reigster class inflation.
  //
  // The GR8_NOREX class is always used in a way that won't be constrained to a
  // sub-class, so sub-classes like GR8_ABCD_L are allowed to expand to the
  // full GR8 class.
  if (RC == &X86::GR8_NOREXRegClass)
    return RC;

  const TargetRegisterClass *Super = RC;
  TargetRegisterClass::sc_iterator I = RC->getSuperClasses();
  do {
    switch (Super->getID()) {
    case X86::GR8RegClassID:
    case X86::GR16RegClassID:
    case X86::GR32RegClassID:
    case X86::GR64RegClassID:
    case X86::FR32RegClassID:
    case X86::FR64RegClassID:
    case X86::RFP32RegClassID:
    case X86::RFP64RegClassID:
    case X86::RFP80RegClassID:
    case X86::VR128RegClassID:
    case X86::VR256RegClassID:
      // Don't return a super-class that would shrink the spill size.
      // That can happen with the vector and float classes.
      if (Super->getSize() == RC->getSize())
        return Super;
    }
    Super = *I++;
  } while (Super);
  return RC;
}

const TargetRegisterClass *
X86RegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                    unsigned Kind) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  switch (Kind) {
  default: llvm_unreachable("Unexpected Kind in getPointerRegClass!");
  case 0: // Normal GPRs.
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64RegClass;
    return &X86::GR32RegClass;
  case 1: // Normal GPRs except the stack pointer (for encoding reasons).
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOSPRegClass;
    return &X86::GR32_NOSPRegClass;
  case 2: // NOREX GPRs.
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOREXRegClass;
    return &X86::GR32_NOREXRegClass;
  case 3: // NOREX GPRs except the stack pointer (for encoding reasons).
    if (Subtarget.isTarget64BitLP64())
      return &X86::GR64_NOREX_NOSPRegClass;
    return &X86::GR32_NOREX_NOSPRegClass;
  case 4: // Available for tailcall (not callee-saved GPRs).
    return getGPRsForTailCall(MF);
  }
}

const TargetRegisterClass *
X86RegisterInfo::getGPRsForTailCall(const MachineFunction &MF) const {
  const Function *F = MF.getFunction();
  if (IsWin64 || (F && F->getCallingConv() == CallingConv::X86_64_Win64))
    return &X86::GR64_TCW64RegClass;
  else if (Is64Bit)
    return &X86::GR64_TCRegClass;

  bool hasHipeCC = (F ? F->getCallingConv() == CallingConv::HiPE : false);
  if (hasHipeCC)
    return &X86::GR32RegClass;
  return &X86::GR32_TCRegClass;
}

const TargetRegisterClass *
X86RegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (RC == &X86::CCRRegClass) {
    if (Is64Bit)
      return &X86::GR64RegClass;
    else
      return &X86::GR32RegClass;
  }
  return RC;
}

unsigned
X86RegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                     MachineFunction &MF) const {
  const X86FrameLowering *TFI = getFrameLowering(MF);

  unsigned FPDiff = TFI->hasFP(MF) ? 1 : 0;
  switch (RC->getID()) {
  default:
    return 0;
  case X86::GR32RegClassID:
    return 4 - FPDiff;
  case X86::GR64RegClassID:
    return 12 - FPDiff;
  case X86::VR128RegClassID:
    return Is64Bit ? 10 : 4;
  case X86::VR64RegClassID:
    return 4;
  }
}

const MCPhysReg *
X86RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const X86Subtarget &Subtarget = MF->getSubtarget<X86Subtarget>();
  bool HasSSE = Subtarget.hasSSE1();
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();
  bool CallsEHReturn = MF->getMMI().callsEHReturn();

  assert(MF && "MachineFunction required");
  switch (MF->getFunction()->getCallingConv()) {
  case CallingConv::GHC:
  case CallingConv::HiPE:
    return CSR_NoRegs_SaveList;
  case CallingConv::AnyReg:
    if (HasAVX)
      return CSR_64_AllRegs_AVX_SaveList;
    return CSR_64_AllRegs_SaveList;
  case CallingConv::PreserveMost:
    return CSR_64_RT_MostRegs_SaveList;
  case CallingConv::PreserveAll:
    if (HasAVX)
      return CSR_64_RT_AllRegs_AVX_SaveList;
    return CSR_64_RT_AllRegs_SaveList;
  case CallingConv::CXX_FAST_TLS:
    if (Is64Bit)
      return CSR_64_TLS_Darwin_SaveList;
    break;
  case CallingConv::Intel_OCL_BI: {
    if (HasAVX512 && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX512_SaveList;
    if (HasAVX512 && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX512_SaveList;
    if (HasAVX && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX_SaveList;
    if (HasAVX && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX_SaveList;
    if (!HasAVX && !IsWin64 && Is64Bit)
      return CSR_64_Intel_OCL_BI_SaveList;
    break;
  }
  case CallingConv::HHVM:
    return CSR_64_HHVM_SaveList;
  case CallingConv::Cold:
    if (Is64Bit)
      return CSR_64_MostRegs_SaveList;
    break;
  case CallingConv::X86_64_Win64:
    return CSR_Win64_SaveList;
  case CallingConv::X86_64_SysV:
    if (CallsEHReturn)
      return CSR_64EHRet_SaveList;
    return CSR_64_SaveList;
  case CallingConv::X86_INTR:
    if (Is64Bit) {
      if (HasAVX)
        return CSR_64_AllRegs_AVX_SaveList;
      else
        return CSR_64_AllRegs_SaveList;
    } else {
      if (HasSSE)
        return CSR_32_AllRegs_SSE_SaveList;
      else
        return CSR_32_AllRegs_SaveList;
    }
  default:
    break;
  }

  if (Is64Bit) {
    if (IsWin64)
      return CSR_Win64_SaveList;
    if (CallsEHReturn)
      return CSR_64EHRet_SaveList;
    return CSR_64_SaveList;
  }
  if (CallsEHReturn)
    return CSR_32EHRet_SaveList;
  return CSR_32_SaveList;
}

const uint32_t *
X86RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  bool HasSSE = Subtarget.hasSSE1();
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();

  switch (CC) {
  case CallingConv::GHC:
  case CallingConv::HiPE:
    return CSR_NoRegs_RegMask;
  case CallingConv::AnyReg:
    if (HasAVX)
      return CSR_64_AllRegs_AVX_RegMask;
    return CSR_64_AllRegs_RegMask;
  case CallingConv::PreserveMost:
    return CSR_64_RT_MostRegs_RegMask;
  case CallingConv::PreserveAll:
    if (HasAVX)
      return CSR_64_RT_AllRegs_AVX_RegMask;
    return CSR_64_RT_AllRegs_RegMask;
  case CallingConv::CXX_FAST_TLS:
    if (Is64Bit)
      return CSR_64_TLS_Darwin_RegMask;
    break;
  case CallingConv::Intel_OCL_BI: {
    if (HasAVX512 && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX512_RegMask;
    if (HasAVX512 && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX512_RegMask;
    if (HasAVX && IsWin64)
      return CSR_Win64_Intel_OCL_BI_AVX_RegMask;
    if (HasAVX && Is64Bit)
      return CSR_64_Intel_OCL_BI_AVX_RegMask;
    if (!HasAVX && !IsWin64 && Is64Bit)
      return CSR_64_Intel_OCL_BI_RegMask;
    break;
  }
  case CallingConv::HHVM:
    return CSR_64_HHVM_RegMask;
  case CallingConv::Cold:
    if (Is64Bit)
      return CSR_64_MostRegs_RegMask;
    break;
  case CallingConv::X86_64_Win64:
    return CSR_Win64_RegMask;
  case CallingConv::X86_64_SysV:
    return CSR_64_RegMask;
  case CallingConv::X86_INTR:
    if (Is64Bit) {
      if (HasAVX)
        return CSR_64_AllRegs_AVX_RegMask;
      else
        return CSR_64_AllRegs_RegMask;
    } else {
      if (HasSSE)
        return CSR_32_AllRegs_SSE_RegMask;
      else
        return CSR_32_AllRegs_RegMask;
    }
    default:
      break;
  }

  // Unlike getCalleeSavedRegs(), we don't have MMI so we can't check
  // callsEHReturn().
  if (Is64Bit) {
    if (IsWin64)
      return CSR_Win64_RegMask;
    return CSR_64_RegMask;
  }
  return CSR_32_RegMask;
}

const uint32_t*
X86RegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

const uint32_t *X86RegisterInfo::getDarwinTLSCallPreservedMask() const {
  return CSR_64_TLS_Darwin_RegMask;
}

BitVector X86RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const X86FrameLowering *TFI = getFrameLowering(MF);

  // Set the stack-pointer register and its aliases as reserved.
  for (MCSubRegIterator I(X86::RSP, this, /*IncludeSelf=*/true); I.isValid();
       ++I)
    Reserved.set(*I);

  // Set the instruction pointer register and its aliases as reserved.
  for (MCSubRegIterator I(X86::RIP, this, /*IncludeSelf=*/true); I.isValid();
       ++I)
    Reserved.set(*I);

  // Set the frame-pointer register and its aliases as reserved if needed.
  if (TFI->hasFP(MF)) {
    for (MCSubRegIterator I(X86::RBP, this, /*IncludeSelf=*/true); I.isValid();
         ++I)
      Reserved.set(*I);
  }

  // Set the base-pointer register and its aliases as reserved if needed.
  if (hasBasePointer(MF)) {
    CallingConv::ID CC = MF.getFunction()->getCallingConv();
    const uint32_t *RegMask = getCallPreservedMask(MF, CC);
    if (MachineOperand::clobbersPhysReg(RegMask, getBaseRegister()))
      report_fatal_error(
        "Stack realignment in presence of dynamic allocas is not supported with"
        "this calling convention.");

    unsigned BasePtr = getX86SubSuperRegister(getBaseRegister(), 64);
    for (MCSubRegIterator I(BasePtr, this, /*IncludeSelf=*/true);
         I.isValid(); ++I)
      Reserved.set(*I);
  }

  // Mark the segment registers as reserved.
  Reserved.set(X86::CS);
  Reserved.set(X86::SS);
  Reserved.set(X86::DS);
  Reserved.set(X86::ES);
  Reserved.set(X86::FS);
  Reserved.set(X86::GS);

  // Mark the floating point stack registers as reserved.
  for (unsigned n = 0; n != 8; ++n)
    Reserved.set(X86::ST0 + n);

  // Reserve the registers that only exist in 64-bit mode.
  if (!Is64Bit) {
    // These 8-bit registers are part of the x86-64 extension even though their
    // super-registers are old 32-bits.
    Reserved.set(X86::SIL);
    Reserved.set(X86::DIL);
    Reserved.set(X86::BPL);
    Reserved.set(X86::SPL);

    for (unsigned n = 0; n != 8; ++n) {
      // R8, R9, ...
      for (MCRegAliasIterator AI(X86::R8 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);

      // XMM8, XMM9, ...
      for (MCRegAliasIterator AI(X86::XMM8 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);
    }
  }
  if (!Is64Bit || !MF.getSubtarget<X86Subtarget>().hasAVX512()) {
    for (unsigned n = 16; n != 32; ++n) {
      for (MCRegAliasIterator AI(X86::XMM0 + n, this, true); AI.isValid(); ++AI)
        Reserved.set(*AI);
    }
  }

  return Reserved;
}

void X86RegisterInfo::adjustStackMapLiveOutMask(uint32_t *Mask) const {
  // Check if the EFLAGS register is marked as live-out. This shouldn't happen,
  // because the calling convention defines the EFLAGS register as NOT
  // preserved.
  //
  // Unfortunatelly the EFLAGS show up as live-out after branch folding. Adding
  // an assert to track this and clear the register afterwards to avoid
  // unnecessary crashes during release builds.
  assert(!(Mask[X86::EFLAGS / 32] & (1U << (X86::EFLAGS % 32))) &&
         "EFLAGS are not live-out from a patchpoint.");

  // Also clean other registers that don't need preserving (IP).
  for (auto Reg : {X86::EFLAGS, X86::RIP, X86::EIP, X86::IP})
    Mask[Reg / 32] &= ~(1U << (Reg % 32));
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

static bool CantUseSP(const MachineFrameInfo *MFI) {
  return MFI->hasVarSizedObjects() || MFI->hasOpaqueSPAdjustment();
}

bool X86RegisterInfo::hasBasePointer(const MachineFunction &MF) const {
   const MachineFrameInfo *MFI = MF.getFrameInfo();

   if (!EnableBasePointer)
     return false;

   // When we need stack realignment, we can't address the stack from the frame
   // pointer.  When we have dynamic allocas or stack-adjusting inline asm, we
   // can't address variables from the stack pointer.  MS inline asm can
   // reference locals while also adjusting the stack pointer.  When we can't
   // use both the SP and the FP, we need a separate base pointer register.
   bool CantUseFP = needsStackRealignment(MF);
   return CantUseFP && CantUseSP(MFI);
}

bool X86RegisterInfo::canRealignStack(const MachineFunction &MF) const {
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;

  const MachineFrameInfo *MFI = MF.getFrameInfo();
  const MachineRegisterInfo *MRI = &MF.getRegInfo();

  // Stack realignment requires a frame pointer.  If we already started
  // register allocation with frame pointer elimination, it is too late now.
  if (!MRI->canReserveReg(FramePtr))
    return false;

  // If a base pointer is necessary.  Check that it isn't too late to reserve
  // it.
  if (CantUseSP(MFI))
    return MRI->canReserveReg(BasePtr);
  return true;
}

bool X86RegisterInfo::hasReservedSpillSlot(const MachineFunction &MF,
                                           unsigned Reg, int &FrameIdx) const {
  // Since X86 defines assignCalleeSavedSpillSlots which always return true
  // this function neither used nor tested.
  llvm_unreachable("Unused function on X86. Otherwise need a test case.");
}

void
X86RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                     int SPAdj, unsigned FIOperandNum,
                                     RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const X86FrameLowering *TFI = getFrameLowering(MF);
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  unsigned BasePtr;

  unsigned Opc = MI.getOpcode();
  bool AfterFPPop = Opc == X86::TAILJMPm64 || Opc == X86::TAILJMPm ||
                    Opc == X86::TCRETURNmi || Opc == X86::TCRETURNmi64;

  if (hasBasePointer(MF))
    BasePtr = (FrameIndex < 0 ? FramePtr : getBaseRegister());
  else if (needsStackRealignment(MF))
    BasePtr = (FrameIndex < 0 ? FramePtr : StackPtr);
  else if (AfterFPPop)
    BasePtr = StackPtr;
  else
    BasePtr = (TFI->hasFP(MF) ? FramePtr : StackPtr);

  // LOCAL_ESCAPE uses a single offset, with no register. It only works in the
  // simple FP case, and doesn't work with stack realignment. On 32-bit, the
  // offset is from the traditional base pointer location.  On 64-bit, the
  // offset is from the SP at the end of the prologue, not the FP location. This
  // matches the behavior of llvm.frameaddress.
  unsigned IgnoredFrameReg;
  if (Opc == TargetOpcode::LOCAL_ESCAPE) {
    MachineOperand &FI = MI.getOperand(FIOperandNum);
    int Offset;
    Offset = TFI->getFrameIndexReference(MF, FrameIndex, IgnoredFrameReg);
    FI.ChangeToImmediate(Offset);
    return;
  }

  // For LEA64_32r when BasePtr is 32-bits (X32) we can use full-size 64-bit
  // register as source operand, semantic is the same and destination is
  // 32-bits. It saves one byte per lea in code since 0x67 prefix is avoided.
  if (Opc == X86::LEA64_32r && X86::GR32RegClass.contains(BasePtr))
    BasePtr = getX86SubSuperRegister(BasePtr, 64);

  // This must be part of a four operand memory reference.  Replace the
  // FrameIndex with base register with EBP.  Add an offset to the offset.
  MI.getOperand(FIOperandNum).ChangeToRegister(BasePtr, false);

  // Now add the frame object offset to the offset from EBP.
  int FIOffset;
  if (AfterFPPop) {
    // Tail call jmp happens after FP is popped.
    const MachineFrameInfo *MFI = MF.getFrameInfo();
    FIOffset = MFI->getObjectOffset(FrameIndex) - TFI->getOffsetOfLocalArea();
  } else
    FIOffset = TFI->getFrameIndexReference(MF, FrameIndex, IgnoredFrameReg);

  if (BasePtr == StackPtr)
    FIOffset += SPAdj;

  // The frame index format for stackmaps and patchpoints is different from the
  // X86 format. It only has a FI and an offset.
  if (Opc == TargetOpcode::STACKMAP || Opc == TargetOpcode::PATCHPOINT) {
    assert(BasePtr == FramePtr && "Expected the FP as base register");
    int64_t Offset = MI.getOperand(FIOperandNum + 1).getImm() + FIOffset;
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return;
  }

  if (MI.getOperand(FIOperandNum+3).isImm()) {
    // Offset is a 32-bit integer.
    int Imm = (int)(MI.getOperand(FIOperandNum + 3).getImm());
    int Offset = FIOffset + Imm;
    assert((!Is64Bit || isInt<32>((long long)FIOffset + Imm)) &&
           "Requesting 64-bit offset in 32-bit immediate!");
    MI.getOperand(FIOperandNum + 3).ChangeToImmediate(Offset);
  } else {
    // Offset is symbolic. This is extremely rare.
    uint64_t Offset = FIOffset +
      (uint64_t)MI.getOperand(FIOperandNum+3).getOffset();
    MI.getOperand(FIOperandNum + 3).setOffset(Offset);
  }
}

unsigned X86RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const X86FrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? FramePtr : StackPtr;
}

unsigned
X86RegisterInfo::getPtrSizedFrameRegister(const MachineFunction &MF) const {
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  unsigned FrameReg = getFrameRegister(MF);
  if (Subtarget.isTarget64BitILP32())
    FrameReg = getX86SubSuperRegister(FrameReg, 32);
  return FrameReg;
}

unsigned llvm::get512BitSuperRegister(unsigned Reg) {
  if (Reg >= X86::XMM0 && Reg <= X86::XMM31)
    return X86::ZMM0 + (Reg - X86::XMM0);
  if (Reg >= X86::YMM0 && Reg <= X86::YMM31)
    return X86::ZMM0 + (Reg - X86::YMM0);
  if (Reg >= X86::ZMM0 && Reg <= X86::ZMM31)
    return Reg;
  llvm_unreachable("Unexpected SIMD register");
}
