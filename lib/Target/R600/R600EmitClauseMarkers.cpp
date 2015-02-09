//===-- R600EmitClauseMarkers.cpp - Emit CF_ALU ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Add CF_ALU. R600 Alu instructions are grouped in clause which can hold
/// 128 Alu instructions ; these instructions can access up to 4 prefetched
/// 4 lines of 16 registers from constant buffers. Such ALU clauses are
/// initiated by CF_ALU instructions.
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

using namespace llvm;

namespace llvm {
  void initializeR600EmitClauseMarkersPass(PassRegistry&);
}

namespace {

class R600EmitClauseMarkers : public MachineFunctionPass {

private:
  const R600InstrInfo *TII;
  int Address;

  unsigned OccupiedDwords(MachineInstr *MI) const {
    switch (MI->getOpcode()) {
    case AMDGPU::INTERP_PAIR_XY:
    case AMDGPU::INTERP_PAIR_ZW:
    case AMDGPU::INTERP_VEC_LOAD:
    case AMDGPU::DOT_4:
      return 4;
    case AMDGPU::KILL:
      return 0;
    default:
      break;
    }

    // These will be expanded to two ALU instructions in the
    // ExpandSpecialInstructions pass.
    if (TII->isLDSRetInstr(MI->getOpcode()))
      return 2;

    if(TII->isVector(*MI) ||
        TII->isCubeOp(MI->getOpcode()) ||
        TII->isReductionOp(MI->getOpcode()))
      return 4;

    unsigned NumLiteral = 0;
    for (MachineInstr::mop_iterator It = MI->operands_begin(),
        E = MI->operands_end(); It != E; ++It) {
      MachineOperand &MO = *It;
      if (MO.isReg() && MO.getReg() == AMDGPU::ALU_LITERAL_X)
        ++NumLiteral;
    }
    return 1 + NumLiteral;
  }

  bool isALU(const MachineInstr *MI) const {
    if (TII->isALUInstr(MI->getOpcode()))
      return true;
    if (TII->isVector(*MI) || TII->isCubeOp(MI->getOpcode()))
      return true;
    switch (MI->getOpcode()) {
    case AMDGPU::PRED_X:
    case AMDGPU::INTERP_PAIR_XY:
    case AMDGPU::INTERP_PAIR_ZW:
    case AMDGPU::INTERP_VEC_LOAD:
    case AMDGPU::COPY:
    case AMDGPU::DOT_4:
      return true;
    default:
      return false;
    }
  }

  bool IsTrivialInst(MachineInstr *MI) const {
    switch (MI->getOpcode()) {
    case AMDGPU::KILL:
    case AMDGPU::RETURN:
    case AMDGPU::IMPLICIT_DEF:
      return true;
    default:
      return false;
    }
  }

  std::pair<unsigned, unsigned> getAccessedBankLine(unsigned Sel) const {
    // Sel is (512 + (kc_bank << 12) + ConstIndex) << 2
    // (See also R600ISelLowering.cpp)
    // ConstIndex value is in [0, 4095];
    return std::pair<unsigned, unsigned>(
        ((Sel >> 2) - 512) >> 12, // KC_BANK
        // Line Number of ConstIndex
        // A line contains 16 constant registers however KCX bank can lock
        // two line at the same time ; thus we want to get an even line number.
        // Line number can be retrieved with (>>4), using (>>5) <<1 generates
        // an even number.
        ((((Sel >> 2) - 512) & 4095) >> 5) << 1);
  }

  bool SubstituteKCacheBank(MachineInstr *MI,
      std::vector<std::pair<unsigned, unsigned> > &CachedConsts,
      bool UpdateInstr = true) const {
    std::vector<std::pair<unsigned, unsigned> > UsedKCache;

    if (!TII->isALUInstr(MI->getOpcode()) && MI->getOpcode() != AMDGPU::DOT_4)
      return true;

    const SmallVectorImpl<std::pair<MachineOperand *, int64_t> > &Consts =
        TII->getSrcs(MI);
    assert((TII->isALUInstr(MI->getOpcode()) ||
        MI->getOpcode() == AMDGPU::DOT_4) && "Can't assign Const");
    for (unsigned i = 0, n = Consts.size(); i < n; ++i) {
      if (Consts[i].first->getReg() != AMDGPU::ALU_CONST)
        continue;
      unsigned Sel = Consts[i].second;
      unsigned Chan = Sel & 3, Index = ((Sel >> 2) - 512) & 31;
      unsigned KCacheIndex = Index * 4 + Chan;
      const std::pair<unsigned, unsigned> &BankLine = getAccessedBankLine(Sel);
      if (CachedConsts.empty()) {
        CachedConsts.push_back(BankLine);
        UsedKCache.push_back(std::pair<unsigned, unsigned>(0, KCacheIndex));
        continue;
      }
      if (CachedConsts[0] == BankLine) {
        UsedKCache.push_back(std::pair<unsigned, unsigned>(0, KCacheIndex));
        continue;
      }
      if (CachedConsts.size() == 1) {
        CachedConsts.push_back(BankLine);
        UsedKCache.push_back(std::pair<unsigned, unsigned>(1, KCacheIndex));
        continue;
      }
      if (CachedConsts[1] == BankLine) {
        UsedKCache.push_back(std::pair<unsigned, unsigned>(1, KCacheIndex));
        continue;
      }
      return false;
    }

    if (!UpdateInstr)
      return true;

    for (unsigned i = 0, j = 0, n = Consts.size(); i < n; ++i) {
      if (Consts[i].first->getReg() != AMDGPU::ALU_CONST)
        continue;
      switch(UsedKCache[j].first) {
      case 0:
        Consts[i].first->setReg(
            AMDGPU::R600_KC0RegClass.getRegister(UsedKCache[j].second));
        break;
      case 1:
        Consts[i].first->setReg(
            AMDGPU::R600_KC1RegClass.getRegister(UsedKCache[j].second));
        break;
      default:
        llvm_unreachable("Wrong Cache Line");
      }
      j++;
    }
    return true;
  }

  bool canClauseLocalKillFitInClause(
                        unsigned AluInstCount,
                        std::vector<std::pair<unsigned, unsigned> > KCacheBanks,
                        MachineBasicBlock::iterator Def,
                        MachineBasicBlock::iterator BBEnd) {
    const R600RegisterInfo &TRI = TII->getRegisterInfo();
    for (MachineInstr::const_mop_iterator
           MOI = Def->operands_begin(),
           MOE = Def->operands_end(); MOI != MOE; ++MOI) {
      if (!MOI->isReg() || !MOI->isDef() ||
          TRI.isPhysRegLiveAcrossClauses(MOI->getReg()))
        continue;

      // Def defines a clause local register, so check that its use will fit
      // in the clause.
      unsigned LastUseCount = 0;
      for (MachineBasicBlock::iterator UseI = Def; UseI != BBEnd; ++UseI) {
        AluInstCount += OccupiedDwords(UseI);
        // Make sure we won't need to end the clause due to KCache limitations.
        if (!SubstituteKCacheBank(UseI, KCacheBanks, false))
          return false;

        // We have reached the maximum instruction limit before finding the
        // use that kills this register, so we cannot use this def in the
        // current clause.
        if (AluInstCount >= TII->getMaxAlusPerClause())
          return false;

        // Register kill flags have been cleared by the time we get to this
        // pass, but it is safe to assume that all uses of this register
        // occur in the same basic block as its definition, because
        // it is illegal for the scheduler to schedule them in
        // different blocks.
        if (UseI->findRegisterUseOperandIdx(MOI->getReg()))
          LastUseCount = AluInstCount;

        if (UseI != Def && UseI->findRegisterDefOperandIdx(MOI->getReg()) != -1)
          break;
      }
      if (LastUseCount)
        return LastUseCount <= TII->getMaxAlusPerClause();
      llvm_unreachable("Clause local register live at end of clause.");
    }
    return true;
  }

  MachineBasicBlock::iterator
  MakeALUClause(MachineBasicBlock &MBB, MachineBasicBlock::iterator I) {
    MachineBasicBlock::iterator ClauseHead = I;
    std::vector<std::pair<unsigned, unsigned> > KCacheBanks;
    bool PushBeforeModifier = false;
    unsigned AluInstCount = 0;
    for (MachineBasicBlock::iterator E = MBB.end(); I != E; ++I) {
      if (IsTrivialInst(I))
        continue;
      if (!isALU(I))
        break;
      if (AluInstCount > TII->getMaxAlusPerClause())
        break;
      if (I->getOpcode() == AMDGPU::PRED_X) {
        // We put PRED_X in its own clause to ensure that ifcvt won't create
        // clauses with more than 128 insts.
        // IfCvt is indeed checking that "then" and "else" branches of an if
        // statement have less than ~60 insts thus converted clauses can't be
        // bigger than ~121 insts (predicate setter needs to be in the same
        // clause as predicated alus).
        if (AluInstCount > 0)
          break;
        if (TII->getFlagOp(I).getImm() & MO_FLAG_PUSH)
          PushBeforeModifier = true;
        AluInstCount ++;
        continue;
      }
      // XXX: GROUP_BARRIER instructions cannot be in the same ALU clause as:
      //
      // * KILL or INTERP instructions
      // * Any instruction that sets UPDATE_EXEC_MASK or UPDATE_PRED bits
      // * Uses waterfalling (i.e. INDEX_MODE = AR.X)
      //
      // XXX: These checks have not been implemented yet.
      if (TII->mustBeLastInClause(I->getOpcode())) {
        I++;
        break;
      }

      // If this instruction defines a clause local register, make sure
      // its use can fit in this clause.
      if (!canClauseLocalKillFitInClause(AluInstCount, KCacheBanks, I, E))
        break;

      if (!SubstituteKCacheBank(I, KCacheBanks))
        break;
      AluInstCount += OccupiedDwords(I);
    }
    unsigned Opcode = PushBeforeModifier ?
        AMDGPU::CF_ALU_PUSH_BEFORE : AMDGPU::CF_ALU;
    BuildMI(MBB, ClauseHead, MBB.findDebugLoc(ClauseHead), TII->get(Opcode))
    // We don't use the ADDR field until R600ControlFlowFinalizer pass, where
    // it is safe to assume it is 0. However if we always put 0 here, the ifcvt
    // pass may assume that identical ALU clause starter at the beginning of a 
    // true and false branch can be factorized which is not the case.
        .addImm(Address++) // ADDR
        .addImm(KCacheBanks.empty()?0:KCacheBanks[0].first) // KB0
        .addImm((KCacheBanks.size() < 2)?0:KCacheBanks[1].first) // KB1
        .addImm(KCacheBanks.empty()?0:2) // KM0
        .addImm((KCacheBanks.size() < 2)?0:2) // KM1
        .addImm(KCacheBanks.empty()?0:KCacheBanks[0].second) // KLINE0
        .addImm((KCacheBanks.size() < 2)?0:KCacheBanks[1].second) // KLINE1
        .addImm(AluInstCount) // COUNT
        .addImm(1); // Enabled
    return I;
  }

public:
  static char ID;
  R600EmitClauseMarkers() : MachineFunctionPass(ID), TII(nullptr), Address(0) {

    initializeR600EmitClauseMarkersPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    TII = static_cast<const R600InstrInfo *>(MF.getSubtarget().getInstrInfo());

    for (MachineFunction::iterator BB = MF.begin(), BB_E = MF.end();
                                                    BB != BB_E; ++BB) {
      MachineBasicBlock &MBB = *BB;
      MachineBasicBlock::iterator I = MBB.begin();
      if (I->getOpcode() == AMDGPU::CF_ALU)
        continue; // BB was already parsed
      for (MachineBasicBlock::iterator E = MBB.end(); I != E;) {
        if (isALU(I))
          I = MakeALUClause(MBB, I);
        else
          ++I;
      }
    }
    return false;
  }

  const char *getPassName() const override {
    return "R600 Emit Clause Markers Pass";
  }
};

char R600EmitClauseMarkers::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(R600EmitClauseMarkers, "emitclausemarkers",
                      "R600 Emit Clause Markters", false, false)
INITIALIZE_PASS_END(R600EmitClauseMarkers, "emitclausemarkers",
                      "R600 Emit Clause Markters", false, false)

llvm::FunctionPass *llvm::createR600EmitClauseMarkers() {
  return new R600EmitClauseMarkers();
}

