//===-- rvexSchedule.td - rvex Scheduling Definitions ------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Functional units across rvex chips sets. Based on GCC/rvex backend files.
//===----------------------------------------------------------------------===//
def P0 : FuncUnit;
def P1 : FuncUnit;
def P2 : FuncUnit;
def P3 : FuncUnit;

//===----------------------------------------------------------------------===//
// Instruction Itinerary classes used for rvex
//===----------------------------------------------------------------------===//
def IIAlu              : InstrItinClass;
def IILoad             : InstrItinClass;
def IIStore            : InstrItinClass;
def IIBranch           : InstrItinClass;
def IIHiLo             : InstrItinClass;
def IIImul             : InstrItinClass;
def IIIdiv             : InstrItinClass;

def IIPseudo           : InstrItinClass;

//===----------------------------------------------------------------------===//
// rvex Generic instruction itineraries.
//===----------------------------------------------------------------------===//
// http://llvm.org/docs/doxygen/html/structllvm_1_1InstrStage.html

def rvexGenericItineraries : ProcessorItineraries<[P0, P1, P2], [], [
  InstrItinData<IIAlu              , [InstrStage<1,  [P0, P1]>]>,
  InstrItinData<IILoad             , [InstrStage<1,  [P0, P1]>]>,
  InstrItinData<IIStore            , [InstrStage<1,  [P0]>]>,
  InstrItinData<IIBranch           , [InstrStage<1,  [P0]>]>,
  InstrItinData<IIHiLo             , [InstrStage<1,  [P1]>]>,
  InstrItinData<IIImul             , [InstrStage<1,  [P1]>]>,
  InstrItinData<IIIdiv             , [InstrStage<1,  [P2]>]>,
  InstrItinData<IIPseudo           , [InstrStage<1,  [P0, P1, P2]>]>
]>;

/*
def rvexGenericItineraries : ProcessorItineraries<[P0, P1, P2], [], [
  InstrItinData<IIAlu              , [InstrStage<1,  [P0, P1]>]>,
  InstrItinData<IILoad             , [InstrStage<1,  [P0, P1]>]>,
  InstrItinData<IIStore            , [InstrStage<1,  [P0]>]>,
  InstrItinData<IIBranch           , [InstrStage<1,  [P0]>]>,
  InstrItinData<IIHiLo             , [InstrStage<1,  [P1]>]>,
  InstrItinData<IIImul             , [InstrStage<1,  [P1]>]>,
  InstrItinData<IIIdiv             , [InstrStage<1,  [P2]>]>,
  InstrItinData<IIPseudo           , [InstrStage<1,  [P0, P1, P2]>]>
]>;*/

// Processor itineraries
def rvexModel : SchedMachineModel {
  // Max issue per cycle == bundle width.
  let IssueWidth = 3;
  let Itineraries = rvexGenericItineraries;
}
