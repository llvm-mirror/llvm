//===------- X86ExpandPseudo.cpp - Expand pseudo instructions -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling, if-conversion, other late
// optimizations, or simply the encoding of the instructions.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86FrameLowering.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h" // For IDs of passes that are preserved.
#include "llvm/IR/GlobalValue.h"
using namespace llvm;

#define DEBUG_TYPE "x86-pseudo"

namespace {
class X86ExpandPseudo : public MachineFunctionPass {
public:
  static char ID;
  X86ExpandPseudo() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const X86Subtarget *STI;
  const X86InstrInfo *TII;
  const X86RegisterInfo *TRI;
  const X86FrameLowering *X86FL;

  bool runOnMachineFunction(MachineFunction &Fn) override;

  const char *getPassName() const override {
    return "X86 pseudo instruction expansion pass";
  }

private:
  bool ExpandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI);
  bool ExpandMBB(MachineBasicBlock &MBB);
};
char X86ExpandPseudo::ID = 0;
} // End anonymous namespace.

/// If \p MBBI is a pseudo instruction, this method expands
/// it to the corresponding (sequence of) actual instruction(s).
/// \returns true if \p MBBI has been expanded.
bool X86ExpandPseudo::ExpandMI(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  DebugLoc DL = MBBI->getDebugLoc();
  switch (Opcode) {
  default:
    return false;
  case X86::TCRETURNdi:
  case X86::TCRETURNri:
  case X86::TCRETURNmi:
  case X86::TCRETURNdi64:
  case X86::TCRETURNri64:
  case X86::TCRETURNmi64: {
    bool isMem = Opcode == X86::TCRETURNmi || Opcode == X86::TCRETURNmi64;
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    MachineOperand &StackAdjust = MBBI->getOperand(isMem ? 5 : 1);
    assert(StackAdjust.isImm() && "Expecting immediate value.");

    // Adjust stack pointer.
    int StackAdj = StackAdjust.getImm();

    if (StackAdj) {
      // Check for possible merge with preceding ADD instruction.
      StackAdj += X86FL->mergeSPUpdates(MBB, MBBI, true);
      X86FL->emitSPUpdate(MBB, MBBI, StackAdj, /*InEpilogue=*/true);
    }

    // Jump to label or value in register.
    bool IsWin64 = STI->isTargetWin64();
    if (Opcode == X86::TCRETURNdi || Opcode == X86::TCRETURNdi64) {
      unsigned Op = (Opcode == X86::TCRETURNdi)
                        ? X86::TAILJMPd
                        : (IsWin64 ? X86::TAILJMPd64_REX : X86::TAILJMPd64);
      MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(Op));
      if (JumpTarget.isGlobal())
        MIB.addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset(),
                             JumpTarget.getTargetFlags());
      else {
        assert(JumpTarget.isSymbol());
        MIB.addExternalSymbol(JumpTarget.getSymbolName(),
                              JumpTarget.getTargetFlags());
      }
    } else if (Opcode == X86::TCRETURNmi || Opcode == X86::TCRETURNmi64) {
      unsigned Op = (Opcode == X86::TCRETURNmi)
                        ? X86::TAILJMPm
                        : (IsWin64 ? X86::TAILJMPm64_REX : X86::TAILJMPm64);
      MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(Op));
      for (unsigned i = 0; i != 5; ++i)
        MIB.addOperand(MBBI->getOperand(i));
    } else if (Opcode == X86::TCRETURNri64) {
      BuildMI(MBB, MBBI, DL,
              TII->get(IsWin64 ? X86::TAILJMPr64_REX : X86::TAILJMPr64))
          .addReg(JumpTarget.getReg(), RegState::Kill);
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(X86::TAILJMPr))
          .addReg(JumpTarget.getReg(), RegState::Kill);
    }

    MachineInstr *NewMI = std::prev(MBBI);
    NewMI->copyImplicitOps(*MBBI->getParent()->getParent(), MBBI);

    // Delete the pseudo instruction TCRETURN.
    MBB.erase(MBBI);

    return true;
  }
  case X86::EH_RETURN:
  case X86::EH_RETURN64: {
    MachineOperand &DestAddr = MBBI->getOperand(0);
    assert(DestAddr.isReg() && "Offset should be in register!");
    const bool Uses64BitFramePtr =
        STI->isTarget64BitLP64() || STI->isTargetNaCl64();
    unsigned StackPtr = TRI->getStackRegister();
    BuildMI(MBB, MBBI, DL,
            TII->get(Uses64BitFramePtr ? X86::MOV64rr : X86::MOV32rr), StackPtr)
        .addReg(DestAddr.getReg());
    // The EH_RETURN pseudo is really removed during the MC Lowering.
    return true;
  }
  case X86::IRET: {
    // Adjust stack to erase error code
    int64_t StackAdj = MBBI->getOperand(0).getImm();
    X86FL->emitSPUpdate(MBB, MBBI, StackAdj, true);
    // Replace pseudo with machine iret
    BuildMI(MBB, MBBI, DL,
            TII->get(STI->is64Bit() ? X86::IRET64 : X86::IRET32));
    MBB.erase(MBBI);
    return true;
  }
  case X86::EH_RESTORE: {
    // Restore ESP and EBP, and optionally ESI if required.
    bool IsSEH = isAsynchronousEHPersonality(classifyEHPersonality(
        MBB.getParent()->getFunction()->getPersonalityFn()));
    X86FL->restoreWin32EHStackPointers(MBB, MBBI, DL, /*RestoreSP=*/IsSEH);
    MBBI->eraseFromParent();
    return true;
  }
  }
  llvm_unreachable("Previous switch has a fallthrough?");
}

/// Expand all pseudo instructions contained in \p MBB.
/// \returns true if any expansion occurred for \p MBB.
bool X86ExpandPseudo::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  // MBBI may be invalidated by the expansion.
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= ExpandMI(MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool X86ExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  STI = &static_cast<const X86Subtarget &>(MF.getSubtarget());
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  X86FL = STI->getFrameLowering();

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= ExpandMBB(MBB);
  return Modified;
}

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createX86ExpandPseudoPass() {
  return new X86ExpandPseudo();
}
