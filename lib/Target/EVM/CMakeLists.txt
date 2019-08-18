set(LLVM_TARGET_DEFINITIONS EVM.td)

tablegen(LLVM EVMGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM EVMGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM EVMGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM EVMGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM EVMGenCallingConv.inc -gen-callingconv)
tablegen(LLVM EVMGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM EVMGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM EVMGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM EVMGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM EVMGenDisassemblerTables.inc -gen-disassembler)

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
  EVMCallTransformation.cpp
  EVMVRegToMem.cpp
  EVMPrepareStackification.cpp
  EVMStackification.cpp
  EVMConvertRegToStack.cpp
  EVMShrinkPush.cpp
  EVMPrepareForLiveIntervals.cpp
  EVMArgumentMove.cpp
  EVMExpandPseudos.cpp
  EVMExpandFramePointer.cpp
  EVMFinalization.cpp
  EVMUtils.cpp
  )


add_subdirectory(AsmParser)
add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(Disassembler)
