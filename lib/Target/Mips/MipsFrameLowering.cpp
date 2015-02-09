//===-- MipsFrameLowering.cpp - Mips Frame Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MipsFrameLowering.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "MipsAnalyzeImmediate.h"
#include "MipsInstrInfo.h"
#include "MipsMachineFunction.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;


//===----------------------------------------------------------------------===//
//
// Stack Frame Processing methods
// +----------------------------+
//
// The stack is allocated decrementing the stack pointer on
// the first instruction of a function prologue. Once decremented,
// all stack references are done thought a positive offset
// from the stack/frame pointer, so the stack is considering
// to grow up! Otherwise terrible hacks would have to be made
// to get this stack ABI compliant :)
//
//  The stack frame required by the ABI (after call):
//  Offset
//
//  0                 ----------
//  4                 Args to pass
//  .                 saved $GP  (used in PIC)
//  .                 Alloca allocations
//  .                 Local Area
//  .                 CPU "Callee Saved" Registers
//  .                 saved FP
//  .                 saved RA
//  .                 FPU "Callee Saved" Registers
//  StackSize         -----------
//
// Offset - offset from sp after stack allocation on function prologue
//
// The sp is the stack pointer subtracted/added from the stack size
// at the Prologue/Epilogue
//
// References to the previous stack (to obtain arguments) are done
// with offsets that exceeds the stack size: (stacksize+(4*(num_arg-1))
//
// Examples:
// - reference to the actual stack frame
//   for any local area var there is smt like : FI >= 0, StackOffset: 4
//     sw REGX, 4(SP)
//
// - reference to previous stack frame
//   suppose there's a load to the 5th arguments : FI < 0, StackOffset: 16.
//   The emitted instruction will be something like:
//     lw REGX, 16+StackSize(SP)
//
// Since the total stack size is unknown on LowerFormalArguments, all
// stack references (ObjectOffset) created to reference the function
// arguments, are negative numbers. This way, on eliminateFrameIndex it's
// possible to detect those references and the offsets are adjusted to
// their real location.
//
//===----------------------------------------------------------------------===//

const MipsFrameLowering *MipsFrameLowering::create(const MipsSubtarget &ST) {
  if (ST.inMips16Mode())
    return llvm::createMips16FrameLowering(ST);

  return llvm::createMipsSEFrameLowering(ST);
}

// hasFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
bool MipsFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
      MFI->hasVarSizedObjects() || MFI->isFrameAddressTaken();
}

uint64_t MipsFrameLowering::estimateStackSize(const MachineFunction &MF) const {
  const MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  int64_t Offset = 0;

  // Iterate over fixed sized objects.
  for (int I = MFI->getObjectIndexBegin(); I != 0; ++I)
    Offset = std::max(Offset, -MFI->getObjectOffset(I));

  // Conservatively assume all callee-saved registers will be saved.
  for (const MCPhysReg *R = TRI.getCalleeSavedRegs(&MF); *R; ++R) {
    unsigned Size = TRI.getMinimalPhysRegClass(*R)->getSize();
    Offset = RoundUpToAlignment(Offset + Size, Size);
  }

  unsigned MaxAlign = MFI->getMaxAlignment();

  // Check that MaxAlign is not zero if there is a stack object that is not a
  // callee-saved spill.
  assert(!MFI->getObjectIndexEnd() || MaxAlign);

  // Iterate over other objects.
  for (unsigned I = 0, E = MFI->getObjectIndexEnd(); I != E; ++I)
    Offset = RoundUpToAlignment(Offset + MFI->getObjectSize(I), MaxAlign);

  // Call frame.
  if (MFI->adjustsStack() && hasReservedCallFrame(MF))
    Offset = RoundUpToAlignment(Offset + MFI->getMaxCallFrameSize(),
                                std::max(MaxAlign, getStackAlignment()));

  return RoundUpToAlignment(Offset, getStackAlignment());
}
