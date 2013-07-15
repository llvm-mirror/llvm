//===----------------------------------------------------------------------===//
// Instruction Selector Subtarget Control
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// This file defines a pass used to change the subtarget for the
// Mips Instruction selector.
//
//===----------------------------------------------------------------------===//

#include "MipsISelDAGToDAG.h"
#include "MipsModuleISelDAGToDAG.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

bool MipsModuleDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  DEBUG(errs() << "In MipsModuleDAGToDAGISel::runMachineFunction\n");
  const_cast<MipsSubtarget&>(Subtarget).resetSubtarget(&MF);
  return false;
}

char MipsModuleDAGToDAGISel::ID = 0;

}


llvm::FunctionPass *llvm::createMipsModuleISelDag(MipsTargetMachine &TM) {
  return new MipsModuleDAGToDAGISel(TM);
}


