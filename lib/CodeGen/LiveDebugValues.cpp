//===------ LiveDebugValues.cpp - Tracking Debug Value MIs ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This pass implements a data flow analysis that propagates debug location
/// information by inserting additional DBG_VALUE instructions into the machine
/// instruction stream. The pass internally builds debug location liveness
/// ranges to determine the points where additional DBG_VALUEs need to be
/// inserted.
///
/// This is a separate pass from DbgValueHistoryCalculator to facilitate
/// testing and improve modularity.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <deque>
#include <list>

using namespace llvm;

#define DEBUG_TYPE "live-debug-values"

STATISTIC(NumInserted, "Number of DBG_VALUE instructions inserted");

namespace {

class LiveDebugValues : public MachineFunctionPass {

private:
  const TargetRegisterInfo *TRI;
  const TargetInstrInfo *TII;

  typedef std::pair<const DILocalVariable *, const DILocation *>
      InlinedVariable;

  /// A potentially inlined instance of a variable.
  struct DebugVariable {
    const DILocalVariable *Var;
    const DILocation *InlinedAt;

    DebugVariable(const DILocalVariable *_var, const DILocation *_inlinedAt)
        : Var(_var), InlinedAt(_inlinedAt) {}

    bool operator==(const DebugVariable &DV) const {
      return (Var == DV.Var) && (InlinedAt == DV.InlinedAt);
    }
  };

  /// Member variables and functions for Range Extension across basic blocks.
  struct VarLoc {
    DebugVariable Var;
    const MachineInstr *MI; // MachineInstr should be a DBG_VALUE instr.

    VarLoc(DebugVariable _var, const MachineInstr *_mi) : Var(_var), MI(_mi) {}

    bool operator==(const VarLoc &V) const;
  };

  typedef std::list<VarLoc> VarLocList;
  typedef SmallDenseMap<const MachineBasicBlock *, VarLocList> VarLocInMBB;

  bool OLChanged; // OutgoingLocs got changed for this bb.
  bool MBBJoined; // The MBB was joined.

  void transferDebugValue(MachineInstr &MI, VarLocList &OpenRanges);
  void transferRegisterDef(MachineInstr &MI, VarLocList &OpenRanges);
  void transferTerminatorInst(MachineInstr &MI, VarLocList &OpenRanges,
                              VarLocInMBB &OutLocs);
  void transfer(MachineInstr &MI, VarLocList &OpenRanges, VarLocInMBB &OutLocs);

  void join(MachineBasicBlock &MBB, VarLocInMBB &OutLocs, VarLocInMBB &InLocs);

  bool ExtendRanges(MachineFunction &MF);

public:
  static char ID;

  /// Default construct and initialize the pass.
  LiveDebugValues();

  /// Tell the pass manager which passes we depend on and what
  /// information we preserve.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Print to ostream with a message.
  void printVarLocInMBB(const VarLocInMBB &V, const char *msg,
                        raw_ostream &Out) const;

  /// Calculate the liveness information for the given machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

char LiveDebugValues::ID = 0;
char &llvm::LiveDebugValuesID = LiveDebugValues::ID;
INITIALIZE_PASS(LiveDebugValues, "livedebugvalues", "Live DEBUG_VALUE analysis",
                false, false)

/// Default construct and initialize the pass.
LiveDebugValues::LiveDebugValues() : MachineFunctionPass(ID) {
  initializeLiveDebugValuesPass(*PassRegistry::getPassRegistry());
}

/// Tell the pass manager which passes we depend on and what information we
/// preserve.
void LiveDebugValues::getAnalysisUsage(AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

// \brief If @MI is a DBG_VALUE with debug value described by a defined
// register, returns the number of this register. In the other case, returns 0.
static unsigned isDescribedByReg(const MachineInstr &MI) {
  assert(MI.isDebugValue());
  assert(MI.getNumOperands() == 4);
  // If location of variable is described using a register (directly or
  // indirecltly), this register is always a first operand.
  return MI.getOperand(0).isReg() ? MI.getOperand(0).getReg() : 0;
}

// \brief This function takes two DBG_VALUE instructions and returns true
// if their offsets are equal; otherwise returns false.
static bool areOffsetsEqual(const MachineInstr &MI1, const MachineInstr &MI2) {
  assert(MI1.isDebugValue());
  assert(MI1.getNumOperands() == 4);

  assert(MI2.isDebugValue());
  assert(MI2.getNumOperands() == 4);

  if (!MI1.isIndirectDebugValue() && !MI2.isIndirectDebugValue())
    return true;

  // Check if both MIs are indirect and they are equal.
  if (MI1.isIndirectDebugValue() && MI2.isIndirectDebugValue())
    return MI1.getOperand(1).getImm() == MI2.getOperand(1).getImm();

  return false;
}

//===----------------------------------------------------------------------===//
//            Debug Range Extension Implementation
//===----------------------------------------------------------------------===//

void LiveDebugValues::printVarLocInMBB(const VarLocInMBB &V, const char *msg,
                                       raw_ostream &Out) const {
  Out << "Printing " << msg << ":\n";
  for (const auto &L : V) {
    Out << "MBB: " << L.first->getName() << ":\n";
    for (const auto &VLL : L.second) {
      Out << " Var: " << VLL.Var.Var->getName();
      Out << " MI: ";
      (*VLL.MI).dump();
      Out << "\n";
    }
  }
  Out << "\n";
}

bool LiveDebugValues::VarLoc::operator==(const VarLoc &V) const {
  return (Var == V.Var) && (isDescribedByReg(*MI) == isDescribedByReg(*V.MI)) &&
         (areOffsetsEqual(*MI, *V.MI));
}

/// End all previous ranges related to @MI and start a new range from @MI
/// if it is a DBG_VALUE instr.
void LiveDebugValues::transferDebugValue(MachineInstr &MI,
                                         VarLocList &OpenRanges) {
  if (!MI.isDebugValue())
    return;
  const DILocalVariable *RawVar = MI.getDebugVariable();
  assert(RawVar->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
         "Expected inlined-at fields to agree");
  DebugVariable Var(RawVar, MI.getDebugLoc()->getInlinedAt());

  // End all previous ranges of Var.
  OpenRanges.erase(
      std::remove_if(OpenRanges.begin(), OpenRanges.end(),
                     [&](const VarLoc &V) { return (Var == V.Var); }),
      OpenRanges.end());

  // Add Var to OpenRanges from this DBG_VALUE.
  // TODO: Currently handles DBG_VALUE which has only reg as location.
  if (isDescribedByReg(MI)) {
    VarLoc V(Var, &MI);
    OpenRanges.push_back(std::move(V));
  }
}

/// A definition of a register may mark the end of a range.
void LiveDebugValues::transferRegisterDef(MachineInstr &MI,
                                          VarLocList &OpenRanges) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!(MO.isReg() && MO.isDef() && MO.getReg() &&
          TRI->isPhysicalRegister(MO.getReg())))
      continue;
    // Remove ranges of all aliased registers.
    for (MCRegAliasIterator RAI(MO.getReg(), TRI, true); RAI.isValid(); ++RAI)
      OpenRanges.erase(std::remove_if(OpenRanges.begin(), OpenRanges.end(),
                                      [&](const VarLoc &V) {
                                        return (*RAI ==
                                                isDescribedByReg(*V.MI));
                                      }),
                       OpenRanges.end());
  }
}

/// Terminate all open ranges at the end of the current basic block.
void LiveDebugValues::transferTerminatorInst(MachineInstr &MI,
                                             VarLocList &OpenRanges,
                                             VarLocInMBB &OutLocs) {
  const MachineBasicBlock *CurMBB = MI.getParent();
  if (!(MI.isTerminator() || (&MI == &CurMBB->instr_back())))
    return;

  if (OpenRanges.empty())
    return;

  if (OutLocs.find(CurMBB) == OutLocs.end()) {
    // Create space for new Outgoing locs entries.
    VarLocList VLL;
    OutLocs.insert(std::make_pair(CurMBB, std::move(VLL)));
  }
  auto OL = OutLocs.find(CurMBB);
  assert(OL != OutLocs.end());
  VarLocList &VLL = OL->second;

  for (auto OR : OpenRanges) {
    // Copy OpenRanges to OutLocs, if not already present.
    assert(OR.MI->isDebugValue());
    DEBUG(dbgs() << "Add to OutLocs: "; OR.MI->dump(););
    if (std::find_if(VLL.begin(), VLL.end(),
                     [&](const VarLoc &V) { return (OR == V); }) == VLL.end()) {
      VLL.push_back(std::move(OR));
      OLChanged = true;
    }
  }
  OpenRanges.clear();
}

/// This routine creates OpenRanges and OutLocs.
void LiveDebugValues::transfer(MachineInstr &MI, VarLocList &OpenRanges,
                               VarLocInMBB &OutLocs) {
  transferDebugValue(MI, OpenRanges);
  transferRegisterDef(MI, OpenRanges);
  transferTerminatorInst(MI, OpenRanges, OutLocs);
}

/// This routine joins the analysis results of all incoming edges in @MBB by
/// inserting a new DBG_VALUE instruction at the start of the @MBB - if the same
/// source variable in all the predecessors of @MBB reside in the same location.
void LiveDebugValues::join(MachineBasicBlock &MBB, VarLocInMBB &OutLocs,
                           VarLocInMBB &InLocs) {
  DEBUG(dbgs() << "join MBB: " << MBB.getName() << "\n");

  MBBJoined = false;

  VarLocList InLocsT; // Temporary incoming locations.

  // For all predecessors of this MBB, find the set of VarLocs that can be
  // joined.
  for (auto p : MBB.predecessors()) {
    auto OL = OutLocs.find(p);
    // Join is null in case of empty OutLocs from any of the pred.
    if (OL == OutLocs.end())
      return;

    // Just copy over the Out locs to incoming locs for the first predecessor.
    if (p == *MBB.pred_begin()) {
      InLocsT = OL->second;
      continue;
    }

    // Join with this predecessor.
    VarLocList &VLL = OL->second;
    InLocsT.erase(std::remove_if(InLocsT.begin(), InLocsT.end(),
                                 [&](VarLoc &ILT) {
                                   return (std::find_if(VLL.begin(), VLL.end(),
                                                        [&](const VarLoc &V) {
                                                          return (ILT == V);
                                                        }) == VLL.end());
                                 }),
                  InLocsT.end());
  }

  if (InLocsT.empty())
    return;

  if (InLocs.find(&MBB) == InLocs.end()) {
    // Create space for new Incoming locs entries.
    VarLocList VLL;
    InLocs.insert(std::make_pair(&MBB, std::move(VLL)));
  }
  auto IL = InLocs.find(&MBB);
  assert(IL != InLocs.end());
  VarLocList &ILL = IL->second;

  // Insert DBG_VALUE instructions, if not already inserted.
  for (auto ILT : InLocsT) {
    if (std::find_if(ILL.begin(), ILL.end(), [&](const VarLoc &I) {
          return (ILT == I);
        }) == ILL.end()) {
      // This VarLoc is not found in InLocs i.e. it is not yet inserted. So, a
      // new range is started for the var from the mbb's beginning by inserting
      // a new DBG_VALUE. transfer() will end this range however appropriate.
      const MachineInstr *DMI = ILT.MI;
      MachineInstr *MI =
          BuildMI(MBB, MBB.instr_begin(), DMI->getDebugLoc(), DMI->getDesc(),
                  DMI->isIndirectDebugValue(), DMI->getOperand(0).getReg(), 0,
                  DMI->getDebugVariable(), DMI->getDebugExpression());
      if (DMI->isIndirectDebugValue())
        MI->getOperand(1).setImm(DMI->getOperand(1).getImm());
      DEBUG(dbgs() << "Inserted: "; MI->dump(););
      ++NumInserted;
      MBBJoined = true; // rerun transfer().

      VarLoc V(ILT.Var, MI);
      ILL.push_back(std::move(V));
    }
  }
}

/// Calculate the liveness information for the given machine function and
/// extend ranges across basic blocks.
bool LiveDebugValues::ExtendRanges(MachineFunction &MF) {

  DEBUG(dbgs() << "\nDebug Range Extension\n");

  bool Changed = false;
  OLChanged = MBBJoined = false;

  VarLocList OpenRanges; // Ranges that are open until end of bb.
  VarLocInMBB OutLocs;   // Ranges that exist beyond bb.
  VarLocInMBB InLocs;    // Ranges that are incoming after joining.

  std::deque<MachineBasicBlock *> BBWorklist;

  // Initialize every mbb with OutLocs.
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      transfer(MI, OpenRanges, OutLocs);
  DEBUG(printVarLocInMBB(OutLocs, "OutLocs after initialization", dbgs()));

  // Construct a worklist of MBBs.
  for (auto &MBB : MF)
    BBWorklist.push_back(&MBB);

  // Perform join() and transfer() using the worklist until the ranges converge
  // Ranges have converged when the worklist is empty.
  while (!BBWorklist.empty()) {
    MachineBasicBlock *MBB = BBWorklist.front();
    BBWorklist.pop_front();

    join(*MBB, OutLocs, InLocs);

    if (MBBJoined) {
      Changed = true;
      for (auto &MI : *MBB)
        transfer(MI, OpenRanges, OutLocs);
      DEBUG(printVarLocInMBB(OutLocs, "OutLocs after propagating", dbgs()));
      DEBUG(printVarLocInMBB(InLocs, "InLocs after propagating", dbgs()));

      if (OLChanged) {
        OLChanged = false;
        for (auto s : MBB->successors())
          if (std::find(BBWorklist.begin(), BBWorklist.end(), s) ==
              BBWorklist.end()) // add if not already present.
            BBWorklist.push_back(s);
      }
    }
  }
  DEBUG(printVarLocInMBB(OutLocs, "Final OutLocs", dbgs()));
  DEBUG(printVarLocInMBB(InLocs, "Final InLocs", dbgs()));
  return Changed;
}

bool LiveDebugValues::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();

  bool Changed = false;

  Changed |= ExtendRanges(MF);

  return Changed;
}
