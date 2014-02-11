//===-- rvex.h - Top-level interface for rvex representation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM rvex back-end.
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_rvex_H
#define TARGET_rvex_H

#include "MCTargetDesc/rvexMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
	class rvexTargetMachine;
	class FunctionPass;

	FunctionPass *creatervexISelDag(rvexTargetMachine &TM);

	FunctionPass *creatervexVLIWPacketizer();
	FunctionPass *creatervexPostRAScheduler();
	FunctionPass *creatervexExpandPredSpillCode(rvexTargetMachine &TM);	
	FunctionPass *CreateHelloPass(rvexTargetMachine &TM);	

} // end namespace llvm;

#endif
