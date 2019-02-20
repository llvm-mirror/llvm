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

def FeatureEIP211
    : SubtargetFeature<"EIP211", "HasEIP211", "true", "EIP211 support">;

def HasEIP211 : Predicate<"Subtarget->HasEIP211()">,
                          AssemblerPredicate<"FeatureEIP211">;

def FeatureEIP615
    : SubtargetFeature<"EIP615", "HasEIP615", "true", "EIP615 support">;

def HasEIP615 : Predicate<"Subtarget->HasEIP615()">,
                          AssemblerPredicate<"FeatureEIP615">;

//===----------------------------------------------------------------------===//
// Registers, instruction descriptions.
//===----------------------------------------------------------------------===//

include "EVMRegisterInfo.td"
include "EVMInstrInfo.td"

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
