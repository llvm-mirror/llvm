//===-- AMDGPU.h - MachineFunction passes hw codegen --------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef AMDGPU_H
#define AMDGPU_H

#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class AMDGPUInstrPrinter;
class AMDGPUTargetMachine;
class FunctionPass;
class MCAsmInfo;
class raw_ostream;
class Target;
class TargetMachine;

// R600 Passes
FunctionPass *createR600VectorRegMerger(TargetMachine &tm);
FunctionPass *createR600TextureIntrinsicsReplacer();
FunctionPass *createR600ExpandSpecialInstrsPass(TargetMachine &tm);
FunctionPass *createR600EmitClauseMarkers(TargetMachine &tm);
FunctionPass *createR600Packetizer(TargetMachine &tm);
FunctionPass *createR600ControlFlowFinalizer(TargetMachine &tm);
FunctionPass *createAMDGPUCFGPreparationPass(TargetMachine &tm);
FunctionPass *createAMDGPUCFGStructurizerPass(TargetMachine &tm);

// SI Passes
FunctionPass *createSIAnnotateControlFlowPass();
FunctionPass *createSILowerControlFlowPass(TargetMachine &tm);
FunctionPass *createSICodeEmitterPass(formatted_raw_ostream &OS);
FunctionPass *createSIInsertWaits(TargetMachine &tm);

// Passes common to R600 and SI
Pass *createAMDGPUStructurizeCFGPass();
FunctionPass *createAMDGPUConvertToISAPass(TargetMachine &tm);
FunctionPass *createAMDGPUIndirectAddressingPass(TargetMachine &tm);
FunctionPass *createAMDGPUISelDag(TargetMachine &tm);

extern Target TheAMDGPUTarget;

} // End namespace llvm

namespace ShaderType {
  enum Type {
    PIXEL = 0,
    VERTEX = 1,
    GEOMETRY = 2,
    COMPUTE = 3
  };
}

/// OpenCL uses address spaces to differentiate between
/// various memory regions on the hardware. On the CPU
/// all of the address spaces point to the same memory,
/// however on the GPU, each address space points to
/// a seperate piece of memory that is unique from other
/// memory locations.
namespace AMDGPUAS {
enum AddressSpaces {
  PRIVATE_ADDRESS  = 0, ///< Address space for private memory.
  GLOBAL_ADDRESS   = 1, ///< Address space for global memory (RAT0, VTX0).
  CONSTANT_ADDRESS = 2, ///< Address space for constant memory
  LOCAL_ADDRESS    = 3, ///< Address space for local memory.
  REGION_ADDRESS   = 4, ///< Address space for region memory.
  ADDRESS_NONE     = 5, ///< Address space for unknown memory.
  PARAM_D_ADDRESS  = 6, ///< Address space for direct addressible parameter memory (CONST0)
  PARAM_I_ADDRESS  = 7, ///< Address space for indirect addressible parameter memory (VTX1)
  CONSTANT_BUFFER_0 = 8,
  CONSTANT_BUFFER_1 = 9,
  CONSTANT_BUFFER_2 = 10,
  CONSTANT_BUFFER_3 = 11,
  CONSTANT_BUFFER_4 = 12,
  CONSTANT_BUFFER_5 = 13,
  CONSTANT_BUFFER_6 = 14,
  CONSTANT_BUFFER_7 = 15,
  CONSTANT_BUFFER_8 = 16,
  CONSTANT_BUFFER_9 = 17,
  CONSTANT_BUFFER_10 = 18,
  CONSTANT_BUFFER_11 = 19,
  CONSTANT_BUFFER_12 = 20,
  CONSTANT_BUFFER_13 = 21,
  CONSTANT_BUFFER_14 = 22,
  CONSTANT_BUFFER_15 = 23,
  LAST_ADDRESS     = 24
};

} // namespace AMDGPUAS

#endif // AMDGPU_H
