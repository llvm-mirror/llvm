//===-- rvex.td - Describe the rvex Target Machine ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the rvex target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "rvexRegisterInfo.td"
include "rvexSchedule.td"
include "rvexInstrInfo.td"
include "rvexCallingConv.td"

def rvexInstrInfo : InstrInfo;

// Without this will have error: 'rvex32' is not a recognized processor for 
//  this target (ignoring processor)
//===----------------------------------------------------------------------===//
// rvex Subtarget features                                                    //
//===----------------------------------------------------------------------===//

def FeatureVLIW : SubtargetFeature<"vliw", "IsVLIWEnabled", "true",
                                   "Enable VLIW scheduling">;

//===----------------------------------------------------------------------===//
// rvex processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, SchedMachineModel Model,
           list<SubtargetFeature> Features>
  : ProcessorModel<Name, Model, Features>;

def : Proc<"rvex", rvexModel, []>;
def : Proc<"rvex-vliw", rvexModel, [FeatureVLIW]>;

def rvexAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

// Will generate rvexGenAsmWrite.inc included by rvexInstPrinter.cpp, contents 
//  as follows,
// void rvexInstPrinter::printInstruction(const MCInst *MI, raw_ostream &O) {...}
// const char *rvexInstPrinter::getRegisterName(unsigned RegNo) {...}
def rvex : Target {
// def rvexInstrInfo : InstrInfo as before.
  let InstructionSet = rvexInstrInfo;
  let AssemblyWriters = [rvexAsmWriter];
}
