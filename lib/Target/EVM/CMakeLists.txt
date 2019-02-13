set(LLVM_TARGET_DEFINITIONS EVM.td)

tablegen(LLVM EVMGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM EVMGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM EVMGenCompressInstEmitter.inc -gen-compress-inst-emitter)
tablegen(LLVM EVMGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM EVMGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM EVMGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM EVMGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM EVMGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM EVMGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM EVMGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM EVMGenSystemOperands.inc -gen-searchable-tables)

add_public_tablegen_target(EVMCommonTableGen)

add_llvm_target(EVMCodeGen
  EVMAsmPrinter.cpp
  EVMExpandPseudoInsts.cpp
  EVMFrameLowering.cpp
  EVMInstrInfo.cpp
  EVMISelDAGToDAG.cpp
  EVMISelLowering.cpp
  EVMMCInstLower.cpp
  EVMMergeBaseOffset.cpp
  EVMRegisterInfo.cpp
  EVMSubtarget.cpp
  EVMTargetMachine.cpp
  EVMTargetObjectFile.cpp
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(InstPrinter)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(Utils)
