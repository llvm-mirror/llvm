//===-- SIShrinkInstructions.cpp - Shrink Instructions --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// The pass tries to use the 32-bit encoding for instructions when possible.
//===----------------------------------------------------------------------===//
//

#include "AMDGPU.h"
#include "AMDGPUMCInstLower.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "si-shrink-instructions"

STATISTIC(NumInstructionsShrunk,
          "Number of 64-bit instruction reduced to 32-bit.");
STATISTIC(NumLiteralConstantsFolded,
          "Number of literal constants folded into 32-bit instructions.");

namespace llvm {
  void initializeSIShrinkInstructionsPass(PassRegistry&);
}

using namespace llvm;

namespace {

class SIShrinkInstructions : public MachineFunctionPass {
public:
  static char ID;

public:
  SIShrinkInstructions() : MachineFunctionPass(ID) {
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override {
    return "SI Shrink Instructions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SIShrinkInstructions, DEBUG_TYPE,
                      "SI Lower il Copies", false, false)
INITIALIZE_PASS_END(SIShrinkInstructions, DEBUG_TYPE,
                    "SI Lower il Copies", false, false)

char SIShrinkInstructions::ID = 0;

FunctionPass *llvm::createSIShrinkInstructionsPass() {
  return new SIShrinkInstructions();
}

static bool isVGPR(const MachineOperand *MO, const SIRegisterInfo &TRI,
                   const MachineRegisterInfo &MRI) {
  if (!MO->isReg())
    return false;

  if (TargetRegisterInfo::isVirtualRegister(MO->getReg()))
    return TRI.hasVGPRs(MRI.getRegClass(MO->getReg()));

  return TRI.hasVGPRs(TRI.getPhysRegClass(MO->getReg()));
}

static bool canShrink(MachineInstr &MI, const SIInstrInfo *TII,
                      const SIRegisterInfo &TRI,
                      const MachineRegisterInfo &MRI) {

  const MachineOperand *Src2 = TII->getNamedOperand(MI, AMDGPU::OpName::src2);
  // Can't shrink instruction with three operands.
  // FIXME: v_cndmask_b32 has 3 operands and is shrinkable, but we need to add
  // a special case for it.  It can only be shrunk if the third operand
  // is vcc.  We should handle this the same way we handle vopc, by addding
  // a register allocation hint pre-regalloc and then do the shrining
  // post-regalloc.
  if (Src2) {
    switch (MI.getOpcode()) {
      default: return false;

      case AMDGPU::V_MAC_F32_e64:
        if (!isVGPR(Src2, TRI, MRI) ||
            TII->hasModifiersSet(MI, AMDGPU::OpName::src2_modifiers))
          return false;
        break;

      case AMDGPU::V_CNDMASK_B32_e64:
        break;
    }
  }

  const MachineOperand *Src1 = TII->getNamedOperand(MI, AMDGPU::OpName::src1);
  const MachineOperand *Src1Mod =
      TII->getNamedOperand(MI, AMDGPU::OpName::src1_modifiers);

  if (Src1 && (!isVGPR(Src1, TRI, MRI) || (Src1Mod && Src1Mod->getImm() != 0)))
    return false;

  // We don't need to check src0, all input types are legal, so just make sure
  // src0 isn't using any modifiers.
  if (TII->hasModifiersSet(MI, AMDGPU::OpName::src0_modifiers))
    return false;

  // Check output modifiers
  if (TII->hasModifiersSet(MI, AMDGPU::OpName::omod))
    return false;

  if (TII->hasModifiersSet(MI, AMDGPU::OpName::clamp))
    return false;

  return true;
}

/// \brief This function checks \p MI for operands defined by a move immediate
/// instruction and then folds the literal constant into the instruction if it
/// can.  This function assumes that \p MI is a VOP1, VOP2, or VOPC instruction
/// and will only fold literal constants if we are still in SSA.
static void foldImmediates(MachineInstr &MI, const SIInstrInfo *TII,
                           MachineRegisterInfo &MRI, bool TryToCommute = true) {

  if (!MRI.isSSA())
    return;

  assert(TII->isVOP1(MI) || TII->isVOP2(MI) || TII->isVOPC(MI));

  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  int Src0Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src0);
  MachineOperand &Src0 = MI.getOperand(Src0Idx);

  // Only one literal constant is allowed per instruction, so if src0 is a
  // literal constant then we can't do any folding.
  if (Src0.isImm() &&
      TII->isLiteralConstant(Src0, TII->getOpSize(MI, Src0Idx)))
    return;

  // Literal constants and SGPRs can only be used in Src0, so if Src0 is an
  // SGPR, we cannot commute the instruction, so we can't fold any literal
  // constants.
  if (Src0.isReg() && !isVGPR(&Src0, TRI, MRI))
    return;

  // Try to fold Src0
  if (Src0.isReg() && MRI.hasOneUse(Src0.getReg())) {
    unsigned Reg = Src0.getReg();
    MachineInstr *Def = MRI.getUniqueVRegDef(Reg);
    if (Def && Def->isMoveImmediate()) {
      MachineOperand &MovSrc = Def->getOperand(1);
      bool ConstantFolded = false;

      if (MovSrc.isImm() && isUInt<32>(MovSrc.getImm())) {
        Src0.ChangeToImmediate(MovSrc.getImm());
        ConstantFolded = true;
      }
      if (ConstantFolded) {
        if (MRI.use_empty(Reg))
          Def->eraseFromParent();
        ++NumLiteralConstantsFolded;
        return;
      }
    }
  }

  // We have failed to fold src0, so commute the instruction and try again.
  if (TryToCommute && MI.isCommutable() && TII->commuteInstruction(&MI))
    foldImmediates(MI, TII, MRI, false);

}

// Copy MachineOperand with all flags except setting it as implicit.
static MachineOperand copyRegOperandAsImplicit(const MachineOperand &Orig) {
  assert(!Orig.isImplicit());
  return MachineOperand::CreateReg(Orig.getReg(),
                                   Orig.isDef(),
                                   true,
                                   Orig.isKill(),
                                   Orig.isDead(),
                                   Orig.isUndef(),
                                   Orig.isEarlyClobber(),
                                   Orig.getSubReg(),
                                   Orig.isDebug(),
                                   Orig.isInternalRead());
}

bool SIShrinkInstructions::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const SIInstrInfo *TII =
      static_cast<const SIInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  std::vector<unsigned> I1Defs;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
                                                  BI != BE; ++BI) {

    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;
    for (I = MBB.begin(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;

      // Try to use S_MOVK_I32, which will save 4 bytes for small immediates.
      if (MI.getOpcode() == AMDGPU::S_MOV_B32) {
        const MachineOperand &Src = MI.getOperand(1);

        if (Src.isImm()) {
          if (isInt<16>(Src.getImm()) && !TII->isInlineConstant(Src, 4))
            MI.setDesc(TII->get(AMDGPU::S_MOVK_I32));
        }

        continue;
      }

      if (!TII->hasVALU32BitEncoding(MI.getOpcode()))
        continue;

      if (!canShrink(MI, TII, TRI, MRI)) {
        // Try commuting the instruction and see if that enables us to shrink
        // it.
        if (!MI.isCommutable() || !TII->commuteInstruction(&MI) ||
            !canShrink(MI, TII, TRI, MRI))
          continue;
      }

      // getVOPe32 could be -1 here if we started with an instruction that had
      // a 32-bit encoding and then commuted it to an instruction that did not.
      if (!TII->hasVALU32BitEncoding(MI.getOpcode()))
        continue;

      int Op32 = AMDGPU::getVOPe32(MI.getOpcode());

      if (TII->isVOPC(Op32)) {
        unsigned DstReg = MI.getOperand(0).getReg();
        if (TargetRegisterInfo::isVirtualRegister(DstReg)) {
          // VOPC instructions can only write to the VCC register. We can't
          // force them to use VCC here, because this is only one register and
          // cannot deal with sequences which would require multiple copies of
          // VCC, e.g. S_AND_B64 (vcc = V_CMP_...), (vcc = V_CMP_...)
          //
          // So, instead of forcing the instruction to write to VCC, we provide
          // a hint to the register allocator to use VCC and then we we will run
          // this pass again after RA and shrink it if it outputs to VCC.
          MRI.setRegAllocationHint(MI.getOperand(0).getReg(), 0, AMDGPU::VCC);
          continue;
        }
        if (DstReg != AMDGPU::VCC)
          continue;
      }

      if (Op32 == AMDGPU::V_CNDMASK_B32_e32) {
        // We shrink V_CNDMASK_B32_e64 using regalloc hints like we do for VOPC
        // instructions.
        const MachineOperand *Src2 =
            TII->getNamedOperand(MI, AMDGPU::OpName::src2);
        if (!Src2->isReg())
          continue;
        unsigned SReg = Src2->getReg();
        if (TargetRegisterInfo::isVirtualRegister(SReg)) {
          MRI.setRegAllocationHint(SReg, 0, AMDGPU::VCC);
          continue;
        }
        if (SReg != AMDGPU::VCC)
          continue;
      }

      // We can shrink this instruction
      DEBUG(dbgs() << "Shrinking " << MI);

      MachineInstrBuilder Inst32 =
          BuildMI(MBB, I, MI.getDebugLoc(), TII->get(Op32));

      // Add the dst operand if the 32-bit encoding also has an explicit $dst.
      // For VOPC instructions, this is replaced by an implicit def of vcc.
      int Op32DstIdx = AMDGPU::getNamedOperandIdx(Op32, AMDGPU::OpName::dst);
      if (Op32DstIdx != -1) {
        // dst
        Inst32.addOperand(MI.getOperand(0));
      } else {
        assert(MI.getOperand(0).getReg() == AMDGPU::VCC &&
               "Unexpected case");
      }


      Inst32.addOperand(*TII->getNamedOperand(MI, AMDGPU::OpName::src0));

      const MachineOperand *Src1 =
          TII->getNamedOperand(MI, AMDGPU::OpName::src1);
      if (Src1)
        Inst32.addOperand(*Src1);

      const MachineOperand *Src2 =
        TII->getNamedOperand(MI, AMDGPU::OpName::src2);
      if (Src2) {
        int Op32Src2Idx = AMDGPU::getNamedOperandIdx(Op32, AMDGPU::OpName::src2);
        if (Op32Src2Idx != -1) {
          Inst32.addOperand(*Src2);
        } else {
          // In the case of V_CNDMASK_B32_e32, the explicit operand src2 is
          // replaced with an implicit read of vcc.
          assert(Src2->getReg() == AMDGPU::VCC &&
                 "Unexpected missing register operand");
          Inst32.addOperand(copyRegOperandAsImplicit(*Src2));
        }
      }

      ++NumInstructionsShrunk;
      MI.eraseFromParent();

      foldImmediates(*Inst32, TII, MRI);
      DEBUG(dbgs() << "e32 MI = " << *Inst32 << '\n');


    }
  }
  return false;
}
