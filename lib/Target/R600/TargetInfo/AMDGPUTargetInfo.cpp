//===-- TargetInfo/AMDGPUTargetInfo.cpp - TargetInfo for AMDGPU -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetMachine.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

/// \brief The target which suports all AMD GPUs.  This will eventually
///         be deprecated and there will be a R600 target and a GCN target.
Target llvm::TheAMDGPUTarget;
/// \brief The target for GCN GPUs
Target llvm::TheGCNTarget;

/// \brief Extern function to initialize the targets for the AMDGPU backend
extern "C" void LLVMInitializeR600TargetInfo() {
  RegisterTarget<Triple::r600, false>
    R600(TheAMDGPUTarget, "r600", "AMD GPUs HD2XXX-HD6XXX");
  RegisterTarget<Triple::amdgcn, false> GCN(TheGCNTarget, "amdgcn", "AMD GCN GPUs");
}
