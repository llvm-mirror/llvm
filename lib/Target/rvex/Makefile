##===- lib/Target/rvex/Makefile --------------------------*- Makefile -*-===##
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
##===----------------------------------------------------------------------===##

LEVEL = ../../..
LIBRARYNAME = LLVMrvexCodeGen
TARGET = rvex

# Make sure that tblgen is run, first thing.
BUILT_SOURCES = rvexGenRegisterInfo.inc rvexGenInstrInfo.inc \
		rvexGenAsmWriter.inc rvexGenDAGISel.inc \
		rvexGenSubtargetInfo.inc rvexGenCallingConv.inc \
		rvexGenDFAPacketizer.inc

DIRS = TargetInfo MCTargetDesc

include $(LEVEL)/Makefile.common

