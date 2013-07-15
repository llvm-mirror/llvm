//===-- XCoreFrameLowering.cpp - Frame info for XCore Target --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains XCore frame information that doesn't fit anywhere else
// cleanly...
//
//===----------------------------------------------------------------------===//

#include "XCoreFrameLowering.h"
#include "XCore.h"
#include "XCoreInstrInfo.h"
#include "XCoreMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

// helper functions. FIXME: Eliminate.
static inline bool isImmUs(unsigned val) {
  return val <= 11;
}

static inline bool isImmU6(unsigned val) {
  return val < (1 << 6);
}

static inline bool isImmU16(unsigned val) {
  return val < (1 << 16);
}

static void loadFromStack(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator I,
                          unsigned DstReg, int Offset, DebugLoc dl,
                          const TargetInstrInfo &TII) {
  assert(Offset%4 == 0 && "Misaligned stack offset");
  Offset/=4;
  bool isU6 = isImmU6(Offset);
  if (!isU6 && !isImmU16(Offset))
    report_fatal_error("loadFromStack offset too big " + Twine(Offset));
  int Opcode = isU6 ? XCore::LDWSP_ru6 : XCore::LDWSP_lru6;
  BuildMI(MBB, I, dl, TII.get(Opcode), DstReg)
    .addImm(Offset);
}


static void storeToStack(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator I,
                         unsigned SrcReg, int Offset, DebugLoc dl,
                         const TargetInstrInfo &TII) {
  assert(Offset%4 == 0 && "Misaligned stack offset");
  Offset/=4;
  bool isU6 = isImmU6(Offset);
  if (!isU6 && !isImmU16(Offset))
    report_fatal_error("storeToStack offset too big " + Twine(Offset));
  int Opcode = isU6 ? XCore::STWSP_ru6 : XCore::STWSP_lru6;
  BuildMI(MBB, I, dl, TII.get(Opcode))
    .addReg(SrcReg)
    .addImm(Offset);
}


//===----------------------------------------------------------------------===//
// XCoreFrameLowering:
//===----------------------------------------------------------------------===//

XCoreFrameLowering::XCoreFrameLowering(const XCoreSubtarget &sti)
  : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 4, 0) {
  // Do nothing
}

bool XCoreFrameLowering::hasFP(const MachineFunction &MF) const {
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
    MF.getFrameInfo()->hasVarSizedObjects();
}

void XCoreFrameLowering::emitPrologue(MachineFunction &MF) const {
  MachineBasicBlock &MBB = MF.front();   // Prolog goes in entry BB
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  MachineModuleInfo *MMI = &MF.getMMI();
  const XCoreInstrInfo &TII =
    *static_cast<const XCoreInstrInfo*>(MF.getTarget().getInstrInfo());
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  bool FP = hasFP(MF);
  const AttributeSet &PAL = MF.getFunction()->getAttributes();

  if (PAL.hasAttrSomewhere(Attribute::Nest))
    loadFromStack(MBB, MBBI, XCore::R11, 0, dl, TII);

  // Work out frame sizes.
  int FrameSize = MFI->getStackSize();
  assert(FrameSize%4 == 0 && "Misaligned frame size");
  FrameSize/=4;

  bool isU6 = isImmU6(FrameSize);

  if (!isU6 && !isImmU16(FrameSize)) {
    // FIXME could emit multiple instructions.
    report_fatal_error("emitPrologue Frame size too big: " + Twine(FrameSize));
  }
  bool emitFrameMoves = XCoreRegisterInfo::needsFrameMoves(MF);

  bool saveLR = XFI->getUsesLR();
  // Do we need to allocate space on the stack?
  if (FrameSize) {
    int Opcode;
    if (saveLR && (MFI->getObjectOffset(XFI->getLRSpillSlot()) == 0)) {
      Opcode = (isU6) ? XCore::ENTSP_u6 : XCore::ENTSP_lu6;
      MBB.addLiveIn(XCore::LR);
      saveLR = false;
    } else {
      Opcode = (isU6) ? XCore::EXTSP_u6 : XCore::EXTSP_lu6;
    }
    BuildMI(MBB, MBBI, dl, TII.get(Opcode)).addImm(FrameSize);

    if (emitFrameMoves) {

      // Show update of SP.
      MCSymbol *FrameLabel = MMI->getContext().CreateTempSymbol();
      BuildMI(MBB, MBBI, dl, TII.get(XCore::PROLOG_LABEL)).addSym(FrameLabel);
    }
  }
  if (saveLR) {
    int LRSpillOffset = MFI->getObjectOffset(XFI->getLRSpillSlot());
    storeToStack(MBB, MBBI, XCore::LR, LRSpillOffset + FrameSize*4, dl, TII);
    MBB.addLiveIn(XCore::LR);

    if (emitFrameMoves) {
      MCSymbol *SaveLRLabel = MMI->getContext().CreateTempSymbol();
      BuildMI(MBB, MBBI, dl, TII.get(XCore::PROLOG_LABEL)).addSym(SaveLRLabel);
    }
  }

  if (FP) {
    // Save R10 to the stack.
    int FPSpillOffset = MFI->getObjectOffset(XFI->getFPSpillSlot());
    storeToStack(MBB, MBBI, XCore::R10, FPSpillOffset + FrameSize*4, dl, TII);
    // R10 is live-in. It is killed at the spill.
    MBB.addLiveIn(XCore::R10);
    if (emitFrameMoves) {
      MCSymbol *SaveR10Label = MMI->getContext().CreateTempSymbol();
      BuildMI(MBB, MBBI, dl, TII.get(XCore::PROLOG_LABEL)).addSym(SaveR10Label);
    }
    // Set the FP from the SP.
    unsigned FramePtr = XCore::R10;
    BuildMI(MBB, MBBI, dl, TII.get(XCore::LDAWSP_ru6), FramePtr)
      .addImm(0);
    if (emitFrameMoves) {
      // Show FP is now valid.
      MCSymbol *FrameLabel = MMI->getContext().CreateTempSymbol();
      BuildMI(MBB, MBBI, dl, TII.get(XCore::PROLOG_LABEL)).addSym(FrameLabel);
    }
  }
}

void XCoreFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo *MFI            = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  const XCoreInstrInfo &TII =
    *static_cast<const XCoreInstrInfo*>(MF.getTarget().getInstrInfo());
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  DebugLoc dl = MBBI->getDebugLoc();

  bool FP = hasFP(MF);
  if (FP) {
    // Restore the stack pointer.
    unsigned FramePtr = XCore::R10;
    BuildMI(MBB, MBBI, dl, TII.get(XCore::SETSP_1r))
      .addReg(FramePtr);
  }

  // Work out frame sizes.
  int FrameSize = MFI->getStackSize();

  assert(FrameSize%4 == 0 && "Misaligned frame size");

  FrameSize/=4;

  bool isU6 = isImmU6(FrameSize);

  if (!isU6 && !isImmU16(FrameSize)) {
    // FIXME could emit multiple instructions.
    report_fatal_error("emitEpilogue Frame size too big: " + Twine(FrameSize));
  }

  if (FP) {
    // Restore R10
    int FPSpillOffset = MFI->getObjectOffset(XFI->getFPSpillSlot());
    FPSpillOffset += FrameSize*4;
    loadFromStack(MBB, MBBI, XCore::R10, FPSpillOffset, dl, TII);
  }

  bool restoreLR = XFI->getUsesLR();
  if (restoreLR &&
      (FrameSize == 0 || MFI->getObjectOffset(XFI->getLRSpillSlot()) != 0)) {
    int LRSpillOffset = MFI->getObjectOffset(XFI->getLRSpillSlot());
    LRSpillOffset += FrameSize*4;
    loadFromStack(MBB, MBBI, XCore::LR, LRSpillOffset, dl, TII);
    restoreLR = false;
  }

  if (FrameSize) {
    if (restoreLR) {
      // Fold prologue into return instruction
      assert(MFI->getObjectOffset(XFI->getLRSpillSlot()) == 0);
      assert(MBBI->getOpcode() == XCore::RETSP_u6
        || MBBI->getOpcode() == XCore::RETSP_lu6);
      int Opcode = (isU6) ? XCore::RETSP_u6 : XCore::RETSP_lu6;
      BuildMI(MBB, MBBI, dl, TII.get(Opcode)).addImm(FrameSize);
      MBB.erase(MBBI);
    } else {
      int Opcode = (isU6) ? XCore::LDAWSP_ru6 : XCore::LDAWSP_lru6;
      BuildMI(MBB, MBBI, dl, TII.get(Opcode), XCore::SP).addImm(FrameSize);
    }
  }
}

bool XCoreFrameLowering::spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                               MachineBasicBlock::iterator MI,
                                        const std::vector<CalleeSavedInfo> &CSI,
                                          const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getTarget().getInstrInfo();

  XCoreFunctionInfo *XFI = MF->getInfo<XCoreFunctionInfo>();
  bool emitFrameMoves = XCoreRegisterInfo::needsFrameMoves(*MF);

  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  for (std::vector<CalleeSavedInfo>::const_iterator it = CSI.begin();
                                                    it != CSI.end(); ++it) {
    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(it->getReg());

    unsigned Reg = it->getReg();
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, true,
                            it->getFrameIdx(), RC, TRI);
    if (emitFrameMoves) {
      MCSymbol *SaveLabel = MF->getContext().CreateTempSymbol();
      BuildMI(MBB, MI, DL, TII.get(XCore::PROLOG_LABEL)).addSym(SaveLabel);
      XFI->getSpillLabels().push_back(std::make_pair(SaveLabel, *it));
    }
  }
  return true;
}

bool XCoreFrameLowering::restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                                 MachineBasicBlock::iterator MI,
                                        const std::vector<CalleeSavedInfo> &CSI,
                                            const TargetRegisterInfo *TRI) const{
  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getTarget().getInstrInfo();

  bool AtStart = MI == MBB.begin();
  MachineBasicBlock::iterator BeforeI = MI;
  if (!AtStart)
    --BeforeI;
  for (std::vector<CalleeSavedInfo>::const_iterator it = CSI.begin();
                                                    it != CSI.end(); ++it) {
    unsigned Reg = it->getReg();
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.loadRegFromStackSlot(MBB, MI, it->getReg(), it->getFrameIdx(),
                             RC, TRI);
    assert(MI != MBB.begin() &&
           "loadRegFromStackSlot didn't insert any code!");
    // Insert in reverse order.  loadRegFromStackSlot can insert multiple
    // instructions.
    if (AtStart)
      MI = MBB.begin();
    else {
      MI = BeforeI;
      ++MI;
    }
  }
  return true;
}

// This function eliminates ADJCALLSTACKDOWN,
// ADJCALLSTACKUP pseudo instructions
void XCoreFrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const XCoreInstrInfo &TII =
    *static_cast<const XCoreInstrInfo*>(MF.getTarget().getInstrInfo());
  if (!hasReservedCallFrame(MF)) {
    // Turn the adjcallstackdown instruction into 'extsp <amt>' and the
    // adjcallstackup instruction into 'ldaw sp, sp[<amt>]'
    MachineInstr *Old = I;
    uint64_t Amount = Old->getOperand(0).getImm();
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      unsigned Align = getStackAlignment();
      Amount = (Amount+Align-1)/Align*Align;

      assert(Amount%4 == 0);
      Amount /= 4;

      bool isU6 = isImmU6(Amount);
      if (!isU6 && !isImmU16(Amount)) {
        // FIX could emit multiple instructions in this case.
#ifndef NDEBUG
        errs() << "eliminateCallFramePseudoInstr size too big: "
               << Amount << "\n";
#endif
        llvm_unreachable(0);
      }

      MachineInstr *New;
      if (Old->getOpcode() == XCore::ADJCALLSTACKDOWN) {
        int Opcode = isU6 ? XCore::EXTSP_u6 : XCore::EXTSP_lu6;
        New=BuildMI(MF, Old->getDebugLoc(), TII.get(Opcode))
          .addImm(Amount);
      } else {
        assert(Old->getOpcode() == XCore::ADJCALLSTACKUP);
        int Opcode = isU6 ? XCore::LDAWSP_ru6 : XCore::LDAWSP_lru6;
        New=BuildMI(MF, Old->getDebugLoc(), TII.get(Opcode), XCore::SP)
          .addImm(Amount);
      }

      // Replace the pseudo instruction with a new instruction...
      MBB.insert(I, New);
    }
  }
  
  MBB.erase(I);
}

void
XCoreFrameLowering::processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                                     RegScavenger *RS) const {
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RegInfo = MF.getTarget().getRegisterInfo();
  bool LRUsed = MF.getRegInfo().isPhysRegUsed(XCore::LR);
  const TargetRegisterClass *RC = &XCore::GRRegsRegClass;
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  if (LRUsed) {
    MF.getRegInfo().setPhysRegUnused(XCore::LR);

    bool isVarArg = MF.getFunction()->isVarArg();
    int FrameIdx;
    if (! isVarArg) {
      // A fixed offset of 0 allows us to save / restore LR using entsp / retsp.
      FrameIdx = MFI->CreateFixedObject(RC->getSize(), 0, true);
    } else {
      FrameIdx = MFI->CreateStackObject(RC->getSize(), RC->getAlignment(),
                                        false);
    }
    XFI->setUsesLR(FrameIdx);
    XFI->setLRSpillSlot(FrameIdx);
  }
  if (RegInfo->requiresRegisterScavenging(MF)) {
    // Reserve a slot close to SP or frame pointer.
    RS->addScavengingFrameIndex(MFI->CreateStackObject(RC->getSize(),
                                                       RC->getAlignment(),
                                                       false));
  }
  if (hasFP(MF)) {
    // A callee save register is used to hold the FP.
    // This needs saving / restoring in the epilogue / prologue.
    XFI->setFPSpillSlot(MFI->CreateStackObject(RC->getSize(),
                                               RC->getAlignment(),
                                               false));
  }
}
