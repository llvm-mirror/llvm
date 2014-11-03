add_llvm_library(LLVMrvexDesc
  rvexMCTargetDesc.cpp
  rvexMCAsmInfo.cpp
  rvexReadConfig.cpp
  rvexBuildDFA.cpp
  rvexSubtargetInfo.cpp
  )

add_dependencies(LLVMrvexDesc rvexCommonTableGen)
