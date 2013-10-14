//===-- rvexHazardRecognizers.h - PowerPC Hazard Recognizers -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines hazard recognizers for scheduling on rvex processors.
//
//===----------------------------------------------------------------------===//

#ifndef rvexHAZRECS_H
#define rvexHAZRECS_H

#include "rvexInstrInfo.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"

namespace llvm {

/// rvexScoreboardHazardRecognizer - This class implements a scoreboard-based
/// hazard recognizer for generic rvex processors.
class rvexHazardRecognizer : public ScheduleHazardRecognizer {
  const ScheduleDAG *DAG;
  // Scoreboard to track function unit usage. Scoreboard[0] is a
  // mask of the FUs in use in the cycle currently being
  // schedule. Scoreboard[1] is a mask for the next cycle. The
  // Scoreboard is used as a circular buffer with the current cycle
  // indicated by Head.
  //
  // Scoreboard always counts cycles in forward execution order. If used by a
  // bottom-up scheduler, then the scoreboard cycles are the inverse of the
  // scheduler's cycles.
  class Scoreboard {
    unsigned *Data;

    // The maximum number of cycles monitored by the Scoreboard. This
    // value is determined based on the target itineraries to ensure
    // that all hazards can be tracked.
    size_t Depth;
    // Indices into the Scoreboard that represent the current cycle.
    size_t Head;
  public:
    Scoreboard():Data(NULL), Depth(0), Head(0) { }
    ~Scoreboard() {
      delete[] Data;
    }

    size_t getDepth() const { return Depth; }
    unsigned& operator[](size_t idx) const {
      // Depth is expected to be a power-of-2.
      assert(Depth && !(Depth & (Depth - 1)) &&
             "Scoreboard was not initialized properly!");

      return Data[(Head + idx) & (Depth-1)];
    }

    void reset(size_t d = 1) {
      if (Data == NULL) {
        Depth = d;
        Data = new unsigned[Depth];
      }

      memset(Data, 0, Depth * sizeof(Data[0]));
      Head = 0;
    }

    void advance() {
      Head = (Head + 1) & (Depth-1);
    }

    void recede() {
      Head = (Head - 1) & (Depth-1);
    }

    // Print the scoreboard.
    void dump() const;
  };  
public:


  // Itinerary data for the target.
  const InstrItineraryData *ItinData;

 

  /// IssueWidth - Max issue per cycle. 0=Unknown.
  unsigned IssueWidth;

  /// IssueCount - Count instructions issued in this cycle.
  unsigned IssueCount;


  Scoreboard ReservedScoreboard;
  Scoreboard RequiredScoreboard;

public:


  // rvexScoreboardHazardRecognizer(const InstrItineraryData *ItinData,
  //                        const ScheduleDAG *DAG_) :
  //   ScoreboardHazardRecognizer(ItinData, DAG_), DAG(DAG_) {}


  rvexHazardRecognizer(const InstrItineraryData *II,
                           const ScheduleDAG *SchedDAG,
                           const char *ParentDebugType);
  virtual HazardType getHazardType(SUnit *SU, int Stalls);
  virtual bool atIssueLimit() const;
  virtual void Reset();
  virtual void EmitInstruction(SUnit *SU);
  virtual void AdvanceCycle();
  //virtual void RecedeCycle();
};
}

#endif

