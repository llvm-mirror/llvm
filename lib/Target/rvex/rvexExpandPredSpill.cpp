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

#include <iostream>

using namespace llvm;

namespace {

  class rvexExpandPredSpillCode : public MachineFunctionPass {
    rvexTargetMachine &TM;
    const rvexSubtarget &ST;

  public:
    static char ID;
    rvexExpandPredSpillCode(rvexTargetMachine &tm) :
      MachineFunctionPass(ID), TM(tm), ST(*tm.getSubtargetImpl()) {}

    const char *getPassName() const {
      return "rvex Expand Predicate Spill code";
    }
    bool runOnMachineFunction(MachineFunction &MF);
  };

  char rvexExpandPredSpillCode::ID = 0;

bool rvexExpandPredSpillCode::runOnMachineFunction(MachineFunction &MF) {

  const rvexInstrInfo *TII = TM.getInstrInfo();
  unsigned TmpReg = rvex::R63;

  // Loop over all the basic blocks
  for(MachineFunction::iterator MBBb = MF.begin(), MBBe = MF.end();
      MBBb != MBBe; ++ MBBb) {
    MachineBasicBlock* MBB = MBBb;

    // Traverse the basic block
    for(MachineBasicBlock::iterator MII = MBB->begin(); MII!= MBB->end();
        ++MII) {
      MachineInstr *MI = MII;
      DebugLoc DL = MI->getDebugLoc();

      int Opc = MI->getOpcode();
      if(Opc == rvex::STW_PRED) {
        //unsigned FP = MI->getOperand(0).getReg();
        //assert(FI == TM.getRegisterInfo()->getFrameRegister(MF) && 
          //     "Not a Frame Pointer");
        unsigned numOps = MI->getNumOperands();
        for(unsigned i=0;i<numOps;++i) {
          std::cout << "operand["<< i <<"] = ";
          if(MI->getOperand(i).isFI()) std::cout << "FI\n";
          else if(MI->getOperand(i).isImm()) std::cout << "Imm\n";
          else if(MI->getOperand(i).isReg()) std::cout << "Reg\n";
          else std::cout << "?\n";
        }
        assert(MI->getOperand(0).isReg() && "Not Register. Store");
        int SrcReg = MI->getOperand(0).getReg();
        assert(rvex::BRRegsRegClass.contains(SrcReg) &&
               "Src is not a Branch Register");
        assert(MI->getOperand(1).isReg() && "Not a Register. Store");
        unsigned FP = MI->getOperand(1).getReg();
        assert(FP == TM.getRegisterInfo()->getFrameRegister(MF) &&
               "Operand 1 is not FrameReg");
        assert(MI->getOperand(2).isImm() && "Operand 2 Not offset");
        int FI = MI->getOperand(2).getImm();



        unsigned Opc = rvex::rvexADDC, ZeroReg = rvex::R0;

        // MBB = BuildMI(MBB, MII, DL, TII->get(Opc));
        BuildMI(*MBB, MII, DL, TII->get(Opc))
          .addReg(TmpReg, RegState::Define)
          .addReg(SrcReg, RegState::Define)
          .addReg(ZeroReg).addReg(ZeroReg)
          .addReg(SrcReg, RegState::Kill);

        BuildMI(*MBB, MII, DL, TII->get(rvex::ST)).addReg(TmpReg)
                .addReg(FP).addImm(FI);

        MII = MBB->erase(MI);
        --MI;
      }
      else if(Opc == rvex::LDW_PRED) {
       unsigned numOps = MI->getNumOperands();
        for(unsigned i=0;i<numOps;++i) {
          std::cout << "operand["<< i <<"] = ";
          if(MI->getOperand(i).isFI()) std::cout << "FI\n";
          else if(MI->getOperand(i).isImm()) std::cout << "Imm\n";
          else if(MI->getOperand(i).isReg()) std::cout << "Reg\n";
          else std::cout << "?\n";
        }

        assert(MI->getOperand(0).isReg() && "Not a register"); 
        int DstReg = MI->getOperand(0).getReg();
        assert(rvex::BRRegsRegClass.contains(DstReg) &&
               "Not a Predicate Register");
        assert(MI->getOperand(1).isReg() && "Not a register");
        unsigned FP = MI->getOperand(1).getReg();
        assert(FP == TM.getRegisterInfo()->getFrameRegister(MF) &&
               "Not a Frame Pointer");
        assert(MI->getOperand(2).isImm() && "Not a Frame Index");
        unsigned FI = MI->getOperand(2).getImm();

        BuildMI(*MBB, MII, DL, TII->get(rvex::LD), TmpReg)
                .addReg(FP).addImm(FI);
        // BuildMI(*MBB, MII, DL, TII->get(rvex::MTB), DstReg).addReg(TmpReg);
      
        unsigned Opc = rvex::CMPNE, ZeroReg = rvex::R0;
        BuildMI(*MBB, MII, DL, TII->get(Opc), DstReg)
          .addReg(ZeroReg)
          .addReg(TmpReg, RegState::Kill);  

     

        MII = MBB->erase(MI);
        --MI;
      }
    }
  }

  return true;
}

} // namespace

FunctionPass *llvm::creatervexExpandPredSpillCode(rvexTargetMachine &TM) {
  return new rvexExpandPredSpillCode(TM);
}
