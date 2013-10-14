//===----- ScoreboardHazardRecognizer.cpp - Scheduler Support -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScoreboardHazardRecognizer class, which
// encapsultes hazard-avoidance heuristics for scheduling, based on the
// scheduling itineraries specified for the target.
//
//===----------------------------------------------------------------------===//

#include "rvexHazardRecognizers.h"
#include "rvex.h"
#include "rvexInstrInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

void rvexScoreboardHazardRecognizer::Reset() {
  IssueCount = 0;
  RequiredScoreboard.reset();
  ReservedScoreboard.reset();
}

bool rvexScoreboardHazardRecognizer::atIssueLimit() const {
  if (IssueWidth == 0)
    return false;

  return IssueCount == IssueWidth;
}

ScheduleHazardRecognizer::HazardType
rvexScoreboardHazardRecognizer::getHazardType(SUnit *SU, int Stalls) {
  if (!ItinData || ItinData->isEmpty())
    return NoHazard;

  // Note that stalls will be negative for bottom-up scheduling.
  int cycle = Stalls;

  // Use the itinerary for the underlying instruction to check for
  // free FU's in the scoreboard at the appropriate future cycles.

  const MCInstrDesc *MCID = DAG->getInstrDesc(SU);
  if (MCID == NULL) {
    // Don't check hazards for non-machineinstr Nodes.
    return NoHazard;
  }
  unsigned idx = MCID->getSchedClass();
  for (const InstrStage *IS = ItinData->beginStage(idx),
         *E = ItinData->endStage(idx); IS != E; ++IS) {
    // We must find one of the stage's units free for every cycle the
    // stage is occupied. FIXME it would be more accurate to find the
    // same unit free in all the cycles.
    for (unsigned int i = 0; i < IS->getCycles(); ++i) {
      int StageCycle = cycle + (int)i;
      if (StageCycle < 0)
        continue;

      if (StageCycle >= (int)RequiredScoreboard.getDepth()) {
        assert((StageCycle - Stalls) < (int)RequiredScoreboard.getDepth() &&
               "Scoreboard depth exceeded!");
        // This stage was stalled beyond pipeline depth, so cannot conflict.
        break;
      }

      unsigned freeUnits = IS->getUnits();
      switch (IS->getReservationKind()) {
      case InstrStage::Required:
        // Required FUs conflict with both reserved and required ones
        freeUnits &= ~ReservedScoreboard[StageCycle];
        // FALLTHROUGH
      case InstrStage::Reserved:
        // Reserved FUs can conflict only with required ones.
        freeUnits &= ~RequiredScoreboard[StageCycle];
        break;
      }

      if (!freeUnits) {
        DEBUG(dbgs() << "*** Hazard in cycle +" << StageCycle << ", ");
        DEBUG(dbgs() << "SU(" << SU->NodeNum << "): ");
        DEBUG(DAG->dumpNode(SU));
        return NoopHazard;
      }
    }

    // Advance the cycle to the next stage.
    cycle += IS->getNextCycles();
  }

  return NoHazard;
}

void rvexScoreboardHazardRecognizer::EmitInstruction(SUnit *SU) {
  if (!ItinData || ItinData->isEmpty())
    return;

  // Use the itinerary for the underlying instruction to reserve FU's
  // in the scoreboard at the appropriate future cycles.
  const MCInstrDesc *MCID = DAG->getInstrDesc(SU);
  assert(MCID && "The scheduler must filter non-machineinstrs");
  if (DAG->TII->isZeroCost(MCID->Opcode))
    return;

  ++IssueCount;

  unsigned cycle = 0;

  unsigned idx = MCID->getSchedClass();
  for (const InstrStage *IS = ItinData->beginStage(idx),
         *E = ItinData->endStage(idx); IS != E; ++IS) {
    // We must reserve one of the stage's units for every cycle the
    // stage is occupied. FIXME it would be more accurate to reserve
    // the same unit free in all the cycles.
    for (unsigned int i = 0; i < IS->getCycles(); ++i) {
      assert(((cycle + i) < RequiredScoreboard.getDepth()) &&
             "Scoreboard depth exceeded!");

      unsigned freeUnits = IS->getUnits();
      switch (IS->getReservationKind()) {
      case InstrStage::Required:
        // Required FUs conflict with both reserved and required ones
        freeUnits &= ~ReservedScoreboard[cycle + i];
        // FALLTHROUGH
      case InstrStage::Reserved:
        // Reserved FUs can conflict only with required ones.
        freeUnits &= ~RequiredScoreboard[cycle + i];
        break;
      }

      // reduce to a single unit
      unsigned freeUnit = 0;
      do {
        freeUnit = freeUnits;
        freeUnits = freeUnit & (freeUnit - 1);
      } while (freeUnits);

      if (IS->getReservationKind() == InstrStage::Required)
        RequiredScoreboard[cycle + i] |= freeUnit;
      else
        ReservedScoreboard[cycle + i] |= freeUnit;
    }

    // Advance the cycle to the next stage.
    cycle += IS->getNextCycles();
  }

  //DEBUG(ReservedScoreboard.dump());
  //DEBUG(RequiredScoreboard.dump());
}

void rvexScoreboardHazardRecognizer::AdvanceCycle() {
  IssueCount = 0;
  ReservedScoreboard[0] = 0; ReservedScoreboard.advance();
  RequiredScoreboard[0] = 0; RequiredScoreboard.advance();
}

