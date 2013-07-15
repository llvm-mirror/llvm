//===-- NVPTXISelDAGToDAG.cpp - A dag to dag inst selector for NVPTX ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the NVPTX target.
//
//===----------------------------------------------------------------------===//

#include "NVPTXISelDAGToDAG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "nvptx-isel"

using namespace llvm;

static cl::opt<bool> UseFMADInstruction(
    "nvptx-mad-enable", cl::ZeroOrMore,
    cl::desc("NVPTX Specific: Enable generating FMAD instructions"),
    cl::init(false));

static cl::opt<int>
FMAContractLevel("nvptx-fma-level", cl::ZeroOrMore,
                 cl::desc("NVPTX Specific: FMA contraction (0: don't do it"
                          " 1: do it  2: do it aggressively"),
                 cl::init(2));

static cl::opt<int> UsePrecDivF32(
    "nvptx-prec-divf32", cl::ZeroOrMore,
    cl::desc("NVPTX Specifies: 0 use div.approx, 1 use div.full, 2 use"
             " IEEE Compliant F32 div.rnd if avaiable."),
    cl::init(2));

static cl::opt<bool>
UsePrecSqrtF32("nvptx-prec-sqrtf32",
          cl::desc("NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn."),
          cl::init(true));

/// createNVPTXISelDag - This pass converts a legalized DAG into a
/// NVPTX-specific DAG, ready for instruction scheduling.
FunctionPass *llvm::createNVPTXISelDag(NVPTXTargetMachine &TM,
                                       llvm::CodeGenOpt::Level OptLevel) {
  return new NVPTXDAGToDAGISel(TM, OptLevel);
}

NVPTXDAGToDAGISel::NVPTXDAGToDAGISel(NVPTXTargetMachine &tm,
                                     CodeGenOpt::Level OptLevel)
    : SelectionDAGISel(tm, OptLevel),
      Subtarget(tm.getSubtarget<NVPTXSubtarget>()) {
  // Always do fma.f32 fpcontract if the target supports the instruction.
  // Always do fma.f64 fpcontract if the target supports the instruction.
  // Do mad.f32 is nvptx-mad-enable is specified and the target does not
  // support fma.f32.

  doFMADF32 = (OptLevel > 0) && UseFMADInstruction && !Subtarget.hasFMAF32();
  doFMAF32 = (OptLevel > 0) && Subtarget.hasFMAF32() && (FMAContractLevel >= 1);
  doFMAF64 = (OptLevel > 0) && Subtarget.hasFMAF64() && (FMAContractLevel >= 1);
  doFMAF32AGG =
      (OptLevel > 0) && Subtarget.hasFMAF32() && (FMAContractLevel == 2);
  doFMAF64AGG =
      (OptLevel > 0) && Subtarget.hasFMAF64() && (FMAContractLevel == 2);

  allowFMA = (FMAContractLevel >= 1) || UseFMADInstruction;

  UseF32FTZ = false;

  doMulWide = (OptLevel > 0);

  // Decide how to translate f32 div
  do_DIVF32_PREC = UsePrecDivF32;
  // Decide how to translate f32 sqrt
  do_SQRTF32_PREC = UsePrecSqrtF32;
  // sm less than sm_20 does not support div.rnd. Use div.full.
  if (do_DIVF32_PREC == 2 && !Subtarget.reqPTX20())
    do_DIVF32_PREC = 1;

}

/// Select - Select instructions not customized! Used for
/// expanded, promoted and normal instructions.
SDNode *NVPTXDAGToDAGISel::Select(SDNode *N) {

  if (N->isMachineOpcode())
    return NULL; // Already selected.

  SDNode *ResNode = NULL;
  switch (N->getOpcode()) {
  case ISD::LOAD:
    ResNode = SelectLoad(N);
    break;
  case ISD::STORE:
    ResNode = SelectStore(N);
    break;
  case NVPTXISD::LoadV2:
  case NVPTXISD::LoadV4:
    ResNode = SelectLoadVector(N);
    break;
  case NVPTXISD::LDGV2:
  case NVPTXISD::LDGV4:
  case NVPTXISD::LDUV2:
  case NVPTXISD::LDUV4:
    ResNode = SelectLDGLDUVector(N);
    break;
  case NVPTXISD::StoreV2:
  case NVPTXISD::StoreV4:
    ResNode = SelectStoreVector(N);
    break;
  case NVPTXISD::LoadParam:
  case NVPTXISD::LoadParamV2:
  case NVPTXISD::LoadParamV4:
    ResNode = SelectLoadParam(N);
    break;
  case NVPTXISD::StoreRetval:
  case NVPTXISD::StoreRetvalV2:
  case NVPTXISD::StoreRetvalV4:
    ResNode = SelectStoreRetval(N);
    break;
  case NVPTXISD::StoreParam:
  case NVPTXISD::StoreParamV2:
  case NVPTXISD::StoreParamV4:
  case NVPTXISD::StoreParamS32:
  case NVPTXISD::StoreParamU32:
    ResNode = SelectStoreParam(N);
    break;
  default:
    break;
  }
  if (ResNode)
    return ResNode;
  return SelectCode(N);
}

static unsigned int getCodeAddrSpace(MemSDNode *N,
                                     const NVPTXSubtarget &Subtarget) {
  const Value *Src = N->getSrcValue();

  if (!Src)
    return NVPTX::PTXLdStInstCode::GENERIC;

  if (const PointerType *PT = dyn_cast<PointerType>(Src->getType())) {
    switch (PT->getAddressSpace()) {
    case llvm::ADDRESS_SPACE_LOCAL: return NVPTX::PTXLdStInstCode::LOCAL;
    case llvm::ADDRESS_SPACE_GLOBAL: return NVPTX::PTXLdStInstCode::GLOBAL;
    case llvm::ADDRESS_SPACE_SHARED: return NVPTX::PTXLdStInstCode::SHARED;
    case llvm::ADDRESS_SPACE_GENERIC: return NVPTX::PTXLdStInstCode::GENERIC;
    case llvm::ADDRESS_SPACE_PARAM: return NVPTX::PTXLdStInstCode::PARAM;
    case llvm::ADDRESS_SPACE_CONST: return NVPTX::PTXLdStInstCode::CONSTANT;
    default: break;
    }
  }
  return NVPTX::PTXLdStInstCode::GENERIC;
}

SDNode *NVPTXDAGToDAGISel::SelectLoad(SDNode *N) {
  SDLoc dl(N);
  LoadSDNode *LD = cast<LoadSDNode>(N);
  EVT LoadedVT = LD->getMemoryVT();
  SDNode *NVPTXLD = NULL;

  // do not support pre/post inc/dec
  if (LD->isIndexed())
    return NULL;

  if (!LoadedVT.isSimple())
    return NULL;

  // Address Space Setting
  unsigned int codeAddrSpace = getCodeAddrSpace(LD, Subtarget);

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool isVolatile = LD->isVolatile();
  if (codeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      codeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      codeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    isVolatile = false;

  // Vector Setting
  MVT SimpleVT = LoadedVT.getSimpleVT();
  unsigned vecType = NVPTX::PTXLdStInstCode::Scalar;
  if (SimpleVT.isVector()) {
    unsigned num = SimpleVT.getVectorNumElements();
    if (num == 2)
      vecType = NVPTX::PTXLdStInstCode::V2;
    else if (num == 4)
      vecType = NVPTX::PTXLdStInstCode::V4;
    else
      return NULL;
  }

  // Type Setting: fromType + fromTypeWidth
  //
  // Sign   : ISD::SEXTLOAD
  // Unsign : ISD::ZEXTLOAD, ISD::NON_EXTLOAD or ISD::EXTLOAD and the
  //          type is integer
  // Float  : ISD::NON_EXTLOAD or ISD::EXTLOAD and the type is float
  MVT ScalarVT = SimpleVT.getScalarType();
  // Read at least 8 bits (predicates are stored as 8-bit values)
  unsigned fromTypeWidth = std::max(8U, ScalarVT.getSizeInBits());
  unsigned int fromType;
  if ((LD->getExtensionType() == ISD::SEXTLOAD))
    fromType = NVPTX::PTXLdStInstCode::Signed;
  else if (ScalarVT.isFloatingPoint())
    fromType = NVPTX::PTXLdStInstCode::Float;
  else
    fromType = NVPTX::PTXLdStInstCode::Unsigned;

  // Create the machine instruction DAG
  SDValue Chain = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue Addr;
  SDValue Offset, Base;
  unsigned Opcode;
  MVT::SimpleValueType TargetVT = LD->getValueType(0).getSimpleVT().SimpleTy;

  if (SelectDirectAddr(N1, Addr)) {
    switch (TargetVT) {
    case MVT::i8:
      Opcode = NVPTX::LD_i8_avar;
      break;
    case MVT::i16:
      Opcode = NVPTX::LD_i16_avar;
      break;
    case MVT::i32:
      Opcode = NVPTX::LD_i32_avar;
      break;
    case MVT::i64:
      Opcode = NVPTX::LD_i64_avar;
      break;
    case MVT::f32:
      Opcode = NVPTX::LD_f32_avar;
      break;
    case MVT::f64:
      Opcode = NVPTX::LD_f64_avar;
      break;
    default:
      return NULL;
    }
    SDValue Ops[] = { getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(fromType),
                      getI32Imm(fromTypeWidth), Addr, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRsi64(N1.getNode(), N1, Base, Offset)
                 : SelectADDRsi(N1.getNode(), N1, Base, Offset)) {
    switch (TargetVT) {
    case MVT::i8:
      Opcode = NVPTX::LD_i8_asi;
      break;
    case MVT::i16:
      Opcode = NVPTX::LD_i16_asi;
      break;
    case MVT::i32:
      Opcode = NVPTX::LD_i32_asi;
      break;
    case MVT::i64:
      Opcode = NVPTX::LD_i64_asi;
      break;
    case MVT::f32:
      Opcode = NVPTX::LD_f32_asi;
      break;
    case MVT::f64:
      Opcode = NVPTX::LD_f64_asi;
      break;
    default:
      return NULL;
    }
    SDValue Ops[] = { getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(fromType),
                      getI32Imm(fromTypeWidth), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRri64(N1.getNode(), N1, Base, Offset)
                 : SelectADDRri(N1.getNode(), N1, Base, Offset)) {
    if (Subtarget.is64Bit()) {
      switch (TargetVT) {
      case MVT::i8:
        Opcode = NVPTX::LD_i8_ari_64;
        break;
      case MVT::i16:
        Opcode = NVPTX::LD_i16_ari_64;
        break;
      case MVT::i32:
        Opcode = NVPTX::LD_i32_ari_64;
        break;
      case MVT::i64:
        Opcode = NVPTX::LD_i64_ari_64;
        break;
      case MVT::f32:
        Opcode = NVPTX::LD_f32_ari_64;
        break;
      case MVT::f64:
        Opcode = NVPTX::LD_f64_ari_64;
        break;
      default:
        return NULL;
      }
    } else {
      switch (TargetVT) {
      case MVT::i8:
        Opcode = NVPTX::LD_i8_ari;
        break;
      case MVT::i16:
        Opcode = NVPTX::LD_i16_ari;
        break;
      case MVT::i32:
        Opcode = NVPTX::LD_i32_ari;
        break;
      case MVT::i64:
        Opcode = NVPTX::LD_i64_ari;
        break;
      case MVT::f32:
        Opcode = NVPTX::LD_f32_ari;
        break;
      case MVT::f64:
        Opcode = NVPTX::LD_f64_ari;
        break;
      default:
        return NULL;
      }
    }
    SDValue Ops[] = { getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(fromType),
                      getI32Imm(fromTypeWidth), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else {
    if (Subtarget.is64Bit()) {
      switch (TargetVT) {
      case MVT::i8:
        Opcode = NVPTX::LD_i8_areg_64;
        break;
      case MVT::i16:
        Opcode = NVPTX::LD_i16_areg_64;
        break;
      case MVT::i32:
        Opcode = NVPTX::LD_i32_areg_64;
        break;
      case MVT::i64:
        Opcode = NVPTX::LD_i64_areg_64;
        break;
      case MVT::f32:
        Opcode = NVPTX::LD_f32_areg_64;
        break;
      case MVT::f64:
        Opcode = NVPTX::LD_f64_areg_64;
        break;
      default:
        return NULL;
      }
    } else {
      switch (TargetVT) {
      case MVT::i8:
        Opcode = NVPTX::LD_i8_areg;
        break;
      case MVT::i16:
        Opcode = NVPTX::LD_i16_areg;
        break;
      case MVT::i32:
        Opcode = NVPTX::LD_i32_areg;
        break;
      case MVT::i64:
        Opcode = NVPTX::LD_i64_areg;
        break;
      case MVT::f32:
        Opcode = NVPTX::LD_f32_areg;
        break;
      case MVT::f64:
        Opcode = NVPTX::LD_f64_areg;
        break;
      default:
        return NULL;
      }
    }
    SDValue Ops[] = { getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(fromType),
                      getI32Imm(fromTypeWidth), N1, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  }

  if (NVPTXLD != NULL) {
    MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
    MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
    cast<MachineSDNode>(NVPTXLD)->setMemRefs(MemRefs0, MemRefs0 + 1);
  }

  return NVPTXLD;
}

SDNode *NVPTXDAGToDAGISel::SelectLoadVector(SDNode *N) {

  SDValue Chain = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDValue Addr, Offset, Base;
  unsigned Opcode;
  SDLoc DL(N);
  SDNode *LD;
  MemSDNode *MemSD = cast<MemSDNode>(N);
  EVT LoadedVT = MemSD->getMemoryVT();

  if (!LoadedVT.isSimple())
    return NULL;

  // Address Space Setting
  unsigned int CodeAddrSpace = getCodeAddrSpace(MemSD, Subtarget);

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool IsVolatile = MemSD->isVolatile();
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    IsVolatile = false;

  // Vector Setting
  MVT SimpleVT = LoadedVT.getSimpleVT();

  // Type Setting: fromType + fromTypeWidth
  //
  // Sign   : ISD::SEXTLOAD
  // Unsign : ISD::ZEXTLOAD, ISD::NON_EXTLOAD or ISD::EXTLOAD and the
  //          type is integer
  // Float  : ISD::NON_EXTLOAD or ISD::EXTLOAD and the type is float
  MVT ScalarVT = SimpleVT.getScalarType();
  // Read at least 8 bits (predicates are stored as 8-bit values)
  unsigned FromTypeWidth = std::max(8U, ScalarVT.getSizeInBits());
  unsigned int FromType;
  // The last operand holds the original LoadSDNode::getExtensionType() value
  unsigned ExtensionType = cast<ConstantSDNode>(
      N->getOperand(N->getNumOperands() - 1))->getZExtValue();
  if (ExtensionType == ISD::SEXTLOAD)
    FromType = NVPTX::PTXLdStInstCode::Signed;
  else if (ScalarVT.isFloatingPoint())
    FromType = NVPTX::PTXLdStInstCode::Float;
  else
    FromType = NVPTX::PTXLdStInstCode::Unsigned;

  unsigned VecType;

  switch (N->getOpcode()) {
  case NVPTXISD::LoadV2:
    VecType = NVPTX::PTXLdStInstCode::V2;
    break;
  case NVPTXISD::LoadV4:
    VecType = NVPTX::PTXLdStInstCode::V4;
    break;
  default:
    return NULL;
  }

  EVT EltVT = N->getValueType(0);

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return NULL;
    case NVPTXISD::LoadV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::LDV_i8_v2_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::LDV_i16_v2_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::LDV_i32_v2_avar;
        break;
      case MVT::i64:
        Opcode = NVPTX::LDV_i64_v2_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::LDV_f32_v2_avar;
        break;
      case MVT::f64:
        Opcode = NVPTX::LDV_f64_v2_avar;
        break;
      }
      break;
    case NVPTXISD::LoadV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::LDV_i8_v4_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::LDV_i16_v4_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::LDV_i32_v4_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::LDV_f32_v4_avar;
        break;
      }
      break;
    }

    SDValue Ops[] = { getI32Imm(IsVolatile), getI32Imm(CodeAddrSpace),
                      getI32Imm(VecType), getI32Imm(FromType),
                      getI32Imm(FromTypeWidth), Addr, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRsi64(Op1.getNode(), Op1, Base, Offset)
                 : SelectADDRsi(Op1.getNode(), Op1, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return NULL;
    case NVPTXISD::LoadV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::LDV_i8_v2_asi;
        break;
      case MVT::i16:
        Opcode = NVPTX::LDV_i16_v2_asi;
        break;
      case MVT::i32:
        Opcode = NVPTX::LDV_i32_v2_asi;
        break;
      case MVT::i64:
        Opcode = NVPTX::LDV_i64_v2_asi;
        break;
      case MVT::f32:
        Opcode = NVPTX::LDV_f32_v2_asi;
        break;
      case MVT::f64:
        Opcode = NVPTX::LDV_f64_v2_asi;
        break;
      }
      break;
    case NVPTXISD::LoadV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::LDV_i8_v4_asi;
        break;
      case MVT::i16:
        Opcode = NVPTX::LDV_i16_v4_asi;
        break;
      case MVT::i32:
        Opcode = NVPTX::LDV_i32_v4_asi;
        break;
      case MVT::f32:
        Opcode = NVPTX::LDV_f32_v4_asi;
        break;
      }
      break;
    }

    SDValue Ops[] = { getI32Imm(IsVolatile), getI32Imm(CodeAddrSpace),
                      getI32Imm(VecType), getI32Imm(FromType),
                      getI32Imm(FromTypeWidth), Base, Offset, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                 : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v2_ari_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v2_ari_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v2_ari_64;
          break;
        case MVT::i64:
          Opcode = NVPTX::LDV_i64_v2_ari_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v2_ari_64;
          break;
        case MVT::f64:
          Opcode = NVPTX::LDV_f64_v2_ari_64;
          break;
        }
        break;
      case NVPTXISD::LoadV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v4_ari_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v4_ari_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v4_ari_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v4_ari_64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v2_ari;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v2_ari;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v2_ari;
          break;
        case MVT::i64:
          Opcode = NVPTX::LDV_i64_v2_ari;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v2_ari;
          break;
        case MVT::f64:
          Opcode = NVPTX::LDV_f64_v2_ari;
          break;
        }
        break;
      case NVPTXISD::LoadV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v4_ari;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v4_ari;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v4_ari;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v4_ari;
          break;
        }
        break;
      }
    }

    SDValue Ops[] = { getI32Imm(IsVolatile), getI32Imm(CodeAddrSpace),
                      getI32Imm(VecType), getI32Imm(FromType),
                      getI32Imm(FromTypeWidth), Base, Offset, Chain };

    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v2_areg_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v2_areg_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v2_areg_64;
          break;
        case MVT::i64:
          Opcode = NVPTX::LDV_i64_v2_areg_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v2_areg_64;
          break;
        case MVT::f64:
          Opcode = NVPTX::LDV_f64_v2_areg_64;
          break;
        }
        break;
      case NVPTXISD::LoadV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v4_areg_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v4_areg_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v4_areg_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v4_areg_64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v2_areg;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v2_areg;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v2_areg;
          break;
        case MVT::i64:
          Opcode = NVPTX::LDV_i64_v2_areg;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v2_areg;
          break;
        case MVT::f64:
          Opcode = NVPTX::LDV_f64_v2_areg;
          break;
        }
        break;
      case NVPTXISD::LoadV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::LDV_i8_v4_areg;
          break;
        case MVT::i16:
          Opcode = NVPTX::LDV_i16_v4_areg;
          break;
        case MVT::i32:
          Opcode = NVPTX::LDV_i32_v4_areg;
          break;
        case MVT::f32:
          Opcode = NVPTX::LDV_f32_v4_areg;
          break;
        }
        break;
      }
    }

    SDValue Ops[] = { getI32Imm(IsVolatile), getI32Imm(CodeAddrSpace),
                      getI32Imm(VecType), getI32Imm(FromType),
                      getI32Imm(FromTypeWidth), Op1, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  }

  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(LD)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return LD;
}

SDNode *NVPTXDAGToDAGISel::SelectLDGLDUVector(SDNode *N) {

  SDValue Chain = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  unsigned Opcode;
  SDLoc DL(N);
  SDNode *LD;
  MemSDNode *Mem = cast<MemSDNode>(N);
  SDValue Base, Offset, Addr;

  EVT EltVT = Mem->getMemoryVT().getVectorElementType();

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return NULL;
    case NVPTXISD::LDGV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::INT_PTX_LDG_G_v2i8_ELE_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::INT_PTX_LDG_G_v2i16_ELE_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::INT_PTX_LDG_G_v2i32_ELE_avar;
        break;
      case MVT::i64:
        Opcode = NVPTX::INT_PTX_LDG_G_v2i64_ELE_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::INT_PTX_LDG_G_v2f32_ELE_avar;
        break;
      case MVT::f64:
        Opcode = NVPTX::INT_PTX_LDG_G_v2f64_ELE_avar;
        break;
      }
      break;
    case NVPTXISD::LDUV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::INT_PTX_LDU_G_v2i8_ELE_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::INT_PTX_LDU_G_v2i16_ELE_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::INT_PTX_LDU_G_v2i32_ELE_avar;
        break;
      case MVT::i64:
        Opcode = NVPTX::INT_PTX_LDU_G_v2i64_ELE_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::INT_PTX_LDU_G_v2f32_ELE_avar;
        break;
      case MVT::f64:
        Opcode = NVPTX::INT_PTX_LDU_G_v2f64_ELE_avar;
        break;
      }
      break;
    case NVPTXISD::LDGV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::INT_PTX_LDG_G_v4i8_ELE_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::INT_PTX_LDG_G_v4i16_ELE_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::INT_PTX_LDG_G_v4i32_ELE_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::INT_PTX_LDG_G_v4f32_ELE_avar;
        break;
      }
      break;
    case NVPTXISD::LDUV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::INT_PTX_LDU_G_v4i8_ELE_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::INT_PTX_LDU_G_v4i16_ELE_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::INT_PTX_LDU_G_v4i32_ELE_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::INT_PTX_LDU_G_v4f32_ELE_avar;
        break;
      }
      break;
    }

    SDValue Ops[] = { Addr, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(),
                                ArrayRef<SDValue>(Ops, 2));
  } else if (Subtarget.is64Bit()
                 ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                 : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i8_ELE_ari64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i16_ELE_ari64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i32_ELE_ari64;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i64_ELE_ari64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f32_ELE_ari64;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f64_ELE_ari64;
          break;
        }
        break;
      case NVPTXISD::LDUV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i8_ELE_ari64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i16_ELE_ari64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i32_ELE_ari64;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i64_ELE_ari64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f32_ELE_ari64;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f64_ELE_ari64;
          break;
        }
        break;
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i8_ELE_ari64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i16_ELE_ari64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i32_ELE_ari64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4f32_ELE_ari64;
          break;
        }
        break;
      case NVPTXISD::LDUV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i8_ELE_ari64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i16_ELE_ari64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i32_ELE_ari64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4f32_ELE_ari64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i8_ELE_ari32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i16_ELE_ari32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i32_ELE_ari32;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i64_ELE_ari32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f32_ELE_ari32;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f64_ELE_ari32;
          break;
        }
        break;
      case NVPTXISD::LDUV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i8_ELE_ari32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i16_ELE_ari32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i32_ELE_ari32;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i64_ELE_ari32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f32_ELE_ari32;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f64_ELE_ari32;
          break;
        }
        break;
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i8_ELE_ari32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i16_ELE_ari32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i32_ELE_ari32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4f32_ELE_ari32;
          break;
        }
        break;
      case NVPTXISD::LDUV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i8_ELE_ari32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i16_ELE_ari32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i32_ELE_ari32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4f32_ELE_ari32;
          break;
        }
        break;
      }
    }

    SDValue Ops[] = { Base, Offset, Chain };

    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(),
                                ArrayRef<SDValue>(Ops, 3));
  } else {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i8_ELE_areg64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i16_ELE_areg64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i32_ELE_areg64;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i64_ELE_areg64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f32_ELE_areg64;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f64_ELE_areg64;
          break;
        }
        break;
      case NVPTXISD::LDUV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i8_ELE_areg64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i16_ELE_areg64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i32_ELE_areg64;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i64_ELE_areg64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f32_ELE_areg64;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f64_ELE_areg64;
          break;
        }
        break;
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i8_ELE_areg64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i16_ELE_areg64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i32_ELE_areg64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4f32_ELE_areg64;
          break;
        }
        break;
      case NVPTXISD::LDUV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i8_ELE_areg64;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i16_ELE_areg64;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i32_ELE_areg64;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4f32_ELE_areg64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i8_ELE_areg32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i16_ELE_areg32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i32_ELE_areg32;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2i64_ELE_areg32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f32_ELE_areg32;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDG_G_v2f64_ELE_areg32;
          break;
        }
        break;
      case NVPTXISD::LDUV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i8_ELE_areg32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i16_ELE_areg32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i32_ELE_areg32;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2i64_ELE_areg32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f32_ELE_areg32;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDU_G_v2f64_ELE_areg32;
          break;
        }
        break;
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i8_ELE_areg32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i16_ELE_areg32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4i32_ELE_areg32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_G_v4f32_ELE_areg32;
          break;
        }
        break;
      case NVPTXISD::LDUV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i8_ELE_areg32;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i16_ELE_areg32;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4i32_ELE_areg32;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_G_v4f32_ELE_areg32;
          break;
        }
        break;
      }
    }

    SDValue Ops[] = { Op1, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(),
                                ArrayRef<SDValue>(Ops, 2));
  }

  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(LD)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return LD;
}

SDNode *NVPTXDAGToDAGISel::SelectStore(SDNode *N) {
  SDLoc dl(N);
  StoreSDNode *ST = cast<StoreSDNode>(N);
  EVT StoreVT = ST->getMemoryVT();
  SDNode *NVPTXST = NULL;

  // do not support pre/post inc/dec
  if (ST->isIndexed())
    return NULL;

  if (!StoreVT.isSimple())
    return NULL;

  // Address Space Setting
  unsigned int codeAddrSpace = getCodeAddrSpace(ST, Subtarget);

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool isVolatile = ST->isVolatile();
  if (codeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      codeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      codeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    isVolatile = false;

  // Vector Setting
  MVT SimpleVT = StoreVT.getSimpleVT();
  unsigned vecType = NVPTX::PTXLdStInstCode::Scalar;
  if (SimpleVT.isVector()) {
    unsigned num = SimpleVT.getVectorNumElements();
    if (num == 2)
      vecType = NVPTX::PTXLdStInstCode::V2;
    else if (num == 4)
      vecType = NVPTX::PTXLdStInstCode::V4;
    else
      return NULL;
  }

  // Type Setting: toType + toTypeWidth
  // - for integer type, always use 'u'
  //
  MVT ScalarVT = SimpleVT.getScalarType();
  unsigned toTypeWidth = ScalarVT.getSizeInBits();
  unsigned int toType;
  if (ScalarVT.isFloatingPoint())
    toType = NVPTX::PTXLdStInstCode::Float;
  else
    toType = NVPTX::PTXLdStInstCode::Unsigned;

  // Create the machine instruction DAG
  SDValue Chain = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  SDValue Addr;
  SDValue Offset, Base;
  unsigned Opcode;
  MVT::SimpleValueType SourceVT =
      N1.getNode()->getValueType(0).getSimpleVT().SimpleTy;

  if (SelectDirectAddr(N2, Addr)) {
    switch (SourceVT) {
    case MVT::i8:
      Opcode = NVPTX::ST_i8_avar;
      break;
    case MVT::i16:
      Opcode = NVPTX::ST_i16_avar;
      break;
    case MVT::i32:
      Opcode = NVPTX::ST_i32_avar;
      break;
    case MVT::i64:
      Opcode = NVPTX::ST_i64_avar;
      break;
    case MVT::f32:
      Opcode = NVPTX::ST_f32_avar;
      break;
    case MVT::f64:
      Opcode = NVPTX::ST_f64_avar;
      break;
    default:
      return NULL;
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(toType),
                      getI32Imm(toTypeWidth), Addr, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRsi64(N2.getNode(), N2, Base, Offset)
                 : SelectADDRsi(N2.getNode(), N2, Base, Offset)) {
    switch (SourceVT) {
    case MVT::i8:
      Opcode = NVPTX::ST_i8_asi;
      break;
    case MVT::i16:
      Opcode = NVPTX::ST_i16_asi;
      break;
    case MVT::i32:
      Opcode = NVPTX::ST_i32_asi;
      break;
    case MVT::i64:
      Opcode = NVPTX::ST_i64_asi;
      break;
    case MVT::f32:
      Opcode = NVPTX::ST_f32_asi;
      break;
    case MVT::f64:
      Opcode = NVPTX::ST_f64_asi;
      break;
    default:
      return NULL;
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(toType),
                      getI32Imm(toTypeWidth), Base, Offset, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRri64(N2.getNode(), N2, Base, Offset)
                 : SelectADDRri(N2.getNode(), N2, Base, Offset)) {
    if (Subtarget.is64Bit()) {
      switch (SourceVT) {
      case MVT::i8:
        Opcode = NVPTX::ST_i8_ari_64;
        break;
      case MVT::i16:
        Opcode = NVPTX::ST_i16_ari_64;
        break;
      case MVT::i32:
        Opcode = NVPTX::ST_i32_ari_64;
        break;
      case MVT::i64:
        Opcode = NVPTX::ST_i64_ari_64;
        break;
      case MVT::f32:
        Opcode = NVPTX::ST_f32_ari_64;
        break;
      case MVT::f64:
        Opcode = NVPTX::ST_f64_ari_64;
        break;
      default:
        return NULL;
      }
    } else {
      switch (SourceVT) {
      case MVT::i8:
        Opcode = NVPTX::ST_i8_ari;
        break;
      case MVT::i16:
        Opcode = NVPTX::ST_i16_ari;
        break;
      case MVT::i32:
        Opcode = NVPTX::ST_i32_ari;
        break;
      case MVT::i64:
        Opcode = NVPTX::ST_i64_ari;
        break;
      case MVT::f32:
        Opcode = NVPTX::ST_f32_ari;
        break;
      case MVT::f64:
        Opcode = NVPTX::ST_f64_ari;
        break;
      default:
        return NULL;
      }
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(toType),
                      getI32Imm(toTypeWidth), Base, Offset, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else {
    if (Subtarget.is64Bit()) {
      switch (SourceVT) {
      case MVT::i8:
        Opcode = NVPTX::ST_i8_areg_64;
        break;
      case MVT::i16:
        Opcode = NVPTX::ST_i16_areg_64;
        break;
      case MVT::i32:
        Opcode = NVPTX::ST_i32_areg_64;
        break;
      case MVT::i64:
        Opcode = NVPTX::ST_i64_areg_64;
        break;
      case MVT::f32:
        Opcode = NVPTX::ST_f32_areg_64;
        break;
      case MVT::f64:
        Opcode = NVPTX::ST_f64_areg_64;
        break;
      default:
        return NULL;
      }
    } else {
      switch (SourceVT) {
      case MVT::i8:
        Opcode = NVPTX::ST_i8_areg;
        break;
      case MVT::i16:
        Opcode = NVPTX::ST_i16_areg;
        break;
      case MVT::i32:
        Opcode = NVPTX::ST_i32_areg;
        break;
      case MVT::i64:
        Opcode = NVPTX::ST_i64_areg;
        break;
      case MVT::f32:
        Opcode = NVPTX::ST_f32_areg;
        break;
      case MVT::f64:
        Opcode = NVPTX::ST_f64_areg;
        break;
      default:
        return NULL;
      }
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile), getI32Imm(codeAddrSpace),
                      getI32Imm(vecType), getI32Imm(toType),
                      getI32Imm(toTypeWidth), N2, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  }

  if (NVPTXST != NULL) {
    MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
    MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
    cast<MachineSDNode>(NVPTXST)->setMemRefs(MemRefs0, MemRefs0 + 1);
  }

  return NVPTXST;
}

SDNode *NVPTXDAGToDAGISel::SelectStoreVector(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDValue Addr, Offset, Base;
  unsigned Opcode;
  SDLoc DL(N);
  SDNode *ST;
  EVT EltVT = Op1.getValueType();
  MemSDNode *MemSD = cast<MemSDNode>(N);
  EVT StoreVT = MemSD->getMemoryVT();

  // Address Space Setting
  unsigned CodeAddrSpace = getCodeAddrSpace(MemSD, Subtarget);

  if (CodeAddrSpace == NVPTX::PTXLdStInstCode::CONSTANT) {
    report_fatal_error("Cannot store to pointer that points to constant "
                       "memory space");
  }

  // Volatile Setting
  // - .volatile is only availalble for .global and .shared
  bool IsVolatile = MemSD->isVolatile();
  if (CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::SHARED &&
      CodeAddrSpace != NVPTX::PTXLdStInstCode::GENERIC)
    IsVolatile = false;

  // Type Setting: toType + toTypeWidth
  // - for integer type, always use 'u'
  assert(StoreVT.isSimple() && "Store value is not simple");
  MVT ScalarVT = StoreVT.getSimpleVT().getScalarType();
  unsigned ToTypeWidth = ScalarVT.getSizeInBits();
  unsigned ToType;
  if (ScalarVT.isFloatingPoint())
    ToType = NVPTX::PTXLdStInstCode::Float;
  else
    ToType = NVPTX::PTXLdStInstCode::Unsigned;

  SmallVector<SDValue, 12> StOps;
  SDValue N2;
  unsigned VecType;

  switch (N->getOpcode()) {
  case NVPTXISD::StoreV2:
    VecType = NVPTX::PTXLdStInstCode::V2;
    StOps.push_back(N->getOperand(1));
    StOps.push_back(N->getOperand(2));
    N2 = N->getOperand(3);
    break;
  case NVPTXISD::StoreV4:
    VecType = NVPTX::PTXLdStInstCode::V4;
    StOps.push_back(N->getOperand(1));
    StOps.push_back(N->getOperand(2));
    StOps.push_back(N->getOperand(3));
    StOps.push_back(N->getOperand(4));
    N2 = N->getOperand(5);
    break;
  default:
    return NULL;
  }

  StOps.push_back(getI32Imm(IsVolatile));
  StOps.push_back(getI32Imm(CodeAddrSpace));
  StOps.push_back(getI32Imm(VecType));
  StOps.push_back(getI32Imm(ToType));
  StOps.push_back(getI32Imm(ToTypeWidth));

  if (SelectDirectAddr(N2, Addr)) {
    switch (N->getOpcode()) {
    default:
      return NULL;
    case NVPTXISD::StoreV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::STV_i8_v2_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::STV_i16_v2_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::STV_i32_v2_avar;
        break;
      case MVT::i64:
        Opcode = NVPTX::STV_i64_v2_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::STV_f32_v2_avar;
        break;
      case MVT::f64:
        Opcode = NVPTX::STV_f64_v2_avar;
        break;
      }
      break;
    case NVPTXISD::StoreV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::STV_i8_v4_avar;
        break;
      case MVT::i16:
        Opcode = NVPTX::STV_i16_v4_avar;
        break;
      case MVT::i32:
        Opcode = NVPTX::STV_i32_v4_avar;
        break;
      case MVT::f32:
        Opcode = NVPTX::STV_f32_v4_avar;
        break;
      }
      break;
    }
    StOps.push_back(Addr);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRsi64(N2.getNode(), N2, Base, Offset)
                 : SelectADDRsi(N2.getNode(), N2, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return NULL;
    case NVPTXISD::StoreV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::STV_i8_v2_asi;
        break;
      case MVT::i16:
        Opcode = NVPTX::STV_i16_v2_asi;
        break;
      case MVT::i32:
        Opcode = NVPTX::STV_i32_v2_asi;
        break;
      case MVT::i64:
        Opcode = NVPTX::STV_i64_v2_asi;
        break;
      case MVT::f32:
        Opcode = NVPTX::STV_f32_v2_asi;
        break;
      case MVT::f64:
        Opcode = NVPTX::STV_f64_v2_asi;
        break;
      }
      break;
    case NVPTXISD::StoreV4:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i8:
        Opcode = NVPTX::STV_i8_v4_asi;
        break;
      case MVT::i16:
        Opcode = NVPTX::STV_i16_v4_asi;
        break;
      case MVT::i32:
        Opcode = NVPTX::STV_i32_v4_asi;
        break;
      case MVT::f32:
        Opcode = NVPTX::STV_f32_v4_asi;
        break;
      }
      break;
    }
    StOps.push_back(Base);
    StOps.push_back(Offset);
  } else if (Subtarget.is64Bit()
                 ? SelectADDRri64(N2.getNode(), N2, Base, Offset)
                 : SelectADDRri(N2.getNode(), N2, Base, Offset)) {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v2_ari_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v2_ari_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v2_ari_64;
          break;
        case MVT::i64:
          Opcode = NVPTX::STV_i64_v2_ari_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v2_ari_64;
          break;
        case MVT::f64:
          Opcode = NVPTX::STV_f64_v2_ari_64;
          break;
        }
        break;
      case NVPTXISD::StoreV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v4_ari_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v4_ari_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v4_ari_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v4_ari_64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v2_ari;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v2_ari;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v2_ari;
          break;
        case MVT::i64:
          Opcode = NVPTX::STV_i64_v2_ari;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v2_ari;
          break;
        case MVT::f64:
          Opcode = NVPTX::STV_f64_v2_ari;
          break;
        }
        break;
      case NVPTXISD::StoreV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v4_ari;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v4_ari;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v4_ari;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v4_ari;
          break;
        }
        break;
      }
    }
    StOps.push_back(Base);
    StOps.push_back(Offset);
  } else {
    if (Subtarget.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v2_areg_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v2_areg_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v2_areg_64;
          break;
        case MVT::i64:
          Opcode = NVPTX::STV_i64_v2_areg_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v2_areg_64;
          break;
        case MVT::f64:
          Opcode = NVPTX::STV_f64_v2_areg_64;
          break;
        }
        break;
      case NVPTXISD::StoreV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v4_areg_64;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v4_areg_64;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v4_areg_64;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v4_areg_64;
          break;
        }
        break;
      }
    } else {
      switch (N->getOpcode()) {
      default:
        return NULL;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v2_areg;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v2_areg;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v2_areg;
          break;
        case MVT::i64:
          Opcode = NVPTX::STV_i64_v2_areg;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v2_areg;
          break;
        case MVT::f64:
          Opcode = NVPTX::STV_f64_v2_areg;
          break;
        }
        break;
      case NVPTXISD::StoreV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return NULL;
        case MVT::i8:
          Opcode = NVPTX::STV_i8_v4_areg;
          break;
        case MVT::i16:
          Opcode = NVPTX::STV_i16_v4_areg;
          break;
        case MVT::i32:
          Opcode = NVPTX::STV_i32_v4_areg;
          break;
        case MVT::f32:
          Opcode = NVPTX::STV_f32_v4_areg;
          break;
        }
        break;
      }
    }
    StOps.push_back(N2);
  }

  StOps.push_back(Chain);

  ST = CurDAG->getMachineNode(Opcode, DL, MVT::Other, StOps);

  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(ST)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return ST;
}

SDNode *NVPTXDAGToDAGISel::SelectLoadParam(SDNode *Node) {
  SDValue Chain = Node->getOperand(0);
  SDValue Offset = Node->getOperand(2);
  SDValue Flag = Node->getOperand(3);
  SDLoc DL(Node);
  MemSDNode *Mem = cast<MemSDNode>(Node);

  unsigned VecSize;
  switch (Node->getOpcode()) {
  default:
    return NULL;
  case NVPTXISD::LoadParam:
    VecSize = 1;
    break;
  case NVPTXISD::LoadParamV2:
    VecSize = 2;
    break;
  case NVPTXISD::LoadParamV4:
    VecSize = 4;
    break;
  }

  EVT EltVT = Node->getValueType(0);
  EVT MemVT = Mem->getMemoryVT();

  unsigned Opc = 0;

  switch (VecSize) {
  default:
    return NULL;
  case 1:
    switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opc = NVPTX::LoadParamMemI8;
      break;
    case MVT::i8:
      Opc = NVPTX::LoadParamMemI8;
      break;
    case MVT::i16:
      Opc = NVPTX::LoadParamMemI16;
      break;
    case MVT::i32:
      Opc = NVPTX::LoadParamMemI32;
      break;
    case MVT::i64:
      Opc = NVPTX::LoadParamMemI64;
      break;
    case MVT::f32:
      Opc = NVPTX::LoadParamMemF32;
      break;
    case MVT::f64:
      Opc = NVPTX::LoadParamMemF64;
      break;
    }
    break;
  case 2:
    switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opc = NVPTX::LoadParamMemV2I8;
      break;
    case MVT::i8:
      Opc = NVPTX::LoadParamMemV2I8;
      break;
    case MVT::i16:
      Opc = NVPTX::LoadParamMemV2I16;
      break;
    case MVT::i32:
      Opc = NVPTX::LoadParamMemV2I32;
      break;
    case MVT::i64:
      Opc = NVPTX::LoadParamMemV2I64;
      break;
    case MVT::f32:
      Opc = NVPTX::LoadParamMemV2F32;
      break;
    case MVT::f64:
      Opc = NVPTX::LoadParamMemV2F64;
      break;
    }
    break;
  case 4:
    switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opc = NVPTX::LoadParamMemV4I8;
      break;
    case MVT::i8:
      Opc = NVPTX::LoadParamMemV4I8;
      break;
    case MVT::i16:
      Opc = NVPTX::LoadParamMemV4I16;
      break;
    case MVT::i32:
      Opc = NVPTX::LoadParamMemV4I32;
      break;
    case MVT::f32:
      Opc = NVPTX::LoadParamMemV4F32;
      break;
    }
    break;
  }

  SDVTList VTs;
  if (VecSize == 1) {
    VTs = CurDAG->getVTList(EltVT, MVT::Other, MVT::Glue);
  } else if (VecSize == 2) {
    VTs = CurDAG->getVTList(EltVT, EltVT, MVT::Other, MVT::Glue);
  } else {
    EVT EVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other, MVT::Glue };
    VTs = CurDAG->getVTList(&EVTs[0], 5);
  }

  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();

  SmallVector<SDValue, 2> Ops;
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, MVT::i32));
  Ops.push_back(Chain);
  Ops.push_back(Flag);

  SDNode *Ret =
      CurDAG->getMachineNode(Opc, DL, VTs, Ops);
  return Ret;
}

SDNode *NVPTXDAGToDAGISel::SelectStoreRetval(SDNode *N) {
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  SDValue Offset = N->getOperand(1);
  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();
  MemSDNode *Mem = cast<MemSDNode>(N);

  // How many elements do we have?
  unsigned NumElts = 1;
  switch (N->getOpcode()) {
  default:
    return NULL;
  case NVPTXISD::StoreRetval:
    NumElts = 1;
    break;
  case NVPTXISD::StoreRetvalV2:
    NumElts = 2;
    break;
  case NVPTXISD::StoreRetvalV4:
    NumElts = 4;
    break;
  }

  // Build vector of operands
  SmallVector<SDValue, 6> Ops;
  for (unsigned i = 0; i < NumElts; ++i)
    Ops.push_back(N->getOperand(i + 2));
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, MVT::i32));
  Ops.push_back(Chain);

  // Determine target opcode
  // If we have an i1, use an 8-bit store. The lowering code in
  // NVPTXISelLowering will have already emitted an upcast.
  unsigned Opcode = 0;
  switch (NumElts) {
  default:
    return NULL;
  case 1:
    switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opcode = NVPTX::StoreRetvalI8;
      break;
    case MVT::i8:
      Opcode = NVPTX::StoreRetvalI8;
      break;
    case MVT::i16:
      Opcode = NVPTX::StoreRetvalI16;
      break;
    case MVT::i32:
      Opcode = NVPTX::StoreRetvalI32;
      break;
    case MVT::i64:
      Opcode = NVPTX::StoreRetvalI64;
      break;
    case MVT::f32:
      Opcode = NVPTX::StoreRetvalF32;
      break;
    case MVT::f64:
      Opcode = NVPTX::StoreRetvalF64;
      break;
    }
    break;
  case 2:
    switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opcode = NVPTX::StoreRetvalV2I8;
      break;
    case MVT::i8:
      Opcode = NVPTX::StoreRetvalV2I8;
      break;
    case MVT::i16:
      Opcode = NVPTX::StoreRetvalV2I16;
      break;
    case MVT::i32:
      Opcode = NVPTX::StoreRetvalV2I32;
      break;
    case MVT::i64:
      Opcode = NVPTX::StoreRetvalV2I64;
      break;
    case MVT::f32:
      Opcode = NVPTX::StoreRetvalV2F32;
      break;
    case MVT::f64:
      Opcode = NVPTX::StoreRetvalV2F64;
      break;
    }
    break;
  case 4:
    switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
    default:
      return NULL;
    case MVT::i1:
      Opcode = NVPTX::StoreRetvalV4I8;
      break;
    case MVT::i8:
      Opcode = NVPTX::StoreRetvalV4I8;
      break;
    case MVT::i16:
      Opcode = NVPTX::StoreRetvalV4I16;
      break;
    case MVT::i32:
      Opcode = NVPTX::StoreRetvalV4I32;
      break;
    case MVT::f32:
      Opcode = NVPTX::StoreRetvalV4F32;
      break;
    }
    break;
  }

  SDNode *Ret =
      CurDAG->getMachineNode(Opcode, DL, MVT::Other, Ops);
  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(Ret)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return Ret;
}

SDNode *NVPTXDAGToDAGISel::SelectStoreParam(SDNode *N) {
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  SDValue Param = N->getOperand(1);
  unsigned ParamVal = cast<ConstantSDNode>(Param)->getZExtValue();
  SDValue Offset = N->getOperand(2);
  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();
  MemSDNode *Mem = cast<MemSDNode>(N);
  SDValue Flag = N->getOperand(N->getNumOperands() - 1);

  // How many elements do we have?
  unsigned NumElts = 1;
  switch (N->getOpcode()) {
  default:
    return NULL;
  case NVPTXISD::StoreParamU32:
  case NVPTXISD::StoreParamS32:
  case NVPTXISD::StoreParam:
    NumElts = 1;
    break;
  case NVPTXISD::StoreParamV2:
    NumElts = 2;
    break;
  case NVPTXISD::StoreParamV4:
    NumElts = 4;
    break;
  }

  // Build vector of operands
  SmallVector<SDValue, 8> Ops;
  for (unsigned i = 0; i < NumElts; ++i)
    Ops.push_back(N->getOperand(i + 3));
  Ops.push_back(CurDAG->getTargetConstant(ParamVal, MVT::i32));
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, MVT::i32));
  Ops.push_back(Chain);
  Ops.push_back(Flag);

  // Determine target opcode
  // If we have an i1, use an 8-bit store. The lowering code in
  // NVPTXISelLowering will have already emitted an upcast.
  unsigned Opcode = 0;
  switch (N->getOpcode()) {
  default:
    switch (NumElts) {
    default:
      return NULL;
    case 1:
      switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i1:
        Opcode = NVPTX::StoreParamI8;
        break;
      case MVT::i8:
        Opcode = NVPTX::StoreParamI8;
        break;
      case MVT::i16:
        Opcode = NVPTX::StoreParamI16;
        break;
      case MVT::i32:
        Opcode = NVPTX::StoreParamI32;
        break;
      case MVT::i64:
        Opcode = NVPTX::StoreParamI64;
        break;
      case MVT::f32:
        Opcode = NVPTX::StoreParamF32;
        break;
      case MVT::f64:
        Opcode = NVPTX::StoreParamF64;
        break;
      }
      break;
    case 2:
      switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i1:
        Opcode = NVPTX::StoreParamV2I8;
        break;
      case MVT::i8:
        Opcode = NVPTX::StoreParamV2I8;
        break;
      case MVT::i16:
        Opcode = NVPTX::StoreParamV2I16;
        break;
      case MVT::i32:
        Opcode = NVPTX::StoreParamV2I32;
        break;
      case MVT::i64:
        Opcode = NVPTX::StoreParamV2I64;
        break;
      case MVT::f32:
        Opcode = NVPTX::StoreParamV2F32;
        break;
      case MVT::f64:
        Opcode = NVPTX::StoreParamV2F64;
        break;
      }
      break;
    case 4:
      switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
      default:
        return NULL;
      case MVT::i1:
        Opcode = NVPTX::StoreParamV4I8;
        break;
      case MVT::i8:
        Opcode = NVPTX::StoreParamV4I8;
        break;
      case MVT::i16:
        Opcode = NVPTX::StoreParamV4I16;
        break;
      case MVT::i32:
        Opcode = NVPTX::StoreParamV4I32;
        break;
      case MVT::f32:
        Opcode = NVPTX::StoreParamV4F32;
        break;
      }
      break;
    }
    break;
  // Special case: if we have a sign-extend/zero-extend node, insert the
  // conversion instruction first, and use that as the value operand to
  // the selected StoreParam node.
  case NVPTXISD::StoreParamU32: {
    Opcode = NVPTX::StoreParamI32;
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE,
                                                MVT::i32);
    SDNode *Cvt = CurDAG->getMachineNode(NVPTX::CVT_u32_u16, DL,
                                         MVT::i32, Ops[0], CvtNone);
    Ops[0] = SDValue(Cvt, 0);
    break;
  }
  case NVPTXISD::StoreParamS32: {
    Opcode = NVPTX::StoreParamI32;
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE,
                                                MVT::i32);
    SDNode *Cvt = CurDAG->getMachineNode(NVPTX::CVT_s32_s16, DL,
                                         MVT::i32, Ops[0], CvtNone);
    Ops[0] = SDValue(Cvt, 0);
    break;
  }
  }

  SDVTList RetVTs = CurDAG->getVTList(MVT::Other, MVT::Glue);
  SDNode *Ret =
      CurDAG->getMachineNode(Opcode, DL, RetVTs, Ops);
  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(Ret)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return Ret;
}

// SelectDirectAddr - Match a direct address for DAG.
// A direct address could be a globaladdress or externalsymbol.
bool NVPTXDAGToDAGISel::SelectDirectAddr(SDValue N, SDValue &Address) {
  // Return true if TGA or ES.
  if (N.getOpcode() == ISD::TargetGlobalAddress ||
      N.getOpcode() == ISD::TargetExternalSymbol) {
    Address = N;
    return true;
  }
  if (N.getOpcode() == NVPTXISD::Wrapper) {
    Address = N.getOperand(0);
    return true;
  }
  if (N.getOpcode() == ISD::INTRINSIC_WO_CHAIN) {
    unsigned IID = cast<ConstantSDNode>(N.getOperand(0))->getZExtValue();
    if (IID == Intrinsic::nvvm_ptr_gen_to_param)
      if (N.getOperand(1).getOpcode() == NVPTXISD::MoveParam)
        return (SelectDirectAddr(N.getOperand(1).getOperand(0), Address));
  }
  return false;
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi_imp(
    SDNode *OpNode, SDValue Addr, SDValue &Base, SDValue &Offset, MVT mvt) {
  if (Addr.getOpcode() == ISD::ADD) {
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      SDValue base = Addr.getOperand(0);
      if (SelectDirectAddr(base, Base)) {
        Offset = CurDAG->getTargetConstant(CN->getZExtValue(), mvt);
        return true;
      }
    }
  }
  return false;
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi(SDNode *OpNode, SDValue Addr,
                                     SDValue &Base, SDValue &Offset) {
  return SelectADDRsi_imp(OpNode, Addr, Base, Offset, MVT::i32);
}

// symbol+offset
bool NVPTXDAGToDAGISel::SelectADDRsi64(SDNode *OpNode, SDValue Addr,
                                       SDValue &Base, SDValue &Offset) {
  return SelectADDRsi_imp(OpNode, Addr, Base, Offset, MVT::i64);
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri_imp(
    SDNode *OpNode, SDValue Addr, SDValue &Base, SDValue &Offset, MVT mvt) {
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), mvt);
    Offset = CurDAG->getTargetConstant(0, mvt);
    return true;
  }
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false; // direct calls.

  if (Addr.getOpcode() == ISD::ADD) {
    if (SelectDirectAddr(Addr.getOperand(0), Addr)) {
      return false;
    }
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1))) {
      if (FrameIndexSDNode *FIN =
              dyn_cast<FrameIndexSDNode>(Addr.getOperand(0)))
        // Constant offset from frame ref.
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), mvt);
      else
        Base = Addr.getOperand(0);
      Offset = CurDAG->getTargetConstant(CN->getZExtValue(), mvt);
      return true;
    }
  }
  return false;
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri(SDNode *OpNode, SDValue Addr,
                                     SDValue &Base, SDValue &Offset) {
  return SelectADDRri_imp(OpNode, Addr, Base, Offset, MVT::i32);
}

// register+offset
bool NVPTXDAGToDAGISel::SelectADDRri64(SDNode *OpNode, SDValue Addr,
                                       SDValue &Base, SDValue &Offset) {
  return SelectADDRri_imp(OpNode, Addr, Base, Offset, MVT::i64);
}

bool NVPTXDAGToDAGISel::ChkMemSDNodeAddressSpace(SDNode *N,
                                                 unsigned int spN) const {
  const Value *Src = NULL;
  // Even though MemIntrinsicSDNode is a subclas of MemSDNode,
  // the classof() for MemSDNode does not include MemIntrinsicSDNode
  // (See SelectionDAGNodes.h). So we need to check for both.
  if (MemSDNode *mN = dyn_cast<MemSDNode>(N)) {
    Src = mN->getSrcValue();
  } else if (MemSDNode *mN = dyn_cast<MemIntrinsicSDNode>(N)) {
    Src = mN->getSrcValue();
  }
  if (!Src)
    return false;
  if (const PointerType *PT = dyn_cast<PointerType>(Src->getType()))
    return (PT->getAddressSpace() == spN);
  return false;
}

/// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
/// inline asm expressions.
bool NVPTXDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, char ConstraintCode, std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1;
  switch (ConstraintCode) {
  default:
    return true;
  case 'm': // memory
    if (SelectDirectAddr(Op, Op0)) {
      OutOps.push_back(Op0);
      OutOps.push_back(CurDAG->getTargetConstant(0, MVT::i32));
      return false;
    }
    if (SelectADDRri(Op.getNode(), Op, Op0, Op1)) {
      OutOps.push_back(Op0);
      OutOps.push_back(Op1);
      return false;
    }
    break;
  }
  return true;
}

// Return true if N is a undef or a constant.
// If N was undef, return a (i8imm 0) in Retval
// If N was imm, convert it to i8imm and return in Retval
// Note: The convert to i8imm is required, otherwise the
// pattern matcher inserts a bunch of IMOVi8rr to convert
// the imm to i8imm, and this causes instruction selection
// to fail.
bool NVPTXDAGToDAGISel::UndefOrImm(SDValue Op, SDValue N, SDValue &Retval) {
  if (!(N.getOpcode() == ISD::UNDEF) && !(N.getOpcode() == ISD::Constant))
    return false;

  if (N.getOpcode() == ISD::UNDEF)
    Retval = CurDAG->getTargetConstant(0, MVT::i8);
  else {
    ConstantSDNode *cn = cast<ConstantSDNode>(N.getNode());
    unsigned retval = cn->getZExtValue();
    Retval = CurDAG->getTargetConstant(retval, MVT::i8);
  }
  return true;
}
