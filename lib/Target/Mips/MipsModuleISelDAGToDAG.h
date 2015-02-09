//===---- MipsModuleISelDAGToDAG.h -  Change Subtarget             --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass used to change the subtarget for the
// Mips Instruction selector.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSMODULEISELDAGTODAG_H
#define LLVM_LIB_TARGET_MIPS_MIPSMODULEISELDAGTODAG_H

#include "Mips.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"


//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MipsModuleDAGToDAGISel - MIPS specific code to select MIPS machine
// instructions for SelectionDAG operations.
//===----------------------------------------------------------------------===//
namespace llvm {

class MipsModuleDAGToDAGISel : public MachineFunctionPass {
public:

  static char ID;

  explicit MipsModuleDAGToDAGISel(MipsTargetMachine &TM_)
      : MachineFunctionPass(ID), TM(TM_) {}

  // Pass Name
  const char *getPassName() const override {
    return "MIPS DAG->DAG Pattern Instruction Selection";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

protected:
  MipsTargetMachine &TM;
};

/// createMipsISelDag - This pass converts a legalized DAG into a
/// MIPS-specific DAG, ready for instruction scheduling.
FunctionPass *createMipsModuleISelDag(MipsTargetMachine &TM);
}

#endif
