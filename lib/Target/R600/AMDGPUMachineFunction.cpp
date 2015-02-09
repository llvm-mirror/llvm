#include "AMDGPUMachineFunction.h"
#include "AMDGPU.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
using namespace llvm;

static const char *const ShaderTypeAttribute = "ShaderType";

// Pin the vtable to this file.
void AMDGPUMachineFunction::anchor() {}

AMDGPUMachineFunction::AMDGPUMachineFunction(const MachineFunction &MF) :
  MachineFunctionInfo(),
  ShaderType(ShaderType::COMPUTE),
  LDSSize(0),
  ScratchSize(0),
  IsKernel(true) {
  AttributeSet Set = MF.getFunction()->getAttributes();
  Attribute A = Set.getAttribute(AttributeSet::FunctionIndex,
                                 ShaderTypeAttribute);

  if (A.isStringAttribute()) {
    StringRef Str = A.getValueAsString();
    if (Str.getAsInteger(0, ShaderType))
      llvm_unreachable("Can't parse shader type!");
  }
}
