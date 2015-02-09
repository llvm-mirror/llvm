//===-- PPCFrameLowering.cpp - PPC Frame Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PPC implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "PPCFrameLowering.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

/// VRRegNo - Map from a numbered VR register to its enum value.
///
static const uint16_t VRRegNo[] = {
 PPC::V0 , PPC::V1 , PPC::V2 , PPC::V3 , PPC::V4 , PPC::V5 , PPC::V6 , PPC::V7 ,
 PPC::V8 , PPC::V9 , PPC::V10, PPC::V11, PPC::V12, PPC::V13, PPC::V14, PPC::V15,
 PPC::V16, PPC::V17, PPC::V18, PPC::V19, PPC::V20, PPC::V21, PPC::V22, PPC::V23,
 PPC::V24, PPC::V25, PPC::V26, PPC::V27, PPC::V28, PPC::V29, PPC::V30, PPC::V31
};

PPCFrameLowering::PPCFrameLowering(const PPCSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                          (STI.hasQPX() || STI.isBGQ()) ? 32 : 16, 0),
      Subtarget(STI) {}

// With the SVR4 ABI, callee-saved registers have fixed offsets on the stack.
const PPCFrameLowering::SpillSlot *PPCFrameLowering::getCalleeSavedSpillSlots(
    unsigned &NumEntries) const {
  if (Subtarget.isDarwinABI()) {
    NumEntries = 1;
    if (Subtarget.isPPC64()) {
      static const SpillSlot darwin64Offsets = {PPC::X31, -8};
      return &darwin64Offsets;
    } else {
      static const SpillSlot darwinOffsets = {PPC::R31, -4};
      return &darwinOffsets;
    }
  }

  // Early exit if not using the SVR4 ABI.
  if (!Subtarget.isSVR4ABI()) {
    NumEntries = 0;
    return nullptr;
  }

  // Note that the offsets here overlap, but this is fixed up in
  // processFunctionBeforeFrameFinalized.

  static const SpillSlot Offsets[] = {
      // Floating-point register save area offsets.
      {PPC::F31, -8},
      {PPC::F30, -16},
      {PPC::F29, -24},
      {PPC::F28, -32},
      {PPC::F27, -40},
      {PPC::F26, -48},
      {PPC::F25, -56},
      {PPC::F24, -64},
      {PPC::F23, -72},
      {PPC::F22, -80},
      {PPC::F21, -88},
      {PPC::F20, -96},
      {PPC::F19, -104},
      {PPC::F18, -112},
      {PPC::F17, -120},
      {PPC::F16, -128},
      {PPC::F15, -136},
      {PPC::F14, -144},

      // General register save area offsets.
      {PPC::R31, -4},
      {PPC::R30, -8},
      {PPC::R29, -12},
      {PPC::R28, -16},
      {PPC::R27, -20},
      {PPC::R26, -24},
      {PPC::R25, -28},
      {PPC::R24, -32},
      {PPC::R23, -36},
      {PPC::R22, -40},
      {PPC::R21, -44},
      {PPC::R20, -48},
      {PPC::R19, -52},
      {PPC::R18, -56},
      {PPC::R17, -60},
      {PPC::R16, -64},
      {PPC::R15, -68},
      {PPC::R14, -72},

      // CR save area offset.  We map each of the nonvolatile CR fields
      // to the slot for CR2, which is the first of the nonvolatile CR
      // fields to be assigned, so that we only allocate one save slot.
      // See PPCRegisterInfo::hasReservedSpillSlot() for more information.
      {PPC::CR2, -4},

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},

      // Vector register save area
      {PPC::V31, -16},
      {PPC::V30, -32},
      {PPC::V29, -48},
      {PPC::V28, -64},
      {PPC::V27, -80},
      {PPC::V26, -96},
      {PPC::V25, -112},
      {PPC::V24, -128},
      {PPC::V23, -144},
      {PPC::V22, -160},
      {PPC::V21, -176},
      {PPC::V20, -192}};

  static const SpillSlot Offsets64[] = {
      // Floating-point register save area offsets.
      {PPC::F31, -8},
      {PPC::F30, -16},
      {PPC::F29, -24},
      {PPC::F28, -32},
      {PPC::F27, -40},
      {PPC::F26, -48},
      {PPC::F25, -56},
      {PPC::F24, -64},
      {PPC::F23, -72},
      {PPC::F22, -80},
      {PPC::F21, -88},
      {PPC::F20, -96},
      {PPC::F19, -104},
      {PPC::F18, -112},
      {PPC::F17, -120},
      {PPC::F16, -128},
      {PPC::F15, -136},
      {PPC::F14, -144},

      // General register save area offsets.
      {PPC::X31, -8},
      {PPC::X30, -16},
      {PPC::X29, -24},
      {PPC::X28, -32},
      {PPC::X27, -40},
      {PPC::X26, -48},
      {PPC::X25, -56},
      {PPC::X24, -64},
      {PPC::X23, -72},
      {PPC::X22, -80},
      {PPC::X21, -88},
      {PPC::X20, -96},
      {PPC::X19, -104},
      {PPC::X18, -112},
      {PPC::X17, -120},
      {PPC::X16, -128},
      {PPC::X15, -136},
      {PPC::X14, -144},

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},

      // Vector register save area
      {PPC::V31, -16},
      {PPC::V30, -32},
      {PPC::V29, -48},
      {PPC::V28, -64},
      {PPC::V27, -80},
      {PPC::V26, -96},
      {PPC::V25, -112},
      {PPC::V24, -128},
      {PPC::V23, -144},
      {PPC::V22, -160},
      {PPC::V21, -176},
      {PPC::V20, -192}};

  if (Subtarget.isPPC64()) {
    NumEntries = array_lengthof(Offsets64);

    return Offsets64;
  } else {
    NumEntries = array_lengthof(Offsets);

    return Offsets;
  }
}

/// RemoveVRSaveCode - We have found that this function does not need any code
/// to manipulate the VRSAVE register, even though it uses vector registers.
/// This can happen when the only registers used are known to be live in or out
/// of the function.  Remove all of the VRSAVE related code from the function.
/// FIXME: The removal of the code results in a compile failure at -O0 when the
/// function contains a function call, as the GPR containing original VRSAVE
/// contents is spilled and reloaded around the call.  Without the prolog code,
/// the spill instruction refers to an undefined register.  This code needs
/// to account for all uses of that GPR.
static void RemoveVRSaveCode(MachineInstr *MI) {
  MachineBasicBlock *Entry = MI->getParent();
  MachineFunction *MF = Entry->getParent();

  // We know that the MTVRSAVE instruction immediately follows MI.  Remove it.
  MachineBasicBlock::iterator MBBI = MI;
  ++MBBI;
  assert(MBBI != Entry->end() && MBBI->getOpcode() == PPC::MTVRSAVE);
  MBBI->eraseFromParent();

  bool RemovedAllMTVRSAVEs = true;
  // See if we can find and remove the MTVRSAVE instruction from all of the
  // epilog blocks.
  for (MachineFunction::iterator I = MF->begin(), E = MF->end(); I != E; ++I) {
    // If last instruction is a return instruction, add an epilogue
    if (!I->empty() && I->back().isReturn()) {
      bool FoundIt = false;
      for (MBBI = I->end(); MBBI != I->begin(); ) {
        --MBBI;
        if (MBBI->getOpcode() == PPC::MTVRSAVE) {
          MBBI->eraseFromParent();  // remove it.
          FoundIt = true;
          break;
        }
      }
      RemovedAllMTVRSAVEs &= FoundIt;
    }
  }

  // If we found and removed all MTVRSAVE instructions, remove the read of
  // VRSAVE as well.
  if (RemovedAllMTVRSAVEs) {
    MBBI = MI;
    assert(MBBI != Entry->begin() && "UPDATE_VRSAVE is first instr in block?");
    --MBBI;
    assert(MBBI->getOpcode() == PPC::MFVRSAVE && "VRSAVE instrs wandered?");
    MBBI->eraseFromParent();
  }

  // Finally, nuke the UPDATE_VRSAVE.
  MI->eraseFromParent();
}

// HandleVRSaveUpdate - MI is the UPDATE_VRSAVE instruction introduced by the
// instruction selector.  Based on the vector registers that have been used,
// transform this into the appropriate ORI instruction.
static void HandleVRSaveUpdate(MachineInstr *MI, const TargetInstrInfo &TII) {
  MachineFunction *MF = MI->getParent()->getParent();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  DebugLoc dl = MI->getDebugLoc();

  unsigned UsedRegMask = 0;
  for (unsigned i = 0; i != 32; ++i)
    if (MF->getRegInfo().isPhysRegUsed(VRRegNo[i]))
      UsedRegMask |= 1 << (31-i);

  // Live in and live out values already must be in the mask, so don't bother
  // marking them.
  for (MachineRegisterInfo::livein_iterator
       I = MF->getRegInfo().livein_begin(),
       E = MF->getRegInfo().livein_end(); I != E; ++I) {
    unsigned RegNo = TRI->getEncodingValue(I->first);
    if (VRRegNo[RegNo] == I->first)        // If this really is a vector reg.
      UsedRegMask &= ~(1 << (31-RegNo));   // Doesn't need to be marked.
  }

  // Live out registers appear as use operands on return instructions.
  for (MachineFunction::const_iterator BI = MF->begin(), BE = MF->end();
       UsedRegMask != 0 && BI != BE; ++BI) {
    const MachineBasicBlock &MBB = *BI;
    if (MBB.empty() || !MBB.back().isReturn())
      continue;
    const MachineInstr &Ret = MBB.back();
    for (unsigned I = 0, E = Ret.getNumOperands(); I != E; ++I) {
      const MachineOperand &MO = Ret.getOperand(I);
      if (!MO.isReg() || !PPC::VRRCRegClass.contains(MO.getReg()))
        continue;
      unsigned RegNo = TRI->getEncodingValue(MO.getReg());
      UsedRegMask &= ~(1 << (31-RegNo));
    }
  }

  // If no registers are used, turn this into a copy.
  if (UsedRegMask == 0) {
    // Remove all VRSAVE code.
    RemoveVRSaveCode(MI);
    return;
  }

  unsigned SrcReg = MI->getOperand(1).getReg();
  unsigned DstReg = MI->getOperand(0).getReg();

  if ((UsedRegMask & 0xFFFF) == UsedRegMask) {
    if (DstReg != SrcReg)
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
        .addReg(SrcReg)
        .addImm(UsedRegMask);
    else
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
        .addReg(SrcReg, RegState::Kill)
        .addImm(UsedRegMask);
  } else if ((UsedRegMask & 0xFFFF0000) == UsedRegMask) {
    if (DstReg != SrcReg)
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
        .addReg(SrcReg)
        .addImm(UsedRegMask >> 16);
    else
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
        .addReg(SrcReg, RegState::Kill)
        .addImm(UsedRegMask >> 16);
  } else {
    if (DstReg != SrcReg)
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
        .addReg(SrcReg)
        .addImm(UsedRegMask >> 16);
    else
      BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
        .addReg(SrcReg, RegState::Kill)
        .addImm(UsedRegMask >> 16);

    BuildMI(*MI->getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
      .addReg(DstReg, RegState::Kill)
      .addImm(UsedRegMask & 0xFFFF);
  }

  // Remove the old UPDATE_VRSAVE instruction.
  MI->eraseFromParent();
}

static bool spillsCR(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->isCRSpilled();
}

static bool spillsVRSAVE(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->isVRSAVESpilled();
}

static bool hasSpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasSpills();
}

static bool hasNonRISpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasNonRISpills();
}

/// determineFrameLayout - Determine the size of the frame and maximum call
/// frame size.
unsigned PPCFrameLowering::determineFrameLayout(MachineFunction &MF,
                                                bool UpdateMF,
                                                bool UseEstimate) const {
  MachineFrameInfo *MFI = MF.getFrameInfo();

  // Get the number of bytes to allocate from the FrameInfo
  unsigned FrameSize =
    UseEstimate ? MFI->estimateStackSize(MF) : MFI->getStackSize();

  // Get stack alignments. The frame must be aligned to the greatest of these:
  unsigned TargetAlign = getStackAlignment(); // alignment required per the ABI
  unsigned MaxAlign = MFI->getMaxAlignment(); // algmt required by data in frame
  unsigned AlignMask = std::max(MaxAlign, TargetAlign) - 1;

  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());

  // If we are a leaf function, and use up to 224 bytes of stack space,
  // don't have a frame pointer, calls, or dynamic alloca then we do not need
  // to adjust the stack pointer (we fit in the Red Zone).
  // The 32-bit SVR4 ABI has no Red Zone. However, it can still generate
  // stackless code if all local vars are reg-allocated.
  bool DisableRedZone = MF.getFunction()->getAttributes().
    hasAttribute(AttributeSet::FunctionIndex, Attribute::NoRedZone);
  if (!DisableRedZone &&
      (Subtarget.isPPC64() ||                      // 32-bit SVR4, no stack-
       !Subtarget.isSVR4ABI() ||                   //   allocated locals.
        FrameSize == 0) &&
      FrameSize <= 224 &&                          // Fits in red zone.
      !MFI->hasVarSizedObjects() &&                // No dynamic alloca.
      !MFI->adjustsStack() &&                      // No calls.
      !RegInfo->hasBasePointer(MF)) { // No special alignment.
    // No need for frame
    if (UpdateMF)
      MFI->setStackSize(0);
    return 0;
  }

  // Get the maximum call frame size of all the calls.
  unsigned maxCallFrameSize = MFI->getMaxCallFrameSize();

  // Maximum call frame needs to be at least big enough for linkage area.
  unsigned minCallFrameSize = getLinkageSize(Subtarget.isPPC64(),
                                             Subtarget.isDarwinABI(),
                                             Subtarget.isELFv2ABI());
  maxCallFrameSize = std::max(maxCallFrameSize, minCallFrameSize);

  // If we have dynamic alloca then maxCallFrameSize needs to be aligned so
  // that allocations will be aligned.
  if (MFI->hasVarSizedObjects())
    maxCallFrameSize = (maxCallFrameSize + AlignMask) & ~AlignMask;

  // Update maximum call frame size.
  if (UpdateMF)
    MFI->setMaxCallFrameSize(maxCallFrameSize);

  // Include call frame size in total.
  FrameSize += maxCallFrameSize;

  // Make sure the frame is aligned.
  FrameSize = (FrameSize + AlignMask) & ~AlignMask;

  // Update frame info.
  if (UpdateMF)
    MFI->setStackSize(FrameSize);

  return FrameSize;
}

// hasFP - Return true if the specified function actually has a dedicated frame
// pointer register.
bool PPCFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  // FIXME: This is pretty much broken by design: hasFP() might be called really
  // early, before the stack layout was calculated and thus hasFP() might return
  // true or false here depending on the time of call.
  return (MFI->getStackSize()) && needsFP(MF);
}

// needsFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
bool PPCFrameLowering::needsFP(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();

  // Naked functions have no stack frame pushed, so we don't have a frame
  // pointer.
  if (MF.getFunction()->getAttributes().hasAttribute(
          AttributeSet::FunctionIndex, Attribute::Naked))
    return false;

  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
    MFI->hasVarSizedObjects() ||
    MFI->hasStackMap() || MFI->hasPatchPoint() ||
    (MF.getTarget().Options.GuaranteedTailCallOpt &&
     MF.getInfo<PPCFunctionInfo>()->hasFastCall());
}

void PPCFrameLowering::replaceFPWithRealFP(MachineFunction &MF) const {
  bool is31 = needsFP(MF);
  unsigned FPReg  = is31 ? PPC::R31 : PPC::R1;
  unsigned FP8Reg = is31 ? PPC::X31 : PPC::X1;

  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());
  bool HasBP = RegInfo->hasBasePointer(MF);
  unsigned BPReg  = HasBP ? (unsigned) RegInfo->getBaseRegister(MF) : FPReg;
  unsigned BP8Reg = HasBP ? (unsigned) PPC::X30 : FPReg;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
       BI != BE; ++BI)
    for (MachineBasicBlock::iterator MBBI = BI->end(); MBBI != BI->begin(); ) {
      --MBBI;
      for (unsigned I = 0, E = MBBI->getNumOperands(); I != E; ++I) {
        MachineOperand &MO = MBBI->getOperand(I);
        if (!MO.isReg())
          continue;

        switch (MO.getReg()) {
        case PPC::FP:
          MO.setReg(FPReg);
          break;
        case PPC::FP8:
          MO.setReg(FP8Reg);
          break;
        case PPC::BP:
          MO.setReg(BPReg);
          break;
        case PPC::BP8:
          MO.setReg(BP8Reg);
          break;

        }
      }
    }
}

void PPCFrameLowering::emitPrologue(MachineFunction &MF) const {
  MachineBasicBlock &MBB = MF.front();   // Prolog goes in entry BB
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const PPCInstrInfo &TII =
      *static_cast<const PPCInstrInfo *>(Subtarget.getInstrInfo());
  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());

  MachineModuleInfo &MMI = MF.getMMI();
  const MCRegisterInfo *MRI = MMI.getContext().getRegisterInfo();
  DebugLoc dl;
  bool needsCFI = MMI.hasDebugInfo() ||
    MF.getFunction()->needsUnwindTableEntry();
  bool isPIC = MF.getTarget().getRelocationModel() == Reloc::PIC_;

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();
  // Get the ABI.
  bool isDarwinABI = Subtarget.isDarwinABI();
  bool isSVR4ABI = Subtarget.isSVR4ABI();
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  assert((isDarwinABI || isSVR4ABI) &&
         "Currently only Darwin and SVR4 ABIs are supported for PowerPC.");

  // Scan the prolog, looking for an UPDATE_VRSAVE instruction.  If we find it,
  // process it.
  if (!isSVR4ABI)
    for (unsigned i = 0; MBBI != MBB.end(); ++i, ++MBBI) {
      if (MBBI->getOpcode() == PPC::UPDATE_VRSAVE) {
        HandleVRSaveUpdate(MBBI, TII);
        break;
      }
    }

  // Move MBBI back to the beginning of the function.
  MBBI = MBB.begin();

  // Work out frame sizes.
  unsigned FrameSize = determineFrameLayout(MF);
  int NegFrameSize = -FrameSize;
  if (!isInt<32>(NegFrameSize))
    llvm_unreachable("Unhandled stack size!");

  if (MFI->isFrameAddressTaken())
    replaceFPWithRealFP(MF);

  // Check if the link register (LR) must be saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  const SmallVectorImpl<unsigned> &MustSaveCRs = FI->getMustSaveCRs();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);

  unsigned SPReg       = isPPC64 ? PPC::X1  : PPC::R1;
  unsigned BPReg       = RegInfo->getBaseRegister(MF);
  unsigned FPReg       = isPPC64 ? PPC::X31 : PPC::R31;
  unsigned LRReg       = isPPC64 ? PPC::LR8 : PPC::LR;
  unsigned ScratchReg  = isPPC64 ? PPC::X0  : PPC::R0;
  unsigned TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  //  ...(R12/X12 is volatile in both Darwin & SVR4, & can't be a function arg.)
  const MCInstrDesc& MFLRInst = TII.get(isPPC64 ? PPC::MFLR8
                                                : PPC::MFLR );
  const MCInstrDesc& StoreInst = TII.get(isPPC64 ? PPC::STD
                                                 : PPC::STW );
  const MCInstrDesc& StoreUpdtInst = TII.get(isPPC64 ? PPC::STDU
                                                     : PPC::STWU );
  const MCInstrDesc& StoreUpdtIdxInst = TII.get(isPPC64 ? PPC::STDUX
                                                        : PPC::STWUX);
  const MCInstrDesc& LoadImmShiftedInst = TII.get(isPPC64 ? PPC::LIS8
                                                          : PPC::LIS );
  const MCInstrDesc& OrImmInst = TII.get(isPPC64 ? PPC::ORI8
                                                 : PPC::ORI );
  const MCInstrDesc& OrInst = TII.get(isPPC64 ? PPC::OR8
                                              : PPC::OR );
  const MCInstrDesc& SubtractCarryingInst = TII.get(isPPC64 ? PPC::SUBFC8
                                                            : PPC::SUBFC);
  const MCInstrDesc& SubtractImmCarryingInst = TII.get(isPPC64 ? PPC::SUBFIC8
                                                               : PPC::SUBFIC);

  // Regarding this assert: Even though LR is saved in the caller's frame (i.e.,
  // LROffset is positive), that slot is callee-owned. Because PPC32 SVR4 has no
  // Red Zone, an asynchronous event (a form of "callee") could claim a frame &
  // overwrite it, so PPC32 SVR4 must claim at least a minimal frame to save LR.
  assert((isPPC64 || !isSVR4ABI || !(!FrameSize && (MustSaveLR || HasFP))) &&
         "FrameSize must be >0 to save/restore the FP or LR for 32-bit SVR4.");

  int LROffset = PPCFrameLowering::getReturnSaveOffset(isPPC64, isDarwinABI);

  int FPOffset = 0;
  if (HasFP) {
    if (isSVR4ABI) {
      MachineFrameInfo *FFI = MF.getFrameInfo();
      int FPIndex = FI->getFramePointerSaveIndex();
      assert(FPIndex && "No Frame Pointer Save Slot!");
      FPOffset = FFI->getObjectOffset(FPIndex);
    } else {
      FPOffset =
          PPCFrameLowering::getFramePointerSaveOffset(isPPC64, isDarwinABI);
    }
  }

  int BPOffset = 0;
  if (HasBP) {
    if (isSVR4ABI) {
      MachineFrameInfo *FFI = MF.getFrameInfo();
      int BPIndex = FI->getBasePointerSaveIndex();
      assert(BPIndex && "No Base Pointer Save Slot!");
      BPOffset = FFI->getObjectOffset(BPIndex);
    } else {
      BPOffset =
        PPCFrameLowering::getBasePointerSaveOffset(isPPC64,
                                                   isDarwinABI,
                                                   isPIC);
    }
  }

  int PBPOffset = 0;
  if (FI->usesPICBase()) {
    MachineFrameInfo *FFI = MF.getFrameInfo();
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = FFI->getObjectOffset(PBPIndex);
  }

  // Get stack alignments.
  unsigned MaxAlign = MFI->getMaxAlignment();
  if (HasBP && MaxAlign > 1)
    assert(isPowerOf2_32(MaxAlign) && isInt<16>(MaxAlign) &&
           "Invalid alignment!");

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple STDU/STWU/STD/STW immediate offset operand.
  bool isLargeFrame = !isInt<16>(NegFrameSize);

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, MFLRInst, ScratchReg);

  assert((isPPC64 || MustSaveCRs.empty()) &&
         "Prologue CR saving supported only in 64-bit mode");

  if (!MustSaveCRs.empty()) { // will only occur for PPC64
    // FIXME: In the ELFv2 ABI, we are not required to save all CR fields.
    // If only one or two CR fields are clobbered, it could be more
    // efficient to use mfocrf to selectively save just those fields.
    MachineInstrBuilder MIB =
      BuildMI(MBB, MBBI, dl, TII.get(PPC::MFCR8), TempReg);
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      MIB.addReg(MustSaveCRs[i], RegState::ImplicitKill);
  }

  if (HasFP)
    // FIXME: On PPC32 SVR4, we must not spill before claiming the stackframe.
    BuildMI(MBB, MBBI, dl, StoreInst)
      .addReg(FPReg)
      .addImm(FPOffset)
      .addReg(SPReg);

  if (FI->usesPICBase())
    // FIXME: On PPC32 SVR4, we must not spill before claiming the stackframe.
    BuildMI(MBB, MBBI, dl, StoreInst)
      .addReg(PPC::R30)
      .addImm(PBPOffset)
      .addReg(SPReg);

  if (HasBP)
    // FIXME: On PPC32 SVR4, we must not spill before claiming the stackframe.
    BuildMI(MBB, MBBI, dl, StoreInst)
      .addReg(BPReg)
      .addImm(BPOffset)
      .addReg(SPReg);

  if (MustSaveLR)
    // FIXME: On PPC32 SVR4, we must not spill before claiming the stackframe.
    BuildMI(MBB, MBBI, dl, StoreInst)
      .addReg(ScratchReg)
      .addImm(LROffset)
      .addReg(SPReg);

  if (!MustSaveCRs.empty()) // will only occur for PPC64
    BuildMI(MBB, MBBI, dl, TII.get(PPC::STW8))
      .addReg(TempReg, getKillRegState(true))
      .addImm(8)
      .addReg(SPReg);

  // Skip the rest if this is a leaf function & all spills fit in the Red Zone.
  if (!FrameSize) return;

  // Adjust stack pointer: r1 += NegFrameSize.
  // If there is a preferred stack alignment, align R1 now

  if (HasBP) {
    // Save a copy of r1 as the base pointer.
    BuildMI(MBB, MBBI, dl, OrInst, BPReg)
      .addReg(SPReg)
      .addReg(SPReg);
  }

  if (HasBP && MaxAlign > 1) {
    if (isPPC64)
      BuildMI(MBB, MBBI, dl, TII.get(PPC::RLDICL), ScratchReg)
        .addReg(SPReg)
        .addImm(0)
        .addImm(64 - Log2_32(MaxAlign));
    else // PPC32...
      BuildMI(MBB, MBBI, dl, TII.get(PPC::RLWINM), ScratchReg)
        .addReg(SPReg)
        .addImm(0)
        .addImm(32 - Log2_32(MaxAlign))
        .addImm(31);
    if (!isLargeFrame) {
      BuildMI(MBB, MBBI, dl, SubtractImmCarryingInst, ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addImm(NegFrameSize);
    } else {
      BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, TempReg)
        .addImm(NegFrameSize >> 16);
      BuildMI(MBB, MBBI, dl, OrImmInst, TempReg)
        .addReg(TempReg, RegState::Kill)
        .addImm(NegFrameSize & 0xFFFF);
      BuildMI(MBB, MBBI, dl, SubtractCarryingInst, ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addReg(TempReg, RegState::Kill);
    }
    BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
      .addReg(SPReg, RegState::Kill)
      .addReg(SPReg)
      .addReg(ScratchReg);

  } else if (!isLargeFrame) {
    BuildMI(MBB, MBBI, dl, StoreUpdtInst, SPReg)
      .addReg(SPReg)
      .addImm(NegFrameSize)
      .addReg(SPReg);

  } else {
    BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
      .addImm(NegFrameSize >> 16);
    BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
      .addReg(ScratchReg, RegState::Kill)
      .addImm(NegFrameSize & 0xFFFF);
    BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
      .addReg(SPReg, RegState::Kill)
      .addReg(SPReg)
      .addReg(ScratchReg);
  }

  // Add Call Frame Information for the instructions we generated above.
  if (needsCFI) {
    unsigned CFIIndex;

    if (HasBP) {
      // Define CFA in terms of BP. Do this in preference to using FP/SP,
      // because if the stack needed aligning then CFA won't be at a fixed
      // offset from FP/SP.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));
    } else {
      // Adjust the definition of CFA to account for the change in SP.
      assert(NegFrameSize);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createDefCfaOffset(nullptr, NegFrameSize));
    }
    BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    if (HasFP) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, FPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (FI->usesPICBase()) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(PPC::R30, true);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, PBPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (HasBP) {
      // Describe where BP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, BPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (MustSaveLR) {
      // Describe where LR was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(LRReg, true);
      CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, LROffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  // If there is a frame pointer, copy R1 into R31
  if (HasFP) {
    BuildMI(MBB, MBBI, dl, OrInst, FPReg)
      .addReg(SPReg)
      .addReg(SPReg);

    if (!HasBP && needsCFI) {
      // Change the definition of CFA from SP+offset to FP+offset, because SP
      // will change at every alloca.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      unsigned CFIIndex = MMI.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));

      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  if (needsCFI) {
    // Describe where callee saved registers were saved, at fixed offsets from
    // CFA.
    const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();
    for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
      unsigned Reg = CSI[I].getReg();
      if (Reg == PPC::LR || Reg == PPC::LR8 || Reg == PPC::RM) continue;

      // This is a bit of a hack: CR2LT, CR2GT, CR2EQ and CR2UN are just
      // subregisters of CR2. We just need to emit a move of CR2.
      if (PPC::CRBITRCRegClass.contains(Reg))
        continue;

      // For SVR4, don't emit a move for the CR spill slot if we haven't
      // spilled CRs.
      if (isSVR4ABI && (PPC::CR2 <= Reg && Reg <= PPC::CR4)
          && MustSaveCRs.empty())
        continue;

      // For 64-bit SVR4 when we have spilled CRs, the spill location
      // is SP+8, not a frame-relative slot.
      if (isSVR4ABI && isPPC64 && (PPC::CR2 <= Reg && Reg <= PPC::CR4)) {
        // In the ELFv1 ABI, only CR2 is noted in CFI and stands in for
        // the whole CR word.  In the ELFv2 ABI, every CR that was
        // actually saved gets its own CFI record.
        unsigned CRReg = isELFv2ABI? Reg : (unsigned) PPC::CR2;
        unsigned CFIIndex = MMI.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(CRReg, true), 8));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
        continue;
      }

      int Offset = MFI->getObjectOffset(CSI[I].getFrameIdx());
      unsigned CFIIndex = MMI.addFrameInst(MCCFIInstruction::createOffset(
          nullptr, MRI->getDwarfRegNum(Reg, true), Offset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }
}

void PPCFrameLowering::emitEpilogue(MachineFunction &MF,
                                MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  assert(MBBI != MBB.end() && "Returning block has no terminator");
  const PPCInstrInfo &TII =
      *static_cast<const PPCInstrInfo *>(Subtarget.getInstrInfo());
  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());

  unsigned RetOpcode = MBBI->getOpcode();
  DebugLoc dl;

  assert((RetOpcode == PPC::BLR ||
          RetOpcode == PPC::BLR8 ||
          RetOpcode == PPC::TCRETURNri ||
          RetOpcode == PPC::TCRETURNdi ||
          RetOpcode == PPC::TCRETURNai ||
          RetOpcode == PPC::TCRETURNri8 ||
          RetOpcode == PPC::TCRETURNdi8 ||
          RetOpcode == PPC::TCRETURNai8) &&
         "Can only insert epilog into returning blocks");

  // Get alignment info so we know how to restore the SP.
  const MachineFrameInfo *MFI = MF.getFrameInfo();

  // Get the number of bytes allocated from the FrameInfo.
  int FrameSize = MFI->getStackSize();

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();
  // Get the ABI.
  bool isDarwinABI = Subtarget.isDarwinABI();
  bool isSVR4ABI = Subtarget.isSVR4ABI();
  bool isPIC = MF.getTarget().getRelocationModel() == Reloc::PIC_;

  // Check if the link register (LR) has been saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  const SmallVectorImpl<unsigned> &MustSaveCRs = FI->getMustSaveCRs();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);

  unsigned SPReg      = isPPC64 ? PPC::X1  : PPC::R1;
  unsigned BPReg      = RegInfo->getBaseRegister(MF);
  unsigned FPReg      = isPPC64 ? PPC::X31 : PPC::R31;
  unsigned ScratchReg  = isPPC64 ? PPC::X0  : PPC::R0;
  unsigned TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  const MCInstrDesc& MTLRInst = TII.get( isPPC64 ? PPC::MTLR8
                                                 : PPC::MTLR );
  const MCInstrDesc& LoadInst = TII.get( isPPC64 ? PPC::LD
                                                 : PPC::LWZ );
  const MCInstrDesc& LoadImmShiftedInst = TII.get( isPPC64 ? PPC::LIS8
                                                           : PPC::LIS );
  const MCInstrDesc& OrImmInst = TII.get( isPPC64 ? PPC::ORI8
                                                  : PPC::ORI );
  const MCInstrDesc& AddImmInst = TII.get( isPPC64 ? PPC::ADDI8
                                                   : PPC::ADDI );
  const MCInstrDesc& AddInst = TII.get( isPPC64 ? PPC::ADD8
                                                : PPC::ADD4 );

  int LROffset = PPCFrameLowering::getReturnSaveOffset(isPPC64, isDarwinABI);

  int FPOffset = 0;
  if (HasFP) {
    if (isSVR4ABI) {
      MachineFrameInfo *FFI = MF.getFrameInfo();
      int FPIndex = FI->getFramePointerSaveIndex();
      assert(FPIndex && "No Frame Pointer Save Slot!");
      FPOffset = FFI->getObjectOffset(FPIndex);
    } else {
      FPOffset =
          PPCFrameLowering::getFramePointerSaveOffset(isPPC64, isDarwinABI);
    }
  }

  int BPOffset = 0;
  if (HasBP) {
    if (isSVR4ABI) {
      MachineFrameInfo *FFI = MF.getFrameInfo();
      int BPIndex = FI->getBasePointerSaveIndex();
      assert(BPIndex && "No Base Pointer Save Slot!");
      BPOffset = FFI->getObjectOffset(BPIndex);
    } else {
      BPOffset =
        PPCFrameLowering::getBasePointerSaveOffset(isPPC64,
                                                   isDarwinABI,
                                                   isPIC);
    }
  }

  int PBPOffset = 0;
  if (FI->usesPICBase()) {
    MachineFrameInfo *FFI = MF.getFrameInfo();
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = FFI->getObjectOffset(PBPIndex);
  }

  bool UsesTCRet =  RetOpcode == PPC::TCRETURNri ||
    RetOpcode == PPC::TCRETURNdi ||
    RetOpcode == PPC::TCRETURNai ||
    RetOpcode == PPC::TCRETURNri8 ||
    RetOpcode == PPC::TCRETURNdi8 ||
    RetOpcode == PPC::TCRETURNai8;

  if (UsesTCRet) {
    int MaxTCRetDelta = FI->getTailCallSPDelta();
    MachineOperand &StackAdjust = MBBI->getOperand(1);
    assert(StackAdjust.isImm() && "Expecting immediate value.");
    // Adjust stack pointer.
    int StackAdj = StackAdjust.getImm();
    int Delta = StackAdj - MaxTCRetDelta;
    assert((Delta >= 0) && "Delta must be positive");
    if (MaxTCRetDelta>0)
      FrameSize += (StackAdj +Delta);
    else
      FrameSize += StackAdj;
  }

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple LD/LWZ immediate offset operand.
  bool isLargeFrame = !isInt<16>(FrameSize);

  if (FrameSize) {
    // In the prologue, the loaded (or persistent) stack pointer value is offset
    // by the STDU/STDUX/STWU/STWUX instruction.  Add this offset back now.

    // If this function contained a fastcc call and GuaranteedTailCallOpt is
    // enabled (=> hasFastCall()==true) the fastcc call might contain a tail
    // call which invalidates the stack pointer value in SP(0). So we use the
    // value of R31 in this case.
    if (FI->hasFastCall()) {
      assert(HasFP && "Expecting a valid frame pointer.");
      if (!isLargeFrame) {
        BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
          .addReg(FPReg).addImm(FrameSize);
      } else {
        BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
          .addImm(FrameSize >> 16);
        BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
          .addReg(ScratchReg, RegState::Kill)
          .addImm(FrameSize & 0xFFFF);
        BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(SPReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
      }
    } else if (!isLargeFrame && !HasBP && !MFI->hasVarSizedObjects()) {
      BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
        .addReg(SPReg)
        .addImm(FrameSize);
    } else {
      BuildMI(MBB, MBBI, dl, LoadInst, SPReg)
        .addImm(0)
        .addReg(SPReg);
    }

  }

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
      .addImm(LROffset)
      .addReg(SPReg);

  assert((isPPC64 || MustSaveCRs.empty()) &&
         "Epilogue CR restoring supported only in 64-bit mode");

  if (!MustSaveCRs.empty()) // will only occur for PPC64
    BuildMI(MBB, MBBI, dl, TII.get(PPC::LWZ8), TempReg)
      .addImm(8)
      .addReg(SPReg);

  if (HasFP)
    BuildMI(MBB, MBBI, dl, LoadInst, FPReg)
      .addImm(FPOffset)
      .addReg(SPReg);

  if (FI->usesPICBase())
    // FIXME: On PPC32 SVR4, we must not spill before claiming the stackframe.
    BuildMI(MBB, MBBI, dl, LoadInst)
      .addReg(PPC::R30)
      .addImm(PBPOffset)
      .addReg(SPReg);

  if (HasBP)
    BuildMI(MBB, MBBI, dl, LoadInst, BPReg)
      .addImm(BPOffset)
      .addReg(SPReg);

  if (!MustSaveCRs.empty()) // will only occur for PPC64
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      BuildMI(MBB, MBBI, dl, TII.get(PPC::MTOCRF8), MustSaveCRs[i])
        .addReg(TempReg, getKillRegState(i == e-1));

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, MTLRInst).addReg(ScratchReg);

  // Callee pop calling convention. Pop parameter/linkage area. Used for tail
  // call optimization
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (RetOpcode == PPC::BLR || RetOpcode == PPC::BLR8) &&
      MF.getFunction()->getCallingConv() == CallingConv::Fast) {
     PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
     unsigned CallerAllocatedAmt = FI->getMinReservedArea();

     if (CallerAllocatedAmt && isInt<16>(CallerAllocatedAmt)) {
       BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
         .addReg(SPReg).addImm(CallerAllocatedAmt);
     } else {
       BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
          .addImm(CallerAllocatedAmt >> 16);
       BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
          .addReg(ScratchReg, RegState::Kill)
          .addImm(CallerAllocatedAmt & 0xFFFF);
       BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(SPReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
     }
  } else if (RetOpcode == PPC::TCRETURNdi) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB)).
      addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
  } else if (RetOpcode == PPC::TCRETURNri) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR));
  } else if (RetOpcode == PPC::TCRETURNai) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA)).addImm(JumpTarget.getImm());
  } else if (RetOpcode == PPC::TCRETURNdi8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB8)).
      addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
  } else if (RetOpcode == PPC::TCRETURNri8) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR8));
  } else if (RetOpcode == PPC::TCRETURNai8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA8)).addImm(JumpTarget.getImm());
  }
}

/// MustSaveLR - Return true if this function requires that we save the LR
/// register onto the stack in the prolog and restore it in the epilog of the
/// function.
static bool MustSaveLR(const MachineFunction &MF, unsigned LR) {
  const PPCFunctionInfo *MFI = MF.getInfo<PPCFunctionInfo>();

  // We need a save/restore of LR if there is any def of LR (which is
  // defined by calls, including the PIC setup sequence), or if there is
  // some use of the LR stack slot (e.g. for builtin_return_address).
  // (LR comes in 32 and 64 bit versions.)
  MachineRegisterInfo::def_iterator RI = MF.getRegInfo().def_begin(LR);
  return RI !=MF.getRegInfo().def_end() || MFI->isLRStoreRequired();
}

void
PPCFrameLowering::processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                                   RegScavenger *) const {
  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());

  //  Save and clear the LR state.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  unsigned LR = RegInfo->getRARegister();
  FI->setMustSaveLR(MustSaveLR(MF, LR));
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MRI.setPhysRegUnused(LR);

  //  Save R31 if necessary
  int FPSI = FI->getFramePointerSaveIndex();
  bool isPPC64 = Subtarget.isPPC64();
  bool isDarwinABI  = Subtarget.isDarwinABI();
  bool isPIC = MF.getTarget().getRelocationModel() == Reloc::PIC_;
  MachineFrameInfo *MFI = MF.getFrameInfo();

  // If the frame pointer save index hasn't been defined yet.
  if (!FPSI && needsFP(MF)) {
    // Find out what the fix offset of the frame pointer save area.
    int FPOffset = getFramePointerSaveOffset(isPPC64, isDarwinABI);
    // Allocate the frame index for frame pointer save area.
    FPSI = MFI->CreateFixedObject(isPPC64? 8 : 4, FPOffset, true);
    // Save the result.
    FI->setFramePointerSaveIndex(FPSI);
  }

  int BPSI = FI->getBasePointerSaveIndex();
  if (!BPSI && RegInfo->hasBasePointer(MF)) {
    int BPOffset = getBasePointerSaveOffset(isPPC64, isDarwinABI, isPIC);
    // Allocate the frame index for the base pointer save area.
    BPSI = MFI->CreateFixedObject(isPPC64? 8 : 4, BPOffset, true);
    // Save the result.
    FI->setBasePointerSaveIndex(BPSI);
  }

  // Reserve stack space for the PIC Base register (R30).
  // Only used in SVR4 32-bit.
  if (FI->usesPICBase()) {
    int PBPSI = FI->getPICBasePointerSaveIndex();
    PBPSI = MFI->CreateFixedObject(4, -8, true);
    FI->setPICBasePointerSaveIndex(PBPSI);
  }

  // Reserve stack space to move the linkage area to in case of a tail call.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = FI->getTailCallSPDelta()) < 0) {
    MFI->CreateFixedObject(-1 * TCSPDelta, TCSPDelta, true);
  }

  // For 32-bit SVR4, allocate the nonvolatile CR spill slot iff the
  // function uses CR 2, 3, or 4.
  if (!isPPC64 && !isDarwinABI &&
      (MRI.isPhysRegUsed(PPC::CR2) ||
       MRI.isPhysRegUsed(PPC::CR3) ||
       MRI.isPhysRegUsed(PPC::CR4))) {
    int FrameIdx = MFI->CreateFixedObject((uint64_t)4, (int64_t)-4, true);
    FI->setCRSpillFrameIndex(FrameIdx);
  }
}

void PPCFrameLowering::processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                                       RegScavenger *RS) const {
  // Early exit if not using the SVR4 ABI.
  if (!Subtarget.isSVR4ABI()) {
    addScavengingSpillSlot(MF, RS);
    return;
  }

  // Get callee saved register information.
  MachineFrameInfo *FFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = FFI->getCalleeSavedInfo();

  // Early exit if no callee saved registers are modified!
  if (CSI.empty() && !needsFP(MF)) {
    addScavengingSpillSlot(MF, RS);
    return;
  }

  unsigned MinGPR = PPC::R31;
  unsigned MinG8R = PPC::X31;
  unsigned MinFPR = PPC::F31;
  unsigned MinVR = PPC::V31;

  bool HasGPSaveArea = false;
  bool HasG8SaveArea = false;
  bool HasFPSaveArea = false;
  bool HasVRSAVESaveArea = false;
  bool HasVRSaveArea = false;

  SmallVector<CalleeSavedInfo, 18> GPRegs;
  SmallVector<CalleeSavedInfo, 18> G8Regs;
  SmallVector<CalleeSavedInfo, 18> FPRegs;
  SmallVector<CalleeSavedInfo, 18> VRegs;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    if (PPC::GPRCRegClass.contains(Reg)) {
      HasGPSaveArea = true;

      GPRegs.push_back(CSI[i]);

      if (Reg < MinGPR) {
        MinGPR = Reg;
      }
    } else if (PPC::G8RCRegClass.contains(Reg)) {
      HasG8SaveArea = true;

      G8Regs.push_back(CSI[i]);

      if (Reg < MinG8R) {
        MinG8R = Reg;
      }
    } else if (PPC::F8RCRegClass.contains(Reg)) {
      HasFPSaveArea = true;

      FPRegs.push_back(CSI[i]);

      if (Reg < MinFPR) {
        MinFPR = Reg;
      }
    } else if (PPC::CRBITRCRegClass.contains(Reg) ||
               PPC::CRRCRegClass.contains(Reg)) {
      ; // do nothing, as we already know whether CRs are spilled
    } else if (PPC::VRSAVERCRegClass.contains(Reg)) {
      HasVRSAVESaveArea = true;
    } else if (PPC::VRRCRegClass.contains(Reg)) {
      HasVRSaveArea = true;

      VRegs.push_back(CSI[i]);

      if (Reg < MinVR) {
        MinVR = Reg;
      }
    } else {
      llvm_unreachable("Unknown RegisterClass!");
    }
  }

  PPCFunctionInfo *PFI = MF.getInfo<PPCFunctionInfo>();
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();

  int64_t LowerBound = 0;

  // Take into account stack space reserved for tail calls.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = PFI->getTailCallSPDelta()) < 0) {
    LowerBound = TCSPDelta;
  }

  // The Floating-point register save area is right below the back chain word
  // of the previous stack frame.
  if (HasFPSaveArea) {
    for (unsigned i = 0, e = FPRegs.size(); i != e; ++i) {
      int FI = FPRegs[i].getFrameIdx();

      FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
    }

    LowerBound -= (31 - TRI->getEncodingValue(MinFPR) + 1) * 8;
  }

  // Check whether the frame pointer register is allocated. If so, make sure it
  // is spilled to the correct offset.
  if (needsFP(MF)) {
    HasGPSaveArea = true;

    int FI = PFI->getFramePointerSaveIndex();
    assert(FI && "No Frame Pointer Save Slot!");

    FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
  }

  if (PFI->usesPICBase()) {
    HasGPSaveArea = true;

    int FI = PFI->getPICBasePointerSaveIndex();
    assert(FI && "No PIC Base Pointer Save Slot!");

    FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
  }

  const PPCRegisterInfo *RegInfo =
      static_cast<const PPCRegisterInfo *>(Subtarget.getRegisterInfo());
  if (RegInfo->hasBasePointer(MF)) {
    HasGPSaveArea = true;

    int FI = PFI->getBasePointerSaveIndex();
    assert(FI && "No Base Pointer Save Slot!");

    FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
  }

  // General register save area starts right below the Floating-point
  // register save area.
  if (HasGPSaveArea || HasG8SaveArea) {
    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = GPRegs.size(); i != e; ++i) {
      int FI = GPRegs[i].getFrameIdx();

      FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
    }

    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = G8Regs.size(); i != e; ++i) {
      int FI = G8Regs[i].getFrameIdx();

      FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
    }

    unsigned MinReg =
      std::min<unsigned>(TRI->getEncodingValue(MinGPR),
                         TRI->getEncodingValue(MinG8R));

    if (Subtarget.isPPC64()) {
      LowerBound -= (31 - MinReg + 1) * 8;
    } else {
      LowerBound -= (31 - MinReg + 1) * 4;
    }
  }

  // For 32-bit only, the CR save area is below the general register
  // save area.  For 64-bit SVR4, the CR save area is addressed relative
  // to the stack pointer and hence does not need an adjustment here.
  // Only CR2 (the first nonvolatile spilled) has an associated frame
  // index so that we have a single uniform save area.
  if (spillsCR(MF) && !(Subtarget.isPPC64() && Subtarget.isSVR4ABI())) {
    // Adjust the frame index of the CR spill slot.
    for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
      unsigned Reg = CSI[i].getReg();

      if ((Subtarget.isSVR4ABI() && Reg == PPC::CR2)
          // Leave Darwin logic as-is.
          || (!Subtarget.isSVR4ABI() &&
              (PPC::CRBITRCRegClass.contains(Reg) ||
               PPC::CRRCRegClass.contains(Reg)))) {
        int FI = CSI[i].getFrameIdx();

        FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
      }
    }

    LowerBound -= 4; // The CR save area is always 4 bytes long.
  }

  if (HasVRSAVESaveArea) {
    // FIXME SVR4: Is it actually possible to have multiple elements in CSI
    //             which have the VRSAVE register class?
    // Adjust the frame index of the VRSAVE spill slot.
    for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
      unsigned Reg = CSI[i].getReg();

      if (PPC::VRSAVERCRegClass.contains(Reg)) {
        int FI = CSI[i].getFrameIdx();

        FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
      }
    }

    LowerBound -= 4; // The VRSAVE save area is always 4 bytes long.
  }

  if (HasVRSaveArea) {
    // Insert alignment padding, we need 16-byte alignment.
    LowerBound = (LowerBound - 15) & ~(15);

    for (unsigned i = 0, e = VRegs.size(); i != e; ++i) {
      int FI = VRegs[i].getFrameIdx();

      FFI->setObjectOffset(FI, LowerBound + FFI->getObjectOffset(FI));
    }
  }

  addScavengingSpillSlot(MF, RS);
}

void
PPCFrameLowering::addScavengingSpillSlot(MachineFunction &MF,
                                         RegScavenger *RS) const {
  // Reserve a slot closest to SP or frame pointer if we have a dynalloc or
  // a large stack, which will require scavenging a register to materialize a
  // large offset.

  // We need to have a scavenger spill slot for spills if the frame size is
  // large. In case there is no free register for large-offset addressing,
  // this slot is used for the necessary emergency spill. Also, we need the
  // slot for dynamic stack allocations.

  // The scavenger might be invoked if the frame offset does not fit into
  // the 16-bit immediate. We don't know the complete frame size here
  // because we've not yet computed callee-saved register spills or the
  // needed alignment padding.
  unsigned StackSize = determineFrameLayout(MF, false, true);
  MachineFrameInfo *MFI = MF.getFrameInfo();
  if (MFI->hasVarSizedObjects() || spillsCR(MF) || spillsVRSAVE(MF) ||
      hasNonRISpills(MF) || (hasSpills(MF) && !isInt<16>(StackSize))) {
    const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
    const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
    const TargetRegisterClass *RC = Subtarget.isPPC64() ? G8RC : GPRC;
    RS->addScavengingFrameIndex(MFI->CreateStackObject(RC->getSize(),
                                                       RC->getAlignment(),
                                                       false));

    // Might we have over-aligned allocas?
    bool HasAlVars = MFI->hasVarSizedObjects() &&
                     MFI->getMaxAlignment() > getStackAlignment();

    // These kinds of spills might need two registers.
    if (spillsCR(MF) || spillsVRSAVE(MF) || HasAlVars)
      RS->addScavengingFrameIndex(MFI->CreateStackObject(RC->getSize(),
                                                         RC->getAlignment(),
                                                         false));

  }
}

bool
PPCFrameLowering::spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI,
                                     const std::vector<CalleeSavedInfo> &CSI,
                                     const TargetRegisterInfo *TRI) const {

  // Currently, this function only handles SVR4 32- and 64-bit ABIs.
  // Return false otherwise to maintain pre-existing behavior.
  if (!Subtarget.isSVR4ABI())
    return false;

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII =
      *static_cast<const PPCInstrInfo *>(Subtarget.getInstrInfo());
  DebugLoc DL;
  bool CRSpilled = false;
  MachineInstrBuilder CRMIB;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    // Only Darwin actually uses the VRSAVE register, but it can still appear
    // here if, for example, @llvm.eh.unwind.init() is used.  If we're not on
    // Darwin, ignore it.
    if (Reg == PPC::VRSAVE && !Subtarget.isDarwinABI())
      continue;

    // CR2 through CR4 are the nonvolatile CR fields.
    bool IsCRField = PPC::CR2 <= Reg && Reg <= PPC::CR4;

    // Add the callee-saved register as live-in; it's killed at the spill.
    MBB.addLiveIn(Reg);

    if (CRSpilled && IsCRField) {
      CRMIB.addReg(Reg, RegState::ImplicitKill);
      continue;
    }

    // Insert the spill to the stack frame.
    if (IsCRField) {
      PPCFunctionInfo *FuncInfo = MF->getInfo<PPCFunctionInfo>();
      if (Subtarget.isPPC64()) {
        // The actual spill will happen at the start of the prologue.
        FuncInfo->addMustSaveCR(Reg);
      } else {
        CRSpilled = true;
        FuncInfo->setSpillsCR();

        // 32-bit:  FP-relative.  Note that we made sure CR2-CR4 all have
        // the same frame index in PPCRegisterInfo::hasReservedSpillSlot.
        CRMIB = BuildMI(*MF, DL, TII.get(PPC::MFCR), PPC::R12)
                  .addReg(Reg, RegState::ImplicitKill);

        MBB.insert(MI, CRMIB);
        MBB.insert(MI, addFrameReference(BuildMI(*MF, DL, TII.get(PPC::STW))
                                         .addReg(PPC::R12,
                                                 getKillRegState(true)),
                                         CSI[i].getFrameIdx()));
      }
    } else {
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.storeRegToStackSlot(MBB, MI, Reg, true,
                              CSI[i].getFrameIdx(), RC, TRI);
    }
  }
  return true;
}

static void
restoreCRs(bool isPPC64, bool is31,
           bool CR2Spilled, bool CR3Spilled, bool CR4Spilled,
           MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
           const std::vector<CalleeSavedInfo> &CSI, unsigned CSIIndex) {

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *MF->getSubtarget<PPCSubtarget>().getInstrInfo();
  DebugLoc DL;
  unsigned RestoreOp, MoveReg;

  if (isPPC64)
    // This is handled during epilogue generation.
    return;
  else {
    // 32-bit:  FP-relative
    MBB.insert(MI, addFrameReference(BuildMI(*MF, DL, TII.get(PPC::LWZ),
                                             PPC::R12),
                                     CSI[CSIIndex].getFrameIdx()));
    RestoreOp = PPC::MTOCRF;
    MoveReg = PPC::R12;
  }

  if (CR2Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR2)
               .addReg(MoveReg, getKillRegState(!CR3Spilled && !CR4Spilled)));

  if (CR3Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR3)
               .addReg(MoveReg, getKillRegState(!CR4Spilled)));

  if (CR4Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR4)
               .addReg(MoveReg, getKillRegState(true)));
}

void PPCFrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      I->getOpcode() == PPC::ADJCALLSTACKUP) {
    // Add (actually subtract) back the amount the callee popped on return.
    if (int CalleeAmt =  I->getOperand(1).getImm()) {
      bool is64Bit = Subtarget.isPPC64();
      CalleeAmt *= -1;
      unsigned StackReg = is64Bit ? PPC::X1 : PPC::R1;
      unsigned TmpReg = is64Bit ? PPC::X0 : PPC::R0;
      unsigned ADDIInstr = is64Bit ? PPC::ADDI8 : PPC::ADDI;
      unsigned ADDInstr = is64Bit ? PPC::ADD8 : PPC::ADD4;
      unsigned LISInstr = is64Bit ? PPC::LIS8 : PPC::LIS;
      unsigned ORIInstr = is64Bit ? PPC::ORI8 : PPC::ORI;
      MachineInstr *MI = I;
      DebugLoc dl = MI->getDebugLoc();

      if (isInt<16>(CalleeAmt)) {
        BuildMI(MBB, I, dl, TII.get(ADDIInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addImm(CalleeAmt);
      } else {
        MachineBasicBlock::iterator MBBI = I;
        BuildMI(MBB, MBBI, dl, TII.get(LISInstr), TmpReg)
          .addImm(CalleeAmt >> 16);
        BuildMI(MBB, MBBI, dl, TII.get(ORIInstr), TmpReg)
          .addReg(TmpReg, RegState::Kill)
          .addImm(CalleeAmt & 0xFFFF);
        BuildMI(MBB, MBBI, dl, TII.get(ADDInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addReg(TmpReg);
      }
    }
  }
  // Simply discard ADJCALLSTACKDOWN, ADJCALLSTACKUP instructions.
  MBB.erase(I);
}

bool
PPCFrameLowering::restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        const std::vector<CalleeSavedInfo> &CSI,
                                        const TargetRegisterInfo *TRI) const {

  // Currently, this function only handles SVR4 32- and 64-bit ABIs.
  // Return false otherwise to maintain pre-existing behavior.
  if (!Subtarget.isSVR4ABI())
    return false;

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII =
      *static_cast<const PPCInstrInfo *>(Subtarget.getInstrInfo());
  bool CR2Spilled = false;
  bool CR3Spilled = false;
  bool CR4Spilled = false;
  unsigned CSIIndex = 0;

  // Initialize insertion-point logic; we will be restoring in reverse
  // order of spill.
  MachineBasicBlock::iterator I = MI, BeforeI = I;
  bool AtStart = I == MBB.begin();

  if (!AtStart)
    --BeforeI;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();

    // Only Darwin actually uses the VRSAVE register, but it can still appear
    // here if, for example, @llvm.eh.unwind.init() is used.  If we're not on
    // Darwin, ignore it.
    if (Reg == PPC::VRSAVE && !Subtarget.isDarwinABI())
      continue;

    if (Reg == PPC::CR2) {
      CR2Spilled = true;
      // The spill slot is associated only with CR2, which is the
      // first nonvolatile spilled.  Save it here.
      CSIIndex = i;
      continue;
    } else if (Reg == PPC::CR3) {
      CR3Spilled = true;
      continue;
    } else if (Reg == PPC::CR4) {
      CR4Spilled = true;
      continue;
    } else {
      // When we first encounter a non-CR register after seeing at
      // least one CR register, restore all spilled CRs together.
      if ((CR2Spilled || CR3Spilled || CR4Spilled)
          && !(PPC::CR2 <= Reg && Reg <= PPC::CR4)) {
        bool is31 = needsFP(*MF);
        restoreCRs(Subtarget.isPPC64(), is31,
                   CR2Spilled, CR3Spilled, CR4Spilled,
                   MBB, I, CSI, CSIIndex);
        CR2Spilled = CR3Spilled = CR4Spilled = false;
      }

      // Default behavior for non-CR saves.
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.loadRegFromStackSlot(MBB, I, Reg, CSI[i].getFrameIdx(),
                               RC, TRI);
      assert(I != MBB.begin() &&
             "loadRegFromStackSlot didn't insert any code!");
      }

    // Insert in reverse order.
    if (AtStart)
      I = MBB.begin();
    else {
      I = BeforeI;
      ++I;
    }
  }

  // If we haven't yet spilled the CRs, do so now.
  if (CR2Spilled || CR3Spilled || CR4Spilled) {
    bool is31 = needsFP(*MF);
    restoreCRs(Subtarget.isPPC64(), is31, CR2Spilled, CR3Spilled, CR4Spilled,
               MBB, I, CSI, CSIIndex);
  }

  return true;
}
