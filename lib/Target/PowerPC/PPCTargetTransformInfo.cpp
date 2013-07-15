//===-- PPCTargetTransformInfo.cpp - PPC specific TTI pass ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// PPC target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ppctti"
#include "PPC.h"
#include "PPCTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/CostTable.h"
using namespace llvm;

// Declare the pass initialization routine locally as target-specific passes
// don't havve a target-wide initialization entry point, and so we rely on the
// pass constructor initialization.
namespace llvm {
void initializePPCTTIPass(PassRegistry &);
}

namespace {

class PPCTTI : public ImmutablePass, public TargetTransformInfo {
  const PPCTargetMachine *TM;
  const PPCSubtarget *ST;
  const PPCTargetLowering *TLI;

  /// Estimate the overhead of scalarizing an instruction. Insert and Extract
  /// are set if the result needs to be inserted and/or extracted from vectors.
  unsigned getScalarizationOverhead(Type *Ty, bool Insert, bool Extract) const;

public:
  PPCTTI() : ImmutablePass(ID), TM(0), ST(0), TLI(0) {
    llvm_unreachable("This pass cannot be directly constructed");
  }

  PPCTTI(const PPCTargetMachine *TM)
      : ImmutablePass(ID), TM(TM), ST(TM->getSubtargetImpl()),
        TLI(TM->getTargetLowering()) {
    initializePPCTTIPass(*PassRegistry::getPassRegistry());
  }

  virtual void initializePass() {
    pushTTIStack(this);
  }

  virtual void finalizePass() {
    popTTIStack();
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    TargetTransformInfo::getAnalysisUsage(AU);
  }

  /// Pass identification.
  static char ID;

  /// Provide necessary pointer adjustments for the two base classes.
  virtual void *getAdjustedAnalysisPointer(const void *ID) {
    if (ID == &TargetTransformInfo::ID)
      return (TargetTransformInfo*)this;
    return this;
  }

  /// \name Scalar TTI Implementations
  /// @{
  virtual PopcntSupportKind getPopcntSupport(unsigned TyWidth) const;

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  virtual unsigned getNumberOfRegisters(bool Vector) const;
  virtual unsigned getRegisterBitWidth(bool Vector) const;
  virtual unsigned getMaximumUnrollFactor() const;
  virtual unsigned getArithmeticInstrCost(unsigned Opcode, Type *Ty,
                                          OperandValueKind,
                                          OperandValueKind) const;
  virtual unsigned getShuffleCost(ShuffleKind Kind, Type *Tp,
                                  int Index, Type *SubTp) const;
  virtual unsigned getCastInstrCost(unsigned Opcode, Type *Dst,
                                    Type *Src) const;
  virtual unsigned getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                      Type *CondTy) const;
  virtual unsigned getVectorInstrCost(unsigned Opcode, Type *Val,
                                      unsigned Index) const;
  virtual unsigned getMemoryOpCost(unsigned Opcode, Type *Src,
                                   unsigned Alignment,
                                   unsigned AddressSpace) const;

  /// @}
};

} // end anonymous namespace

INITIALIZE_AG_PASS(PPCTTI, TargetTransformInfo, "ppctti",
                   "PPC Target Transform Info", true, true, false)
char PPCTTI::ID = 0;

ImmutablePass *
llvm::createPPCTargetTransformInfoPass(const PPCTargetMachine *TM) {
  return new PPCTTI(TM);
}


//===----------------------------------------------------------------------===//
//
// PPC cost model.
//
//===----------------------------------------------------------------------===//

PPCTTI::PopcntSupportKind PPCTTI::getPopcntSupport(unsigned TyWidth) const {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  if (ST->hasPOPCNTD() && TyWidth <= 64)
    return PSK_FastHardware;
  return PSK_Software;
}

unsigned PPCTTI::getNumberOfRegisters(bool Vector) const {
  if (Vector && !ST->hasAltivec())
    return 0;
  return 32;
}

unsigned PPCTTI::getRegisterBitWidth(bool Vector) const {
  if (Vector) {
    if (ST->hasAltivec()) return 128;
    return 0;
  }

  if (ST->isPPC64())
    return 64;
  return 32;

}

unsigned PPCTTI::getMaximumUnrollFactor() const {
  unsigned Directive = ST->getDarwinDirective();
  // The 440 has no SIMD support, but floating-point instructions
  // have a 5-cycle latency, so unroll by 5x for latency hiding.
  if (Directive == PPC::DIR_440)
    return 5;

  // The A2 has no SIMD support, but floating-point instructions
  // have a 6-cycle latency, so unroll by 6x for latency hiding.
  if (Directive == PPC::DIR_A2)
    return 6;

  // FIXME: For lack of any better information, do no harm...
  if (Directive == PPC::DIR_E500mc || Directive == PPC::DIR_E5500)
    return 1;

  // For most things, modern systems have two execution units (and
  // out-of-order execution).
  return 2;
}

unsigned PPCTTI::getArithmeticInstrCost(unsigned Opcode, Type *Ty,
                                        OperandValueKind Op1Info,
                                        OperandValueKind Op2Info) const {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  // Fallback to the default implementation.
  return TargetTransformInfo::getArithmeticInstrCost(Opcode, Ty, Op1Info,
                                                     Op2Info);
}

unsigned PPCTTI::getShuffleCost(ShuffleKind Kind, Type *Tp, int Index,
                                Type *SubTp) const {
  return TargetTransformInfo::getShuffleCost(Kind, Tp, Index, SubTp);
}

unsigned PPCTTI::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src) const {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  return TargetTransformInfo::getCastInstrCost(Opcode, Dst, Src);
}

unsigned PPCTTI::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                    Type *CondTy) const {
  return TargetTransformInfo::getCmpSelInstrCost(Opcode, ValTy, CondTy);
}

unsigned PPCTTI::getVectorInstrCost(unsigned Opcode, Type *Val,
                                    unsigned Index) const {
  assert(Val->isVectorTy() && "This must be a vector type");

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // Estimated cost of a load-hit-store delay.  This was obtained
  // experimentally as a minimum needed to prevent unprofitable
  // vectorization for the paq8p benchmark.  It may need to be
  // raised further if other unprofitable cases remain.
  unsigned LHSPenalty = 12;

  // Vector element insert/extract with Altivec is very expensive,
  // because they require store and reload with the attendant
  // processor stall for load-hit-store.  Until VSX is available,
  // these need to be estimated as very costly.
  if (ISD == ISD::EXTRACT_VECTOR_ELT ||
      ISD == ISD::INSERT_VECTOR_ELT)
    return LHSPenalty +
      TargetTransformInfo::getVectorInstrCost(Opcode, Val, Index);

  return TargetTransformInfo::getVectorInstrCost(Opcode, Val, Index);
}

unsigned PPCTTI::getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                                 unsigned AddressSpace) const {
  // Legalize the type.
  std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Src);
  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");

  // Each load/store unit costs 1.
  unsigned Cost = LT.first * 1;

  // PPC in general does not support unaligned loads and stores. They'll need
  // to be decomposed based on the alignment factor.
  unsigned SrcBytes = LT.second.getStoreSize();
  if (SrcBytes && Alignment && Alignment < SrcBytes)
    Cost *= (SrcBytes/Alignment);

  return Cost;
}

