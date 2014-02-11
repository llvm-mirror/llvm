#include "rvexTargetMachine.h"
#include "rvexSubtarget.h"
#include "rvexMachineFunction.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "rvexTargetMachine.h"
#include "rvexSubtarget.h"
#include "rvexMachineFunction.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {


  class rvexHelloPass : public MachineFunctionPass {
    rvexTargetMachine &TM;
    const rvexSubtarget &ST;

  public:
    static char ID;
    rvexHelloPass(rvexTargetMachine &tm) :
      MachineFunctionPass(ID), TM(tm), ST(*tm.getSubtargetImpl()) {}

    const char *getPassName() const {
      return "rvex Hello pass";
    }
    bool runOnMachineFunction(MachineFunction &MF);
  };  

  char rvexHelloPass::ID = 0;


bool rvexHelloPass::runOnMachineFunction(MachineFunction &MF) {
      errs() << "Hello: ";
      errs().write_escaped(MF.getName()) << "\n";
  
  return true;
}

} // namespace


FunctionPass *llvm::CreateHelloPass(rvexTargetMachine &TM) {
  return new rvexHelloPass(TM);
}
