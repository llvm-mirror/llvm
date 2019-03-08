set(LLVM_TARGET_DEFINITIONS EVM.td)

tablegen(LLVM EVMGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM EVMGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM EVMGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM EVMGenCallingConv.inc -gen-callingconv)
tablegen(LLVM EVMGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM EVMGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM EVMGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM EVMGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(EVMCommonTableGen)

add_llvm_target(EVMCodeGen
  EVMAsmPrinter.cpp
  EVMFrameLowering.cpp
  EVMInstrInfo.cpp
  EVMISelDAGToDAG.cpp
  EVMISelLowering.cpp
  EVMMCInstLower.cpp
  EVMRegisterInfo.cpp
  EVMSubtarget.cpp
  EVMTargetMachine.cpp
  EVMTargetObjectFile.cpp
  )

add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
