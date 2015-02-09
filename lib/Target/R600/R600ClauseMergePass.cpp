//===-- R600ClauseMergePass - Merge consecutive CF_ALU -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600EmitClauseMarker pass emits CFAlu instruction in a conservative maneer.
/// This pass is merging consecutive CFAlus where applicable.
/// It needs to be called after IfCvt for best results.
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "R600Defines.h"
#include "R600InstrInfo.h"
#include "R600MachineFunctionInfo.h"
#include "R600RegisterInfo.h"
#include "AMDGPUSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "r600mergeclause"

namespace {

static bool isCFAlu(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  case AMDGPU::CF_ALU:
  case AMDGPU::CF_ALU_PUSH_BEFORE:
    return true;
  default:
    return false;
  }
}

class R600ClauseMergePass : public MachineFunctionPass {

private:
  static char ID;
  const R600InstrInfo *TII;

  unsigned getCFAluSize(const MachineInstr *MI) const;
  bool isCFAluEnabled(const MachineInstr *MI) const;

  /// IfCvt pass can generate "disabled" ALU clause marker that need to be
  /// removed and their content affected to the previous alu clause.
  /// This function parse instructions after CFAlu until it find a disabled
  /// CFAlu and merge the content, or an enabled CFAlu.
  void cleanPotentialDisabledCFAlu(MachineInstr *CFAlu) const;

  /// Check whether LatrCFAlu can be merged into RootCFAlu and do it if
  /// it is the case.
  bool mergeIfPossible(MachineInstr *RootCFAlu, const MachineInstr *LatrCFAlu)
      const;

public:
  R600ClauseMergePass(TargetMachine &tm) : MachineFunctionPass(ID) { }

  bool runOnMachineFunction(MachineFunction &MF) override;

  const char *getPassName() const override;
};

char R600ClauseMergePass::ID = 0;

unsigned R600ClauseMergePass::getCFAluSize(const MachineInstr *MI) const {
  assert(isCFAlu(MI));
  return MI->getOperand(
      TII->getOperandIdx(MI->getOpcode(), AMDGPU::OpName::COUNT)).getImm();
}

bool R600ClauseMergePass::isCFAluEnabled(const MachineInstr *MI) const {
  assert(isCFAlu(MI));
  return MI->getOperand(
      TII->getOperandIdx(MI->getOpcode(), AMDGPU::OpName::Enabled)).getImm();
}

void R600ClauseMergePass::cleanPotentialDisabledCFAlu(MachineInstr *CFAlu)
    const {
  int CntIdx = TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::COUNT);
  MachineBasicBlock::iterator I = CFAlu, E = CFAlu->getParent()->end();
  I++;
  do {
    while (I!= E && !isCFAlu(I))
      I++;
    if (I == E)
      return;
    MachineInstr *MI = I++;
    if (isCFAluEnabled(MI))
      break;
    CFAlu->getOperand(CntIdx).setImm(getCFAluSize(CFAlu) + getCFAluSize(MI));
    MI->eraseFromParent();
  } while (I != E);
}

bool R600ClauseMergePass::mergeIfPossible(MachineInstr *RootCFAlu,
                                          const MachineInstr *LatrCFAlu) const {
  assert(isCFAlu(RootCFAlu) && isCFAlu(LatrCFAlu));
  int CntIdx = TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::COUNT);
  unsigned RootInstCount = getCFAluSize(RootCFAlu),
      LaterInstCount = getCFAluSize(LatrCFAlu);
  unsigned CumuledInsts = RootInstCount + LaterInstCount;
  if (CumuledInsts >= TII->getMaxAlusPerClause()) {
    DEBUG(dbgs() << "Excess inst counts\n");
    return false;
  }
  if (RootCFAlu->getOpcode() == AMDGPU::CF_ALU_PUSH_BEFORE)
    return false;
  // Is KCache Bank 0 compatible ?
  int Mode0Idx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_MODE0);
  int KBank0Idx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_BANK0);
  int KBank0LineIdx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_ADDR0);
  if (LatrCFAlu->getOperand(Mode0Idx).getImm() &&
      RootCFAlu->getOperand(Mode0Idx).getImm() &&
      (LatrCFAlu->getOperand(KBank0Idx).getImm() !=
       RootCFAlu->getOperand(KBank0Idx).getImm() ||
      LatrCFAlu->getOperand(KBank0LineIdx).getImm() !=
      RootCFAlu->getOperand(KBank0LineIdx).getImm())) {
    DEBUG(dbgs() << "Wrong KC0\n");
    return false;
  }
  // Is KCache Bank 1 compatible ?
  int Mode1Idx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_MODE1);
  int KBank1Idx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_BANK1);
  int KBank1LineIdx =
      TII->getOperandIdx(AMDGPU::CF_ALU, AMDGPU::OpName::KCACHE_ADDR1);
  if (LatrCFAlu->getOperand(Mode1Idx).getImm() &&
      RootCFAlu->getOperand(Mode1Idx).getImm() &&
      (LatrCFAlu->getOperand(KBank1Idx).getImm() !=
      RootCFAlu->getOperand(KBank1Idx).getImm() ||
      LatrCFAlu->getOperand(KBank1LineIdx).getImm() !=
      RootCFAlu->getOperand(KBank1LineIdx).getImm())) {
    DEBUG(dbgs() << "Wrong KC0\n");
    return false;
  }
  if (LatrCFAlu->getOperand(Mode0Idx).getImm()) {
    RootCFAlu->getOperand(Mode0Idx).setImm(
        LatrCFAlu->getOperand(Mode0Idx).getImm());
    RootCFAlu->getOperand(KBank0Idx).setImm(
        LatrCFAlu->getOperand(KBank0Idx).getImm());
    RootCFAlu->getOperand(KBank0LineIdx).setImm(
        LatrCFAlu->getOperand(KBank0LineIdx).getImm());
  }
  if (LatrCFAlu->getOperand(Mode1Idx).getImm()) {
    RootCFAlu->getOperand(Mode1Idx).setImm(
        LatrCFAlu->getOperand(Mode1Idx).getImm());
    RootCFAlu->getOperand(KBank1Idx).setImm(
        LatrCFAlu->getOperand(KBank1Idx).getImm());
    RootCFAlu->getOperand(KBank1LineIdx).setImm(
        LatrCFAlu->getOperand(KBank1LineIdx).getImm());
  }
  RootCFAlu->getOperand(CntIdx).setImm(CumuledInsts);
  RootCFAlu->setDesc(TII->get(LatrCFAlu->getOpcode()));
  return true;
}

bool R600ClauseMergePass::runOnMachineFunction(MachineFunction &MF) {
  TII = static_cast<const R600InstrInfo *>(MF.getSubtarget().getInstrInfo());
  for (MachineFunction::iterator BB = MF.begin(), BB_E = MF.end();
                                                  BB != BB_E; ++BB) {
    MachineBasicBlock &MBB = *BB;
    MachineBasicBlock::iterator I = MBB.begin(),  E = MBB.end();
    MachineBasicBlock::iterator LatestCFAlu = E;
    while (I != E) {
      MachineInstr *MI = I++;
      if ((!TII->canBeConsideredALU(MI) && !isCFAlu(MI)) ||
          TII->mustBeLastInClause(MI->getOpcode()))
        LatestCFAlu = E;
      if (!isCFAlu(MI))
        continue;
      cleanPotentialDisabledCFAlu(MI);

      if (LatestCFAlu != E && mergeIfPossible(LatestCFAlu, MI)) {
        MI->eraseFromParent();
      } else {
        assert(MI->getOperand(8).getImm() && "CF ALU instruction disabled");
        LatestCFAlu = MI;
      }
    }
  }
  return false;
}

const char *R600ClauseMergePass::getPassName() const {
  return "R600 Merge Clause Markers Pass";
}

} // end anonymous namespace


llvm::FunctionPass *llvm::createR600ClauseMergePass(TargetMachine &TM) {
  return new R600ClauseMergePass(TM);
}
