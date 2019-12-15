//===-- EVMUtilities.cpp - EVM Utility Functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for EVM.
///
//===----------------------------------------------------------------------===//

#include "EVMUtils.h"
#include "llvm/IR/DerivedTypes.h"

#include <sstream>

using namespace llvm;

unsigned EVM::getEncodedSize(Function &F) {
	// for now we just use a place holder
	return 32;
}

#define COMMENT_FLAG_BITSHIFT 4
#define VALUE_FLAG_BITWIDTH   4
#define VALUE_FLAG_BITSHIFT  16

uint32_t EVM::BuildCommentFlags(AsmComments commentFlag, uint16_t value) {
  return MachineInstr::TAsmComments | (commentFlag << COMMENT_FLAG_BITSHIFT) |
         (value << VALUE_FLAG_BITSHIFT);
}

void EVM::ParseCommentFlags(uint32_t input, AsmComments &commentFlag,
                            uint16_t &value) {
  assert(input & MachineInstr::TAsmComments);
  value = (input >> VALUE_FLAG_BITSHIFT);
  commentFlag = (AsmComments)((input >> COMMENT_FLAG_BITSHIFT) &
                              (1 << (VALUE_FLAG_BITWIDTH - 1)));
}
