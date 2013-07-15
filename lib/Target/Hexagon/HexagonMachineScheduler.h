//===-- HexagonMachineScheduler.h - Custom Hexagon MI scheduler.      ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Custom Hexagon MI scheduler.
//
//===----------------------------------------------------------------------===//

#ifndef HEXAGONASMPRINTER_H
#define HEXAGONASMPRINTER_H

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ResourcePriorityQueue.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

namespace llvm {
//===----------------------------------------------------------------------===//
// ConvergingVLIWScheduler - Implementation of the standard
// MachineSchedStrategy.
//===----------------------------------------------------------------------===//

class VLIWResourceModel {
  /// ResourcesModel - Represents VLIW state.
  /// Not limited to VLIW targets per say, but assumes
  /// definition of DFA by a target.
  DFAPacketizer *ResourcesModel;

  const TargetSchedModel *SchedModel;

  /// Local packet/bundle model. Purely
  /// internal to the MI schedulre at the time.
  std::vector<SUnit*> Packet;

  /// Total packets created.
  unsigned TotalPackets;

public:
VLIWResourceModel(const TargetMachine &TM, const TargetSchedModel *SM) :
    SchedModel(SM), TotalPackets(0) {
    ResourcesModel = TM.getInstrInfo()->CreateTargetScheduleState(&TM,NULL);

    // This hard requirement could be relaxed,
    // but for now do not let it proceed.
    assert(ResourcesModel && "Unimplemented CreateTargetScheduleState.");

    Packet.resize(SchedModel->getIssueWidth());
    Packet.clear();
    ResourcesModel->clearResources();
  }

  ~VLIWResourceModel() {
    delete ResourcesModel;
  }

  void resetPacketState() {
    Packet.clear();
  }

  void resetDFA() {
    ResourcesModel->clearResources();
  }

  void reset() {
    Packet.clear();
    ResourcesModel->clearResources();
  }

  bool isResourceAvailable(SUnit *SU);
  bool reserveResources(SUnit *SU);
  unsigned getTotalPackets() const { return TotalPackets; }
};

/// Extend the standard ScheduleDAGMI to provide more context and override the
/// top-level schedule() driver.
class VLIWMachineScheduler : public ScheduleDAGMI {
public:
  VLIWMachineScheduler(MachineSchedContext *C, MachineSchedStrategy *S):
    ScheduleDAGMI(C, S) {}

  /// Schedule - This is called back from ScheduleDAGInstrs::Run() when it's
  /// time to do some work.
  virtual void schedule();
  /// Perform platform specific DAG postprocessing.
  void postprocessDAG();
};

/// ConvergingVLIWScheduler shrinks the unscheduled zone using heuristics
/// to balance the schedule.
class ConvergingVLIWScheduler : public MachineSchedStrategy {

  /// Store the state used by ConvergingVLIWScheduler heuristics, required
  ///  for the lifetime of one invocation of pickNode().
  struct SchedCandidate {
    // The best SUnit candidate.
    SUnit *SU;

    // Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    // Best scheduling cost.
    int SCost;

    SchedCandidate(): SU(NULL), SCost(0) {}
  };
  /// Represent the type of SchedCandidate found within a single queue.
  enum CandResult {
    NoCand, NodeOrder, SingleExcess, SingleCritical, SingleMax, MultiPressure,
    BestCost};

  /// Each Scheduling boundary is associated with ready queues. It tracks the
  /// current cycle in whichever direction at has moved, and maintains the state
  /// of "hazards" and other interlocks at the current cycle.
  struct SchedBoundary {
    VLIWMachineScheduler *DAG;
    const TargetSchedModel *SchedModel;

    ReadyQueue Available;
    ReadyQueue Pending;
    bool CheckPending;

    ScheduleHazardRecognizer *HazardRec;
    VLIWResourceModel *ResourceModel;

    unsigned CurrCycle;
    unsigned IssueCount;

    /// MinReadyCycle - Cycle of the soonest available instruction.
    unsigned MinReadyCycle;

    // Remember the greatest min operand latency.
    unsigned MaxMinLatency;

    /// Pending queues extend the ready queues with the same ID and the
    /// PendingFlag set.
    SchedBoundary(unsigned ID, const Twine &Name):
      DAG(0), SchedModel(0), Available(ID, Name+".A"),
      Pending(ID << ConvergingVLIWScheduler::LogMaxQID, Name+".P"),
      CheckPending(false), HazardRec(0), ResourceModel(0),
      CurrCycle(0), IssueCount(0),
      MinReadyCycle(UINT_MAX), MaxMinLatency(0) {}

    ~SchedBoundary() {
      delete ResourceModel;
      delete HazardRec;
    }

    void init(VLIWMachineScheduler *dag, const TargetSchedModel *smodel) {
      DAG = dag;
      SchedModel = smodel;
    }

    bool isTop() const {
      return Available.getID() == ConvergingVLIWScheduler::TopQID;
    }

    bool checkHazard(SUnit *SU);

    void releaseNode(SUnit *SU, unsigned ReadyCycle);

    void bumpCycle();

    void bumpNode(SUnit *SU);

    void releasePending();

    void removeReady(SUnit *SU);

    SUnit *pickOnlyChoice();
  };

  VLIWMachineScheduler *DAG;
  const TargetSchedModel *SchedModel;

  // State of the top and bottom scheduled instruction boundaries.
  SchedBoundary Top;
  SchedBoundary Bot;

public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    TopQID = 1,
    BotQID = 2,
    LogMaxQID = 2
  };

  ConvergingVLIWScheduler():
    DAG(0), SchedModel(0), Top(TopQID, "TopQ"), Bot(BotQID, "BotQ") {}

  virtual void initialize(ScheduleDAGMI *dag);

  virtual SUnit *pickNode(bool &IsTopNode);

  virtual void schedNode(SUnit *SU, bool IsTopNode);

  virtual void releaseTopNode(SUnit *SU);

  virtual void releaseBottomNode(SUnit *SU);

  unsigned ReportPackets() {
    return Top.ResourceModel->getTotalPackets() +
           Bot.ResourceModel->getTotalPackets();
  }

protected:
  SUnit *pickNodeBidrectional(bool &IsTopNode);

  int SchedulingCost(ReadyQueue &Q,
                     SUnit *SU, SchedCandidate &Candidate,
                     RegPressureDelta &Delta, bool verbose);

  CandResult pickNodeFromQueue(ReadyQueue &Q,
                               const RegPressureTracker &RPTracker,
                               SchedCandidate &Candidate);
#ifndef NDEBUG
  void traceCandidate(const char *Label, const ReadyQueue &Q, SUnit *SU,
                      PressureElement P = PressureElement());
#endif
};

} // namespace


#endif
