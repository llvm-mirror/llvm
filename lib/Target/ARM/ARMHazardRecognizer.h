//===-- ARMHazardRecognizer.h - ARM Hazard Recognizers ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines hazard recognizers for scheduling ARM functions.
//
//===----------------------------------------------------------------------===//

#ifndef ARMHAZARDRECOGNIZER_H
#define ARMHAZARDRECOGNIZER_H

#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"

namespace llvm {

class ARMBaseInstrInfo;
class ARMBaseRegisterInfo;
class ARMSubtarget;
class MachineInstr;

/// ARMHazardRecognizer handles special constraints that are not expressed in
/// the scheduling itinerary. This is only used during postRA scheduling. The
/// ARM preRA scheduler uses an unspecialized instance of the
/// ScoreboardHazardRecognizer.
class ARMHazardRecognizer : public ScoreboardHazardRecognizer {
  MachineInstr *LastMI;
  unsigned FpMLxStalls;

public:
  ARMHazardRecognizer(const InstrItineraryData *ItinData,
                      const ScheduleDAG *DAG)
    : ScoreboardHazardRecognizer(ItinData, DAG, "post-RA-sched"),
      LastMI(0) {}

  virtual HazardType getHazardType(SUnit *SU, int Stalls);
  virtual void Reset();
  virtual void EmitInstruction(SUnit *SU);
  virtual void AdvanceCycle();
  virtual void RecedeCycle();
};

} // end namespace llvm

#endif // ARMHAZARDRECOGNIZER_H
