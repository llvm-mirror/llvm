//===-- R600ExpandSpecialInstrs.cpp - Expand special instructions ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Vector, Reduction, and Cube instructions need to fill the entire instruction
/// group to work correctly.  This pass expands these individual instructions
/// into several instructions that will completely fill the instruction group.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "R600Defines.h"
#include "R600InstrInfo.h"
#include "R600MachineFunctionInfo.h"
#include "R600RegisterInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

namespace {

class R600ExpandSpecialInstrsPass : public MachineFunctionPass {

private:
  static char ID;
  const R600InstrInfo *TII;

  bool ExpandInputPerspective(MachineInstr& MI);
  bool ExpandInputConstant(MachineInstr& MI);

public:
  R600ExpandSpecialInstrsPass(TargetMachine &tm) : MachineFunctionPass(ID),
    TII(0) { }

  virtual bool runOnMachineFunction(MachineFunction &MF);

  const char *getPassName() const {
    return "R600 Expand special instructions pass";
  }
};

} // End anonymous namespace

char R600ExpandSpecialInstrsPass::ID = 0;

FunctionPass *llvm::createR600ExpandSpecialInstrsPass(TargetMachine &TM) {
  return new R600ExpandSpecialInstrsPass(TM);
}

bool R600ExpandSpecialInstrsPass::runOnMachineFunction(MachineFunction &MF) {
  TII = static_cast<const R600InstrInfo *>(MF.getTarget().getInstrInfo());

  const R600RegisterInfo &TRI = TII->getRegisterInfo();

  for (MachineFunction::iterator BB = MF.begin(), BB_E = MF.end();
                                                  BB != BB_E; ++BB) {
    MachineBasicBlock &MBB = *BB;
    MachineBasicBlock::iterator I = MBB.begin();
    while (I != MBB.end()) {
      MachineInstr &MI = *I;
      I = llvm::next(I);

      switch (MI.getOpcode()) {
      default: break;
      // Expand PRED_X to one of the PRED_SET instructions.
      case AMDGPU::PRED_X: {
        uint64_t Flags = MI.getOperand(3).getImm();
        // The native opcode used by PRED_X is stored as an immediate in the
        // third operand.
        MachineInstr *PredSet = TII->buildDefaultInstruction(MBB, I,
                                            MI.getOperand(2).getImm(), // opcode
                                            MI.getOperand(0).getReg(), // dst
                                            MI.getOperand(1).getReg(), // src0
                                            AMDGPU::ZERO);             // src1
        TII->addFlag(PredSet, 0, MO_FLAG_MASK);
        if (Flags & MO_FLAG_PUSH) {
          TII->setImmOperand(PredSet, AMDGPU::OpName::update_exec_mask, 1);
        } else {
          TII->setImmOperand(PredSet, AMDGPU::OpName::update_pred, 1);
        }
        MI.eraseFromParent();
        continue;
        }
      case AMDGPU::BREAK: {
        MachineInstr *PredSet = TII->buildDefaultInstruction(MBB, I,
                                          AMDGPU::PRED_SETE_INT,
                                          AMDGPU::PREDICATE_BIT,
                                          AMDGPU::ZERO,
                                          AMDGPU::ZERO);
        TII->addFlag(PredSet, 0, MO_FLAG_MASK);
        TII->setImmOperand(PredSet, AMDGPU::OpName::update_exec_mask, 1);

        BuildMI(MBB, I, MBB.findDebugLoc(I),
                TII->get(AMDGPU::PREDICATED_BREAK))
                .addReg(AMDGPU::PREDICATE_BIT);
        MI.eraseFromParent();
        continue;
        }

      case AMDGPU::INTERP_PAIR_XY: {
        MachineInstr *BMI;
        unsigned PReg = AMDGPU::R600_ArrayBaseRegClass.getRegister(
                MI.getOperand(2).getImm());

        for (unsigned Chan = 0; Chan < 4; ++Chan) {
          unsigned DstReg;

          if (Chan < 2)
            DstReg = MI.getOperand(Chan).getReg();
          else
            DstReg = Chan == 2 ? AMDGPU::T0_Z : AMDGPU::T0_W;

          BMI = TII->buildDefaultInstruction(MBB, I, AMDGPU::INTERP_XY,
              DstReg, MI.getOperand(3 + (Chan % 2)).getReg(), PReg);

          if (Chan > 0) {
            BMI->bundleWithPred();
          }
          if (Chan >= 2)
            TII->addFlag(BMI, 0, MO_FLAG_MASK);
          if (Chan != 3)
            TII->addFlag(BMI, 0, MO_FLAG_NOT_LAST);
        }

        MI.eraseFromParent();
        continue;
        }

      case AMDGPU::INTERP_PAIR_ZW: {
        MachineInstr *BMI;
        unsigned PReg = AMDGPU::R600_ArrayBaseRegClass.getRegister(
                MI.getOperand(2).getImm());

        for (unsigned Chan = 0; Chan < 4; ++Chan) {
          unsigned DstReg;

          if (Chan < 2)
            DstReg = Chan == 0 ? AMDGPU::T0_X : AMDGPU::T0_Y;
          else
            DstReg = MI.getOperand(Chan-2).getReg();

          BMI = TII->buildDefaultInstruction(MBB, I, AMDGPU::INTERP_ZW,
              DstReg, MI.getOperand(3 + (Chan % 2)).getReg(), PReg);

          if (Chan > 0) {
            BMI->bundleWithPred();
          }
          if (Chan < 2)
            TII->addFlag(BMI, 0, MO_FLAG_MASK);
          if (Chan != 3)
            TII->addFlag(BMI, 0, MO_FLAG_NOT_LAST);
        }

        MI.eraseFromParent();
        continue;
        }

      case AMDGPU::INTERP_VEC_LOAD: {
        const R600RegisterInfo &TRI = TII->getRegisterInfo();
        MachineInstr *BMI;
        unsigned PReg = AMDGPU::R600_ArrayBaseRegClass.getRegister(
                MI.getOperand(1).getImm());
        unsigned DstReg = MI.getOperand(0).getReg();

        for (unsigned Chan = 0; Chan < 4; ++Chan) {
          BMI = TII->buildDefaultInstruction(MBB, I, AMDGPU::INTERP_LOAD_P0,
              TRI.getSubReg(DstReg, TRI.getSubRegFromChannel(Chan)), PReg);
          if (Chan > 0) {
            BMI->bundleWithPred();
          }
          if (Chan != 3)
            TII->addFlag(BMI, 0, MO_FLAG_NOT_LAST);
        }

        MI.eraseFromParent();
        continue;
        }
      case AMDGPU::DOT_4: {

        const R600RegisterInfo &TRI = TII->getRegisterInfo();

        unsigned DstReg = MI.getOperand(0).getReg();
        unsigned DstBase = TRI.getEncodingValue(DstReg) & HW_REG_MASK;

        for (unsigned Chan = 0; Chan < 4; ++Chan) {
          bool Mask = (Chan != TRI.getHWRegChan(DstReg));
          unsigned SubDstReg =
              AMDGPU::R600_TReg32RegClass.getRegister((DstBase * 4) + Chan);
          MachineInstr *BMI =
              TII->buildSlotOfVectorInstruction(MBB, &MI, Chan, SubDstReg);
          if (Chan > 0) {
            BMI->bundleWithPred();
          }
          if (Mask) {
            TII->addFlag(BMI, 0, MO_FLAG_MASK);
          }
          if (Chan != 3)
            TII->addFlag(BMI, 0, MO_FLAG_NOT_LAST);
          unsigned Opcode = BMI->getOpcode();
          // While not strictly necessary from hw point of view, we force
          // all src operands of a dot4 inst to belong to the same slot.
          unsigned Src0 = BMI->getOperand(
              TII->getOperandIdx(Opcode, AMDGPU::OpName::src0))
              .getReg();
          unsigned Src1 = BMI->getOperand(
              TII->getOperandIdx(Opcode, AMDGPU::OpName::src1))
              .getReg();
          (void) Src0;
          (void) Src1;
          if ((TRI.getEncodingValue(Src0) & 0xff) < 127 &&
              (TRI.getEncodingValue(Src1) & 0xff) < 127)
            assert(TRI.getHWRegChan(Src0) == TRI.getHWRegChan(Src1));
        }
        MI.eraseFromParent();
        continue;
      }
      }

      bool IsReduction = TII->isReductionOp(MI.getOpcode());
      bool IsVector = TII->isVector(MI);
      bool IsCube = TII->isCubeOp(MI.getOpcode());
      if (!IsReduction && !IsVector && !IsCube) {
        continue;
      }

      // Expand the instruction
      //
      // Reduction instructions:
      // T0_X = DP4 T1_XYZW, T2_XYZW
      // becomes:
      // TO_X = DP4 T1_X, T2_X
      // TO_Y (write masked) = DP4 T1_Y, T2_Y
      // TO_Z (write masked) = DP4 T1_Z, T2_Z
      // TO_W (write masked) = DP4 T1_W, T2_W
      //
      // Vector instructions:
      // T0_X = MULLO_INT T1_X, T2_X
      // becomes:
      // T0_X = MULLO_INT T1_X, T2_X
      // T0_Y (write masked) = MULLO_INT T1_X, T2_X
      // T0_Z (write masked) = MULLO_INT T1_X, T2_X
      // T0_W (write masked) = MULLO_INT T1_X, T2_X
      //
      // Cube instructions:
      // T0_XYZW = CUBE T1_XYZW
      // becomes:
      // TO_X = CUBE T1_Z, T1_Y
      // T0_Y = CUBE T1_Z, T1_X
      // T0_Z = CUBE T1_X, T1_Z
      // T0_W = CUBE T1_Y, T1_Z
      for (unsigned Chan = 0; Chan < 4; Chan++) {
        unsigned DstReg = MI.getOperand(
                            TII->getOperandIdx(MI, AMDGPU::OpName::dst)).getReg();
        unsigned Src0 = MI.getOperand(
                           TII->getOperandIdx(MI, AMDGPU::OpName::src0)).getReg();
        unsigned Src1 = 0;

        // Determine the correct source registers
        if (!IsCube) {
          int Src1Idx = TII->getOperandIdx(MI, AMDGPU::OpName::src1);
          if (Src1Idx != -1) {
            Src1 = MI.getOperand(Src1Idx).getReg();
          }
        }
        if (IsReduction) {
          unsigned SubRegIndex = TRI.getSubRegFromChannel(Chan);
          Src0 = TRI.getSubReg(Src0, SubRegIndex);
          Src1 = TRI.getSubReg(Src1, SubRegIndex);
        } else if (IsCube) {
          static const int CubeSrcSwz[] = {2, 2, 0, 1};
          unsigned SubRegIndex0 = TRI.getSubRegFromChannel(CubeSrcSwz[Chan]);
          unsigned SubRegIndex1 = TRI.getSubRegFromChannel(CubeSrcSwz[3 - Chan]);
          Src1 = TRI.getSubReg(Src0, SubRegIndex1);
          Src0 = TRI.getSubReg(Src0, SubRegIndex0);
        }

        // Determine the correct destination registers;
        bool Mask = false;
        bool NotLast = true;
        if (IsCube) {
          unsigned SubRegIndex = TRI.getSubRegFromChannel(Chan);
          DstReg = TRI.getSubReg(DstReg, SubRegIndex);
        } else {
          // Mask the write if the original instruction does not write to
          // the current Channel.
          Mask = (Chan != TRI.getHWRegChan(DstReg));
          unsigned DstBase = TRI.getEncodingValue(DstReg) & HW_REG_MASK;
          DstReg = AMDGPU::R600_TReg32RegClass.getRegister((DstBase * 4) + Chan);
        }

        // Set the IsLast bit
        NotLast = (Chan != 3 );

        // Add the new instruction
        unsigned Opcode = MI.getOpcode();
        switch (Opcode) {
        case AMDGPU::CUBE_r600_pseudo:
          Opcode = AMDGPU::CUBE_r600_real;
          break;
        case AMDGPU::CUBE_eg_pseudo:
          Opcode = AMDGPU::CUBE_eg_real;
          break;
        default:
          break;
        }

        MachineInstr *NewMI =
          TII->buildDefaultInstruction(MBB, I, Opcode, DstReg, Src0, Src1);

        if (Chan != 0)
          NewMI->bundleWithPred();
        if (Mask) {
          TII->addFlag(NewMI, 0, MO_FLAG_MASK);
        }
        if (NotLast) {
          TII->addFlag(NewMI, 0, MO_FLAG_NOT_LAST);
        }
      }
      MI.eraseFromParent();
    }
  }
  return false;
}
