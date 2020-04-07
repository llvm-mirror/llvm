//===-- EVM.td - Describe the EVM Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// EVM subtarget features and instruction predicates.
//===----------------------------------------------------------------------===//

/*
def EIP211 : SubtargetFeature<"eip211", "HasEIP211", "true",
                              "Ethereum Improvement Proposal 211">;

def EIP615 : SubtargetFeature<"eip615", "HasEIP615", "true",
                              "Ethereum Improvement Proposal 615">;

def HasEIP211 : Predicate<"Subtarget->hasEIP211()">,
                          AssemblerPredicate<"FeatureEIP211">;

def HasEIP615 : Predicate<"Subtarget->hasEIP615()">,
                          AssemblerPredicate<"FeatureEIP615">;
*/

def FeatureSubroutine : SubtargetFeature<"subroutine",
                                         "HasSubroutine",
                                         "true",
                                         "EVM Subroutine support">;

def HasSubroutine : Predicate<"Subtarget->hasSubroutine()">,
                    AssemblerPredicate<"FeatureSubroutine">;

class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", []>;
def : Proc<"EVM15", [FeatureSubroutine]>;

//===----------------------------------------------------------------------===//
// Registers, instruction descriptions.
//===----------------------------------------------------------------------===//

include "EVMRegisterInfo.td"
include "EVMInstrInfo.td"
include "EVMCallingConv.td"

//===----------------------------------------------------------------------===//
// EVM processors supported.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;

//===----------------------------------------------------------------------===//
// Define the EVM target.
//===----------------------------------------------------------------------===//

def EVMInstrInfo : InstrInfo {
  let guessInstructionProperties = 0;
}

def EVMAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
  let AllowDuplicateRegisterNames = 1;
}

def EVMAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def EVM : Target {
  let InstructionSet = EVMInstrInfo;
  let AssemblyParsers = [EVMAsmParser];
  let AssemblyWriters = [EVMAsmWriter];
  let AllowRegisterRenaming = 1;
}
