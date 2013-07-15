//=== A15SDOptimizerPass.cpp - Optimize DPR and SPR register accesses on A15==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The Cortex-A15 processor employs a tracking scheme in its register renaming
// in order to process each instruction's micro-ops speculatively and
// out-of-order with appropriate forwarding. The ARM architecture allows VFP
// instructions to read and write 32-bit S-registers.  Each S-register
// corresponds to one half (upper or lower) of an overlaid 64-bit D-register.
//
// There are several instruction patterns which can be used to provide this
// capability which can provide higher performance than other, potentially more
// direct patterns, specifically around when one micro-op reads a D-register
// operand that has recently been written as one or more S-register results.
//
// This file defines a pre-regalloc pass which looks for SPR producers which
// are going to be used by a DPR (or QPR) consumers and creates the more
// optimized access pattern.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "a15-sd-optimizer"
#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMSubtarget.h"
#include "ARMISelLowering.h"
#include "ARMTargetMachine.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetRegisterInfo.h"

#include <set>

using namespace llvm;

namespace {
  struct A15SDOptimizer : public MachineFunctionPass {
    static char ID;
    A15SDOptimizer() : MachineFunctionPass(ID) {}

    virtual bool runOnMachineFunction(MachineFunction &Fn);

    virtual const char *getPassName() const {
      return "ARM A15 S->D optimizer";
    }

  private:
    const ARMBaseInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo *MRI;

    bool runOnInstruction(MachineInstr *MI);

    //
    // Instruction builder helpers
    //
    unsigned createDupLane(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator InsertBefore,
                           DebugLoc DL,
                           unsigned Reg, unsigned Lane,
                           bool QPR=false);

    unsigned createExtractSubreg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator InsertBefore,
                                 DebugLoc DL,
                                 unsigned DReg, unsigned Lane,
                                 const TargetRegisterClass *TRC);

    unsigned createVExt(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator InsertBefore,
                        DebugLoc DL,
                        unsigned Ssub0, unsigned Ssub1);

    unsigned createRegSequence(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator InsertBefore,
                               DebugLoc DL,
                               unsigned Reg1, unsigned Reg2);

    unsigned createInsertSubreg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator InsertBefore,
                                DebugLoc DL, unsigned DReg, unsigned Lane,
                                unsigned ToInsert);

    unsigned createImplicitDef(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator InsertBefore,
                               DebugLoc DL);
    
    //
    // Various property checkers
    //
    bool usesRegClass(MachineOperand &MO, const TargetRegisterClass *TRC);
    bool hasPartialWrite(MachineInstr *MI);
    SmallVector<unsigned, 8> getReadDPRs(MachineInstr *MI);
    unsigned getDPRLaneFromSPR(unsigned SReg);

    //
    // Methods used for getting the definitions of partial registers
    //

    MachineInstr *elideCopies(MachineInstr *MI);
    void elideCopiesAndPHIs(MachineInstr *MI,
                            SmallVectorImpl<MachineInstr*> &Outs);

    //
    // Pattern optimization methods
    //
    unsigned optimizeAllLanesPattern(MachineInstr *MI, unsigned Reg);
    unsigned optimizeSDPattern(MachineInstr *MI);
    unsigned getPrefSPRLane(unsigned SReg);

    //
    // Sanitizing method - used to make sure if don't leave dead code around.
    //
    void eraseInstrWithNoUses(MachineInstr *MI);

    //
    // A map used to track the changes done by this pass.
    //
    std::map<MachineInstr*, unsigned> Replacements;
    std::set<MachineInstr *> DeadInstr;
  };
  char A15SDOptimizer::ID = 0;
} // end anonymous namespace

// Returns true if this is a use of a SPR register.
bool A15SDOptimizer::usesRegClass(MachineOperand &MO,
                                  const TargetRegisterClass *TRC) {
  if (!MO.isReg())
    return false;
  unsigned Reg = MO.getReg();

  if (TargetRegisterInfo::isVirtualRegister(Reg))
    return MRI->getRegClass(Reg)->hasSuperClassEq(TRC);
  else
    return TRC->contains(Reg);
}

unsigned A15SDOptimizer::getDPRLaneFromSPR(unsigned SReg) {
  unsigned DReg = TRI->getMatchingSuperReg(SReg, ARM::ssub_1,
                                           &ARM::DPRRegClass);
  if (DReg != ARM::NoRegister) return ARM::ssub_1;
  return ARM::ssub_0;
}

// Get the subreg type that is most likely to be coalesced
// for an SPR register that will be used in VDUP32d pseudo.
unsigned A15SDOptimizer::getPrefSPRLane(unsigned SReg) {
  if (!TRI->isVirtualRegister(SReg))
    return getDPRLaneFromSPR(SReg);

  MachineInstr *MI = MRI->getVRegDef(SReg);
  if (!MI) return ARM::ssub_0;
  MachineOperand *MO = MI->findRegisterDefOperand(SReg);

  assert(MO->isReg() && "Non register operand found!");
  if (!MO) return ARM::ssub_0;

  if (MI->isCopy() && usesRegClass(MI->getOperand(1),
                                    &ARM::SPRRegClass)) {
    SReg = MI->getOperand(1).getReg();
  }

  if (TargetRegisterInfo::isVirtualRegister(SReg)) {
    if (MO->getSubReg() == ARM::ssub_1) return ARM::ssub_1;
    return ARM::ssub_0;
  }
  return getDPRLaneFromSPR(SReg);
}

// MI is known to be dead. Figure out what instructions
// are also made dead by this and mark them for removal.
void A15SDOptimizer::eraseInstrWithNoUses(MachineInstr *MI) {
  SmallVector<MachineInstr *, 8> Front;
  DeadInstr.insert(MI);

  DEBUG(dbgs() << "Deleting base instruction " << *MI << "\n");
  Front.push_back(MI);

  while (Front.size() != 0) {
    MI = Front.back();
    Front.pop_back();

    // MI is already known to be dead. We need to see
    // if other instructions can also be removed.
    for (unsigned int i = 0; i < MI->getNumOperands(); ++i) {
      MachineOperand &MO = MI->getOperand(i);
      if ((!MO.isReg()) || (!MO.isUse()))
        continue;
      unsigned Reg = MO.getReg();
      if (!TRI->isVirtualRegister(Reg))
        continue;
      MachineOperand *Op = MI->findRegisterDefOperand(Reg);

      if (!Op)
        continue;

      MachineInstr *Def = Op->getParent();

      // We don't need to do anything if we have already marked
      // this instruction as being dead.
      if (DeadInstr.find(Def) != DeadInstr.end())
        continue;

      // Check if all the uses of this instruction are marked as
      // dead. If so, we can also mark this instruction as being
      // dead.
      bool IsDead = true;
      for (unsigned int j = 0; j < Def->getNumOperands(); ++j) {
        MachineOperand &MODef = Def->getOperand(j);
        if ((!MODef.isReg()) || (!MODef.isDef()))
          continue;
        unsigned DefReg = MODef.getReg();
        if (!TRI->isVirtualRegister(DefReg)) {
          IsDead = false;
          break;
        }
        for (MachineRegisterInfo::use_iterator II = MRI->use_begin(Reg),
                            EE = MRI->use_end();
                            II != EE; ++II) {
          // We don't care about self references.
          if (&*II == Def)
            continue;
          if (DeadInstr.find(&*II) == DeadInstr.end()) {
            IsDead = false;
            break;
          }
        }
      }

      if (!IsDead) continue;

      DEBUG(dbgs() << "Deleting instruction " << *Def << "\n");
      DeadInstr.insert(Def);
    }
  }
}

// Creates the more optimized patterns and generally does all the code
// transformations in this pass.
unsigned A15SDOptimizer::optimizeSDPattern(MachineInstr *MI) {
  if (MI->isCopy()) {
    return optimizeAllLanesPattern(MI, MI->getOperand(1).getReg());
  }

  if (MI->isInsertSubreg()) {
    unsigned DPRReg = MI->getOperand(1).getReg();
    unsigned SPRReg = MI->getOperand(2).getReg();

    if (TRI->isVirtualRegister(DPRReg) && TRI->isVirtualRegister(SPRReg)) {
      MachineInstr *DPRMI = MRI->getVRegDef(MI->getOperand(1).getReg());
      MachineInstr *SPRMI = MRI->getVRegDef(MI->getOperand(2).getReg());

      if (DPRMI && SPRMI) {
        // See if the first operand of this insert_subreg is IMPLICIT_DEF
        MachineInstr *ECDef = elideCopies(DPRMI);
        if (ECDef != 0 && ECDef->isImplicitDef()) {
          // Another corner case - if we're inserting something that is purely
          // a subreg copy of a DPR, just use that DPR.

          MachineInstr *EC = elideCopies(SPRMI);
          // Is it a subreg copy of ssub_0?
          if (EC && EC->isCopy() &&
              EC->getOperand(1).getSubReg() == ARM::ssub_0) {
            DEBUG(dbgs() << "Found a subreg copy: " << *SPRMI);

            // Find the thing we're subreg copying out of - is it of the same
            // regclass as DPRMI? (i.e. a DPR or QPR).
            unsigned FullReg = SPRMI->getOperand(1).getReg();
            const TargetRegisterClass *TRC =
              MRI->getRegClass(MI->getOperand(1).getReg());
            if (TRC->hasSuperClassEq(MRI->getRegClass(FullReg))) {
              DEBUG(dbgs() << "Subreg copy is compatible - returning ");
              DEBUG(dbgs() << PrintReg(FullReg) << "\n");
              eraseInstrWithNoUses(MI);
              return FullReg;
            }
          }

          return optimizeAllLanesPattern(MI, MI->getOperand(2).getReg());
        }
      }
    }
    return optimizeAllLanesPattern(MI, MI->getOperand(0).getReg());
  }

  if (MI->isRegSequence() && usesRegClass(MI->getOperand(1),
                                          &ARM::SPRRegClass)) {
    // See if all bar one of the operands are IMPLICIT_DEF and insert the
    // optimizer pattern accordingly.
    unsigned NumImplicit = 0, NumTotal = 0;
    unsigned NonImplicitReg = ~0U;

    for (unsigned I = 1; I < MI->getNumExplicitOperands(); ++I) {
      if (!MI->getOperand(I).isReg())
        continue;
      ++NumTotal;
      unsigned OpReg = MI->getOperand(I).getReg();

      if (!TRI->isVirtualRegister(OpReg))
        break;

      MachineInstr *Def = MRI->getVRegDef(OpReg);
      if (!Def)
        break;
      if (Def->isImplicitDef())
        ++NumImplicit;
      else
        NonImplicitReg = MI->getOperand(I).getReg();
    }

    if (NumImplicit == NumTotal - 1)
      return optimizeAllLanesPattern(MI, NonImplicitReg);
    else
      return optimizeAllLanesPattern(MI, MI->getOperand(0).getReg());
  }

  assert(0 && "Unhandled update pattern!");
  return 0;
}

// Return true if this MachineInstr inserts a scalar (SPR) value into
// a D or Q register.
bool A15SDOptimizer::hasPartialWrite(MachineInstr *MI) {
  // The only way we can do a partial register update is through a COPY,
  // INSERT_SUBREG or REG_SEQUENCE.
  if (MI->isCopy() && usesRegClass(MI->getOperand(1), &ARM::SPRRegClass))
    return true;

  if (MI->isInsertSubreg() && usesRegClass(MI->getOperand(2),
                                           &ARM::SPRRegClass))
    return true;

  if (MI->isRegSequence() && usesRegClass(MI->getOperand(1), &ARM::SPRRegClass))
    return true;

  return false;
}

// Looks through full copies to get the instruction that defines the input
// operand for MI.
MachineInstr *A15SDOptimizer::elideCopies(MachineInstr *MI) {
  if (!MI->isFullCopy())
    return MI;
  if (!TRI->isVirtualRegister(MI->getOperand(1).getReg()))
    return NULL;
  MachineInstr *Def = MRI->getVRegDef(MI->getOperand(1).getReg());
  if (!Def)
    return NULL;
  return elideCopies(Def);
}

// Look through full copies and PHIs to get the set of non-copy MachineInstrs
// that can produce MI.
void A15SDOptimizer::elideCopiesAndPHIs(MachineInstr *MI,
                                        SmallVectorImpl<MachineInstr*> &Outs) {
   // Looking through PHIs may create loops so we need to track what
   // instructions we have visited before.
   std::set<MachineInstr *> Reached;
   SmallVector<MachineInstr *, 8> Front;
   Front.push_back(MI);
   while (Front.size() != 0) {
     MI = Front.back();
     Front.pop_back();

     // If we have already explored this MachineInstr, ignore it.
     if (Reached.find(MI) != Reached.end())
       continue;
     Reached.insert(MI);
     if (MI->isPHI()) {
       for (unsigned I = 1, E = MI->getNumOperands(); I != E; I += 2) {
         unsigned Reg = MI->getOperand(I).getReg();
         if (!TRI->isVirtualRegister(Reg)) {
           continue;
         }
         MachineInstr *NewMI = MRI->getVRegDef(Reg);
         if (!NewMI)
           continue;
         Front.push_back(NewMI);
       }
     } else if (MI->isFullCopy()) {
       if (!TRI->isVirtualRegister(MI->getOperand(1).getReg()))
         continue;
       MachineInstr *NewMI = MRI->getVRegDef(MI->getOperand(1).getReg());
       if (!NewMI)
         continue;
       Front.push_back(NewMI);
     } else {
       DEBUG(dbgs() << "Found partial copy" << *MI <<"\n");
       Outs.push_back(MI);
     }
   }
}

// Return the DPR virtual registers that are read by this machine instruction
// (if any).
SmallVector<unsigned, 8> A15SDOptimizer::getReadDPRs(MachineInstr *MI) {
  if (MI->isCopyLike() || MI->isInsertSubreg() || MI->isRegSequence() ||
      MI->isKill())
    return SmallVector<unsigned, 8>();

  SmallVector<unsigned, 8> Defs;
  for (unsigned i = 0; i < MI->getNumOperands(); ++i) {
    MachineOperand &MO = MI->getOperand(i);

    if (!MO.isReg() || !MO.isUse())
      continue;
    if (!usesRegClass(MO, &ARM::DPRRegClass) &&
        !usesRegClass(MO, &ARM::QPRRegClass))
      continue;

    Defs.push_back(MO.getReg());
  }
  return Defs;
}

// Creates a DPR register from an SPR one by using a VDUP.
unsigned
A15SDOptimizer::createDupLane(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator InsertBefore,
                              DebugLoc DL,
                              unsigned Reg, unsigned Lane, bool QPR) {
  unsigned Out = MRI->createVirtualRegister(QPR ? &ARM::QPRRegClass :
                                                  &ARM::DPRRegClass);
  AddDefaultPred(BuildMI(MBB,
                         InsertBefore,
                         DL,
                         TII->get(QPR ? ARM::VDUPLN32q : ARM::VDUPLN32d),
                         Out)
                   .addReg(Reg)
                   .addImm(Lane));
 
  return Out;
}

// Creates a SPR register from a DPR by copying the value in lane 0.
unsigned
A15SDOptimizer::createExtractSubreg(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator InsertBefore,
                                    DebugLoc DL,
                                    unsigned DReg, unsigned Lane,
                                    const TargetRegisterClass *TRC) {
  unsigned Out = MRI->createVirtualRegister(TRC);
  BuildMI(MBB,
          InsertBefore,
          DL,
          TII->get(TargetOpcode::COPY), Out)
    .addReg(DReg, 0, Lane);

  return Out;
}

// Takes two SPR registers and creates a DPR by using a REG_SEQUENCE.
unsigned
A15SDOptimizer::createRegSequence(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator InsertBefore,
                                  DebugLoc DL,
                                  unsigned Reg1, unsigned Reg2) {
  unsigned Out = MRI->createVirtualRegister(&ARM::QPRRegClass);
  BuildMI(MBB,
          InsertBefore,
          DL,
          TII->get(TargetOpcode::REG_SEQUENCE), Out)
    .addReg(Reg1)
    .addImm(ARM::dsub_0)
    .addReg(Reg2)
    .addImm(ARM::dsub_1);
  return Out;
}

// Takes two DPR registers that have previously been VDUPed (Ssub0 and Ssub1)
// and merges them into one DPR register.
unsigned
A15SDOptimizer::createVExt(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator InsertBefore,
                           DebugLoc DL,
                           unsigned Ssub0, unsigned Ssub1) {
  unsigned Out = MRI->createVirtualRegister(&ARM::DPRRegClass);
  AddDefaultPred(BuildMI(MBB,
                         InsertBefore,
                         DL,
                         TII->get(ARM::VEXTd32), Out)
                   .addReg(Ssub0)
                   .addReg(Ssub1)
                   .addImm(1));
  return Out;
}

unsigned
A15SDOptimizer::createInsertSubreg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator InsertBefore,
                                   DebugLoc DL, unsigned DReg, unsigned Lane,
                                   unsigned ToInsert) {
  unsigned Out = MRI->createVirtualRegister(&ARM::DPR_VFP2RegClass);
  BuildMI(MBB,
          InsertBefore,
          DL,
          TII->get(TargetOpcode::INSERT_SUBREG), Out)
    .addReg(DReg)
    .addReg(ToInsert)
    .addImm(Lane);

  return Out;
}

unsigned
A15SDOptimizer::createImplicitDef(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator InsertBefore,
                                  DebugLoc DL) {
  unsigned Out = MRI->createVirtualRegister(&ARM::DPRRegClass);
  BuildMI(MBB,
          InsertBefore,
          DL,
          TII->get(TargetOpcode::IMPLICIT_DEF), Out);
  return Out;
}

// This function inserts instructions in order to optimize interactions between
// SPR registers and DPR/QPR registers. It does so by performing VDUPs on all
// lanes, and the using VEXT instructions to recompose the result.
unsigned
A15SDOptimizer::optimizeAllLanesPattern(MachineInstr *MI, unsigned Reg) {
  MachineBasicBlock::iterator InsertPt(MI);
  DebugLoc DL = MI->getDebugLoc();
  MachineBasicBlock &MBB = *MI->getParent();
  InsertPt++;
  unsigned Out;

  if (MRI->getRegClass(Reg)->hasSuperClassEq(&ARM::QPRRegClass)) {
    unsigned DSub0 = createExtractSubreg(MBB, InsertPt, DL, Reg,
                                         ARM::dsub_0, &ARM::DPRRegClass);
    unsigned DSub1 = createExtractSubreg(MBB, InsertPt, DL, Reg,
                                         ARM::dsub_1, &ARM::DPRRegClass);

    unsigned Out1 = createDupLane(MBB, InsertPt, DL, DSub0, 0);
    unsigned Out2 = createDupLane(MBB, InsertPt, DL, DSub0, 1);
    Out = createVExt(MBB, InsertPt, DL, Out1, Out2);

    unsigned Out3 = createDupLane(MBB, InsertPt, DL, DSub1, 0);
    unsigned Out4 = createDupLane(MBB, InsertPt, DL, DSub1, 1);
    Out2 = createVExt(MBB, InsertPt, DL, Out3, Out4);

    Out = createRegSequence(MBB, InsertPt, DL, Out, Out2);

  } else if (MRI->getRegClass(Reg)->hasSuperClassEq(&ARM::DPRRegClass)) {
    unsigned Out1 = createDupLane(MBB, InsertPt, DL, Reg, 0);
    unsigned Out2 = createDupLane(MBB, InsertPt, DL, Reg, 1);
    Out = createVExt(MBB, InsertPt, DL, Out1, Out2);

  } else {
    assert(MRI->getRegClass(Reg)->hasSuperClassEq(&ARM::SPRRegClass) &&
           "Found unexpected regclass!");

    unsigned PrefLane = getPrefSPRLane(Reg);
    unsigned Lane;
    switch (PrefLane) {
      case ARM::ssub_0: Lane = 0; break;
      case ARM::ssub_1: Lane = 1; break;
      default: llvm_unreachable("Unknown preferred lane!");
    }

    bool UsesQPR = usesRegClass(MI->getOperand(0), &ARM::QPRRegClass);

    Out = createImplicitDef(MBB, InsertPt, DL);
    Out = createInsertSubreg(MBB, InsertPt, DL, Out, PrefLane, Reg);
    Out = createDupLane(MBB, InsertPt, DL, Out, Lane, UsesQPR);
    eraseInstrWithNoUses(MI);
  }
  return Out;
}

bool A15SDOptimizer::runOnInstruction(MachineInstr *MI) {
  // We look for instructions that write S registers that are then read as
  // D/Q registers. These can only be caused by COPY, INSERT_SUBREG and
  // REG_SEQUENCE pseudos that insert an SPR value into a DPR register or
  // merge two SPR values to form a DPR register.  In order avoid false
  // positives we make sure that there is an SPR producer so we look past
  // COPY and PHI nodes to find it.
  //
  // The best code pattern for when an SPR producer is going to be used by a
  // DPR or QPR consumer depends on whether the other lanes of the
  // corresponding DPR/QPR are currently defined.
  //
  // We can handle these efficiently, depending on the type of
  // pseudo-instruction that is producing the pattern
  //
  //   * COPY:          * VDUP all lanes and merge the results together
  //                      using VEXTs.
  //
  //   * INSERT_SUBREG: * If the SPR value was originally in another DPR/QPR
  //                      lane, and the other lane(s) of the DPR/QPR register
  //                      that we are inserting in are undefined, use the
  //                      original DPR/QPR value. 
  //                    * Otherwise, fall back on the same stategy as COPY.
  //
  //   * REG_SEQUENCE:  * If all except one of the input operands are
  //                      IMPLICIT_DEFs, insert the VDUP pattern for just the
  //                      defined input operand
  //                    * Otherwise, fall back on the same stategy as COPY.
  //

  // First, get all the reads of D-registers done by this instruction.
  SmallVector<unsigned, 8> Defs = getReadDPRs(MI);
  bool Modified = false;

  for (SmallVectorImpl<unsigned>::iterator I = Defs.begin(), E = Defs.end();
     I != E; ++I) {
    // Follow the def-use chain for this DPR through COPYs, and also through
    // PHIs (which are essentially multi-way COPYs). It is because of PHIs that
    // we can end up with multiple defs of this DPR.

    SmallVector<MachineInstr *, 8> DefSrcs;
    if (!TRI->isVirtualRegister(*I))
      continue;
    MachineInstr *Def = MRI->getVRegDef(*I);
    if (!Def)
      continue;

    elideCopiesAndPHIs(Def, DefSrcs);

    for (SmallVectorImpl<MachineInstr *>::iterator II = DefSrcs.begin(),
      EE = DefSrcs.end(); II != EE; ++II) {
      MachineInstr *MI = *II;

      // If we've already analyzed and replaced this operand, don't do
      // anything.
      if (Replacements.find(MI) != Replacements.end())
        continue;

      // Now, work out if the instruction causes a SPR->DPR dependency.
      if (!hasPartialWrite(MI))
        continue;

      // Collect all the uses of this MI's DPR def for updating later.
      SmallVector<MachineOperand*, 8> Uses;
      unsigned DPRDefReg = MI->getOperand(0).getReg();
      for (MachineRegisterInfo::use_iterator I = MRI->use_begin(DPRDefReg),
             E = MRI->use_end(); I != E; ++I)
        Uses.push_back(&I.getOperand());

      // We can optimize this.
      unsigned NewReg = optimizeSDPattern(MI);

      if (NewReg != 0) {
        Modified = true;
        for (SmallVectorImpl<MachineOperand *>::const_iterator I = Uses.begin(),
               E = Uses.end(); I != E; ++I) {
          DEBUG(dbgs() << "Replacing operand "
                       << **I << " with "
                       << PrintReg(NewReg) << "\n");
          (*I)->substVirtReg(NewReg, 0, *TRI);
        }
      }
      Replacements[MI] = NewReg;
    }
  }
  return Modified;
}

bool A15SDOptimizer::runOnMachineFunction(MachineFunction &Fn) {
  TII = static_cast<const ARMBaseInstrInfo*>(Fn.getTarget().getInstrInfo());
  TRI = Fn.getTarget().getRegisterInfo();
  MRI = &Fn.getRegInfo();
  bool Modified = false;

  DEBUG(dbgs() << "Running on function " << Fn.getName()<< "\n");

  DeadInstr.clear();
  Replacements.clear();

  for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E;
       ++MFI) {

    for (MachineBasicBlock::iterator MI = MFI->begin(), ME = MFI->end();
      MI != ME;) {
      Modified |= runOnInstruction(MI++);
    }
 
  }

  for (std::set<MachineInstr *>::iterator I = DeadInstr.begin(),
                                            E = DeadInstr.end();
                                            I != E; ++I) {
    (*I)->eraseFromParent();
  }

  return Modified;
}

FunctionPass *llvm::createA15SDOptimizerPass() {
  return new A15SDOptimizer();
}
