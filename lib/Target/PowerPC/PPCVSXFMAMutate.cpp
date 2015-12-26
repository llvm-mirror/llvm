//===--------------- PPCVSXFMAMutate.cpp - VSX FMA Mutation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass mutates the form of VSX FMA instructions to avoid unnecessary
// copies.
//
//===----------------------------------------------------------------------===//

#include "PPCInstrInfo.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::opt<bool> DisableVSXFMAMutate("disable-ppc-vsx-fma-mutation",
cl::desc("Disable VSX FMA instruction mutation"), cl::Hidden);

#define DEBUG_TYPE "ppc-vsx-fma-mutate"

namespace llvm { namespace PPC {
  int getAltVSXFMAOpcode(uint16_t Opcode);
} }

namespace {
  // PPCVSXFMAMutate pass - For copies between VSX registers and non-VSX registers
  // (Altivec and scalar floating-point registers), we need to transform the
  // copies into subregister copies with other restrictions.
  struct PPCVSXFMAMutate : public MachineFunctionPass {
    static char ID;
    PPCVSXFMAMutate() : MachineFunctionPass(ID) {
      initializePPCVSXFMAMutatePass(*PassRegistry::getPassRegistry());
    }

    LiveIntervals *LIS;
    const PPCInstrInfo *TII;

protected:
    bool processBlock(MachineBasicBlock &MBB) {
      bool Changed = false;

      MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
      const TargetRegisterInfo *TRI = &TII->getRegisterInfo();
      for (MachineBasicBlock::iterator I = MBB.begin(), IE = MBB.end();
           I != IE; ++I) {
        MachineInstr *MI = I;

        // The default (A-type) VSX FMA form kills the addend (it is taken from
        // the target register, which is then updated to reflect the result of
        // the FMA). If the instruction, however, kills one of the registers
        // used for the product, then we can use the M-form instruction (which
        // will take that value from the to-be-defined register).

        int AltOpc = PPC::getAltVSXFMAOpcode(MI->getOpcode());
        if (AltOpc == -1)
          continue;

        // This pass is run after register coalescing, and so we're looking for
        // a situation like this:
        //   ...
        //   %vreg5<def> = COPY %vreg9; VSLRC:%vreg5,%vreg9
        //   %vreg5<def,tied1> = XSMADDADP %vreg5<tied0>, %vreg17, %vreg16,
        //                         %RM<imp-use>; VSLRC:%vreg5,%vreg17,%vreg16
        //   ...
        //   %vreg9<def,tied1> = XSMADDADP %vreg9<tied0>, %vreg17, %vreg19,
        //                         %RM<imp-use>; VSLRC:%vreg9,%vreg17,%vreg19
        //   ...
        // Where we can eliminate the copy by changing from the A-type to the
        // M-type instruction. Specifically, for this example, this means:
        //   %vreg5<def,tied1> = XSMADDADP %vreg5<tied0>, %vreg17, %vreg16,
        //                         %RM<imp-use>; VSLRC:%vreg5,%vreg17,%vreg16
        // is replaced by:
        //   %vreg16<def,tied1> = XSMADDMDP %vreg16<tied0>, %vreg18, %vreg9,
        //                         %RM<imp-use>; VSLRC:%vreg16,%vreg18,%vreg9
        // and we remove: %vreg5<def> = COPY %vreg9; VSLRC:%vreg5,%vreg9

        SlotIndex FMAIdx = LIS->getInstructionIndex(MI);

        VNInfo *AddendValNo =
          LIS->getInterval(MI->getOperand(1).getReg()).Query(FMAIdx).valueIn();

        // This can be null if the register is undef.
        if (!AddendValNo)
          continue;

        MachineInstr *AddendMI = LIS->getInstructionFromIndex(AddendValNo->def);

        // The addend and this instruction must be in the same block.

        if (!AddendMI || AddendMI->getParent() != MI->getParent())
          continue;

        // The addend must be a full copy within the same register class.

        if (!AddendMI->isFullCopy())
          continue;

        unsigned AddendSrcReg = AddendMI->getOperand(1).getReg();
        if (TargetRegisterInfo::isVirtualRegister(AddendSrcReg)) {
          if (MRI.getRegClass(AddendMI->getOperand(0).getReg()) !=
              MRI.getRegClass(AddendSrcReg))
            continue;
        } else {
          // If AddendSrcReg is a physical register, make sure the destination
          // register class contains it.
          if (!MRI.getRegClass(AddendMI->getOperand(0).getReg())
                ->contains(AddendSrcReg))
            continue;
        }

        // In theory, there could be other uses of the addend copy before this
        // fma.  We could deal with this, but that would require additional
        // logic below and I suspect it will not occur in any relevant
        // situations.  Additionally, check whether the copy source is killed
        // prior to the fma.  In order to replace the addend here with the
        // source of the copy, it must still be live here.  We can't use
        // interval testing for a physical register, so as long as we're
        // walking the MIs we may as well test liveness here.
        //
        // FIXME: There is a case that occurs in practice, like this:
        //   %vreg9<def> = COPY %F1; VSSRC:%vreg9
        //   ...
        //   %vreg6<def> = COPY %vreg9; VSSRC:%vreg6,%vreg9
        //   %vreg7<def> = COPY %vreg9; VSSRC:%vreg7,%vreg9
        //   %vreg9<def,tied1> = XSMADDASP %vreg9<tied0>, %vreg1, %vreg4; VSSRC:
        //   %vreg6<def,tied1> = XSMADDASP %vreg6<tied0>, %vreg1, %vreg2; VSSRC:
        //   %vreg7<def,tied1> = XSMADDASP %vreg7<tied0>, %vreg1, %vreg3; VSSRC:
        // which prevents an otherwise-profitable transformation.
        bool OtherUsers = false, KillsAddendSrc = false;
        for (auto J = std::prev(I), JE = MachineBasicBlock::iterator(AddendMI);
             J != JE; --J) {
          if (J->readsVirtualRegister(AddendMI->getOperand(0).getReg())) {
            OtherUsers = true;
            break;
          }
          if (J->modifiesRegister(AddendSrcReg, TRI) ||
              J->killsRegister(AddendSrcReg, TRI)) {
            KillsAddendSrc = true;
            break;
          }
        }

        if (OtherUsers || KillsAddendSrc)
          continue;

        // Find one of the product operands that is killed by this instruction.

        unsigned KilledProdOp = 0, OtherProdOp = 0;
        if (LIS->getInterval(MI->getOperand(2).getReg())
                     .Query(FMAIdx).isKill()) {
          KilledProdOp = 2;
          OtherProdOp  = 3;
        } else if (LIS->getInterval(MI->getOperand(3).getReg())
                     .Query(FMAIdx).isKill()) {
          KilledProdOp = 3;
          OtherProdOp  = 2;
        }

        // If there are no killed product operands, then this transformation is
        // likely not profitable.
        if (!KilledProdOp)
          continue;

        // If the addend copy is used only by this MI, then the addend source
        // register is likely not live here. This could be fixed (based on the
        // legality checks above, the live range for the addend source register
        // could be extended), but it seems likely that such a trivial copy can
        // be coalesced away later, and thus is not worth the effort.
        if (TargetRegisterInfo::isVirtualRegister(AddendSrcReg) &&
            !LIS->getInterval(AddendSrcReg).liveAt(FMAIdx))
          continue;

        // Transform: (O2 * O3) + O1 -> (O2 * O1) + O3.

        unsigned KilledProdReg = MI->getOperand(KilledProdOp).getReg();
        unsigned OtherProdReg  = MI->getOperand(OtherProdOp).getReg();

        unsigned AddSubReg = AddendMI->getOperand(1).getSubReg();
        unsigned KilledProdSubReg = MI->getOperand(KilledProdOp).getSubReg();
        unsigned OtherProdSubReg  = MI->getOperand(OtherProdOp).getSubReg();

        bool AddRegKill = AddendMI->getOperand(1).isKill();
        bool KilledProdRegKill = MI->getOperand(KilledProdOp).isKill();
        bool OtherProdRegKill  = MI->getOperand(OtherProdOp).isKill();

        bool AddRegUndef = AddendMI->getOperand(1).isUndef();
        bool KilledProdRegUndef = MI->getOperand(KilledProdOp).isUndef();
        bool OtherProdRegUndef  = MI->getOperand(OtherProdOp).isUndef();

        unsigned OldFMAReg = MI->getOperand(0).getReg();

        // The transformation doesn't work well with things like:
        //    %vreg5 = A-form-op %vreg5, %vreg11, %vreg5;
        // so leave such things alone.
        if (OldFMAReg == KilledProdReg)
          continue;

        // If there isn't a class that fits, we can't perform the transform.
        // This is needed for correctness with a mixture of VSX and Altivec
        // instructions to make sure that a low VSX register is not assigned to
        // the Altivec instruction.
        if (!MRI.constrainRegClass(KilledProdReg,
                                   MRI.getRegClass(OldFMAReg)))
          continue;

        assert(OldFMAReg == AddendMI->getOperand(0).getReg() &&
               "Addend copy not tied to old FMA output!");

        DEBUG(dbgs() << "VSX FMA Mutation:\n    " << *MI;);

        MI->getOperand(0).setReg(KilledProdReg);
        MI->getOperand(1).setReg(KilledProdReg);
        MI->getOperand(3).setReg(AddendSrcReg);
        MI->getOperand(2).setReg(OtherProdReg);

        MI->getOperand(0).setSubReg(KilledProdSubReg);
        MI->getOperand(1).setSubReg(KilledProdSubReg);
        MI->getOperand(3).setSubReg(AddSubReg);
        MI->getOperand(2).setSubReg(OtherProdSubReg);

        MI->getOperand(1).setIsKill(KilledProdRegKill);
        MI->getOperand(3).setIsKill(AddRegKill);
        MI->getOperand(2).setIsKill(OtherProdRegKill);

        MI->getOperand(1).setIsUndef(KilledProdRegUndef);
        MI->getOperand(3).setIsUndef(AddRegUndef);
        MI->getOperand(2).setIsUndef(OtherProdRegUndef);

        MI->setDesc(TII->get(AltOpc));

        DEBUG(dbgs() << " -> " << *MI);

        // The killed product operand was killed here, so we can reuse it now
        // for the result of the fma.

        LiveInterval &FMAInt = LIS->getInterval(OldFMAReg);
        VNInfo *FMAValNo = FMAInt.getVNInfoAt(FMAIdx.getRegSlot());
        for (auto UI = MRI.reg_nodbg_begin(OldFMAReg), UE = MRI.reg_nodbg_end();
             UI != UE;) {
          MachineOperand &UseMO = *UI;
          MachineInstr *UseMI = UseMO.getParent();
          ++UI;

          // Don't replace the result register of the copy we're about to erase.
          if (UseMI == AddendMI)
            continue;

          UseMO.substVirtReg(KilledProdReg, KilledProdSubReg, *TRI);
        }

        // Extend the live intervals of the killed product operand to hold the
        // fma result.

        LiveInterval &NewFMAInt = LIS->getInterval(KilledProdReg);
        for (LiveInterval::iterator AI = FMAInt.begin(), AE = FMAInt.end();
             AI != AE; ++AI) {
          // Don't add the segment that corresponds to the original copy.
          if (AI->valno == AddendValNo)
            continue;

          VNInfo *NewFMAValNo =
            NewFMAInt.getNextValue(AI->start,
                                   LIS->getVNInfoAllocator());

          NewFMAInt.addSegment(LiveInterval::Segment(AI->start, AI->end,
                                                     NewFMAValNo));
        }
        DEBUG(dbgs() << "  extended: " << NewFMAInt << '\n');

        // Extend the live interval of the addend source (it might end at the
        // copy to be removed, or somewhere in between there and here). This
        // is necessary only if it is a physical register.
        if (!TargetRegisterInfo::isVirtualRegister(AddendSrcReg))
          for (MCRegUnitIterator Units(AddendSrcReg, TRI); Units.isValid();
               ++Units) {
            unsigned Unit = *Units;

            LiveRange &AddendSrcRange = LIS->getRegUnit(Unit);
            AddendSrcRange.extendInBlock(LIS->getMBBStartIdx(&MBB),
                                         FMAIdx.getRegSlot());
            DEBUG(dbgs() << "  extended: " << AddendSrcRange << '\n');
          }

        FMAInt.removeValNo(FMAValNo);
        DEBUG(dbgs() << "  trimmed:  " << FMAInt << '\n');

        // Remove the (now unused) copy.

        DEBUG(dbgs() << "  removing: " << *AddendMI << '\n');
        LIS->RemoveMachineInstrFromMaps(AddendMI);
        AddendMI->eraseFromParent();

        Changed = true;
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      // If we don't have VSX then go ahead and return without doing
      // anything.
      const PPCSubtarget &STI = MF.getSubtarget<PPCSubtarget>();
      if (!STI.hasVSX())
        return false;

      LIS = &getAnalysis<LiveIntervals>();

      TII = STI.getInstrInfo();

      bool Changed = false;

      if (DisableVSXFMAMutate)
        return Changed;

      for (MachineFunction::iterator I = MF.begin(); I != MF.end();) {
        MachineBasicBlock &B = *I++;
        if (processBlock(B))
          Changed = true;
      }

      return Changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LiveIntervals>();
      AU.addPreserved<LiveIntervals>();
      AU.addRequired<SlotIndexes>();
      AU.addPreserved<SlotIndexes>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS_BEGIN(PPCVSXFMAMutate, DEBUG_TYPE,
                      "PowerPC VSX FMA Mutation", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_END(PPCVSXFMAMutate, DEBUG_TYPE,
                    "PowerPC VSX FMA Mutation", false, false)

char &llvm::PPCVSXFMAMutateID = PPCVSXFMAMutate::ID;

char PPCVSXFMAMutate::ID = 0;
FunctionPass *llvm::createPPCVSXFMAMutatePass() {
  return new PPCVSXFMAMutate();
}
