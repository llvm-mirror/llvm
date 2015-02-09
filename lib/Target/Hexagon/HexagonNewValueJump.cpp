//===----- HexagonNewValueJump.cpp - Hexagon Backend New Value Jump -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements NewValueJump pass in Hexagon.
// Ideally, we should merge this as a Peephole pass prior to register
// allocation, but because we have a spill in between the feeder and new value
// jump instructions, we are forced to write after register allocation.
// Having said that, we should re-attempt to pull this earlier at some point
// in future.

// The basic approach looks for sequence of predicated jump, compare instruciton
// that genereates the predicate and, the feeder to the predicate. Once it finds
// all, it collapses compare and jump instruction into a new valu jump
// intstructions.
//
//
//===----------------------------------------------------------------------===//
#include "llvm/PassSupport.h"
#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include <map>
using namespace llvm;

#define DEBUG_TYPE "hexagon-nvj"

STATISTIC(NumNVJGenerated, "Number of New Value Jump Instructions created");

static cl::opt<int>
DbgNVJCount("nvj-count", cl::init(-1), cl::Hidden, cl::desc(
  "Maximum number of predicated jumps to be converted to New Value Jump"));

static cl::opt<bool> DisableNewValueJumps("disable-nvjump", cl::Hidden,
    cl::ZeroOrMore, cl::init(false),
    cl::desc("Disable New Value Jumps"));

namespace llvm {
  void initializeHexagonNewValueJumpPass(PassRegistry&);
}


namespace {
  struct HexagonNewValueJump : public MachineFunctionPass {
    const HexagonInstrInfo    *QII;
    const HexagonRegisterInfo *QRI;

  public:
    static char ID;

    HexagonNewValueJump() : MachineFunctionPass(ID) {
      initializeHexagonNewValueJumpPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineBranchProbabilityInfo>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    const char *getPassName() const override {
      return "Hexagon NewValueJump";
    }

    bool runOnMachineFunction(MachineFunction &Fn) override;

  private:
    /// \brief A handle to the branch probability pass.
    const MachineBranchProbabilityInfo *MBPI;

  };

} // end of anonymous namespace

char HexagonNewValueJump::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonNewValueJump, "hexagon-nvj",
                      "Hexagon NewValueJump", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_END(HexagonNewValueJump, "hexagon-nvj",
                    "Hexagon NewValueJump", false, false)


// We have identified this II could be feeder to NVJ,
// verify that it can be.
static bool canBeFeederToNewValueJump(const HexagonInstrInfo *QII,
                                      const TargetRegisterInfo *TRI,
                                      MachineBasicBlock::iterator II,
                                      MachineBasicBlock::iterator end,
                                      MachineBasicBlock::iterator skip,
                                      MachineFunction &MF) {

  // Predicated instruction can not be feeder to NVJ.
  if (QII->isPredicated(II))
    return false;

  // Bail out if feederReg is a paired register (double regs in
  // our case). One would think that we can check to see if a given
  // register cmpReg1 or cmpReg2 is a sub register of feederReg
  // using -- if (QRI->isSubRegister(feederReg, cmpReg1) logic
  // before the callsite of this function
  // But we can not as it comes in the following fashion.
  //    %D0<def> = Hexagon_S2_lsr_r_p %D0<kill>, %R2<kill>
  //    %R0<def> = KILL %R0, %D0<imp-use,kill>
  //    %P0<def> = CMPEQri %R0<kill>, 0
  // Hence, we need to check if it's a KILL instruction.
  if (II->getOpcode() == TargetOpcode::KILL)
    return false;


  // Make sure there there is no 'def' or 'use' of any of the uses of
  // feeder insn between it's definition, this MI and jump, jmpInst
  // skipping compare, cmpInst.
  // Here's the example.
  //    r21=memub(r22+r24<<#0)
  //    p0 = cmp.eq(r21, #0)
  //    r4=memub(r3+r21<<#0)
  //    if (p0.new) jump:t .LBB29_45
  // Without this check, it will be converted into
  //    r4=memub(r3+r21<<#0)
  //    r21=memub(r22+r24<<#0)
  //    p0 = cmp.eq(r21, #0)
  //    if (p0.new) jump:t .LBB29_45
  // and result WAR hazards if converted to New Value Jump.

  for (unsigned i = 0; i < II->getNumOperands(); ++i) {
    if (II->getOperand(i).isReg() &&
        (II->getOperand(i).isUse() || II->getOperand(i).isDef())) {
      MachineBasicBlock::iterator localII = II;
      ++localII;
      unsigned Reg = II->getOperand(i).getReg();
      for (MachineBasicBlock::iterator localBegin = localII;
                        localBegin != end; ++localBegin) {
        if (localBegin == skip ) continue;
        // Check for Subregisters too.
        if (localBegin->modifiesRegister(Reg, TRI) ||
            localBegin->readsRegister(Reg, TRI))
          return false;
      }
    }
  }
  return true;
}

// These are the common checks that need to performed
// to determine if
// 1. compare instruction can be moved before jump.
// 2. feeder to the compare instruction can be moved before jump.
static bool commonChecksToProhibitNewValueJump(bool afterRA,
                          MachineBasicBlock::iterator MII) {

  // If store in path, bail out.
  if (MII->getDesc().mayStore())
    return false;

  // if call in path, bail out.
  if (MII->getOpcode() == Hexagon::J2_call)
    return false;

  // if NVJ is running prior to RA, do the following checks.
  if (!afterRA) {
    // The following Target Opcode instructions are spurious
    // to new value jump. If they are in the path, bail out.
    // KILL sets kill flag on the opcode. It also sets up a
    // single register, out of pair.
    //    %D0<def> = Hexagon_S2_lsr_r_p %D0<kill>, %R2<kill>
    //    %R0<def> = KILL %R0, %D0<imp-use,kill>
    //    %P0<def> = CMPEQri %R0<kill>, 0
    // PHI can be anything after RA.
    // COPY can remateriaze things in between feeder, compare and nvj.
    if (MII->getOpcode() == TargetOpcode::KILL ||
        MII->getOpcode() == TargetOpcode::PHI  ||
        MII->getOpcode() == TargetOpcode::COPY)
      return false;

    // The following pseudo Hexagon instructions sets "use" and "def"
    // of registers by individual passes in the backend. At this time,
    // we don't know the scope of usage and definitions of these
    // instructions.
    if (MII->getOpcode() == Hexagon::TFR_condset_ii ||
        MII->getOpcode() == Hexagon::TFR_condset_ri ||
        MII->getOpcode() == Hexagon::TFR_condset_ir ||
        MII->getOpcode() == Hexagon::LDriw_pred     ||
        MII->getOpcode() == Hexagon::STriw_pred)
      return false;
  }

  return true;
}

static bool canCompareBeNewValueJump(const HexagonInstrInfo *QII,
                                     const TargetRegisterInfo *TRI,
                                     MachineBasicBlock::iterator II,
                                     unsigned pReg,
                                     bool secondReg,
                                     bool optLocation,
                                     MachineBasicBlock::iterator end,
                                     MachineFunction &MF) {

  MachineInstr *MI = II;

  // If the second operand of the compare is an imm, make sure it's in the
  // range specified by the arch.
  if (!secondReg) {
    int64_t v = MI->getOperand(2).getImm();

    if (!(isUInt<5>(v) ||
         ((MI->getOpcode() == Hexagon::C2_cmpeqi ||
           MI->getOpcode() == Hexagon::C2_cmpgti) &&
          (v == -1))))
      return false;
  }

  unsigned cmpReg1, cmpOp2 = 0; // cmpOp2 assignment silences compiler warning.
  cmpReg1 = MI->getOperand(1).getReg();

  if (secondReg) {
    cmpOp2 = MI->getOperand(2).getReg();

    // Make sure that that second register is not from COPY
    // At machine code level, we don't need this, but if we decide
    // to move new value jump prior to RA, we would be needing this.
    MachineRegisterInfo &MRI = MF.getRegInfo();
    if (secondReg && !TargetRegisterInfo::isPhysicalRegister(cmpOp2)) {
      MachineInstr *def = MRI.getVRegDef(cmpOp2);
      if (def->getOpcode() == TargetOpcode::COPY)
        return false;
    }
  }

  // Walk the instructions after the compare (predicate def) to the jump,
  // and satisfy the following conditions.
  ++II ;
  for (MachineBasicBlock::iterator localII = II; localII != end;
       ++localII) {

    // Check 1.
    // If "common" checks fail, bail out.
    if (!commonChecksToProhibitNewValueJump(optLocation, localII))
      return false;

    // Check 2.
    // If there is a def or use of predicate (result of compare), bail out.
    if (localII->modifiesRegister(pReg, TRI) ||
        localII->readsRegister(pReg, TRI))
      return false;

    // Check 3.
    // If there is a def of any of the use of the compare (operands of compare),
    // bail out.
    // Eg.
    //    p0 = cmp.eq(r2, r0)
    //    r2 = r4
    //    if (p0.new) jump:t .LBB28_3
    if (localII->modifiesRegister(cmpReg1, TRI) ||
        (secondReg && localII->modifiesRegister(cmpOp2, TRI)))
      return false;
  }
  return true;
}

// Given a compare operator, return a matching New Value Jump
// compare operator. Make sure that MI here is included in
// HexagonInstrInfo.cpp::isNewValueJumpCandidate
static unsigned getNewValueJumpOpcode(MachineInstr *MI, int reg,
                                      bool secondRegNewified,
                                      MachineBasicBlock *jmpTarget,
                                      const MachineBranchProbabilityInfo
                                      *MBPI) {
  bool taken = false;
  MachineBasicBlock *Src = MI->getParent();
  const BranchProbability Prediction =
    MBPI->getEdgeProbability(Src, jmpTarget);

  if (Prediction >= BranchProbability(1,2))
    taken = true;

  switch (MI->getOpcode()) {
    case Hexagon::C2_cmpeq:
      return taken ? Hexagon::J4_cmpeq_t_jumpnv_t
                   : Hexagon::J4_cmpeq_t_jumpnv_nt;

    case Hexagon::C2_cmpeqi: {
      if (reg >= 0)
        return taken ? Hexagon::J4_cmpeqi_t_jumpnv_t
                     : Hexagon::J4_cmpeqi_t_jumpnv_nt;
      else
        return taken ? Hexagon::J4_cmpeqn1_t_jumpnv_t
                     : Hexagon::J4_cmpeqn1_t_jumpnv_nt;
    }

    case Hexagon::C2_cmpgt: {
      if (secondRegNewified)
        return taken ? Hexagon::J4_cmplt_t_jumpnv_t
                     : Hexagon::J4_cmplt_t_jumpnv_nt;
      else
        return taken ? Hexagon::J4_cmpgt_t_jumpnv_t
                     : Hexagon::J4_cmpgt_t_jumpnv_nt;
    }

    case Hexagon::C2_cmpgti: {
      if (reg >= 0)
        return taken ? Hexagon::J4_cmpgti_t_jumpnv_t
                     : Hexagon::J4_cmpgti_t_jumpnv_nt;
      else
        return taken ? Hexagon::J4_cmpgtn1_t_jumpnv_t
                     : Hexagon::J4_cmpgtn1_t_jumpnv_nt;
    }

    case Hexagon::C2_cmpgtu: {
      if (secondRegNewified)
        return taken ? Hexagon::J4_cmpltu_t_jumpnv_t
                     : Hexagon::J4_cmpltu_t_jumpnv_nt;
      else
        return taken ? Hexagon::J4_cmpgtu_t_jumpnv_t
                     : Hexagon::J4_cmpgtu_t_jumpnv_nt;
    }

    case Hexagon::C2_cmpgtui:
      return taken ? Hexagon::J4_cmpgtui_t_jumpnv_t
                   : Hexagon::J4_cmpgtui_t_jumpnv_nt;

    default:
       llvm_unreachable("Could not find matching New Value Jump instruction.");
  }
  // return *some value* to avoid compiler warning
  return 0;
}

bool HexagonNewValueJump::runOnMachineFunction(MachineFunction &MF) {

  DEBUG(dbgs() << "********** Hexagon New Value Jump **********\n"
               << "********** Function: "
               << MF.getName() << "\n");

  // If we move NewValueJump before register allocation we'll need live variable
  // analysis here too.

  QII = static_cast<const HexagonInstrInfo *>(MF.getSubtarget().getInstrInfo());
  QRI = static_cast<const HexagonRegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  MBPI = &getAnalysis<MachineBranchProbabilityInfo>();

  if (!QRI->Subtarget.hasV4TOps() ||
      DisableNewValueJumps) {
    return false;
  }

  int nvjCount = DbgNVJCount;
  int nvjGenerated = 0;

  // Loop through all the bb's of the function
  for (MachineFunction::iterator MBBb = MF.begin(), MBBe = MF.end();
        MBBb != MBBe; ++MBBb) {
    MachineBasicBlock* MBB = MBBb;

    DEBUG(dbgs() << "** dumping bb ** "
                 << MBB->getNumber() << "\n");
    DEBUG(MBB->dump());
    DEBUG(dbgs() << "\n" << "********** dumping instr bottom up **********\n");
    bool foundJump    = false;
    bool foundCompare = false;
    bool invertPredicate = false;
    unsigned predReg = 0; // predicate reg of the jump.
    unsigned cmpReg1 = 0;
    int cmpOp2 = 0;
    bool MO1IsKill = false;
    bool MO2IsKill = false;
    MachineBasicBlock::iterator jmpPos;
    MachineBasicBlock::iterator cmpPos;
    MachineInstr *cmpInstr = nullptr, *jmpInstr = nullptr;
    MachineBasicBlock *jmpTarget = nullptr;
    bool afterRA = false;
    bool isSecondOpReg = false;
    bool isSecondOpNewified = false;
    // Traverse the basic block - bottom up
    for (MachineBasicBlock::iterator MII = MBB->end(), E = MBB->begin();
             MII != E;) {
      MachineInstr *MI = --MII;
      if (MI->isDebugValue()) {
        continue;
      }

      if ((nvjCount == 0) || (nvjCount > -1 && nvjCount <= nvjGenerated))
        break;

      DEBUG(dbgs() << "Instr: "; MI->dump(); dbgs() << "\n");

      if (!foundJump &&
         (MI->getOpcode() == Hexagon::J2_jumpt ||
          MI->getOpcode() == Hexagon::J2_jumpf ||
          MI->getOpcode() == Hexagon::J2_jumptnewpt ||
          MI->getOpcode() == Hexagon::J2_jumptnew ||
          MI->getOpcode() == Hexagon::J2_jumpfnewpt ||
          MI->getOpcode() == Hexagon::J2_jumpfnew)) {
        // This is where you would insert your compare and
        // instr that feeds compare
        jmpPos = MII;
        jmpInstr = MI;
        predReg = MI->getOperand(0).getReg();
        afterRA = TargetRegisterInfo::isPhysicalRegister(predReg);

        // If ifconverter had not messed up with the kill flags of the
        // operands, the following check on the kill flag would suffice.
        // if(!jmpInstr->getOperand(0).isKill()) break;

        // This predicate register is live out out of BB
        // this would only work if we can actually use Live
        // variable analysis on phy regs - but LLVM does not
        // provide LV analysis on phys regs.
        //if(LVs.isLiveOut(predReg, *MBB)) break;

        // Get all the successors of this block - which will always
        // be 2. Check if the predicate register is live in in those
        // successor. If yes, we can not delete the predicate -
        // I am doing this only because LLVM does not provide LiveOut
        // at the BB level.
        bool predLive = false;
        for (MachineBasicBlock::const_succ_iterator SI = MBB->succ_begin(),
                            SIE = MBB->succ_end(); SI != SIE; ++SI) {
          MachineBasicBlock* succMBB = *SI;
         if (succMBB->isLiveIn(predReg)) {
            predLive = true;
          }
        }
        if (predLive)
          break;

        jmpTarget = MI->getOperand(1).getMBB();
        foundJump = true;
        if (MI->getOpcode() == Hexagon::J2_jumpf ||
            MI->getOpcode() == Hexagon::J2_jumpfnewpt ||
            MI->getOpcode() == Hexagon::J2_jumpfnew) {
          invertPredicate = true;
        }
        continue;
      }

      // No new value jump if there is a barrier. A barrier has to be in its
      // own packet. A barrier has zero operands. We conservatively bail out
      // here if we see any instruction with zero operands.
      if (foundJump && MI->getNumOperands() == 0)
        break;

      if (foundJump &&
         !foundCompare &&
          MI->getOperand(0).isReg() &&
          MI->getOperand(0).getReg() == predReg) {

        // Not all compares can be new value compare. Arch Spec: 7.6.1.1
        if (QII->isNewValueJumpCandidate(MI)) {

          assert((MI->getDesc().isCompare()) &&
              "Only compare instruction can be collapsed into New Value Jump");
          isSecondOpReg = MI->getOperand(2).isReg();

          if (!canCompareBeNewValueJump(QII, QRI, MII, predReg, isSecondOpReg,
                                        afterRA, jmpPos, MF))
            break;

          cmpInstr = MI;
          cmpPos = MII;
          foundCompare = true;

          // We need cmpReg1 and cmpOp2(imm or reg) while building
          // new value jump instruction.
          cmpReg1 = MI->getOperand(1).getReg();
          if (MI->getOperand(1).isKill())
            MO1IsKill = true;

          if (isSecondOpReg) {
            cmpOp2 = MI->getOperand(2).getReg();
            if (MI->getOperand(2).isKill())
              MO2IsKill = true;
          } else
            cmpOp2 = MI->getOperand(2).getImm();
          continue;
        }
      }

      if (foundCompare && foundJump) {

        // If "common" checks fail, bail out on this BB.
        if (!commonChecksToProhibitNewValueJump(afterRA, MII))
          break;

        bool foundFeeder = false;
        MachineBasicBlock::iterator feederPos = MII;
        if (MI->getOperand(0).isReg() &&
            MI->getOperand(0).isDef() &&
           (MI->getOperand(0).getReg() == cmpReg1 ||
            (isSecondOpReg &&
             MI->getOperand(0).getReg() == (unsigned) cmpOp2))) {

          unsigned feederReg = MI->getOperand(0).getReg();

          // First try to see if we can get the feeder from the first operand
          // of the compare. If we can not, and if secondOpReg is true
          // (second operand of the compare is also register), try that one.
          // TODO: Try to come up with some heuristic to figure out which
          // feeder would benefit.

          if (feederReg == cmpReg1) {
            if (!canBeFeederToNewValueJump(QII, QRI, MII, jmpPos, cmpPos, MF)) {
              if (!isSecondOpReg)
                break;
              else
                continue;
            } else
              foundFeeder = true;
          }

          if (!foundFeeder &&
               isSecondOpReg &&
               feederReg == (unsigned) cmpOp2)
            if (!canBeFeederToNewValueJump(QII, QRI, MII, jmpPos, cmpPos, MF))
              break;

          if (isSecondOpReg) {
            // In case of CMPLT, or CMPLTU, or EQ with the second register
            // to newify, swap the operands.
            if (cmpInstr->getOpcode() == Hexagon::C2_cmpeq &&
                                     feederReg == (unsigned) cmpOp2) {
              unsigned tmp = cmpReg1;
              bool tmpIsKill = MO1IsKill;
              cmpReg1 = cmpOp2;
              MO1IsKill = MO2IsKill;
              cmpOp2 = tmp;
              MO2IsKill = tmpIsKill;
            }

            // Now we have swapped the operands, all we need to check is,
            // if the second operand (after swap) is the feeder.
            // And if it is, make a note.
            if (feederReg == (unsigned)cmpOp2)
              isSecondOpNewified = true;
          }

          // Now that we are moving feeder close the jump,
          // make sure we are respecting the kill values of
          // the operands of the feeder.

          bool updatedIsKill = false;
          for (unsigned i = 0; i < MI->getNumOperands(); i++) {
            MachineOperand &MO = MI->getOperand(i);
            if (MO.isReg() && MO.isUse()) {
              unsigned feederReg = MO.getReg();
              for (MachineBasicBlock::iterator localII = feederPos,
                   end = jmpPos; localII != end; localII++) {
                MachineInstr *localMI = localII;
                for (unsigned j = 0; j < localMI->getNumOperands(); j++) {
                  MachineOperand &localMO = localMI->getOperand(j);
                  if (localMO.isReg() && localMO.isUse() &&
                      localMO.isKill() && feederReg == localMO.getReg()) {
                    // We found that there is kill of a use register
                    // Set up a kill flag on the register
                    localMO.setIsKill(false);
                    MO.setIsKill();
                    updatedIsKill = true;
                    break;
                  }
                }
                if (updatedIsKill) break;
              }
            }
            if (updatedIsKill) break;
          }

          MBB->splice(jmpPos, MI->getParent(), MI);
          MBB->splice(jmpPos, MI->getParent(), cmpInstr);
          DebugLoc dl = MI->getDebugLoc();
          MachineInstr *NewMI;

           assert((QII->isNewValueJumpCandidate(cmpInstr)) &&
                      "This compare is not a New Value Jump candidate.");
          unsigned opc = getNewValueJumpOpcode(cmpInstr, cmpOp2,
                                               isSecondOpNewified,
                                               jmpTarget, MBPI);
          if (invertPredicate)
            opc = QII->getInvertedPredicatedOpcode(opc);

          if (isSecondOpReg)
            NewMI = BuildMI(*MBB, jmpPos, dl,
                                  QII->get(opc))
                                    .addReg(cmpReg1, getKillRegState(MO1IsKill))
                                    .addReg(cmpOp2, getKillRegState(MO2IsKill))
                                    .addMBB(jmpTarget);

          else if ((cmpInstr->getOpcode() == Hexagon::C2_cmpeqi ||
                    cmpInstr->getOpcode() == Hexagon::C2_cmpgti) &&
                    cmpOp2 == -1 )
            // Corresponding new-value compare jump instructions don't have the
            // operand for -1 immediate value.
            NewMI = BuildMI(*MBB, jmpPos, dl,
                                  QII->get(opc))
                                    .addReg(cmpReg1, getKillRegState(MO1IsKill))
                                    .addMBB(jmpTarget);

          else
            NewMI = BuildMI(*MBB, jmpPos, dl,
                                  QII->get(opc))
                                    .addReg(cmpReg1, getKillRegState(MO1IsKill))
                                    .addImm(cmpOp2)
                                    .addMBB(jmpTarget);

          assert(NewMI && "New Value Jump Instruction Not created!");
          (void)NewMI;
          if (cmpInstr->getOperand(0).isReg() &&
              cmpInstr->getOperand(0).isKill())
            cmpInstr->getOperand(0).setIsKill(false);
          if (cmpInstr->getOperand(1).isReg() &&
              cmpInstr->getOperand(1).isKill())
            cmpInstr->getOperand(1).setIsKill(false);
          cmpInstr->eraseFromParent();
          jmpInstr->eraseFromParent();
          ++nvjGenerated;
          ++NumNVJGenerated;
          break;
        }
      }
    }
  }

  return true;

}

FunctionPass *llvm::createHexagonNewValueJump() {
  return new HexagonNewValueJump();
}
