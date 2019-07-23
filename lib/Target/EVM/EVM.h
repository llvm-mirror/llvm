//===-- EVM.h - Top-level interface for EVM -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// EVM back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_EVM_H
#define LLVM_LIB_TARGET_EVM_EVM_H

#include "llvm/Target/TargetMachine.h"

namespace llvm {
class EVMTargetMachine;
class AsmPrinter;
class FunctionPass;
class MCInst;
class MCOperand;
class MachineInstr;
class MachineOperand;
class PassRegistry;

void LowerEVMMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                    const AsmPrinter &AP);
bool LowerEVMMachineOperandToMCOperand(const MachineOperand &MO,
                                         MCOperand &MCOp, const AsmPrinter &AP);

FunctionPass *createEVMISelDag(EVMTargetMachine &TM);

ModulePass   *createEVMCallTransformation();
FunctionPass *createEVMPrepareStackification();
FunctionPass *createEVMStackification();
FunctionPass *createEVMVRegToMem();
FunctionPass *createEVMPrepareForLiveIntervals();
FunctionPass *createEVMConvertRegToStack();
FunctionPass *createEVMShrinkpush();
FunctionPass *createEVMArgumentMove();
FunctionPass *createEVMExpandPseudos();
FunctionPass *createEVMExpandFramePointer();
FunctionPass *createEVMFinalization();

void initializeEVMPrepareStackificationPass(PassRegistry &);
void initializeEVMStackificationPass(PassRegistry &);
void initializeEVMVRegToMemPass(PassRegistry &);
void initializeEVMPrepareForLiveIntervalsPass(PassRegistry &);
void initializeEVMConvertRegToStackPass(PassRegistry &);
void initializeEVMShrinkpushPass(PassRegistry &);
void initializeEVMArgumentMovePass(PassRegistry &);
void initializeEVMExpandPseudosPass(PassRegistry &);
void initializeEVMCallTransformationPass(PassRegistry &);
void initializeEVMFinalizationPass(PassRegistry &);
void initializeEVMExpandFramePointerPass(PassRegistry &);

}

#endif
