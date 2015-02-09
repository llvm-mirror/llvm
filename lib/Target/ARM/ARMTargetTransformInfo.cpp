//===-- ARMTargetTransformInfo.cpp - ARM specific TTI ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARMTargetTransformInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/CostTable.h"
#include "llvm/Target/TargetLowering.h"
using namespace llvm;

#define DEBUG_TYPE "armtti"

unsigned ARMTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty) {
  assert(Ty->isIntegerTy());

  unsigned Bits = Ty->getPrimitiveSizeInBits();
  if (Bits == 0 || Bits > 32)
    return 4;

  int32_t SImmVal = Imm.getSExtValue();
  uint32_t ZImmVal = Imm.getZExtValue();
  if (!ST->isThumb()) {
    if ((SImmVal >= 0 && SImmVal < 65536) ||
        (ARM_AM::getSOImmVal(ZImmVal) != -1) ||
        (ARM_AM::getSOImmVal(~ZImmVal) != -1))
      return 1;
    return ST->hasV6T2Ops() ? 2 : 3;
  }
  if (ST->isThumb2()) {
    if ((SImmVal >= 0 && SImmVal < 65536) ||
        (ARM_AM::getT2SOImmVal(ZImmVal) != -1) ||
        (ARM_AM::getT2SOImmVal(~ZImmVal) != -1))
      return 1;
    return ST->hasV6T2Ops() ? 2 : 3;
  }
  // Thumb1.
  if (SImmVal >= 0 && SImmVal < 256)
    return 1;
  if ((~ZImmVal < 256) || ARM_AM::isThumbImmShiftedVal(ZImmVal))
    return 2;
  // Load from constantpool.
  return 3;
}

unsigned ARMTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // Single to/from double precision conversions.
  static const CostTblEntry<MVT::SimpleValueType> NEONFltDblTbl[] = {
    // Vector fptrunc/fpext conversions.
    { ISD::FP_ROUND,   MVT::v2f64, 2 },
    { ISD::FP_EXTEND,  MVT::v2f32, 2 },
    { ISD::FP_EXTEND,  MVT::v4f32, 4 }
  };

  if (Src->isVectorTy() && ST->hasNEON() && (ISD == ISD::FP_ROUND ||
                                          ISD == ISD::FP_EXTEND)) {
    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Src);
    int Idx = CostTableLookup(NEONFltDblTbl, ISD, LT.second);
    if (Idx != -1)
      return LT.first * NEONFltDblTbl[Idx].Cost;
  }

  EVT SrcTy = TLI->getValueType(Src);
  EVT DstTy = TLI->getValueType(Dst);

  if (!SrcTy.isSimple() || !DstTy.isSimple())
    return BaseT::getCastInstrCost(Opcode, Dst, Src);

  // Some arithmetic, load and store operations have specific instructions
  // to cast up/down their types automatically at no extra cost.
  // TODO: Get these tables to know at least what the related operations are.
  static const TypeConversionCostTblEntry<MVT::SimpleValueType>
  NEONVectorConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i16, 0 },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i16, 0 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i32, 1 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i32, 1 },
    { ISD::TRUNCATE,    MVT::v4i32, MVT::v4i64, 0 },
    { ISD::TRUNCATE,    MVT::v4i16, MVT::v4i32, 1 },

    // The number of vmovl instructions for the extension.
    { ISD::SIGN_EXTEND, MVT::v4i64, MVT::v4i16, 3 },
    { ISD::ZERO_EXTEND, MVT::v4i64, MVT::v4i16, 3 },
    { ISD::SIGN_EXTEND, MVT::v8i32, MVT::v8i8, 3 },
    { ISD::ZERO_EXTEND, MVT::v8i32, MVT::v8i8, 3 },
    { ISD::SIGN_EXTEND, MVT::v8i64, MVT::v8i8, 7 },
    { ISD::ZERO_EXTEND, MVT::v8i64, MVT::v8i8, 7 },
    { ISD::SIGN_EXTEND, MVT::v8i64, MVT::v8i16, 6 },
    { ISD::ZERO_EXTEND, MVT::v8i64, MVT::v8i16, 6 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8, 6 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8, 6 },

    // Operations that we legalize using splitting.
    { ISD::TRUNCATE,    MVT::v16i8, MVT::v16i32, 6 },
    { ISD::TRUNCATE,    MVT::v8i8, MVT::v8i32, 3 },

    // Vector float <-> i32 conversions.
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i32, 1 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i32, 1 },

    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i8, 3 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i8, 3 },
    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i16, 2 },
    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i32, 1 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i32, 1 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i1, 3 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i1, 3 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i8, 3 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i8, 3 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i16, 2 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i16, 4 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i16, 4 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i32, 2 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i16, 8 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i16, 8 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i32, 4 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i32, 4 },

    { ISD::FP_TO_SINT,  MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_UINT,  MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_SINT,  MVT::v4i8, MVT::v4f32, 3 },
    { ISD::FP_TO_UINT,  MVT::v4i8, MVT::v4f32, 3 },
    { ISD::FP_TO_SINT,  MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_UINT,  MVT::v4i16, MVT::v4f32, 2 },

    // Vector double <-> i32 conversions.
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },

    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i8, 4 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i8, 4 },
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i16, 3 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i16, 3 },
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },

    { ISD::FP_TO_SINT,  MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT,  MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT,  MVT::v8i16, MVT::v8f32, 4 },
    { ISD::FP_TO_UINT,  MVT::v8i16, MVT::v8f32, 4 },
    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v16f32, 8 },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v16f32, 8 }
  };

  if (SrcTy.isVector() && ST->hasNEON()) {
    int Idx = ConvertCostTableLookup(NEONVectorConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT());
    if (Idx != -1)
      return NEONVectorConversionTbl[Idx].Cost;
  }

  // Scalar float to integer conversions.
  static const TypeConversionCostTblEntry<MVT::SimpleValueType>
  NEONFloatConversionTbl[] = {
    { ISD::FP_TO_SINT,  MVT::i1, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i1, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i1, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i1, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i8, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i8, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i8, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i8, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i16, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i16, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i16, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i16, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i32, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i32, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i32, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i32, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i64, MVT::f32, 10 },
    { ISD::FP_TO_UINT,  MVT::i64, MVT::f32, 10 },
    { ISD::FP_TO_SINT,  MVT::i64, MVT::f64, 10 },
    { ISD::FP_TO_UINT,  MVT::i64, MVT::f64, 10 }
  };
  if (SrcTy.isFloatingPoint() && ST->hasNEON()) {
    int Idx = ConvertCostTableLookup(NEONFloatConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT());
    if (Idx != -1)
        return NEONFloatConversionTbl[Idx].Cost;
  }

  // Scalar integer to float conversions.
  static const TypeConversionCostTblEntry<MVT::SimpleValueType>
  NEONIntegerConversionTbl[] = {
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i1, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i1, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i1, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i1, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i8, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i8, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i8, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i8, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i16, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i16, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i16, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i16, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i32, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i32, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i32, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i32, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i64, 10 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i64, 10 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i64, 10 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i64, 10 }
  };

  if (SrcTy.isInteger() && ST->hasNEON()) {
    int Idx = ConvertCostTableLookup(NEONIntegerConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT());
    if (Idx != -1)
      return NEONIntegerConversionTbl[Idx].Cost;
  }

  // Scalar integer conversion costs.
  static const TypeConversionCostTblEntry<MVT::SimpleValueType>
  ARMIntegerConversionTbl[] = {
    // i16 -> i64 requires two dependent operations.
    { ISD::SIGN_EXTEND, MVT::i64, MVT::i16, 2 },

    // Truncates on i64 are assumed to be free.
    { ISD::TRUNCATE,    MVT::i32, MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i16, MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i8,  MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i1,  MVT::i64, 0 }
  };

  if (SrcTy.isInteger()) {
    int Idx = ConvertCostTableLookup(ARMIntegerConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT());
    if (Idx != -1)
      return ARMIntegerConversionTbl[Idx].Cost;
  }

  return BaseT::getCastInstrCost(Opcode, Dst, Src);
}

unsigned ARMTTIImpl::getVectorInstrCost(unsigned Opcode, Type *ValTy,
                                        unsigned Index) {
  // Penalize inserting into an D-subregister. We end up with a three times
  // lower estimated throughput on swift.
  if (ST->isSwift() &&
      Opcode == Instruction::InsertElement &&
      ValTy->isVectorTy() &&
      ValTy->getScalarSizeInBits() <= 32)
    return 3;

  // Cross-class copies are expensive on many microarchitectures,
  // so assume they are expensive by default.
  if ((Opcode == Instruction::InsertElement ||
       Opcode == Instruction::ExtractElement) &&
      ValTy->getVectorElementType()->isIntegerTy())
    return 3;

  return BaseT::getVectorInstrCost(Opcode, ValTy, Index);
}

unsigned ARMTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                        Type *CondTy) {

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  // On NEON a a vector select gets lowered to vbsl.
  if (ST->hasNEON() && ValTy->isVectorTy() && ISD == ISD::SELECT) {
    // Lowering of some vector selects is currently far from perfect.
    static const TypeConversionCostTblEntry<MVT::SimpleValueType>
    NEONVectorSelectTbl[] = {
      { ISD::SELECT, MVT::v16i1, MVT::v16i16, 2*16 + 1 + 3*1 + 4*1 },
      { ISD::SELECT, MVT::v8i1, MVT::v8i32, 4*8 + 1*3 + 1*4 + 1*2 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i32, 4*16 + 1*6 + 1*8 + 1*4 },
      { ISD::SELECT, MVT::v4i1, MVT::v4i64, 4*4 + 1*2 + 1 },
      { ISD::SELECT, MVT::v8i1, MVT::v8i64, 50 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i64, 100 }
    };

    EVT SelCondTy = TLI->getValueType(CondTy);
    EVT SelValTy = TLI->getValueType(ValTy);
    if (SelCondTy.isSimple() && SelValTy.isSimple()) {
      int Idx = ConvertCostTableLookup(NEONVectorSelectTbl, ISD,
                                       SelCondTy.getSimpleVT(),
                                       SelValTy.getSimpleVT());
      if (Idx != -1)
        return NEONVectorSelectTbl[Idx].Cost;
    }

    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(ValTy);
    return LT.first;
  }

  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy);
}

unsigned ARMTTIImpl::getAddressComputationCost(Type *Ty, bool IsComplex) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  unsigned NumVectorInstToHideOverhead = 10;

  if (Ty->isVectorTy() && IsComplex)
    return NumVectorInstToHideOverhead;

  // In many cases the address computation is not merged into the instruction
  // addressing mode.
  return 1;
}

unsigned ARMTTIImpl::getFPOpCost(Type *Ty) {
  // Use similar logic that's in ARMISelLowering:
  // Any ARM CPU with VFP2 has floating point, but Thumb1 didn't have access
  // to VFP.

  if (ST->hasVFP2() && !ST->isThumb1Only()) {
    if (Ty->isFloatTy()) {
      return TargetTransformInfo::TCC_Basic;
    }

    if (Ty->isDoubleTy()) {
      return ST->isFPOnlySP() ? TargetTransformInfo::TCC_Expensive :
        TargetTransformInfo::TCC_Basic;
    }
  }

  return TargetTransformInfo::TCC_Expensive;
}

unsigned ARMTTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                                    Type *SubTp) {
  // We only handle costs of reverse and alternate shuffles for now.
  if (Kind != TTI::SK_Reverse && Kind != TTI::SK_Alternate)
    return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);

  if (Kind == TTI::SK_Reverse) {
    static const CostTblEntry<MVT::SimpleValueType> NEONShuffleTbl[] = {
        // Reverse shuffle cost one instruction if we are shuffling within a
        // double word (vrev) or two if we shuffle a quad word (vrev, vext).
        {ISD::VECTOR_SHUFFLE, MVT::v2i32, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2f32, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2i64, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2f64, 1},

        {ISD::VECTOR_SHUFFLE, MVT::v4i32, 2},
        {ISD::VECTOR_SHUFFLE, MVT::v4f32, 2},
        {ISD::VECTOR_SHUFFLE, MVT::v8i16, 2},
        {ISD::VECTOR_SHUFFLE, MVT::v16i8, 2}};

    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Tp);

    int Idx = CostTableLookup(NEONShuffleTbl, ISD::VECTOR_SHUFFLE, LT.second);
    if (Idx == -1)
      return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);

    return LT.first * NEONShuffleTbl[Idx].Cost;
  }
  if (Kind == TTI::SK_Alternate) {
    static const CostTblEntry<MVT::SimpleValueType> NEONAltShuffleTbl[] = {
        // Alt shuffle cost table for ARM. Cost is the number of instructions
        // required to create the shuffled vector.

        {ISD::VECTOR_SHUFFLE, MVT::v2f32, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2i64, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2f64, 1},
        {ISD::VECTOR_SHUFFLE, MVT::v2i32, 1},

        {ISD::VECTOR_SHUFFLE, MVT::v4i32, 2},
        {ISD::VECTOR_SHUFFLE, MVT::v4f32, 2},
        {ISD::VECTOR_SHUFFLE, MVT::v4i16, 2},

        {ISD::VECTOR_SHUFFLE, MVT::v8i16, 16},

        {ISD::VECTOR_SHUFFLE, MVT::v16i8, 32}};

    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Tp);
    int Idx =
        CostTableLookup(NEONAltShuffleTbl, ISD::VECTOR_SHUFFLE, LT.second);
    if (Idx == -1)
      return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
    return LT.first * NEONAltShuffleTbl[Idx].Cost;
  }
  return BaseT::getShuffleCost(Kind, Tp, Index, SubTp);
}

unsigned ARMTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::OperandValueKind Op1Info,
    TTI::OperandValueKind Op2Info, TTI::OperandValueProperties Opd1PropInfo,
    TTI::OperandValueProperties Opd2PropInfo) {

  int ISDOpcode = TLI->InstructionOpcodeToISD(Opcode);
  std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Ty);

  const unsigned FunctionCallDivCost = 20;
  const unsigned ReciprocalDivCost = 10;
  static const CostTblEntry<MVT::SimpleValueType> CostTbl[] = {
    // Division.
    // These costs are somewhat random. Choose a cost of 20 to indicate that
    // vectorizing devision (added function call) is going to be very expensive.
    // Double registers types.
    { ISD::SDIV, MVT::v1i64, 1 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v1i64, 1 * FunctionCallDivCost},
    { ISD::SREM, MVT::v1i64, 1 * FunctionCallDivCost},
    { ISD::UREM, MVT::v1i64, 1 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v2i32, 2 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v2i32, 2 * FunctionCallDivCost},
    { ISD::SREM, MVT::v2i32, 2 * FunctionCallDivCost},
    { ISD::UREM, MVT::v2i32, 2 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v4i16,     ReciprocalDivCost},
    { ISD::UDIV, MVT::v4i16,     ReciprocalDivCost},
    { ISD::SREM, MVT::v4i16, 4 * FunctionCallDivCost},
    { ISD::UREM, MVT::v4i16, 4 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v8i8,      ReciprocalDivCost},
    { ISD::UDIV, MVT::v8i8,      ReciprocalDivCost},
    { ISD::SREM, MVT::v8i8,  8 * FunctionCallDivCost},
    { ISD::UREM, MVT::v8i8,  8 * FunctionCallDivCost},
    // Quad register types.
    { ISD::SDIV, MVT::v2i64, 2 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v2i64, 2 * FunctionCallDivCost},
    { ISD::SREM, MVT::v2i64, 2 * FunctionCallDivCost},
    { ISD::UREM, MVT::v2i64, 2 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v4i32, 4 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v4i32, 4 * FunctionCallDivCost},
    { ISD::SREM, MVT::v4i32, 4 * FunctionCallDivCost},
    { ISD::UREM, MVT::v4i32, 4 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v8i16, 8 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v8i16, 8 * FunctionCallDivCost},
    { ISD::SREM, MVT::v8i16, 8 * FunctionCallDivCost},
    { ISD::UREM, MVT::v8i16, 8 * FunctionCallDivCost},
    { ISD::SDIV, MVT::v16i8, 16 * FunctionCallDivCost},
    { ISD::UDIV, MVT::v16i8, 16 * FunctionCallDivCost},
    { ISD::SREM, MVT::v16i8, 16 * FunctionCallDivCost},
    { ISD::UREM, MVT::v16i8, 16 * FunctionCallDivCost},
    // Multiplication.
  };

  int Idx = -1;

  if (ST->hasNEON())
    Idx = CostTableLookup(CostTbl, ISDOpcode, LT.second);

  if (Idx != -1)
    return LT.first * CostTbl[Idx].Cost;

  unsigned Cost = BaseT::getArithmeticInstrCost(Opcode, Ty, Op1Info, Op2Info,
                                                Opd1PropInfo, Opd2PropInfo);

  // This is somewhat of a hack. The problem that we are facing is that SROA
  // creates a sequence of shift, and, or instructions to construct values.
  // These sequences are recognized by the ISel and have zero-cost. Not so for
  // the vectorized code. Because we have support for v2i64 but not i64 those
  // sequences look particularly beneficial to vectorize.
  // To work around this we increase the cost of v2i64 operations to make them
  // seem less beneficial.
  if (LT.second == MVT::v2i64 &&
      Op2Info == TargetTransformInfo::OK_UniformConstantValue)
    Cost += 4;

  return Cost;
}

unsigned ARMTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                     unsigned Alignment,
                                     unsigned AddressSpace) {
  std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(Src);

  if (Src->isVectorTy() && Alignment != 16 &&
      Src->getVectorElementType()->isDoubleTy()) {
    // Unaligned loads/stores are extremely inefficient.
    // We need 4 uops for vst.1/vld.1 vs 1uop for vldr/vstr.
    return LT.first * 4;
  }
  return LT.first;
}
