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
#include "NVPTXUtilities.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-isel"

static cl::opt<int> UsePrecDivF32(
    "nvptx-prec-divf32", cl::ZeroOrMore, cl::Hidden,
    cl::desc("NVPTX Specifies: 0 use div.approx, 1 use div.full, 2 use"
             " IEEE Compliant F32 div.rnd if available."),
    cl::init(2));

static cl::opt<bool>
UsePrecSqrtF32("nvptx-prec-sqrtf32", cl::Hidden,
          cl::desc("NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn."),
          cl::init(true));

static cl::opt<bool>
FtzEnabled("nvptx-f32ftz", cl::ZeroOrMore, cl::Hidden,
           cl::desc("NVPTX Specific: Flush f32 subnormals to sign-preserving zero."),
           cl::init(false));


/// createNVPTXISelDag - This pass converts a legalized DAG into a
/// NVPTX-specific DAG, ready for instruction scheduling.
FunctionPass *llvm::createNVPTXISelDag(NVPTXTargetMachine &TM,
                                       llvm::CodeGenOpt::Level OptLevel) {
  return new NVPTXDAGToDAGISel(TM, OptLevel);
}

NVPTXDAGToDAGISel::NVPTXDAGToDAGISel(NVPTXTargetMachine &tm,
                                     CodeGenOpt::Level OptLevel)
    : SelectionDAGISel(tm, OptLevel), TM(tm) {
  doMulWide = (OptLevel > 0);
}

bool NVPTXDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
    Subtarget = &static_cast<const NVPTXSubtarget &>(MF.getSubtarget());
    return SelectionDAGISel::runOnMachineFunction(MF);
}

int NVPTXDAGToDAGISel::getDivF32Level() const {
  if (UsePrecDivF32.getNumOccurrences() > 0) {
    // If nvptx-prec-div32=N is used on the command-line, always honor it
    return UsePrecDivF32;
  } else {
    // Otherwise, use div.approx if fast math is enabled
    if (TM.Options.UnsafeFPMath)
      return 0;
    else
      return 2;
  }
}

bool NVPTXDAGToDAGISel::usePrecSqrtF32() const {
  if (UsePrecSqrtF32.getNumOccurrences() > 0) {
    // If nvptx-prec-sqrtf32 is used on the command-line, always honor it
    return UsePrecSqrtF32;
  } else {
    // Otherwise, use sqrt.approx if fast math is enabled
    return !TM.Options.UnsafeFPMath;
  }
}

bool NVPTXDAGToDAGISel::useF32FTZ() const {
  if (FtzEnabled.getNumOccurrences() > 0) {
    // If nvptx-f32ftz is used on the command-line, always honor it
    return FtzEnabled;
  } else {
    const Function *F = MF->getFunction();
    // Otherwise, check for an nvptx-f32ftz attribute on the function
    if (F->hasFnAttribute("nvptx-f32ftz"))
      return F->getFnAttribute("nvptx-f32ftz").getValueAsString() == "true";
    else
      return false;
  }
}

bool NVPTXDAGToDAGISel::allowFMA() const {
  const NVPTXTargetLowering *TL = Subtarget->getTargetLowering();
  return TL->allowFMA(*MF, OptLevel);
}

/// Select - Select instructions not customized! Used for
/// expanded, promoted and normal instructions.
SDNode *NVPTXDAGToDAGISel::Select(SDNode *N) {

  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return nullptr; // Already selected.
  }

  SDNode *ResNode = nullptr;
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
    ResNode = SelectLDGLDU(N);
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
  case ISD::INTRINSIC_WO_CHAIN:
    ResNode = SelectIntrinsicNoChain(N);
    break;
  case ISD::INTRINSIC_W_CHAIN:
    ResNode = SelectIntrinsicChain(N);
    break;
  case NVPTXISD::Tex1DFloatS32:
  case NVPTXISD::Tex1DFloatFloat:
  case NVPTXISD::Tex1DFloatFloatLevel:
  case NVPTXISD::Tex1DFloatFloatGrad:
  case NVPTXISD::Tex1DS32S32:
  case NVPTXISD::Tex1DS32Float:
  case NVPTXISD::Tex1DS32FloatLevel:
  case NVPTXISD::Tex1DS32FloatGrad:
  case NVPTXISD::Tex1DU32S32:
  case NVPTXISD::Tex1DU32Float:
  case NVPTXISD::Tex1DU32FloatLevel:
  case NVPTXISD::Tex1DU32FloatGrad:
  case NVPTXISD::Tex1DArrayFloatS32:
  case NVPTXISD::Tex1DArrayFloatFloat:
  case NVPTXISD::Tex1DArrayFloatFloatLevel:
  case NVPTXISD::Tex1DArrayFloatFloatGrad:
  case NVPTXISD::Tex1DArrayS32S32:
  case NVPTXISD::Tex1DArrayS32Float:
  case NVPTXISD::Tex1DArrayS32FloatLevel:
  case NVPTXISD::Tex1DArrayS32FloatGrad:
  case NVPTXISD::Tex1DArrayU32S32:
  case NVPTXISD::Tex1DArrayU32Float:
  case NVPTXISD::Tex1DArrayU32FloatLevel:
  case NVPTXISD::Tex1DArrayU32FloatGrad:
  case NVPTXISD::Tex2DFloatS32:
  case NVPTXISD::Tex2DFloatFloat:
  case NVPTXISD::Tex2DFloatFloatLevel:
  case NVPTXISD::Tex2DFloatFloatGrad:
  case NVPTXISD::Tex2DS32S32:
  case NVPTXISD::Tex2DS32Float:
  case NVPTXISD::Tex2DS32FloatLevel:
  case NVPTXISD::Tex2DS32FloatGrad:
  case NVPTXISD::Tex2DU32S32:
  case NVPTXISD::Tex2DU32Float:
  case NVPTXISD::Tex2DU32FloatLevel:
  case NVPTXISD::Tex2DU32FloatGrad:
  case NVPTXISD::Tex2DArrayFloatS32:
  case NVPTXISD::Tex2DArrayFloatFloat:
  case NVPTXISD::Tex2DArrayFloatFloatLevel:
  case NVPTXISD::Tex2DArrayFloatFloatGrad:
  case NVPTXISD::Tex2DArrayS32S32:
  case NVPTXISD::Tex2DArrayS32Float:
  case NVPTXISD::Tex2DArrayS32FloatLevel:
  case NVPTXISD::Tex2DArrayS32FloatGrad:
  case NVPTXISD::Tex2DArrayU32S32:
  case NVPTXISD::Tex2DArrayU32Float:
  case NVPTXISD::Tex2DArrayU32FloatLevel:
  case NVPTXISD::Tex2DArrayU32FloatGrad:
  case NVPTXISD::Tex3DFloatS32:
  case NVPTXISD::Tex3DFloatFloat:
  case NVPTXISD::Tex3DFloatFloatLevel:
  case NVPTXISD::Tex3DFloatFloatGrad:
  case NVPTXISD::Tex3DS32S32:
  case NVPTXISD::Tex3DS32Float:
  case NVPTXISD::Tex3DS32FloatLevel:
  case NVPTXISD::Tex3DS32FloatGrad:
  case NVPTXISD::Tex3DU32S32:
  case NVPTXISD::Tex3DU32Float:
  case NVPTXISD::Tex3DU32FloatLevel:
  case NVPTXISD::Tex3DU32FloatGrad:
  case NVPTXISD::TexCubeFloatFloat:
  case NVPTXISD::TexCubeFloatFloatLevel:
  case NVPTXISD::TexCubeS32Float:
  case NVPTXISD::TexCubeS32FloatLevel:
  case NVPTXISD::TexCubeU32Float:
  case NVPTXISD::TexCubeU32FloatLevel:
  case NVPTXISD::TexCubeArrayFloatFloat:
  case NVPTXISD::TexCubeArrayFloatFloatLevel:
  case NVPTXISD::TexCubeArrayS32Float:
  case NVPTXISD::TexCubeArrayS32FloatLevel:
  case NVPTXISD::TexCubeArrayU32Float:
  case NVPTXISD::TexCubeArrayU32FloatLevel:
  case NVPTXISD::Tld4R2DFloatFloat:
  case NVPTXISD::Tld4G2DFloatFloat:
  case NVPTXISD::Tld4B2DFloatFloat:
  case NVPTXISD::Tld4A2DFloatFloat:
  case NVPTXISD::Tld4R2DS64Float:
  case NVPTXISD::Tld4G2DS64Float:
  case NVPTXISD::Tld4B2DS64Float:
  case NVPTXISD::Tld4A2DS64Float:
  case NVPTXISD::Tld4R2DU64Float:
  case NVPTXISD::Tld4G2DU64Float:
  case NVPTXISD::Tld4B2DU64Float:
  case NVPTXISD::Tld4A2DU64Float:
  case NVPTXISD::TexUnified1DFloatS32:
  case NVPTXISD::TexUnified1DFloatFloat:
  case NVPTXISD::TexUnified1DFloatFloatLevel:
  case NVPTXISD::TexUnified1DFloatFloatGrad:
  case NVPTXISD::TexUnified1DS32S32:
  case NVPTXISD::TexUnified1DS32Float:
  case NVPTXISD::TexUnified1DS32FloatLevel:
  case NVPTXISD::TexUnified1DS32FloatGrad:
  case NVPTXISD::TexUnified1DU32S32:
  case NVPTXISD::TexUnified1DU32Float:
  case NVPTXISD::TexUnified1DU32FloatLevel:
  case NVPTXISD::TexUnified1DU32FloatGrad:
  case NVPTXISD::TexUnified1DArrayFloatS32:
  case NVPTXISD::TexUnified1DArrayFloatFloat:
  case NVPTXISD::TexUnified1DArrayFloatFloatLevel:
  case NVPTXISD::TexUnified1DArrayFloatFloatGrad:
  case NVPTXISD::TexUnified1DArrayS32S32:
  case NVPTXISD::TexUnified1DArrayS32Float:
  case NVPTXISD::TexUnified1DArrayS32FloatLevel:
  case NVPTXISD::TexUnified1DArrayS32FloatGrad:
  case NVPTXISD::TexUnified1DArrayU32S32:
  case NVPTXISD::TexUnified1DArrayU32Float:
  case NVPTXISD::TexUnified1DArrayU32FloatLevel:
  case NVPTXISD::TexUnified1DArrayU32FloatGrad:
  case NVPTXISD::TexUnified2DFloatS32:
  case NVPTXISD::TexUnified2DFloatFloat:
  case NVPTXISD::TexUnified2DFloatFloatLevel:
  case NVPTXISD::TexUnified2DFloatFloatGrad:
  case NVPTXISD::TexUnified2DS32S32:
  case NVPTXISD::TexUnified2DS32Float:
  case NVPTXISD::TexUnified2DS32FloatLevel:
  case NVPTXISD::TexUnified2DS32FloatGrad:
  case NVPTXISD::TexUnified2DU32S32:
  case NVPTXISD::TexUnified2DU32Float:
  case NVPTXISD::TexUnified2DU32FloatLevel:
  case NVPTXISD::TexUnified2DU32FloatGrad:
  case NVPTXISD::TexUnified2DArrayFloatS32:
  case NVPTXISD::TexUnified2DArrayFloatFloat:
  case NVPTXISD::TexUnified2DArrayFloatFloatLevel:
  case NVPTXISD::TexUnified2DArrayFloatFloatGrad:
  case NVPTXISD::TexUnified2DArrayS32S32:
  case NVPTXISD::TexUnified2DArrayS32Float:
  case NVPTXISD::TexUnified2DArrayS32FloatLevel:
  case NVPTXISD::TexUnified2DArrayS32FloatGrad:
  case NVPTXISD::TexUnified2DArrayU32S32:
  case NVPTXISD::TexUnified2DArrayU32Float:
  case NVPTXISD::TexUnified2DArrayU32FloatLevel:
  case NVPTXISD::TexUnified2DArrayU32FloatGrad:
  case NVPTXISD::TexUnified3DFloatS32:
  case NVPTXISD::TexUnified3DFloatFloat:
  case NVPTXISD::TexUnified3DFloatFloatLevel:
  case NVPTXISD::TexUnified3DFloatFloatGrad:
  case NVPTXISD::TexUnified3DS32S32:
  case NVPTXISD::TexUnified3DS32Float:
  case NVPTXISD::TexUnified3DS32FloatLevel:
  case NVPTXISD::TexUnified3DS32FloatGrad:
  case NVPTXISD::TexUnified3DU32S32:
  case NVPTXISD::TexUnified3DU32Float:
  case NVPTXISD::TexUnified3DU32FloatLevel:
  case NVPTXISD::TexUnified3DU32FloatGrad:
  case NVPTXISD::TexUnifiedCubeFloatFloat:
  case NVPTXISD::TexUnifiedCubeFloatFloatLevel:
  case NVPTXISD::TexUnifiedCubeS32Float:
  case NVPTXISD::TexUnifiedCubeS32FloatLevel:
  case NVPTXISD::TexUnifiedCubeU32Float:
  case NVPTXISD::TexUnifiedCubeU32FloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayFloatFloat:
  case NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayS32Float:
  case NVPTXISD::TexUnifiedCubeArrayS32FloatLevel:
  case NVPTXISD::TexUnifiedCubeArrayU32Float:
  case NVPTXISD::TexUnifiedCubeArrayU32FloatLevel:
  case NVPTXISD::Tld4UnifiedR2DFloatFloat:
  case NVPTXISD::Tld4UnifiedG2DFloatFloat:
  case NVPTXISD::Tld4UnifiedB2DFloatFloat:
  case NVPTXISD::Tld4UnifiedA2DFloatFloat:
  case NVPTXISD::Tld4UnifiedR2DS64Float:
  case NVPTXISD::Tld4UnifiedG2DS64Float:
  case NVPTXISD::Tld4UnifiedB2DS64Float:
  case NVPTXISD::Tld4UnifiedA2DS64Float:
  case NVPTXISD::Tld4UnifiedR2DU64Float:
  case NVPTXISD::Tld4UnifiedG2DU64Float:
  case NVPTXISD::Tld4UnifiedB2DU64Float:
  case NVPTXISD::Tld4UnifiedA2DU64Float:
    ResNode = SelectTextureIntrinsic(N);
    break;
  case NVPTXISD::Suld1DI8Clamp:
  case NVPTXISD::Suld1DI16Clamp:
  case NVPTXISD::Suld1DI32Clamp:
  case NVPTXISD::Suld1DI64Clamp:
  case NVPTXISD::Suld1DV2I8Clamp:
  case NVPTXISD::Suld1DV2I16Clamp:
  case NVPTXISD::Suld1DV2I32Clamp:
  case NVPTXISD::Suld1DV2I64Clamp:
  case NVPTXISD::Suld1DV4I8Clamp:
  case NVPTXISD::Suld1DV4I16Clamp:
  case NVPTXISD::Suld1DV4I32Clamp:
  case NVPTXISD::Suld1DArrayI8Clamp:
  case NVPTXISD::Suld1DArrayI16Clamp:
  case NVPTXISD::Suld1DArrayI32Clamp:
  case NVPTXISD::Suld1DArrayI64Clamp:
  case NVPTXISD::Suld1DArrayV2I8Clamp:
  case NVPTXISD::Suld1DArrayV2I16Clamp:
  case NVPTXISD::Suld1DArrayV2I32Clamp:
  case NVPTXISD::Suld1DArrayV2I64Clamp:
  case NVPTXISD::Suld1DArrayV4I8Clamp:
  case NVPTXISD::Suld1DArrayV4I16Clamp:
  case NVPTXISD::Suld1DArrayV4I32Clamp:
  case NVPTXISD::Suld2DI8Clamp:
  case NVPTXISD::Suld2DI16Clamp:
  case NVPTXISD::Suld2DI32Clamp:
  case NVPTXISD::Suld2DI64Clamp:
  case NVPTXISD::Suld2DV2I8Clamp:
  case NVPTXISD::Suld2DV2I16Clamp:
  case NVPTXISD::Suld2DV2I32Clamp:
  case NVPTXISD::Suld2DV2I64Clamp:
  case NVPTXISD::Suld2DV4I8Clamp:
  case NVPTXISD::Suld2DV4I16Clamp:
  case NVPTXISD::Suld2DV4I32Clamp:
  case NVPTXISD::Suld2DArrayI8Clamp:
  case NVPTXISD::Suld2DArrayI16Clamp:
  case NVPTXISD::Suld2DArrayI32Clamp:
  case NVPTXISD::Suld2DArrayI64Clamp:
  case NVPTXISD::Suld2DArrayV2I8Clamp:
  case NVPTXISD::Suld2DArrayV2I16Clamp:
  case NVPTXISD::Suld2DArrayV2I32Clamp:
  case NVPTXISD::Suld2DArrayV2I64Clamp:
  case NVPTXISD::Suld2DArrayV4I8Clamp:
  case NVPTXISD::Suld2DArrayV4I16Clamp:
  case NVPTXISD::Suld2DArrayV4I32Clamp:
  case NVPTXISD::Suld3DI8Clamp:
  case NVPTXISD::Suld3DI16Clamp:
  case NVPTXISD::Suld3DI32Clamp:
  case NVPTXISD::Suld3DI64Clamp:
  case NVPTXISD::Suld3DV2I8Clamp:
  case NVPTXISD::Suld3DV2I16Clamp:
  case NVPTXISD::Suld3DV2I32Clamp:
  case NVPTXISD::Suld3DV2I64Clamp:
  case NVPTXISD::Suld3DV4I8Clamp:
  case NVPTXISD::Suld3DV4I16Clamp:
  case NVPTXISD::Suld3DV4I32Clamp:
  case NVPTXISD::Suld1DI8Trap:
  case NVPTXISD::Suld1DI16Trap:
  case NVPTXISD::Suld1DI32Trap:
  case NVPTXISD::Suld1DI64Trap:
  case NVPTXISD::Suld1DV2I8Trap:
  case NVPTXISD::Suld1DV2I16Trap:
  case NVPTXISD::Suld1DV2I32Trap:
  case NVPTXISD::Suld1DV2I64Trap:
  case NVPTXISD::Suld1DV4I8Trap:
  case NVPTXISD::Suld1DV4I16Trap:
  case NVPTXISD::Suld1DV4I32Trap:
  case NVPTXISD::Suld1DArrayI8Trap:
  case NVPTXISD::Suld1DArrayI16Trap:
  case NVPTXISD::Suld1DArrayI32Trap:
  case NVPTXISD::Suld1DArrayI64Trap:
  case NVPTXISD::Suld1DArrayV2I8Trap:
  case NVPTXISD::Suld1DArrayV2I16Trap:
  case NVPTXISD::Suld1DArrayV2I32Trap:
  case NVPTXISD::Suld1DArrayV2I64Trap:
  case NVPTXISD::Suld1DArrayV4I8Trap:
  case NVPTXISD::Suld1DArrayV4I16Trap:
  case NVPTXISD::Suld1DArrayV4I32Trap:
  case NVPTXISD::Suld2DI8Trap:
  case NVPTXISD::Suld2DI16Trap:
  case NVPTXISD::Suld2DI32Trap:
  case NVPTXISD::Suld2DI64Trap:
  case NVPTXISD::Suld2DV2I8Trap:
  case NVPTXISD::Suld2DV2I16Trap:
  case NVPTXISD::Suld2DV2I32Trap:
  case NVPTXISD::Suld2DV2I64Trap:
  case NVPTXISD::Suld2DV4I8Trap:
  case NVPTXISD::Suld2DV4I16Trap:
  case NVPTXISD::Suld2DV4I32Trap:
  case NVPTXISD::Suld2DArrayI8Trap:
  case NVPTXISD::Suld2DArrayI16Trap:
  case NVPTXISD::Suld2DArrayI32Trap:
  case NVPTXISD::Suld2DArrayI64Trap:
  case NVPTXISD::Suld2DArrayV2I8Trap:
  case NVPTXISD::Suld2DArrayV2I16Trap:
  case NVPTXISD::Suld2DArrayV2I32Trap:
  case NVPTXISD::Suld2DArrayV2I64Trap:
  case NVPTXISD::Suld2DArrayV4I8Trap:
  case NVPTXISD::Suld2DArrayV4I16Trap:
  case NVPTXISD::Suld2DArrayV4I32Trap:
  case NVPTXISD::Suld3DI8Trap:
  case NVPTXISD::Suld3DI16Trap:
  case NVPTXISD::Suld3DI32Trap:
  case NVPTXISD::Suld3DI64Trap:
  case NVPTXISD::Suld3DV2I8Trap:
  case NVPTXISD::Suld3DV2I16Trap:
  case NVPTXISD::Suld3DV2I32Trap:
  case NVPTXISD::Suld3DV2I64Trap:
  case NVPTXISD::Suld3DV4I8Trap:
  case NVPTXISD::Suld3DV4I16Trap:
  case NVPTXISD::Suld3DV4I32Trap:
  case NVPTXISD::Suld1DI8Zero:
  case NVPTXISD::Suld1DI16Zero:
  case NVPTXISD::Suld1DI32Zero:
  case NVPTXISD::Suld1DI64Zero:
  case NVPTXISD::Suld1DV2I8Zero:
  case NVPTXISD::Suld1DV2I16Zero:
  case NVPTXISD::Suld1DV2I32Zero:
  case NVPTXISD::Suld1DV2I64Zero:
  case NVPTXISD::Suld1DV4I8Zero:
  case NVPTXISD::Suld1DV4I16Zero:
  case NVPTXISD::Suld1DV4I32Zero:
  case NVPTXISD::Suld1DArrayI8Zero:
  case NVPTXISD::Suld1DArrayI16Zero:
  case NVPTXISD::Suld1DArrayI32Zero:
  case NVPTXISD::Suld1DArrayI64Zero:
  case NVPTXISD::Suld1DArrayV2I8Zero:
  case NVPTXISD::Suld1DArrayV2I16Zero:
  case NVPTXISD::Suld1DArrayV2I32Zero:
  case NVPTXISD::Suld1DArrayV2I64Zero:
  case NVPTXISD::Suld1DArrayV4I8Zero:
  case NVPTXISD::Suld1DArrayV4I16Zero:
  case NVPTXISD::Suld1DArrayV4I32Zero:
  case NVPTXISD::Suld2DI8Zero:
  case NVPTXISD::Suld2DI16Zero:
  case NVPTXISD::Suld2DI32Zero:
  case NVPTXISD::Suld2DI64Zero:
  case NVPTXISD::Suld2DV2I8Zero:
  case NVPTXISD::Suld2DV2I16Zero:
  case NVPTXISD::Suld2DV2I32Zero:
  case NVPTXISD::Suld2DV2I64Zero:
  case NVPTXISD::Suld2DV4I8Zero:
  case NVPTXISD::Suld2DV4I16Zero:
  case NVPTXISD::Suld2DV4I32Zero:
  case NVPTXISD::Suld2DArrayI8Zero:
  case NVPTXISD::Suld2DArrayI16Zero:
  case NVPTXISD::Suld2DArrayI32Zero:
  case NVPTXISD::Suld2DArrayI64Zero:
  case NVPTXISD::Suld2DArrayV2I8Zero:
  case NVPTXISD::Suld2DArrayV2I16Zero:
  case NVPTXISD::Suld2DArrayV2I32Zero:
  case NVPTXISD::Suld2DArrayV2I64Zero:
  case NVPTXISD::Suld2DArrayV4I8Zero:
  case NVPTXISD::Suld2DArrayV4I16Zero:
  case NVPTXISD::Suld2DArrayV4I32Zero:
  case NVPTXISD::Suld3DI8Zero:
  case NVPTXISD::Suld3DI16Zero:
  case NVPTXISD::Suld3DI32Zero:
  case NVPTXISD::Suld3DI64Zero:
  case NVPTXISD::Suld3DV2I8Zero:
  case NVPTXISD::Suld3DV2I16Zero:
  case NVPTXISD::Suld3DV2I32Zero:
  case NVPTXISD::Suld3DV2I64Zero:
  case NVPTXISD::Suld3DV4I8Zero:
  case NVPTXISD::Suld3DV4I16Zero:
  case NVPTXISD::Suld3DV4I32Zero:
    ResNode = SelectSurfaceIntrinsic(N);
    break;
  case ISD::AND:
  case ISD::SRA:
  case ISD::SRL:
    // Try to select BFE
    ResNode = SelectBFE(N);
    break;
  case ISD::ADDRSPACECAST:
    ResNode = SelectAddrSpaceCast(N);
    break;
  default:
    break;
  }
  if (ResNode)
    return ResNode;
  return SelectCode(N);
}

SDNode *NVPTXDAGToDAGISel::SelectIntrinsicChain(SDNode *N) {
  unsigned IID = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  switch (IID) {
  default:
    return NULL;
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_p:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_p:
    return SelectLDGLDU(N);
  }
}

static unsigned int getCodeAddrSpace(MemSDNode *N) {
  const Value *Src = N->getMemOperand()->getValue();

  if (!Src)
    return NVPTX::PTXLdStInstCode::GENERIC;

  if (auto *PT = dyn_cast<PointerType>(Src->getType())) {
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

static bool canLowerToLDG(MemSDNode *N, const NVPTXSubtarget &Subtarget,
                          unsigned CodeAddrSpace, MachineFunction *F) {
  // To use non-coherent caching, the load has to be from global
  // memory and we have to prove that the memory area is not written
  // to anywhere for the duration of the kernel call, not even after
  // the load.
  //
  // To ensure that there are no writes to the memory, we require the
  // underlying pointer to be a noalias (__restrict) kernel parameter
  // that is never used for a write. We can only do this for kernel
  // functions since from within a device function, we cannot know if
  // there were or will be writes to the memory from the caller - or we
  // could, but then we would have to do inter-procedural analysis.
  if (!Subtarget.hasLDG() || CodeAddrSpace != NVPTX::PTXLdStInstCode::GLOBAL ||
      !isKernelFunction(*F->getFunction())) {
    return false;
  }

  // We use GetUnderlyingObjects() here instead of
  // GetUnderlyingObject() mainly because the former looks through phi
  // nodes while the latter does not. We need to look through phi
  // nodes to handle pointer induction variables.
  SmallVector<Value *, 8> Objs;
  GetUnderlyingObjects(const_cast<Value *>(N->getMemOperand()->getValue()),
                       Objs, F->getDataLayout());
  for (Value *Obj : Objs) {
    auto *A = dyn_cast<const Argument>(Obj);
    if (!A || !A->onlyReadsMemory() || !A->hasNoAliasAttr()) return false;
  }

  return true;
}

SDNode *NVPTXDAGToDAGISel::SelectIntrinsicNoChain(SDNode *N) {
  unsigned IID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  switch (IID) {
  default:
    return nullptr;
  case Intrinsic::nvvm_texsurf_handle_internal:
    return SelectTexSurfHandle(N);
  }
}

SDNode *NVPTXDAGToDAGISel::SelectTexSurfHandle(SDNode *N) {
  // Op 0 is the intrinsic ID
  SDValue Wrapper = N->getOperand(1);
  SDValue GlobalVal = Wrapper.getOperand(0);
  return CurDAG->getMachineNode(NVPTX::texsurf_handles, SDLoc(N), MVT::i64,
                                GlobalVal);
}

SDNode *NVPTXDAGToDAGISel::SelectAddrSpaceCast(SDNode *N) {
  SDValue Src = N->getOperand(0);
  AddrSpaceCastSDNode *CastN = cast<AddrSpaceCastSDNode>(N);
  unsigned SrcAddrSpace = CastN->getSrcAddressSpace();
  unsigned DstAddrSpace = CastN->getDestAddressSpace();

  assert(SrcAddrSpace != DstAddrSpace &&
         "addrspacecast must be between different address spaces");

  if (DstAddrSpace == ADDRESS_SPACE_GENERIC) {
    // Specific to generic
    unsigned Opc;
    switch (SrcAddrSpace) {
    default: report_fatal_error("Bad address space in addrspacecast");
    case ADDRESS_SPACE_GLOBAL:
      Opc = TM.is64Bit() ? NVPTX::cvta_global_yes_64 : NVPTX::cvta_global_yes;
      break;
    case ADDRESS_SPACE_SHARED:
      Opc = TM.is64Bit() ? NVPTX::cvta_shared_yes_64 : NVPTX::cvta_shared_yes;
      break;
    case ADDRESS_SPACE_CONST:
      Opc = TM.is64Bit() ? NVPTX::cvta_const_yes_64 : NVPTX::cvta_const_yes;
      break;
    case ADDRESS_SPACE_LOCAL:
      Opc = TM.is64Bit() ? NVPTX::cvta_local_yes_64 : NVPTX::cvta_local_yes;
      break;
    }
    return CurDAG->getMachineNode(Opc, SDLoc(N), N->getValueType(0), Src);
  } else {
    // Generic to specific
    if (SrcAddrSpace != 0)
      report_fatal_error("Cannot cast between two non-generic address spaces");
    unsigned Opc;
    switch (DstAddrSpace) {
    default: report_fatal_error("Bad address space in addrspacecast");
    case ADDRESS_SPACE_GLOBAL:
      Opc = TM.is64Bit() ? NVPTX::cvta_to_global_yes_64
                         : NVPTX::cvta_to_global_yes;
      break;
    case ADDRESS_SPACE_SHARED:
      Opc = TM.is64Bit() ? NVPTX::cvta_to_shared_yes_64
                         : NVPTX::cvta_to_shared_yes;
      break;
    case ADDRESS_SPACE_CONST:
      Opc =
          TM.is64Bit() ? NVPTX::cvta_to_const_yes_64 : NVPTX::cvta_to_const_yes;
      break;
    case ADDRESS_SPACE_LOCAL:
      Opc =
          TM.is64Bit() ? NVPTX::cvta_to_local_yes_64 : NVPTX::cvta_to_local_yes;
      break;
    case ADDRESS_SPACE_PARAM:
      Opc = TM.is64Bit() ? NVPTX::nvvm_ptr_gen_to_param_64
                         : NVPTX::nvvm_ptr_gen_to_param;
      break;
    }
    return CurDAG->getMachineNode(Opc, SDLoc(N), N->getValueType(0), Src);
  }
}

SDNode *NVPTXDAGToDAGISel::SelectLoad(SDNode *N) {
  SDLoc dl(N);
  LoadSDNode *LD = cast<LoadSDNode>(N);
  EVT LoadedVT = LD->getMemoryVT();
  SDNode *NVPTXLD = nullptr;

  // do not support pre/post inc/dec
  if (LD->isIndexed())
    return nullptr;

  if (!LoadedVT.isSimple())
    return nullptr;

  // Address Space Setting
  unsigned int codeAddrSpace = getCodeAddrSpace(LD);

  if (canLowerToLDG(LD, *Subtarget, codeAddrSpace, MF)) {
    return SelectLDGLDU(N);
  }

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
      return nullptr;
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
  MVT::SimpleValueType TargetVT = LD->getSimpleValueType(0).SimpleTy;

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
      return nullptr;
    }
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(codeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Addr, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else if (TM.is64Bit() ? SelectADDRsi64(N1.getNode(), N1, Base, Offset)
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
      return nullptr;
    }
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(codeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else if (TM.is64Bit() ? SelectADDRri64(N1.getNode(), N1, Base, Offset)
                          : SelectADDRri(N1.getNode(), N1, Base, Offset)) {
    if (TM.is64Bit()) {
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
        return nullptr;
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
        return nullptr;
      }
    }
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(codeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), Base, Offset, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  } else {
    if (TM.is64Bit()) {
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
        return nullptr;
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
        return nullptr;
      }
    }
    SDValue Ops[] = { getI32Imm(isVolatile, dl), getI32Imm(codeAddrSpace, dl),
                      getI32Imm(vecType, dl), getI32Imm(fromType, dl),
                      getI32Imm(fromTypeWidth, dl), N1, Chain };
    NVPTXLD = CurDAG->getMachineNode(Opcode, dl, TargetVT, MVT::Other, Ops);
  }

  if (NVPTXLD) {
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
    return nullptr;

  // Address Space Setting
  unsigned int CodeAddrSpace = getCodeAddrSpace(MemSD);

  if (canLowerToLDG(MemSD, *Subtarget, CodeAddrSpace, MF)) {
    return SelectLDGLDU(N);
  }

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
    return nullptr;
  }

  EVT EltVT = N->getValueType(0);

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return nullptr;
    case NVPTXISD::LoadV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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

    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Addr, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else if (TM.is64Bit() ? SelectADDRsi64(Op1.getNode(), Op1, Base, Offset)
                          : SelectADDRsi(Op1.getNode(), Op1, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return nullptr;
    case NVPTXISD::LoadV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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

    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Base, Offset, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else if (TM.is64Bit() ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                          : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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

    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Base, Offset, Chain };

    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case NVPTXISD::LoadV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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

    SDValue Ops[] = { getI32Imm(IsVolatile, DL), getI32Imm(CodeAddrSpace, DL),
                      getI32Imm(VecType, DL), getI32Imm(FromType, DL),
                      getI32Imm(FromTypeWidth, DL), Op1, Chain };
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  }

  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = cast<MemSDNode>(N)->getMemOperand();
  cast<MachineSDNode>(LD)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return LD;
}

SDNode *NVPTXDAGToDAGISel::SelectLDGLDU(SDNode *N) {

  SDValue Chain = N->getOperand(0);
  SDValue Op1;
  MemSDNode *Mem;
  bool IsLDG = true;

  // If this is an LDG intrinsic, the address is the third operand. Its its an
  // LDG/LDU SD node (from custom vector handling), then its the second operand
  if (N->getOpcode() == ISD::INTRINSIC_W_CHAIN) {
    Op1 = N->getOperand(2);
    Mem = cast<MemIntrinsicSDNode>(N);
    unsigned IID = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    switch (IID) {
    default:
      return NULL;
    case Intrinsic::nvvm_ldg_global_f:
    case Intrinsic::nvvm_ldg_global_i:
    case Intrinsic::nvvm_ldg_global_p:
      IsLDG = true;
      break;
    case Intrinsic::nvvm_ldu_global_f:
    case Intrinsic::nvvm_ldu_global_i:
    case Intrinsic::nvvm_ldu_global_p:
      IsLDG = false;
      break;
    }
  } else {
    Op1 = N->getOperand(1);
    Mem = cast<MemSDNode>(N);
  }

  unsigned Opcode;
  SDLoc DL(N);
  SDNode *LD;
  SDValue Base, Offset, Addr;

  EVT EltVT = Mem->getMemoryVT();
  if (EltVT.isVector()) {
    EltVT = EltVT.getVectorElementType();
  }

  if (SelectDirectAddr(Op1, Addr)) {
    switch (N->getOpcode()) {
    default:
      return nullptr;
    case ISD::INTRINSIC_W_CHAIN:
      if (IsLDG) {
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i8avar;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i16avar;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i32avar;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i64avar;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f32avar;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f64avar;
          break;
        }
      } else {
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
        case MVT::i8:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i8avar;
          break;
        case MVT::i16:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i16avar;
          break;
        case MVT::i32:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i32avar;
          break;
        case MVT::i64:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i64avar;
          break;
        case MVT::f32:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f32avar;
          break;
        case MVT::f64:
          Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f64avar;
          break;
        }
      }
      break;
    case NVPTXISD::LDGV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else if (TM.is64Bit() ? SelectADDRri64(Op1.getNode(), Op1, Base, Offset)
                          : SelectADDRri(Op1.getNode(), Op1, Base, Offset)) {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG) {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i8ari64;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i16ari64;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i32ari64;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i64ari64;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f32ari64;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f64ari64;
            break;
          }
        } else {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i8ari64;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i16ari64;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i32ari64;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i64ari64;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f32ari64;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f64ari64;
            break;
          }
        }
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG) {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i8ari;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i16ari;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i32ari;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i64ari;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f32ari;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f64ari;
            break;
          }
        } else {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i8ari;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i16ari;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i32ari;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i64ari;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f32ari;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f64ari;
            break;
          }
        }
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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

    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  } else {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG) {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i8areg64;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i16areg64;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i32areg64;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i64areg64;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f32areg64;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f64areg64;
            break;
          }
        } else {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i8areg64;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i16areg64;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i32areg64;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i64areg64;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f32areg64;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f64areg64;
            break;
          }
        }
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case ISD::LOAD:
      case ISD::INTRINSIC_W_CHAIN:
        if (IsLDG) {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i8areg;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i16areg;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i32areg;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_i64areg;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f32areg;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDG_GLOBAL_f64areg;
            break;
          }
        } else {
          switch (EltVT.getSimpleVT().SimpleTy) {
          default:
            return nullptr;
          case MVT::i8:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i8areg;
            break;
          case MVT::i16:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i16areg;
            break;
          case MVT::i32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i32areg;
            break;
          case MVT::i64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_i64areg;
            break;
          case MVT::f32:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f32areg;
            break;
          case MVT::f64:
            Opcode = NVPTX::INT_PTX_LDU_GLOBAL_f64areg;
            break;
          }
        }
        break;
      case NVPTXISD::LoadV2:
      case NVPTXISD::LDGV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
      case NVPTXISD::LoadV4:
      case NVPTXISD::LDGV4:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
    LD = CurDAG->getMachineNode(Opcode, DL, N->getVTList(), Ops);
  }

  MachineSDNode::mmo_iterator MemRefs0 = MF->allocateMemRefsArray(1);
  MemRefs0[0] = Mem->getMemOperand();
  cast<MachineSDNode>(LD)->setMemRefs(MemRefs0, MemRefs0 + 1);

  return LD;
}

SDNode *NVPTXDAGToDAGISel::SelectStore(SDNode *N) {
  SDLoc dl(N);
  StoreSDNode *ST = cast<StoreSDNode>(N);
  EVT StoreVT = ST->getMemoryVT();
  SDNode *NVPTXST = nullptr;

  // do not support pre/post inc/dec
  if (ST->isIndexed())
    return nullptr;

  if (!StoreVT.isSimple())
    return nullptr;

  // Address Space Setting
  unsigned int codeAddrSpace = getCodeAddrSpace(ST);

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
      return nullptr;
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
  MVT::SimpleValueType SourceVT = N1.getNode()->getSimpleValueType(0).SimpleTy;

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
      return nullptr;
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile, dl),
                      getI32Imm(codeAddrSpace, dl), getI32Imm(vecType, dl),
                      getI32Imm(toType, dl), getI32Imm(toTypeWidth, dl), Addr,
                      Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else if (TM.is64Bit() ? SelectADDRsi64(N2.getNode(), N2, Base, Offset)
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
      return nullptr;
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile, dl),
                      getI32Imm(codeAddrSpace, dl), getI32Imm(vecType, dl),
                      getI32Imm(toType, dl), getI32Imm(toTypeWidth, dl), Base,
                      Offset, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else if (TM.is64Bit() ? SelectADDRri64(N2.getNode(), N2, Base, Offset)
                          : SelectADDRri(N2.getNode(), N2, Base, Offset)) {
    if (TM.is64Bit()) {
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
        return nullptr;
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
        return nullptr;
      }
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile, dl),
                      getI32Imm(codeAddrSpace, dl), getI32Imm(vecType, dl),
                      getI32Imm(toType, dl), getI32Imm(toTypeWidth, dl), Base,
                      Offset, Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  } else {
    if (TM.is64Bit()) {
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
        return nullptr;
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
        return nullptr;
      }
    }
    SDValue Ops[] = { N1, getI32Imm(isVolatile, dl),
                      getI32Imm(codeAddrSpace, dl), getI32Imm(vecType, dl),
                      getI32Imm(toType, dl), getI32Imm(toTypeWidth, dl), N2,
                      Chain };
    NVPTXST = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  }

  if (NVPTXST) {
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
  unsigned CodeAddrSpace = getCodeAddrSpace(MemSD);

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
    return nullptr;
  }

  StOps.push_back(getI32Imm(IsVolatile, DL));
  StOps.push_back(getI32Imm(CodeAddrSpace, DL));
  StOps.push_back(getI32Imm(VecType, DL));
  StOps.push_back(getI32Imm(ToType, DL));
  StOps.push_back(getI32Imm(ToTypeWidth, DL));

  if (SelectDirectAddr(N2, Addr)) {
    switch (N->getOpcode()) {
    default:
      return nullptr;
    case NVPTXISD::StoreV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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
  } else if (TM.is64Bit() ? SelectADDRsi64(N2.getNode(), N2, Base, Offset)
                          : SelectADDRsi(N2.getNode(), N2, Base, Offset)) {
    switch (N->getOpcode()) {
    default:
      return nullptr;
    case NVPTXISD::StoreV2:
      switch (EltVT.getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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
  } else if (TM.is64Bit() ? SelectADDRri64(N2.getNode(), N2, Base, Offset)
                          : SelectADDRri(N2.getNode(), N2, Base, Offset)) {
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
    if (TM.is64Bit()) {
      switch (N->getOpcode()) {
      default:
        return nullptr;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
        return nullptr;
      case NVPTXISD::StoreV2:
        switch (EltVT.getSimpleVT().SimpleTy) {
        default:
          return nullptr;
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
          return nullptr;
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
    return nullptr;
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
    return nullptr;
  case 1:
    switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return nullptr;
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
      return nullptr;
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
      return nullptr;
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
    VTs = CurDAG->getVTList(EVTs);
  }

  unsigned OffsetVal = cast<ConstantSDNode>(Offset)->getZExtValue();

  SmallVector<SDValue, 2> Ops;
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
  Ops.push_back(Chain);
  Ops.push_back(Flag);

  return CurDAG->getMachineNode(Opc, DL, VTs, Ops);
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
    return nullptr;
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
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
  Ops.push_back(Chain);

  // Determine target opcode
  // If we have an i1, use an 8-bit store. The lowering code in
  // NVPTXISelLowering will have already emitted an upcast.
  unsigned Opcode = 0;
  switch (NumElts) {
  default:
    return nullptr;
  case 1:
    switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
    default:
      return nullptr;
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
      return nullptr;
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
      return nullptr;
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
    return nullptr;
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
  Ops.push_back(CurDAG->getTargetConstant(ParamVal, DL, MVT::i32));
  Ops.push_back(CurDAG->getTargetConstant(OffsetVal, DL, MVT::i32));
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
      return nullptr;
    case 1:
      switch (Mem->getMemoryVT().getSimpleVT().SimpleTy) {
      default:
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE, DL,
                                                MVT::i32);
    SDNode *Cvt = CurDAG->getMachineNode(NVPTX::CVT_u32_u16, DL,
                                         MVT::i32, Ops[0], CvtNone);
    Ops[0] = SDValue(Cvt, 0);
    break;
  }
  case NVPTXISD::StoreParamS32: {
    Opcode = NVPTX::StoreParamI32;
    SDValue CvtNone = CurDAG->getTargetConstant(NVPTX::PTXCvtMode::NONE, DL,
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

SDNode *NVPTXDAGToDAGISel::SelectTextureIntrinsic(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDNode *Ret = nullptr;
  unsigned Opc = 0;
  SmallVector<SDValue, 8> Ops;

  switch (N->getOpcode()) {
  default: return nullptr;
  case NVPTXISD::Tex1DFloatS32:
    Opc = NVPTX::TEX_1D_F32_S32;
    break;
  case NVPTXISD::Tex1DFloatFloat:
    Opc = NVPTX::TEX_1D_F32_F32;
    break;
  case NVPTXISD::Tex1DFloatFloatLevel:
    Opc = NVPTX::TEX_1D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DFloatFloatGrad:
    Opc = NVPTX::TEX_1D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DS32S32:
    Opc = NVPTX::TEX_1D_S32_S32;
    break;
  case NVPTXISD::Tex1DS32Float:
    Opc = NVPTX::TEX_1D_S32_F32;
    break;
  case NVPTXISD::Tex1DS32FloatLevel:
    Opc = NVPTX::TEX_1D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DS32FloatGrad:
    Opc = NVPTX::TEX_1D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DU32S32:
    Opc = NVPTX::TEX_1D_U32_S32;
    break;
  case NVPTXISD::Tex1DU32Float:
    Opc = NVPTX::TEX_1D_U32_F32;
    break;
  case NVPTXISD::Tex1DU32FloatLevel:
    Opc = NVPTX::TEX_1D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DU32FloatGrad:
    Opc = NVPTX::TEX_1D_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayFloatS32:
    Opc = NVPTX::TEX_1D_ARRAY_F32_S32;
    break;
  case NVPTXISD::Tex1DArrayFloatFloat:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32;
    break;
  case NVPTXISD::Tex1DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayS32S32:
    Opc = NVPTX::TEX_1D_ARRAY_S32_S32;
    break;
  case NVPTXISD::Tex1DArrayS32Float:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32;
    break;
  case NVPTXISD::Tex1DArrayS32FloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayS32FloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex1DArrayU32S32:
    Opc = NVPTX::TEX_1D_ARRAY_U32_S32;
    break;
  case NVPTXISD::Tex1DArrayU32Float:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32;
    break;
  case NVPTXISD::Tex1DArrayU32FloatLevel:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex1DArrayU32FloatGrad:
    Opc = NVPTX::TEX_1D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DFloatS32:
    Opc = NVPTX::TEX_2D_F32_S32;
    break;
  case NVPTXISD::Tex2DFloatFloat:
    Opc = NVPTX::TEX_2D_F32_F32;
    break;
  case NVPTXISD::Tex2DFloatFloatLevel:
    Opc = NVPTX::TEX_2D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DFloatFloatGrad:
    Opc = NVPTX::TEX_2D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DS32S32:
    Opc = NVPTX::TEX_2D_S32_S32;
    break;
  case NVPTXISD::Tex2DS32Float:
    Opc = NVPTX::TEX_2D_S32_F32;
    break;
  case NVPTXISD::Tex2DS32FloatLevel:
    Opc = NVPTX::TEX_2D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DS32FloatGrad:
    Opc = NVPTX::TEX_2D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DU32S32:
    Opc = NVPTX::TEX_2D_U32_S32;
    break;
  case NVPTXISD::Tex2DU32Float:
    Opc = NVPTX::TEX_2D_U32_F32;
    break;
  case NVPTXISD::Tex2DU32FloatLevel:
    Opc = NVPTX::TEX_2D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DU32FloatGrad:
    Opc = NVPTX::TEX_2D_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayFloatS32:
    Opc = NVPTX::TEX_2D_ARRAY_F32_S32;
    break;
  case NVPTXISD::Tex2DArrayFloatFloat:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32;
    break;
  case NVPTXISD::Tex2DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayS32S32:
    Opc = NVPTX::TEX_2D_ARRAY_S32_S32;
    break;
  case NVPTXISD::Tex2DArrayS32Float:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32;
    break;
  case NVPTXISD::Tex2DArrayS32FloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayS32FloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex2DArrayU32S32:
    Opc = NVPTX::TEX_2D_ARRAY_U32_S32;
    break;
  case NVPTXISD::Tex2DArrayU32Float:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32;
    break;
  case NVPTXISD::Tex2DArrayU32FloatLevel:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex2DArrayU32FloatGrad:
    Opc = NVPTX::TEX_2D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DFloatS32:
    Opc = NVPTX::TEX_3D_F32_S32;
    break;
  case NVPTXISD::Tex3DFloatFloat:
    Opc = NVPTX::TEX_3D_F32_F32;
    break;
  case NVPTXISD::Tex3DFloatFloatLevel:
    Opc = NVPTX::TEX_3D_F32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DFloatFloatGrad:
    Opc = NVPTX::TEX_3D_F32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DS32S32:
    Opc = NVPTX::TEX_3D_S32_S32;
    break;
  case NVPTXISD::Tex3DS32Float:
    Opc = NVPTX::TEX_3D_S32_F32;
    break;
  case NVPTXISD::Tex3DS32FloatLevel:
    Opc = NVPTX::TEX_3D_S32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DS32FloatGrad:
    Opc = NVPTX::TEX_3D_S32_F32_GRAD;
    break;
  case NVPTXISD::Tex3DU32S32:
    Opc = NVPTX::TEX_3D_U32_S32;
    break;
  case NVPTXISD::Tex3DU32Float:
    Opc = NVPTX::TEX_3D_U32_F32;
    break;
  case NVPTXISD::Tex3DU32FloatLevel:
    Opc = NVPTX::TEX_3D_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tex3DU32FloatGrad:
    Opc = NVPTX::TEX_3D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexCubeFloatFloat:
    Opc = NVPTX::TEX_CUBE_F32_F32;
    break;
  case NVPTXISD::TexCubeFloatFloatLevel:
    Opc = NVPTX::TEX_CUBE_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeS32Float:
    Opc = NVPTX::TEX_CUBE_S32_F32;
    break;
  case NVPTXISD::TexCubeS32FloatLevel:
    Opc = NVPTX::TEX_CUBE_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeU32Float:
    Opc = NVPTX::TEX_CUBE_U32_F32;
    break;
  case NVPTXISD::TexCubeU32FloatLevel:
    Opc = NVPTX::TEX_CUBE_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayFloatFloat:
    Opc = NVPTX::TEX_CUBE_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexCubeArrayFloatFloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayS32Float:
    Opc = NVPTX::TEX_CUBE_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexCubeArrayS32FloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexCubeArrayU32Float:
    Opc = NVPTX::TEX_CUBE_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexCubeArrayU32FloatLevel:
    Opc = NVPTX::TEX_CUBE_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tld4R2DFloatFloat:
    Opc = NVPTX::TLD4_R_2D_F32_F32;
    break;
  case NVPTXISD::Tld4G2DFloatFloat:
    Opc = NVPTX::TLD4_G_2D_F32_F32;
    break;
  case NVPTXISD::Tld4B2DFloatFloat:
    Opc = NVPTX::TLD4_B_2D_F32_F32;
    break;
  case NVPTXISD::Tld4A2DFloatFloat:
    Opc = NVPTX::TLD4_A_2D_F32_F32;
    break;
  case NVPTXISD::Tld4R2DS64Float:
    Opc = NVPTX::TLD4_R_2D_S32_F32;
    break;
  case NVPTXISD::Tld4G2DS64Float:
    Opc = NVPTX::TLD4_G_2D_S32_F32;
    break;
  case NVPTXISD::Tld4B2DS64Float:
    Opc = NVPTX::TLD4_B_2D_S32_F32;
    break;
  case NVPTXISD::Tld4A2DS64Float:
    Opc = NVPTX::TLD4_A_2D_S32_F32;
    break;
  case NVPTXISD::Tld4R2DU64Float:
    Opc = NVPTX::TLD4_R_2D_U32_F32;
    break;
  case NVPTXISD::Tld4G2DU64Float:
    Opc = NVPTX::TLD4_G_2D_U32_F32;
    break;
  case NVPTXISD::Tld4B2DU64Float:
    Opc = NVPTX::TLD4_B_2D_U32_F32;
    break;
  case NVPTXISD::Tld4A2DU64Float:
    Opc = NVPTX::TLD4_A_2D_U32_F32;
    break;
  case NVPTXISD::TexUnified1DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_S32;
    break;
  case NVPTXISD::TexUnified1DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32;
    break;
  case NVPTXISD::TexUnified1DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DS32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_S32;
    break;
  case NVPTXISD::TexUnified1DS32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32;
    break;
  case NVPTXISD::TexUnified1DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DU32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_S32;
    break;
  case NVPTXISD::TexUnified1DU32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32;
    break;
  case NVPTXISD::TexUnified1DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayFloatS32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayS32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified1DArrayU32S32:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_S32;
    break;
  case NVPTXISD::TexUnified1DArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnified1DArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified1DArrayU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_1D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_S32;
    break;
  case NVPTXISD::TexUnified2DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32;
    break;
  case NVPTXISD::TexUnified2DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DS32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_S32;
    break;
  case NVPTXISD::TexUnified2DS32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32;
    break;
  case NVPTXISD::TexUnified2DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DU32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_S32;
    break;
  case NVPTXISD::TexUnified2DU32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32;
    break;
  case NVPTXISD::TexUnified2DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayFloatS32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayS32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified2DArrayU32S32:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_S32;
    break;
  case NVPTXISD::TexUnified2DArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnified2DArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified2DArrayU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_2D_ARRAY_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DFloatS32:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_S32;
    break;
  case NVPTXISD::TexUnified3DFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32;
    break;
  case NVPTXISD::TexUnified3DFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DFloatFloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_F32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DS32S32:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_S32;
    break;
  case NVPTXISD::TexUnified3DS32Float:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32;
    break;
  case NVPTXISD::TexUnified3DS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DS32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_S32_F32_GRAD;
    break;
  case NVPTXISD::TexUnified3DU32S32:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_S32;
    break;
  case NVPTXISD::TexUnified3DU32Float:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32;
    break;
  case NVPTXISD::TexUnified3DU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnified3DU32FloatGrad:
    Opc = NVPTX::TEX_UNIFIED_3D_U32_F32_GRAD;
    break;
  case NVPTXISD::TexUnifiedCubeFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_CUBE_F32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeS32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_S32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeU32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_U32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_U32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayFloatFloat:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_F32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_F32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayS32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_S32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayS32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_S32_F32_LEVEL;
    break;
  case NVPTXISD::TexUnifiedCubeArrayU32Float:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_U32_F32;
    break;
  case NVPTXISD::TexUnifiedCubeArrayU32FloatLevel:
    Opc = NVPTX::TEX_UNIFIED_CUBE_ARRAY_U32_F32_LEVEL;
    break;
  case NVPTXISD::Tld4UnifiedR2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DFloatFloat:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_F32_F32;
    break;
  case NVPTXISD::Tld4UnifiedR2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DS64Float:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_S32_F32;
    break;
  case NVPTXISD::Tld4UnifiedR2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_R_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedG2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_G_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedB2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_B_2D_U32_F32;
    break;
  case NVPTXISD::Tld4UnifiedA2DU64Float:
    Opc = NVPTX::TLD4_UNIFIED_A_2D_U32_F32;
    break;
  }

  // Copy over operands
  for (unsigned i = 1; i < N->getNumOperands(); ++i) {
    Ops.push_back(N->getOperand(i));
  }

  Ops.push_back(Chain);
  Ret = CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops);
  return Ret;
}

SDNode *NVPTXDAGToDAGISel::SelectSurfaceIntrinsic(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDValue TexHandle = N->getOperand(1);
  SDNode *Ret = nullptr;
  unsigned Opc = 0;
  SmallVector<SDValue, 8> Ops;
  switch (N->getOpcode()) {
  default: return nullptr;
  case NVPTXISD::Suld1DI8Clamp:
    Opc = NVPTX::SULD_1D_I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI16Clamp:
    Opc = NVPTX::SULD_1D_I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI32Clamp:
    Opc = NVPTX::SULD_1D_I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI64Clamp:
    Opc = NVPTX::SULD_1D_I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I8Clamp:
    Opc = NVPTX::SULD_1D_V2I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I16Clamp:
    Opc = NVPTX::SULD_1D_V2I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I32Clamp:
    Opc = NVPTX::SULD_1D_V2I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I64Clamp:
    Opc = NVPTX::SULD_1D_V2I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I8Clamp:
    Opc = NVPTX::SULD_1D_V4I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I16Clamp:
    Opc = NVPTX::SULD_1D_V4I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I32Clamp:
    Opc = NVPTX::SULD_1D_V4I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI64Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I64Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I8Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I16Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I32Clamp:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI8Clamp:
    Opc = NVPTX::SULD_2D_I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI16Clamp:
    Opc = NVPTX::SULD_2D_I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI32Clamp:
    Opc = NVPTX::SULD_2D_I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI64Clamp:
    Opc = NVPTX::SULD_2D_I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I8Clamp:
    Opc = NVPTX::SULD_2D_V2I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I16Clamp:
    Opc = NVPTX::SULD_2D_V2I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I32Clamp:
    Opc = NVPTX::SULD_2D_V2I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I64Clamp:
    Opc = NVPTX::SULD_2D_V2I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I8Clamp:
    Opc = NVPTX::SULD_2D_V4I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I16Clamp:
    Opc = NVPTX::SULD_2D_V4I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I32Clamp:
    Opc = NVPTX::SULD_2D_V4I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI64Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I64Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I8Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I16Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I32Clamp:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI8Clamp:
    Opc = NVPTX::SULD_3D_I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI16Clamp:
    Opc = NVPTX::SULD_3D_I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI32Clamp:
    Opc = NVPTX::SULD_3D_I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI64Clamp:
    Opc = NVPTX::SULD_3D_I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I8Clamp:
    Opc = NVPTX::SULD_3D_V2I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I16Clamp:
    Opc = NVPTX::SULD_3D_V2I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I32Clamp:
    Opc = NVPTX::SULD_3D_V2I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I64Clamp:
    Opc = NVPTX::SULD_3D_V2I64_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I8Clamp:
    Opc = NVPTX::SULD_3D_V4I8_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I16Clamp:
    Opc = NVPTX::SULD_3D_V4I16_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I32Clamp:
    Opc = NVPTX::SULD_3D_V4I32_CLAMP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI8Trap:
    Opc = NVPTX::SULD_1D_I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI16Trap:
    Opc = NVPTX::SULD_1D_I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI32Trap:
    Opc = NVPTX::SULD_1D_I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI64Trap:
    Opc = NVPTX::SULD_1D_I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I8Trap:
    Opc = NVPTX::SULD_1D_V2I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I16Trap:
    Opc = NVPTX::SULD_1D_V2I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I32Trap:
    Opc = NVPTX::SULD_1D_V2I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I64Trap:
    Opc = NVPTX::SULD_1D_V2I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I8Trap:
    Opc = NVPTX::SULD_1D_V4I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I16Trap:
    Opc = NVPTX::SULD_1D_V4I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I32Trap:
    Opc = NVPTX::SULD_1D_V4I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI64Trap:
    Opc = NVPTX::SULD_1D_ARRAY_I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I64Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I8Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I16Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I32Trap:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI8Trap:
    Opc = NVPTX::SULD_2D_I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI16Trap:
    Opc = NVPTX::SULD_2D_I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI32Trap:
    Opc = NVPTX::SULD_2D_I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI64Trap:
    Opc = NVPTX::SULD_2D_I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I8Trap:
    Opc = NVPTX::SULD_2D_V2I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I16Trap:
    Opc = NVPTX::SULD_2D_V2I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I32Trap:
    Opc = NVPTX::SULD_2D_V2I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I64Trap:
    Opc = NVPTX::SULD_2D_V2I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I8Trap:
    Opc = NVPTX::SULD_2D_V4I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I16Trap:
    Opc = NVPTX::SULD_2D_V4I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I32Trap:
    Opc = NVPTX::SULD_2D_V4I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI64Trap:
    Opc = NVPTX::SULD_2D_ARRAY_I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I64Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I8Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I16Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I32Trap:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI8Trap:
    Opc = NVPTX::SULD_3D_I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI16Trap:
    Opc = NVPTX::SULD_3D_I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI32Trap:
    Opc = NVPTX::SULD_3D_I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI64Trap:
    Opc = NVPTX::SULD_3D_I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I8Trap:
    Opc = NVPTX::SULD_3D_V2I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I16Trap:
    Opc = NVPTX::SULD_3D_V2I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I32Trap:
    Opc = NVPTX::SULD_3D_V2I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I64Trap:
    Opc = NVPTX::SULD_3D_V2I64_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I8Trap:
    Opc = NVPTX::SULD_3D_V4I8_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I16Trap:
    Opc = NVPTX::SULD_3D_V4I16_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I32Trap:
    Opc = NVPTX::SULD_3D_V4I32_TRAP;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI8Zero:
    Opc = NVPTX::SULD_1D_I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI16Zero:
    Opc = NVPTX::SULD_1D_I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI32Zero:
    Opc = NVPTX::SULD_1D_I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DI64Zero:
    Opc = NVPTX::SULD_1D_I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I8Zero:
    Opc = NVPTX::SULD_1D_V2I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I16Zero:
    Opc = NVPTX::SULD_1D_V2I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I32Zero:
    Opc = NVPTX::SULD_1D_V2I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV2I64Zero:
    Opc = NVPTX::SULD_1D_V2I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I8Zero:
    Opc = NVPTX::SULD_1D_V4I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I16Zero:
    Opc = NVPTX::SULD_1D_V4I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DV4I32Zero:
    Opc = NVPTX::SULD_1D_V4I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayI64Zero:
    Opc = NVPTX::SULD_1D_ARRAY_I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV2I64Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V2I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I8Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I16Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld1DArrayV4I32Zero:
    Opc = NVPTX::SULD_1D_ARRAY_V4I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI8Zero:
    Opc = NVPTX::SULD_2D_I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI16Zero:
    Opc = NVPTX::SULD_2D_I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI32Zero:
    Opc = NVPTX::SULD_2D_I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DI64Zero:
    Opc = NVPTX::SULD_2D_I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I8Zero:
    Opc = NVPTX::SULD_2D_V2I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I16Zero:
    Opc = NVPTX::SULD_2D_V2I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I32Zero:
    Opc = NVPTX::SULD_2D_V2I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV2I64Zero:
    Opc = NVPTX::SULD_2D_V2I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I8Zero:
    Opc = NVPTX::SULD_2D_V4I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I16Zero:
    Opc = NVPTX::SULD_2D_V4I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DV4I32Zero:
    Opc = NVPTX::SULD_2D_V4I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayI64Zero:
    Opc = NVPTX::SULD_2D_ARRAY_I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV2I64Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V2I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I8Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I16Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld2DArrayV4I32Zero:
    Opc = NVPTX::SULD_2D_ARRAY_V4I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI8Zero:
    Opc = NVPTX::SULD_3D_I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI16Zero:
    Opc = NVPTX::SULD_3D_I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI32Zero:
    Opc = NVPTX::SULD_3D_I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DI64Zero:
    Opc = NVPTX::SULD_3D_I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I8Zero:
    Opc = NVPTX::SULD_3D_V2I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I16Zero:
    Opc = NVPTX::SULD_3D_V2I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I32Zero:
    Opc = NVPTX::SULD_3D_V2I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV2I64Zero:
    Opc = NVPTX::SULD_3D_V2I64_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I8Zero:
    Opc = NVPTX::SULD_3D_V4I8_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I16Zero:
    Opc = NVPTX::SULD_3D_V4I16_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  case NVPTXISD::Suld3DV4I32Zero:
    Opc = NVPTX::SULD_3D_V4I32_ZERO;
    Ops.push_back(TexHandle);
    Ops.push_back(N->getOperand(2));
    Ops.push_back(N->getOperand(3));
    Ops.push_back(N->getOperand(4));
    Ops.push_back(Chain);
    break;
  }
  Ret = CurDAG->getMachineNode(Opc, SDLoc(N), N->getVTList(), Ops);
  return Ret;
}


/// SelectBFE - Look for instruction sequences that can be made more efficient
/// by using the 'bfe' (bit-field extract) PTX instruction
SDNode *NVPTXDAGToDAGISel::SelectBFE(SDNode *N) {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue Len;
  SDValue Start;
  SDValue Val;
  bool IsSigned = false;

  if (N->getOpcode() == ISD::AND) {
    // Canonicalize the operands
    // We want 'and %val, %mask'
    if (isa<ConstantSDNode>(LHS) && !isa<ConstantSDNode>(RHS)) {
      std::swap(LHS, RHS);
    }

    ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(RHS);
    if (!Mask) {
      // We need a constant mask on the RHS of the AND
      return NULL;
    }

    // Extract the mask bits
    uint64_t MaskVal = Mask->getZExtValue();
    if (!isMask_64(MaskVal)) {
      // We *could* handle shifted masks here, but doing so would require an
      // 'and' operation to fix up the low-order bits so we would trade
      // shr+and for bfe+and, which has the same throughput
      return NULL;
    }

    // How many bits are in our mask?
    uint64_t NumBits = countTrailingOnes(MaskVal);
    Len = CurDAG->getTargetConstant(NumBits, DL, MVT::i32);

    if (LHS.getOpcode() == ISD::SRL || LHS.getOpcode() == ISD::SRA) {
      // We have a 'srl/and' pair, extract the effective start bit and length
      Val = LHS.getNode()->getOperand(0);
      Start = LHS.getNode()->getOperand(1);
      ConstantSDNode *StartConst = dyn_cast<ConstantSDNode>(Start);
      if (StartConst) {
        uint64_t StartVal = StartConst->getZExtValue();
        // How many "good" bits do we have left?  "good" is defined here as bits
        // that exist in the original value, not shifted in.
        uint64_t GoodBits = Start.getValueType().getSizeInBits() - StartVal;
        if (NumBits > GoodBits) {
          // Do not handle the case where bits have been shifted in. In theory
          // we could handle this, but the cost is likely higher than just
          // emitting the srl/and pair.
          return NULL;
        }
        Start = CurDAG->getTargetConstant(StartVal, DL, MVT::i32);
      } else {
        // Do not handle the case where the shift amount (can be zero if no srl
        // was found) is not constant. We could handle this case, but it would
        // require run-time logic that would be more expensive than just
        // emitting the srl/and pair.
        return NULL;
      }
    } else {
      // Do not handle the case where the LHS of the and is not a shift. While
      // it would be trivial to handle this case, it would just transform
      // 'and' -> 'bfe', but 'and' has higher-throughput.
      return NULL;
    }
  } else if (N->getOpcode() == ISD::SRL || N->getOpcode() == ISD::SRA) {
    if (LHS->getOpcode() == ISD::AND) {
      ConstantSDNode *ShiftCnst = dyn_cast<ConstantSDNode>(RHS);
      if (!ShiftCnst) {
        // Shift amount must be constant
        return NULL;
      }

      uint64_t ShiftAmt = ShiftCnst->getZExtValue();

      SDValue AndLHS = LHS->getOperand(0);
      SDValue AndRHS = LHS->getOperand(1);

      // Canonicalize the AND to have the mask on the RHS
      if (isa<ConstantSDNode>(AndLHS)) {
        std::swap(AndLHS, AndRHS);
      }

      ConstantSDNode *MaskCnst = dyn_cast<ConstantSDNode>(AndRHS);
      if (!MaskCnst) {
        // Mask must be constant
        return NULL;
      }

      uint64_t MaskVal = MaskCnst->getZExtValue();
      uint64_t NumZeros;
      uint64_t NumBits;
      if (isMask_64(MaskVal)) {
        NumZeros = 0;
        // The number of bits in the result bitfield will be the number of
        // trailing ones (the AND) minus the number of bits we shift off
        NumBits = countTrailingOnes(MaskVal) - ShiftAmt;
      } else if (isShiftedMask_64(MaskVal)) {
        NumZeros = countTrailingZeros(MaskVal);
        unsigned NumOnes = countTrailingOnes(MaskVal >> NumZeros);
        // The number of bits in the result bitfield will be the number of
        // trailing zeros plus the number of set bits in the mask minus the
        // number of bits we shift off
        NumBits = NumZeros + NumOnes - ShiftAmt;
      } else {
        // This is not a mask we can handle
        return NULL;
      }

      if (ShiftAmt < NumZeros) {
        // Handling this case would require extra logic that would make this
        // transformation non-profitable
        return NULL;
      }

      Val = AndLHS;
      Start = CurDAG->getTargetConstant(ShiftAmt, DL, MVT::i32);
      Len = CurDAG->getTargetConstant(NumBits, DL, MVT::i32);
    } else if (LHS->getOpcode() == ISD::SHL) {
      // Here, we have a pattern like:
      //
      // (sra (shl val, NN), MM)
      // or
      // (srl (shl val, NN), MM)
      //
      // If MM >= NN, we can efficiently optimize this with bfe
      Val = LHS->getOperand(0);

      SDValue ShlRHS = LHS->getOperand(1);
      ConstantSDNode *ShlCnst = dyn_cast<ConstantSDNode>(ShlRHS);
      if (!ShlCnst) {
        // Shift amount must be constant
        return NULL;
      }
      uint64_t InnerShiftAmt = ShlCnst->getZExtValue();

      SDValue ShrRHS = RHS;
      ConstantSDNode *ShrCnst = dyn_cast<ConstantSDNode>(ShrRHS);
      if (!ShrCnst) {
        // Shift amount must be constant
        return NULL;
      }
      uint64_t OuterShiftAmt = ShrCnst->getZExtValue();

      // To avoid extra codegen and be profitable, we need Outer >= Inner
      if (OuterShiftAmt < InnerShiftAmt) {
        return NULL;
      }

      // If the outer shift is more than the type size, we have no bitfield to
      // extract (since we also check that the inner shift is <= the outer shift
      // then this also implies that the inner shift is < the type size)
      if (OuterShiftAmt >= Val.getValueType().getSizeInBits()) {
        return NULL;
      }

      Start =
        CurDAG->getTargetConstant(OuterShiftAmt - InnerShiftAmt, DL, MVT::i32);
      Len =
        CurDAG->getTargetConstant(Val.getValueType().getSizeInBits() -
                                  OuterShiftAmt, DL, MVT::i32);

      if (N->getOpcode() == ISD::SRA) {
        // If we have a arithmetic right shift, we need to use the signed bfe
        // variant
        IsSigned = true;
      }
    } else {
      // No can do...
      return NULL;
    }
  } else {
    // No can do...
    return NULL;
  }


  unsigned Opc;
  // For the BFE operations we form here from "and" and "srl", always use the
  // unsigned variants.
  if (Val.getValueType() == MVT::i32) {
    if (IsSigned) {
      Opc = NVPTX::BFE_S32rii;
    } else {
      Opc = NVPTX::BFE_U32rii;
    }
  } else if (Val.getValueType() == MVT::i64) {
    if (IsSigned) {
      Opc = NVPTX::BFE_S64rii;
    } else {
      Opc = NVPTX::BFE_U64rii;
    }
  } else {
    // We cannot handle this type
    return NULL;
  }

  SDValue Ops[] = {
    Val, Start, Len
  };

  return CurDAG->getMachineNode(Opc, DL, N->getVTList(), Ops);
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
        Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(OpNode),
                                           mvt);
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
    Offset = CurDAG->getTargetConstant(0, SDLoc(OpNode), mvt);
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
      Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(OpNode),
                                         mvt);
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
  const Value *Src = nullptr;
  if (MemSDNode *mN = dyn_cast<MemSDNode>(N)) {
    if (spN == 0 && mN->getMemOperand()->getPseudoValue())
      return true;
    Src = mN->getMemOperand()->getValue();
  }
  if (!Src)
    return false;
  if (auto *PT = dyn_cast<PointerType>(Src->getType()))
    return (PT->getAddressSpace() == spN);
  return false;
}

/// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
/// inline asm expressions.
bool NVPTXDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, unsigned ConstraintID, std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1;
  switch (ConstraintID) {
  default:
    return true;
  case InlineAsm::Constraint_m: // memory
    if (SelectDirectAddr(Op, Op0)) {
      OutOps.push_back(Op0);
      OutOps.push_back(CurDAG->getTargetConstant(0, SDLoc(Op), MVT::i32));
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
