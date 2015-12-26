//===--- HexagonGenMux.cpp ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// During instruction selection, MUX instructions are generated for
// conditional assignments. Since such assignments often present an
// opportunity to predicate instructions, HexagonExpandCondsets
// expands MUXes into pairs of conditional transfers, and then proceeds
// with predication of the producers/consumers of the registers involved.
// This happens after exiting from the SSA form, but before the machine
// instruction scheduler. After the scheduler and after the register
// allocation there can be cases of pairs of conditional transfers
// resulting from a MUX where neither of them was further predicated. If
// these transfers are now placed far enough from the instruction defining
// the predicate register, they cannot use the .new form. In such cases it
// is better to collapse them back to a single MUX instruction.

#define DEBUG_TYPE "hexmux"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "HexagonTargetMachine.h"

using namespace llvm;

namespace llvm {
  FunctionPass *createHexagonGenMux();
  void initializeHexagonGenMuxPass(PassRegistry& Registry);
}

namespace {
  class HexagonGenMux : public MachineFunctionPass {
  public:
    static char ID;
    HexagonGenMux() : MachineFunctionPass(ID), HII(0), HRI(0) {
      initializeHexagonGenMuxPass(*PassRegistry::getPassRegistry());
    }
    const char *getPassName() const override {
      return "Hexagon generate mux instructions";
    }
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    const HexagonInstrInfo *HII;
    const HexagonRegisterInfo *HRI;

    struct CondsetInfo {
      unsigned PredR;
      unsigned TrueX, FalseX;
      CondsetInfo() : PredR(0), TrueX(UINT_MAX), FalseX(UINT_MAX) {}
    };
    struct DefUseInfo {
      BitVector Defs, Uses;
      DefUseInfo() : Defs(), Uses() {}
      DefUseInfo(const BitVector &D, const BitVector &U) : Defs(D), Uses(U) {}
    };
    struct MuxInfo {
      MachineBasicBlock::iterator At;
      unsigned DefR, PredR;
      MachineOperand *SrcT, *SrcF;
      MachineInstr *Def1, *Def2;
      MuxInfo(MachineBasicBlock::iterator It, unsigned DR, unsigned PR,
            MachineOperand *TOp, MachineOperand *FOp,
            MachineInstr *D1, MachineInstr *D2)
        : At(It), DefR(DR), PredR(PR), SrcT(TOp), SrcF(FOp), Def1(D1),
          Def2(D2) {}
    };
    typedef DenseMap<MachineInstr*,unsigned> InstrIndexMap;
    typedef DenseMap<unsigned,DefUseInfo> DefUseInfoMap;
    typedef SmallVector<MuxInfo,4> MuxInfoList;

    bool isRegPair(unsigned Reg) const {
      return Hexagon::DoubleRegsRegClass.contains(Reg);
    }
    void getSubRegs(unsigned Reg, BitVector &SRs) const;
    void expandReg(unsigned Reg, BitVector &Set) const;
    void getDefsUses(const MachineInstr *MI, BitVector &Defs,
          BitVector &Uses) const;
    void buildMaps(MachineBasicBlock &B, InstrIndexMap &I2X,
          DefUseInfoMap &DUM);
    bool isCondTransfer(unsigned Opc) const;
    unsigned getMuxOpcode(const MachineOperand &Src1,
          const MachineOperand &Src2) const;
    bool genMuxInBlock(MachineBasicBlock &B);
  };

  char HexagonGenMux::ID = 0;
}

INITIALIZE_PASS(HexagonGenMux, "hexagon-mux",
  "Hexagon generate mux instructions", false, false)


void HexagonGenMux::getSubRegs(unsigned Reg, BitVector &SRs) const {
  for (MCSubRegIterator I(Reg, HRI); I.isValid(); ++I)
    SRs[*I] = true;
}


void HexagonGenMux::expandReg(unsigned Reg, BitVector &Set) const {
  if (isRegPair(Reg))
    getSubRegs(Reg, Set);
  else
    Set[Reg] = true;
}


void HexagonGenMux::getDefsUses(const MachineInstr *MI, BitVector &Defs,
      BitVector &Uses) const {
  // First, get the implicit defs and uses for this instruction.
  unsigned Opc = MI->getOpcode();
  const MCInstrDesc &D = HII->get(Opc);
  if (const MCPhysReg *R = D.ImplicitDefs)
    while (*R)
      expandReg(*R++, Defs);
  if (const MCPhysReg *R = D.ImplicitUses)
    while (*R)
      expandReg(*R++, Uses);

  // Look over all operands, and collect explicit defs and uses.
  for (ConstMIOperands Mo(MI); Mo.isValid(); ++Mo) {
    if (!Mo->isReg() || Mo->isImplicit())
      continue;
    unsigned R = Mo->getReg();
    BitVector &Set = Mo->isDef() ? Defs : Uses;
    expandReg(R, Set);
  }
}


void HexagonGenMux::buildMaps(MachineBasicBlock &B, InstrIndexMap &I2X,
      DefUseInfoMap &DUM) {
  unsigned Index = 0;
  unsigned NR = HRI->getNumRegs();
  BitVector Defs(NR), Uses(NR);

  for (MachineBasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    MachineInstr *MI = &*I;
    I2X.insert(std::make_pair(MI, Index));
    Defs.reset();
    Uses.reset();
    getDefsUses(MI, Defs, Uses);
    DUM.insert(std::make_pair(Index, DefUseInfo(Defs, Uses)));
    Index++;
  }
}


bool HexagonGenMux::isCondTransfer(unsigned Opc) const {
  switch (Opc) {
    case Hexagon::A2_tfrt:
    case Hexagon::A2_tfrf:
    case Hexagon::C2_cmoveit:
    case Hexagon::C2_cmoveif:
      return true;
  }
  return false;
}


unsigned HexagonGenMux::getMuxOpcode(const MachineOperand &Src1,
      const MachineOperand &Src2) const {
  bool IsReg1 = Src1.isReg(), IsReg2 = Src2.isReg();
  if (IsReg1)
    return IsReg2 ? Hexagon::C2_mux : Hexagon::C2_muxir;
  if (IsReg2)
    return Hexagon::C2_muxri;

  // Neither is a register. The first source is extendable, but the second
  // is not (s8).
  if (Src2.isImm() && isInt<8>(Src2.getImm()))
    return Hexagon::C2_muxii;

  return 0;
}


bool HexagonGenMux::genMuxInBlock(MachineBasicBlock &B) {
  bool Changed = false;
  InstrIndexMap I2X;
  DefUseInfoMap DUM;
  buildMaps(B, I2X, DUM);

  typedef DenseMap<unsigned,CondsetInfo> CondsetMap;
  CondsetMap CM;
  MuxInfoList ML;

  MachineBasicBlock::iterator NextI, End = B.end();
  for (MachineBasicBlock::iterator I = B.begin(); I != End; I = NextI) {
    MachineInstr *MI = &*I;
    NextI = std::next(I);
    unsigned Opc = MI->getOpcode();
    if (!isCondTransfer(Opc))
      continue;
    unsigned DR = MI->getOperand(0).getReg();
    if (isRegPair(DR))
      continue;

    unsigned PR = MI->getOperand(1).getReg();
    unsigned Idx = I2X.lookup(MI);
    CondsetMap::iterator F = CM.find(DR);
    bool IfTrue = HII->isPredicatedTrue(Opc);

    // If there is no record of a conditional transfer for this register,
    // or the predicate register differs, create a new record for it.
    if (F != CM.end() && F->second.PredR != PR) {
      CM.erase(F);
      F = CM.end();
    }
    if (F == CM.end()) {
      auto It = CM.insert(std::make_pair(DR, CondsetInfo()));
      F = It.first;
      F->second.PredR = PR;
    }
    CondsetInfo &CI = F->second;
    if (IfTrue)
      CI.TrueX = Idx;
    else
      CI.FalseX = Idx;
    if (CI.TrueX == UINT_MAX || CI.FalseX == UINT_MAX)
      continue;

    // There is now a complete definition of DR, i.e. we have the predicate
    // register, the definition if-true, and definition if-false.

    // First, check if both definitions are far enough from the definition
    // of the predicate register.
    unsigned MinX = std::min(CI.TrueX, CI.FalseX);
    unsigned MaxX = std::max(CI.TrueX, CI.FalseX);
    unsigned SearchX = (MaxX > 4) ? MaxX-4 : 0;
    bool NearDef = false;
    for (unsigned X = SearchX; X < MaxX; ++X) {
      const DefUseInfo &DU = DUM.lookup(X);
      if (!DU.Defs[PR])
        continue;
      NearDef = true;
      break;
    }
    if (NearDef)
      continue;

    // The predicate register is not defined in the last few instructions.
    // Check if the conversion to MUX is possible (either "up", i.e. at the
    // place of the earlier partial definition, or "down", where the later
    // definition is located). Examine all defs and uses between these two
    // definitions.
    // SR1, SR2 - source registers from the first and the second definition.
    MachineBasicBlock::iterator It1 = B.begin(), It2 = B.begin();
    std::advance(It1, MinX);
    std::advance(It2, MaxX);
    MachineInstr *Def1 = It1, *Def2 = It2;
    MachineOperand *Src1 = &Def1->getOperand(2), *Src2 = &Def2->getOperand(2);
    unsigned SR1 = Src1->isReg() ? Src1->getReg() : 0;
    unsigned SR2 = Src2->isReg() ? Src2->getReg() : 0;
    bool Failure = false, CanUp = true, CanDown = true;
    for (unsigned X = MinX+1; X < MaxX; X++) {
      const DefUseInfo &DU = DUM.lookup(X);
      if (DU.Defs[PR] || DU.Defs[DR] || DU.Uses[DR]) {
        Failure = true;
        break;
      }
      if (CanDown && DU.Defs[SR1])
        CanDown = false;
      if (CanUp && DU.Defs[SR2])
        CanUp = false;
    }
    if (Failure || (!CanUp && !CanDown))
      continue;

    MachineOperand *SrcT = (MinX == CI.TrueX) ? Src1 : Src2;
    MachineOperand *SrcF = (MinX == CI.FalseX) ? Src1 : Src2;
    // Prefer "down", since this will move the MUX farther away from the
    // predicate definition.
    MachineBasicBlock::iterator At = CanDown ? Def2 : Def1;
    ML.push_back(MuxInfo(At, DR, PR, SrcT, SrcF, Def1, Def2));
  }

  for (unsigned I = 0, N = ML.size(); I < N; ++I) {
    MuxInfo &MX = ML[I];
    MachineBasicBlock &B = *MX.At->getParent();
    DebugLoc DL = MX.At->getDebugLoc();
    unsigned MxOpc = getMuxOpcode(*MX.SrcT, *MX.SrcF);
    if (!MxOpc)
      continue;
    BuildMI(B, MX.At, DL, HII->get(MxOpc), MX.DefR)
      .addReg(MX.PredR)
      .addOperand(*MX.SrcT)
      .addOperand(*MX.SrcF);
    B.erase(MX.Def1);
    B.erase(MX.Def2);
    Changed = true;
  }

  return Changed;
}

bool HexagonGenMux::runOnMachineFunction(MachineFunction &MF) {
  HII = MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  HRI = MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  bool Changed = false;
  for (auto &I : MF)
    Changed |= genMuxInBlock(I);
  return Changed;
}

FunctionPass *llvm::createHexagonGenMux() {
  return new HexagonGenMux();
}

