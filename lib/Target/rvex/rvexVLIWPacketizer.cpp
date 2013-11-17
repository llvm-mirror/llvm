//===--- rvexVLIWPacketizer.cpp - VLIW Packetizer -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//                               rvex Backend
//
// Author: David Juhasz
// E-mail: juhda@caesar.elte.hu
// Institute: Dept. of Programming Languages and Compilers, ELTE IK, Hungary
//
// The research is supported by the European Union and co-financed by the
// European Social Fund (grant agreement no. TAMOP
// 4.2.1./B-09/1/KMR-2010-0003).
//
// The original implementation of rvex VLIW Packetizer was made using that of
// Hexagon VLIW Packetizer.
//
// This implements a simple VLIW packetizer using DFA. The packetizer works on
// machine basic blocks. For each instruction I in BB, the packetizer consults
// the DFA to see if machine resources are available to execute I. If so, the
// packetizer checks if I depends on any instruction J in the current packet.
// If no dependency is found, I is added to current packet and machine resource
// is marked as taken. If any dependency is found, a target API call is made to
// prune the dependence.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "packets"

#include "rvex.h"
#include "rvexInstrInfo.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/MC/MCInstrItineraries.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace rvexII;

namespace {
  class rvexVLIWPacketizer : public MachineFunctionPass {

  public:
    static char ID;
    rvexVLIWPacketizer() : MachineFunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineLoopInfo>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    const char *getPassName() const {
      return "rvex VLIW Packetizer";
    }

    bool runOnMachineFunction(MachineFunction &Fn);
  };
  char rvexVLIWPacketizer::ID = 0;

  class rvexVLIWPacketizerList : public VLIWPacketizerList {

    // Check if there is a dependence between some instruction already in this
    // packet and this instruction.
    bool Dependence;

    int SlotsUsed;

    // Only check for dependence if there are resources available to schedule
    // this instruction.
    bool FoundSequentialDependence;

  public:
    // Ctor.
    rvexVLIWPacketizerList(MachineFunction &MF, MachineLoopInfo &MLI,
                             MachineDominatorTree &MDT);

		//default implementation of virtual function addToPacket will do

    // initPacketizerState - initialize some internal flags.
    virtual void initPacketizerState(void);


    // ignorePseudoInstruction - Ignore bundling of pseudo instructions.
    virtual bool ignorePseudoInstruction(MachineInstr *MI,
                                         MachineBasicBlock *MBB);

    virtual MachineBasicBlock::iterator addToPacket(MachineInstr *MI);  

    // isSoloInstruction - return true if instruction MI can not be packetized
    // with any other instruction, which means that MI itself is a packet.
    virtual bool isSoloInstruction(MachineInstr *MI);



    // isLegalToPacketizeTogether - Is it legal to packetize SUI and SUJ
    // together.
    virtual bool isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ);

    // isLegalToPruneDependencies - Is it legal to prune dependece between SUI
    // and SUJ.
    virtual bool isLegalToPruneDependencies(SUnit *SUI, SUnit *SUJ);

  private:
    // isDirectJump - Return true if the instruction is a direct jump.
    bool isDirectJump(const MachineInstr *MI) const;

    // isrvexSoloInstruction - Return true if TSFlags:4 is 1.
    bool isrvexSoloInstruction(const MachineInstr *MI) const;

    // isrvexLongInstruction - Return true if TSFlags:5 is 1.
    bool isrvexLongInstruction(const MachineInstr *MI) const;

    // rvexTypeOf - Return rvexType of MI.
		rvexType rvexTypeOf(const MachineInstr *MI) const;

    // isrvexCtrInstruction - Return true if rvexType of MI is TypeCtr.
    bool isrvexCtrInstruction(const MachineInstr *MI) const;

		// isrvexMemInstruction - Return true if rvexType of MI is TypeMeS or
		// TypeMeL.
    bool isrvexMemInstruction(const MachineInstr *MI) const;
  };
}

// rvexVLIWPacketizerList Ctor.
rvexVLIWPacketizerList::rvexVLIWPacketizerList(MachineFunction &MF,
                                                   MachineLoopInfo &MLI,
                                                   MachineDominatorTree &MDT)
  : VLIWPacketizerList(MF, MLI, MDT, true){
}

bool rvexVLIWPacketizer::runOnMachineFunction(MachineFunction &Fn) {
  DEBUG(errs() << "Voor VLIW packetizer!\n");
  const TargetInstrInfo *TII = Fn.getTarget().getInstrInfo();
  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfo>();
  MachineDominatorTree &MDT = getAnalysis<MachineDominatorTree>();

  // Instantiate the packetizer.
  rvexVLIWPacketizerList Packetizer(Fn, MLI, MDT);

  // DFA state table should not be empty.
  assert(Packetizer.getResourceTracker() && "Empty DFA table!");

	// Hexagon deletes KILL instructions here, but in the case of rvex there
	// should be no KILLs, as there are no sub-registers.
	
  // Loop over all of the basic blocks.
  for (MachineFunction::iterator MBB = Fn.begin(), MBBe = Fn.end();
       MBB != MBBe; ++MBB) {
    // Find scheduling regions and schedule / packetize each region.
    for(MachineBasicBlock::iterator RegionEnd = MBB->end(), MBBb = MBB->begin();
        RegionEnd != MBBb;) {
      // The next region starts above the previous region. Look backward in the
      // instruction stream until we find the nearest boundary.
      MachineBasicBlock::iterator I = RegionEnd;
      for(; I != MBBb; --I) {
        if (TII->isSchedulingBoundary(llvm::prior(I), MBB, Fn))
          break;
      }

      // Skip empty regions and regions with one instruction.
      MachineBasicBlock::iterator priorEnd = llvm::prior(RegionEnd);
      if (I == RegionEnd || I == priorEnd) {
        RegionEnd = priorEnd;
        continue;
      }

      Packetizer.PacketizeMIs(MBB, I, RegionEnd);
      RegionEnd = I;
    }
  }

  return true;
}


// initPacketizerState - Initialize packetizer flags
void rvexVLIWPacketizerList::initPacketizerState() {
  Dependence = false;
  SlotsUsed = 0;
  FoundSequentialDependence = false;
}

// ignorePseudoInstruction - Ignore bundling of pseudo instructions.
bool rvexVLIWPacketizerList::ignorePseudoInstruction(MachineInstr *MI,
                                                       MachineBasicBlock *MBB) {
  assert(!isSoloInstruction(MI) && "Solo instruction should not be here!");
  if(MI->isDebugValue()) {
    return true;
  } else {
    //all other instructions should have functional unit mapped to them.
    assert(ResourceTracker->getInstrItins()->beginStage(MI->getDesc().getSchedClass())->getUnits()
           && "Instruction without FuncUnit!");
    return false;
  }
}

// isSoloInstruction - Returns true for instructions that must be
// scheduled in their own packet.
bool rvexVLIWPacketizerList::isSoloInstruction(MachineInstr *MI) {
  //Tile Reference Manual doesn't imply any solo instructions.
  //TODO: hexagon says EHLabel to be a solo instruction as well
  if (MI->isInlineAsm() || isrvexSoloInstruction(MI)) {
    return true;
  } else {
    return false;
  }	
}

static bool isImmInstructon(MachineInstr *MI) {

  unsigned i;
  for (i = 0; i < MI->getNumOperands(); i++){
    if (MI->getOperand(i).isImm()) {
      int temp = MI->getOperand(i).getImm();
      // DEBUG(errs() << "Imm operand found: "<<temp<<"\n");
      if(!isInt<9>(temp)) {
        DEBUG(errs() << "Imm operand found: "<<temp<<"\n");
        // MI->dump();
        return true;
      }
    }
    if (MI->getOperand(i).isGlobal()) {
      DEBUG(errs() << "Global found!\n");
      // MI->dump();
      return true;
    }
  }

  return false;

}



// isLegalToPacketizeTogether:
// SUI is the current instruction that is out side of the current packet.
// SUJ is the current instruction inside the current packet against which that
// SUI will be packetized.
bool rvexVLIWPacketizerList::isLegalToPacketizeTogether(SUnit *SUI,
                                                          SUnit *SUJ) {
  MachineInstr *I = SUI->getInstr();
  MachineInstr *J = SUJ->getInstr();
  assert(I && J && "Unable to packetize null instruction!");
  assert(!isSoloInstruction(I) && !ignorePseudoInstruction(I, I->getParent()) &&
         "Something gone wrong with packetizer mechanism!");

  const MCInstrDesc &MCIDI = I->getDesc();
  const MCInstrDesc &MCIDJ = J->getDesc();

  // DEBUG(errs() << "i en j:\n");
  // I->dump();
  // J->dump();
  //In the case of rvex, two control flow instructions cannot have resource
  //in the same time.

  // if (isImmInstructon(I)) {
  //   SlotsUsed += 2;
  //   // return false;
  // }
  // else
  //   SlotsUsed += 1;
  // if (isImmInstructon(J)) {
  //   SlotsUsed += 2;
  //   // return false;
  // }
  // else
  //   SlotsUsed += 1;  

  if(SUJ->isSucc(SUI)) {
    //FIXME: is Succs not a set? -- use the loop only to find the index...
    for(unsigned i = 0;
        (i < SUJ->Succs.size()) && !FoundSequentialDependence;
        ++i) {

      if(SUJ->Succs[i].getSUnit() != SUI) {
        continue;
      }

      SDep Dep = SUJ->Succs[i];
      SDep::Kind DepType = Dep.getKind();
      unsigned DepReg = 0;
      if(DepType != SDep::Order) {
        DepReg = Dep.getReg();
      }

      if((MCIDI.isCall() || MCIDI.isReturn()) && DepType == SDep::Order) {
        DEBUG(errs() << "return \n");
        FoundSequentialDependence = true;
        // do nothing
      }

      //Hexagon handles predicated instructions here, but rvex instructions
      //are not predicated

      else if(isDirectJump(I) &&
              !MCIDJ.isBranch() &&
              !MCIDJ.isCall() &&
              (DepType == SDep::Order)) {
        // Ignore Order dependences between unconditional direct branches
        // and non-control-flow instructions
        // do nothing
      }
      else if(MCIDI.isConditionalBranch() && (DepType != SDep::Data) &&
              (DepType != SDep::Output)) {
        // Ignore all dependences for jumps except for true and output
        // dependences
        // do nothing
      }

      else if(DepType == SDep::Data) {
        DEBUG(errs() << "data \n");
        FoundSequentialDependence = true;
      }

      // zero-reg can be targeted by multiple instructions
      else if(DepType == SDep::Output && DepReg != rvex::R0) {
        DEBUG(errs() << "zero reg\n");
        FoundSequentialDependence = true;
      }

      else if(DepType == SDep::Order && Dep.isArtificial()) {
        // Ignore artificial dependencies
        // do nothing
      }

      // Skip over anti-dependences. Two instructions that are
      // anti-dependent can share a packet
      else if(DepType != SDep::Anti) {
        DEBUG(errs() << "anti dependencies\n");
        FoundSequentialDependence = false;
      }
    }

    if(FoundSequentialDependence) {
      Dependence = true;
      DEBUG(errs() << "false\n");
      return false;
    }
  }

  // if (SlotsUsed > 14) {
  //   DEBUG(errs() << "Final Slots: "<<SlotsUsed<<"\n");
  //   Dependence = true;
  //   // SlotsUsed = 0;
  //   return false;
  // }

  // DEBUG(errs() << "Slots: "<<SlotsUsed<<"\n");
  DEBUG(errs() << "true\n");
  return true;
}

  // addToPacket - Add MI to the current packet.
  MachineBasicBlock::iterator rvexVLIWPacketizerList::addToPacket(MachineInstr *MI) {
    DEBUG(errs() << "rvex add!\n");
    MI->dump();
    MachineBasicBlock::iterator MII = MI;
    CurrentPacketMIs.push_back(MI);
    ResourceTracker->reserveResources(MI);
    if (isImmInstructon(MI))
    {
      ResourceTracker->reserveResources(MI);
    }
    return MII;
  }


// isLegalToPruneDependencies
bool rvexVLIWPacketizerList::isLegalToPruneDependencies(SUnit *SUI,
                                                          SUnit *SUJ) {
  MachineInstr *I = SUI->getInstr();
  assert(I && SUJ->getInstr() && "Unable to packetize null instruction!");

	if(Dependence) {
    //FIXME: check out what Hexagon backend does here...
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                         Private Helper Functions
//===----------------------------------------------------------------------===//

// isDirectJump - Return true if the instruction is a direct jump.
bool rvexVLIWPacketizerList::isDirectJump(const MachineInstr *MI) const {
  //return (MI->getOpcode() == T64::J);
  return (MI->getOpcode() == rvex::NOP);
}

// isrvexSoloInstruction - Return true if TSFlags:4 is 1.
bool rvexVLIWPacketizerList::
isrvexSoloInstruction(const MachineInstr *MI) const {
//  const uint64_t F = MI->getDesc().TSFlags;
  //return ((F >> SoloPos) & SoloMask);
  if (MI->getOpcode() == rvex::NOP)
    return true;
  if (MI->getOpcode() == rvex::CALL)
    return true;
  return false;
}

// isrvexLongInstruction - Return true if TSFlags:5 is 1.
bool rvexVLIWPacketizerList::
isrvexLongInstruction(const MachineInstr *MI) const {
//  const uint64_t F = MI->getDesc().TSFlags;
  //return ((F >> LongPos) & LongMask);
  return false;
}

// rvexTypeOf - Return the appropriate value of rvexType.
rvexType rvexVLIWPacketizerList::
rvexTypeOf(const MachineInstr *MI) const {
  const uint64_t F = MI->getDesc().TSFlags;
	return (rvexType) ((F >> TypePos) & TypeMask);
}

// isrvexCtrInstruction - Return true if rvexType of MI is TypeCtr.
bool rvexVLIWPacketizerList::
isrvexCtrInstruction(const MachineInstr *MI) const {
  //return (rvexTypeOf(MI) == TypeCtr);
  return false;
}

// isrvexMemInstruction - Return true if rvexType of MI is TypeMeS or
// TypeMeL.
bool rvexVLIWPacketizerList::
isrvexMemInstruction(const MachineInstr *MI) const {
//  rvexType type = rvexTypeOf(MI);
  //return (type == TypeMeS || type == TypeMeL);
  return false;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

FunctionPass *llvm::creatervexVLIWPacketizer() {
  return new rvexVLIWPacketizer();
}

