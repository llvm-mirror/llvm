add_llvm_library(LLVMrvexDesc
  rvexMCTargetDesc.cpp
  rvexMCAsmInfo.cpp
  )

add_dependencies(LLVMrvexDesc rvexCommonTableGen)
