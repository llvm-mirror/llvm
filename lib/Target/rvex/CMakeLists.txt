# CMakeLists.txt
# Our td all in rvex.td, rvexRegisterInfo.td and rvexInstrInfo.td included in 
#  rvex.td.
set(LLVM_TARGET_DEFINITIONS rvex.td)

# Generate rvexGenRegisterInfo.inc and rvexGenInstrInfo.inc which included by 
#  your hand code C++ files. 
# rvexGenRegisterInfo.inc came from rvexRegisterInfo.td, rvexGenInstrInfo.inc 
#  came from rvexInstrInfo.td.
tablegen(LLVM rvexGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM rvexGenInstrInfo.inc -gen-instr-info)

tablegen(LLVM rvexGenCodeEmitter.inc -gen-emitter)
tablegen(LLVM rvexGenMCCodeEmitter.inc -gen-emitter -mc-emitter)

tablegen(LLVM rvexGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM rvexGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM rvexGenCallingConv.inc -gen-callingconv)
tablegen(LLVM rvexGenSubtargetInfo.inc -gen-subtarget)

tablegen(LLVM rvexGenDFAPacketizer.inc -gen-dfa-packetizer)

# rvexCommonTableGen must be defined
add_public_tablegen_target(rvexCommonTableGen)

# rvexCodeGen should match with LLVMBuild.txt rvexCodeGen
add_llvm_target(rvexCodeGen
  rvexAsmPrinter.cpp
  rvexInstrInfo.cpp
  rvexISelDAGToDAG.cpp
  rvexISelLowering.cpp
  rvexFrameLowering.cpp
  rvexMCInstLower.cpp
  rvexMachineFunction.cpp
  rvexRegisterInfo.cpp
  rvexSubtarget.cpp
  rvexTargetMachine.cpp
  rvexTargetObjectFile.cpp
  rvexSelectionDAGInfo.cpp
  rvexVLIWPacketizer.cpp
  rvexHazardRecognizers.cpp
  rvexPostRAScheduler.cpp
  rvexExpandPredSpill.cpp
  )

# Should match with "subdirectories =  MCTargetDesc TargetInfo" in LLVMBuild.txt
add_subdirectory(InstPrinter)
add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
