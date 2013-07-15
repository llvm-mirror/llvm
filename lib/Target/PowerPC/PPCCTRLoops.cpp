//===-- PPCCTRLoops.cpp - Identify and generate CTR loops -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass identifies loops where we can generate the PPC branch instructions
// that decrement and test the count register (CTR) (bdnz and friends).
//
// The pattern that defines the induction variable can changed depending on
// prior optimizations.  For example, the IndVarSimplify phase run by 'opt'
// normalizes induction variables, and the Loop Strength Reduction pass
// run by 'llc' may also make changes to the induction variable.
//
// Criteria for CTR loops:
//  - Countable loops (w/ ind. var for a trip count)
//  - Try inner-most loops first
//  - No nested CTR loops.
//  - No function calls in loops.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ctrloops"

#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "PPCTargetMachine.h"
#include "PPC.h"

#ifndef NDEBUG
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#endif

#include <algorithm>
#include <vector>

using namespace llvm;

#ifndef NDEBUG
static cl::opt<int> CTRLoopLimit("ppc-max-ctrloop", cl::Hidden, cl::init(-1));
#endif

STATISTIC(NumCTRLoops, "Number of loops converted to CTR loops");

namespace llvm {
  void initializePPCCTRLoopsPass(PassRegistry&);
#ifndef NDEBUG
  void initializePPCCTRLoopsVerifyPass(PassRegistry&);
#endif
}

namespace {
  struct PPCCTRLoops : public FunctionPass {

#ifndef NDEBUG
    static int Counter;
#endif

  public:
    static char ID;

    PPCCTRLoops() : FunctionPass(ID), TM(0) {
      initializePPCCTRLoopsPass(*PassRegistry::getPassRegistry());
    }
    PPCCTRLoops(PPCTargetMachine &TM) : FunctionPass(ID), TM(&TM) {
      initializePPCCTRLoopsPass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<LoopInfo>();
      AU.addPreserved<LoopInfo>();
      AU.addRequired<DominatorTree>();
      AU.addPreserved<DominatorTree>();
      AU.addRequired<ScalarEvolution>();
    }

  private:
    bool mightUseCTR(const Triple &TT, BasicBlock *BB);
    bool convertToCTRLoop(Loop *L);

  private:
    PPCTargetMachine *TM;
    LoopInfo *LI;
    ScalarEvolution *SE;
    DataLayout *TD;
    DominatorTree *DT;
    const TargetLibraryInfo *LibInfo;
  };

  char PPCCTRLoops::ID = 0;
#ifndef NDEBUG
  int PPCCTRLoops::Counter = 0;
#endif

#ifndef NDEBUG
  struct PPCCTRLoopsVerify : public MachineFunctionPass {
  public:
    static char ID;

    PPCCTRLoopsVerify() : MachineFunctionPass(ID) {
      initializePPCCTRLoopsVerifyPass(*PassRegistry::getPassRegistry());
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    virtual bool runOnMachineFunction(MachineFunction &MF);

  private:
    MachineDominatorTree *MDT;
  };

  char PPCCTRLoopsVerify::ID = 0;
#endif // NDEBUG
} // end anonymous namespace

INITIALIZE_PASS_BEGIN(PPCCTRLoops, "ppc-ctr-loops", "PowerPC CTR Loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTree)
INITIALIZE_PASS_DEPENDENCY(LoopInfo)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution)
INITIALIZE_PASS_END(PPCCTRLoops, "ppc-ctr-loops", "PowerPC CTR Loops",
                    false, false)

FunctionPass *llvm::createPPCCTRLoops(PPCTargetMachine &TM) {
  return new PPCCTRLoops(TM);
}

#ifndef NDEBUG
INITIALIZE_PASS_BEGIN(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                      "PowerPC CTR Loops Verify", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                    "PowerPC CTR Loops Verify", false, false)

FunctionPass *llvm::createPPCCTRLoopsVerify() {
  return new PPCCTRLoopsVerify();
}
#endif // NDEBUG

bool PPCCTRLoops::runOnFunction(Function &F) {
  LI = &getAnalysis<LoopInfo>();
  SE = &getAnalysis<ScalarEvolution>();
  DT = &getAnalysis<DominatorTree>();
  TD = getAnalysisIfAvailable<DataLayout>();
  LibInfo = getAnalysisIfAvailable<TargetLibraryInfo>();

  bool MadeChange = false;

  for (LoopInfo::iterator I = LI->begin(), E = LI->end();
       I != E; ++I) {
    Loop *L = *I;
    if (!L->getParentLoop())
      MadeChange |= convertToCTRLoop(L);
  }

  return MadeChange;
}

bool PPCCTRLoops::mightUseCTR(const Triple &TT, BasicBlock *BB) {
  for (BasicBlock::iterator J = BB->begin(), JE = BB->end();
       J != JE; ++J) {
    if (CallInst *CI = dyn_cast<CallInst>(J)) {
      if (InlineAsm *IA = dyn_cast<InlineAsm>(CI->getCalledValue())) {
        // Inline ASM is okay, unless it clobbers the ctr register.
        InlineAsm::ConstraintInfoVector CIV = IA->ParseConstraints();
        for (unsigned i = 0, ie = CIV.size(); i < ie; ++i) {
          InlineAsm::ConstraintInfo &C = CIV[i];
          if (C.Type != InlineAsm::isInput)
            for (unsigned j = 0, je = C.Codes.size(); j < je; ++j)
              if (StringRef(C.Codes[j]).equals_lower("{ctr}"))
                return true;
        }

        continue;
      }

      if (!TM)
        return true;
      const TargetLowering *TLI = TM->getTargetLowering();

      if (Function *F = CI->getCalledFunction()) {
        // Most intrinsics don't become function calls, but some might.
        // sin, cos, exp and log are always calls.
        unsigned Opcode;
        if (F->getIntrinsicID() != Intrinsic::not_intrinsic) {
          switch (F->getIntrinsicID()) {
          default: continue;

// VisualStudio defines setjmp as _setjmp
#if defined(_MSC_VER) && defined(setjmp) && \
                       !defined(setjmp_undefined_for_msvc)
#  pragma push_macro("setjmp")
#  undef setjmp
#  define setjmp_undefined_for_msvc
#endif

          case Intrinsic::setjmp:

#if defined(_MSC_VER) && defined(setjmp_undefined_for_msvc)
 // let's return it to _setjmp state
#  pragma pop_macro("setjmp")
#  undef setjmp_undefined_for_msvc
#endif

          case Intrinsic::longjmp:
          case Intrinsic::memcpy:
          case Intrinsic::memmove:
          case Intrinsic::memset:
          case Intrinsic::powi:
          case Intrinsic::log:
          case Intrinsic::log2:
          case Intrinsic::log10:
          case Intrinsic::exp:
          case Intrinsic::exp2:
          case Intrinsic::pow:
          case Intrinsic::sin:
          case Intrinsic::cos:
            return true;
          case Intrinsic::sqrt:      Opcode = ISD::FSQRT;      break;
          case Intrinsic::floor:     Opcode = ISD::FFLOOR;     break;
          case Intrinsic::ceil:      Opcode = ISD::FCEIL;      break;
          case Intrinsic::trunc:     Opcode = ISD::FTRUNC;     break;
          case Intrinsic::rint:      Opcode = ISD::FRINT;      break;
          case Intrinsic::nearbyint: Opcode = ISD::FNEARBYINT; break;
          }
        }

        // PowerPC does not use [US]DIVREM or other library calls for
        // operations on regular types which are not otherwise library calls
        // (i.e. soft float or atomics). If adapting for targets that do,
        // additional care is required here.

        LibFunc::Func Func;
        if (!F->hasLocalLinkage() && F->hasName() && LibInfo &&
            LibInfo->getLibFunc(F->getName(), Func) &&
            LibInfo->hasOptimizedCodeGen(Func)) {
          // Non-read-only functions are never treated as intrinsics.
          if (!CI->onlyReadsMemory())
            return true;

          // Conversion happens only for FP calls.
          if (!CI->getArgOperand(0)->getType()->isFloatingPointTy())
            return true;

          switch (Func) {
          default: return true;
          case LibFunc::copysign:
          case LibFunc::copysignf:
          case LibFunc::copysignl:
            continue; // ISD::FCOPYSIGN is never a library call.
          case LibFunc::fabs:
          case LibFunc::fabsf:
          case LibFunc::fabsl:
            continue; // ISD::FABS is never a library call.
          case LibFunc::sqrt:
          case LibFunc::sqrtf:
          case LibFunc::sqrtl:
            Opcode = ISD::FSQRT; break;
          case LibFunc::floor:
          case LibFunc::floorf:
          case LibFunc::floorl:
            Opcode = ISD::FFLOOR; break;
          case LibFunc::nearbyint:
          case LibFunc::nearbyintf:
          case LibFunc::nearbyintl:
            Opcode = ISD::FNEARBYINT; break;
          case LibFunc::ceil:
          case LibFunc::ceilf:
          case LibFunc::ceill:
            Opcode = ISD::FCEIL; break;
          case LibFunc::rint:
          case LibFunc::rintf:
          case LibFunc::rintl:
            Opcode = ISD::FRINT; break;
          case LibFunc::trunc:
          case LibFunc::truncf:
          case LibFunc::truncl:
            Opcode = ISD::FTRUNC; break;
          }

          MVT VTy =
            TLI->getSimpleValueType(CI->getArgOperand(0)->getType(), true);
          if (VTy == MVT::Other)
            return true;
          
          if (TLI->isOperationLegalOrCustom(Opcode, VTy))
            continue;
          else if (VTy.isVector() &&
                   TLI->isOperationLegalOrCustom(Opcode, VTy.getScalarType()))
            continue;

          return true;
        }
      }

      return true;
    } else if (isa<BinaryOperator>(J) &&
               J->getType()->getScalarType()->isPPC_FP128Ty()) {
      // Most operations on ppc_f128 values become calls.
      return true;
    } else if (isa<UIToFPInst>(J) || isa<SIToFPInst>(J) ||
               isa<FPToUIInst>(J) || isa<FPToSIInst>(J)) {
      CastInst *CI = cast<CastInst>(J);
      if (CI->getSrcTy()->getScalarType()->isPPC_FP128Ty() ||
          CI->getDestTy()->getScalarType()->isPPC_FP128Ty() ||
          (TT.isArch32Bit() &&
           (CI->getSrcTy()->getScalarType()->isIntegerTy(64) ||
            CI->getDestTy()->getScalarType()->isIntegerTy(64))
          ))
        return true;
    } else if (TT.isArch32Bit() &&
               J->getType()->getScalarType()->isIntegerTy(64) &&
               (J->getOpcode() == Instruction::UDiv ||
                J->getOpcode() == Instruction::SDiv ||
                J->getOpcode() == Instruction::URem ||
                J->getOpcode() == Instruction::SRem)) {
      return true;
    } else if (isa<IndirectBrInst>(J) || isa<InvokeInst>(J)) {
      // On PowerPC, indirect jumps use the counter register.
      return true;
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(J)) {
      if (!TM)
        return true;
      const TargetLowering *TLI = TM->getTargetLowering();

      if (TLI->supportJumpTables() &&
          SI->getNumCases()+1 >= (unsigned) TLI->getMinimumJumpTableEntries())
        return true;
    }
  }

  return false;
}

bool PPCCTRLoops::convertToCTRLoop(Loop *L) {
  bool MadeChange = false;

  Triple TT = Triple(L->getHeader()->getParent()->getParent()->
                     getTargetTriple());
  if (!TT.isArch32Bit() && !TT.isArch64Bit())
    return MadeChange; // Unknown arch. type.

  // Process nested loops first.
  for (Loop::iterator I = L->begin(), E = L->end(); I != E; ++I) {
    MadeChange |= convertToCTRLoop(*I);
  }

  // If a nested loop has been converted, then we can't convert this loop.
  if (MadeChange)
    return MadeChange;

#ifndef NDEBUG
  // Stop trying after reaching the limit (if any).
  int Limit = CTRLoopLimit;
  if (Limit >= 0) {
    if (Counter >= CTRLoopLimit)
      return false;
    Counter++;
  }
#endif

  // We don't want to spill/restore the counter register, and so we don't
  // want to use the counter register if the loop contains calls.
  for (Loop::block_iterator I = L->block_begin(), IE = L->block_end();
       I != IE; ++I)
    if (mightUseCTR(TT, *I))
      return MadeChange;

  SmallVector<BasicBlock*, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  BasicBlock *CountedExitBlock = 0;
  const SCEV *ExitCount = 0;
  BranchInst *CountedExitBranch = 0;
  for (SmallVectorImpl<BasicBlock *>::iterator I = ExitingBlocks.begin(),
       IE = ExitingBlocks.end(); I != IE; ++I) {
    const SCEV *EC = SE->getExitCount(L, *I);
    DEBUG(dbgs() << "Exit Count for " << *L << " from block " <<
                    (*I)->getName() << ": " << *EC << "\n");
    if (isa<SCEVCouldNotCompute>(EC))
      continue;
    if (const SCEVConstant *ConstEC = dyn_cast<SCEVConstant>(EC)) {
      if (ConstEC->getValue()->isZero())
        continue;
    } else if (!SE->isLoopInvariant(EC, L))
      continue;

    if (SE->getTypeSizeInBits(EC->getType()) > (TT.isArch64Bit() ? 64 : 32))
      continue;

    // We now have a loop-invariant count of loop iterations (which is not the
    // constant zero) for which we know that this loop will not exit via this
    // exisiting block.

    // We need to make sure that this block will run on every loop iteration.
    // For this to be true, we must dominate all blocks with backedges. Such
    // blocks are in-loop predecessors to the header block.
    bool NotAlways = false;
    for (pred_iterator PI = pred_begin(L->getHeader()),
         PIE = pred_end(L->getHeader()); PI != PIE; ++PI) {
      if (!L->contains(*PI))
        continue;

      if (!DT->dominates(*I, *PI)) {
        NotAlways = true;
        break;
      }
    }

    if (NotAlways)
      continue;

    // Make sure this blocks ends with a conditional branch.
    Instruction *TI = (*I)->getTerminator();
    if (!TI)
      continue;

    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      if (!BI->isConditional())
        continue;

      CountedExitBranch = BI;
    } else
      continue;

    // Note that this block may not be the loop latch block, even if the loop
    // has a latch block.
    CountedExitBlock = *I;
    ExitCount = EC;
    break;
  }

  if (!CountedExitBlock)
    return MadeChange;

  BasicBlock *Preheader = L->getLoopPreheader();

  // If we don't have a preheader, then insert one. If we already have a
  // preheader, then we can use it (except if the preheader contains a use of
  // the CTR register because some such uses might be reordered by the
  // selection DAG after the mtctr instruction).
  if (!Preheader || mightUseCTR(TT, Preheader))
    Preheader = InsertPreheaderForLoop(L, this);
  if (!Preheader)
    return MadeChange;

  DEBUG(dbgs() << "Preheader for exit count: " << Preheader->getName() << "\n");

  // Insert the count into the preheader and replace the condition used by the
  // selected branch.
  MadeChange = true;

  SCEVExpander SCEVE(*SE, "loopcnt");
  LLVMContext &C = SE->getContext();
  Type *CountType = TT.isArch64Bit() ? Type::getInt64Ty(C) :
                                       Type::getInt32Ty(C);
  if (!ExitCount->getType()->isPointerTy() &&
      ExitCount->getType() != CountType)
    ExitCount = SE->getZeroExtendExpr(ExitCount, CountType);
  ExitCount = SE->getAddExpr(ExitCount,
                             SE->getConstant(CountType, 1)); 
  Value *ECValue = SCEVE.expandCodeFor(ExitCount, CountType,
                                       Preheader->getTerminator());

  IRBuilder<> CountBuilder(Preheader->getTerminator());
  Module *M = Preheader->getParent()->getParent();
  Value *MTCTRFunc = Intrinsic::getDeclaration(M, Intrinsic::ppc_mtctr,
                                               CountType);
  CountBuilder.CreateCall(MTCTRFunc, ECValue);

  IRBuilder<> CondBuilder(CountedExitBranch);
  Value *DecFunc =
    Intrinsic::getDeclaration(M, Intrinsic::ppc_is_decremented_ctr_nonzero);
  Value *NewCond = CondBuilder.CreateCall(DecFunc);
  Value *OldCond = CountedExitBranch->getCondition();
  CountedExitBranch->setCondition(NewCond);

  // The false branch must exit the loop.
  if (!L->contains(CountedExitBranch->getSuccessor(0)))
    CountedExitBranch->swapSuccessors();

  // The old condition may be dead now, and may have even created a dead PHI
  // (the original induction variable).
  RecursivelyDeleteTriviallyDeadInstructions(OldCond);
  DeleteDeadPHIs(CountedExitBlock);

  ++NumCTRLoops;
  return MadeChange;
}

#ifndef NDEBUG
static bool clobbersCTR(const MachineInstr *MI) {
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg()) {
      if (MO.isDef() && (MO.getReg() == PPC::CTR || MO.getReg() == PPC::CTR8))
        return true;
    } else if (MO.isRegMask()) {
      if (MO.clobbersPhysReg(PPC::CTR) || MO.clobbersPhysReg(PPC::CTR8))
        return true;
    }
  }

  return false;
}

static bool verifyCTRBranch(MachineBasicBlock *MBB,
                            MachineBasicBlock::iterator I) {
  MachineBasicBlock::iterator BI = I;
  SmallSet<MachineBasicBlock *, 16>   Visited;
  SmallVector<MachineBasicBlock *, 8> Preds;
  bool CheckPreds;

  if (I == MBB->begin()) {
    Visited.insert(MBB);
    goto queue_preds;
  } else
    --I;

check_block:
  Visited.insert(MBB);
  if (I == MBB->end())
    goto queue_preds;

  CheckPreds = true;
  for (MachineBasicBlock::iterator IE = MBB->begin();; --I) {
    unsigned Opc = I->getOpcode();
    if (Opc == PPC::MTCTRloop || Opc == PPC::MTCTR8loop) {
      CheckPreds = false;
      break;
    }

    if (I != BI && clobbersCTR(I)) {
      DEBUG(dbgs() << "BB#" << MBB->getNumber() << " (" <<
                      MBB->getFullName() << ") instruction " << *I <<
                      " clobbers CTR, invalidating " << "BB#" <<
                      BI->getParent()->getNumber() << " (" <<
                      BI->getParent()->getFullName() << ") instruction " <<
                      *BI << "\n");
      return false;
    }

    if (I == IE)
      break;
  }

  if (!CheckPreds && Preds.empty())
    return true;

  if (CheckPreds) {
queue_preds:
    if (MachineFunction::iterator(MBB) == MBB->getParent()->begin()) {
      DEBUG(dbgs() << "Unable to find a MTCTR instruction for BB#" <<
                      BI->getParent()->getNumber() << " (" <<
                      BI->getParent()->getFullName() << ") instruction " <<
                      *BI << "\n");
      return false;
    }

    for (MachineBasicBlock::pred_iterator PI = MBB->pred_begin(),
         PIE = MBB->pred_end(); PI != PIE; ++PI)
      Preds.push_back(*PI);
  }

  do {
    MBB = Preds.pop_back_val();
    if (!Visited.count(MBB)) {
      I = MBB->getLastNonDebugInstr();
      goto check_block;
    }
  } while (!Preds.empty());

  return true;
}

bool PPCCTRLoopsVerify::runOnMachineFunction(MachineFunction &MF) {
  MDT = &getAnalysis<MachineDominatorTree>();

  // Verify that all bdnz/bdz instructions are dominated by a loop mtctr before
  // any other instructions that might clobber the ctr register.
  for (MachineFunction::iterator I = MF.begin(), IE = MF.end();
       I != IE; ++I) {
    MachineBasicBlock *MBB = I;
    if (!MDT->isReachableFromEntry(MBB))
      continue;

    for (MachineBasicBlock::iterator MII = MBB->getFirstTerminator(),
      MIIE = MBB->end(); MII != MIIE; ++MII) {
      unsigned Opc = MII->getOpcode();
      if (Opc == PPC::BDNZ8 || Opc == PPC::BDNZ ||
          Opc == PPC::BDZ8  || Opc == PPC::BDZ)
        if (!verifyCTRBranch(MBB, MII))
          llvm_unreachable("Invalid PPC CTR loop!");
    }
  }

  return false;
}
#endif // NDEBUG

