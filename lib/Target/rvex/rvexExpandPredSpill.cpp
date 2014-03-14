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

bool isLoad(unsigned Opc) {
  switch(Opc) {
    case rvex::LD:
    case rvex::LB:
    case rvex::LBu:
    case rvex::LH:
    case rvex::LHu:
      return true;   

    default:
      return false;
  }
}

bool isStore(unsigned Opc) {
  switch(Opc) {
    case rvex::ST:
    case rvex::SB:
    case rvex::SH:
      return true;   

    default:
      return false;
  }
}

bool rvexExpandPredSpillCode::runOnMachineFunction(MachineFunction &MF) {

  const rvexInstrInfo *TII = TM.getInstrInfo();
  unsigned TmpReg = rvex::R63;

  bool found_ldw = false;
  bool found_branch = false;
  unsigned ldw_defines = 0;

  // Loop over all the basic blocks
  for(MachineFunction::iterator MBBb = MF.begin(), MBBe = MF.end();
      MBBb != MBBe; ++ MBBb) {
    MachineBasicBlock* MBB = MBBb;

    found_ldw = false;
    found_branch = false;
    // Traverse the basic block
    for(MachineBasicBlock::iterator MII = MBB->begin(); MII!= MBB->end();
        ++MII) {
      MachineInstr *MI = MII;
      DebugLoc DL = MI->getDebugLoc();

      int Opc = MI->getOpcode();

      // FIXME another hack to introduce extra nops when required
      if (MI->isConditionalBranch() && found_branch) {
        DEBUG(dbgs() << "Found branch & compare\n\tInsert NOP\n");
        BuildMI(*MBB, MI, DL, TII->get(rvex::NOP));
        found_branch = false;
      }      
      if (MI->isCompare()) {
        found_branch = true;
      }


      // FIXME Hack to introduce nops when spillcode was used after machine scheduling
      // In first pass Load instruction sets bool value
      // If second instruction is store and bool value is true introduce a NOOP
      if (isStore(Opc) && found_ldw) {

        if (MI->getOperand(0).getReg() == ldw_defines) {
          BuildMI(*MBB, MI, DL, TII->get(rvex::NOP));
        }
        if (MI->getOperand(1).getReg() == ldw_defines) {
          BuildMI(*MBB, MI, DL, TII->get(rvex::NOP));
        }        

        found_ldw = false;
      }

      if (isLoad(Opc) && found_ldw) {

        if (MI->getOperand(0).getReg() == ldw_defines) {
          BuildMI(*MBB, MI, DL, TII->get(rvex::NOP));
        }
        if (MI->getOperand(1).getReg() == ldw_defines) {
          BuildMI(*MBB, MI, DL, TII->get(rvex::NOP));
        }        

        found_ldw = false;
      }      

      if (isLoad(Opc)) {
        found_ldw = true;
        ldw_defines = MI->getOperand(0).getReg();
      }


      if(Opc == rvex::STW_PRED) {
        //unsigned FP = MI->getOperand(0).getReg();
        //assert(FI == TM.getRegisterInfo()->getFrameRegister(MF) && 
          //     "Not a Frame Pointer");
        unsigned numOps = MI->getNumOperands();
        for(unsigned i=0;i<numOps;++i) {
          // std::cout << "operand["<< i <<"] = ";
          // if(MI->getOperand(i).isFI()) std::cout << "FI\n";
          // else if(MI->getOperand(i).isImm()) std::cout << "Imm\n";
          // else if(MI->getOperand(i).isReg()) std::cout << "Reg\n";
          // else std::cout << "?\n";
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
          // std::cout << "operand["<< i <<"] = ";
          // if(MI->getOperand(i).isFI()) std::cout << "FI\n";
          // else if(MI->getOperand(i).isImm()) std::cout << "Imm\n";
          // else if(MI->getOperand(i).isReg()) std::cout << "Reg\n";
          // else std::cout << "?\n";
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
