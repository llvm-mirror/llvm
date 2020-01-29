//===- EVMStackStatus --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_EVMSTACKSTATUS_H
#define LLVM_LIB_TARGET_EVM_EVMSTACKSTATUS_H

#include "llvm/ADT/DenseMap.h"
#include "EVMTargetMachine.h"
#include "EVMStackStatus.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/Support/Debug.h"

#include <vector>

#define DEBUG_TYPE "evm_stackstatus"

using namespace llvm;
namespace {

class EVMStackStatus {
public:
  EVMStackStatus() {}

  void swap(unsigned depth);
  void dup(unsigned depth);
  void push(unsigned reg);
  void pop();

  unsigned get(unsigned depth) const;
  void dump() const;

  unsigned getSizeOfXRegion() const {
    return sizeOfXRegion;
  }
  unsigned getSizeOfLRegion() const {
    return getStackDepth() - sizeOfXRegion;
  }

  bool empty() const {
    return stackElements.empty();
  }

  void clear() {
    stackElements.clear();
    sizeOfXRegion = 0;
  }

  const std::vector<unsigned> &getStackElements() const {
    return stackElements;
  }

  unsigned findRegDepth(unsigned reg) const;

  // Stack depth = size of X + size of L
  unsigned getStackDepth() const;

  void instantiateXRegionStack(std::vector<unsigned> &stack) {
    assert(getStackDepth() == 0);
    for (auto element : stack) {
      stackElements.push_back(element);
    }
    sizeOfXRegion = stack.size();
  }

private:
  // stack arrangements.
  std::vector<unsigned> stackElements;

  unsigned sizeOfXRegion;
};

unsigned EVMStackStatus::getStackDepth() const {
  return stackElements.size();
}

unsigned EVMStackStatus::get(unsigned depth) const {
  return stackElements.rbegin()[depth];
}

void EVMStackStatus::swap(unsigned depth) {
    assert(depth != 0);
    assert(stackElements.size() >= 2);
    LLVM_DEBUG({
      unsigned first = stackElements.rbegin()[0];
      unsigned second = stackElements.rbegin()[depth];
      unsigned fst_idx = Register::virtReg2Index(first);
      unsigned snd_idx = Register::virtReg2Index(second);
      dbgs() << "  SWAP" << depth << ": Swapping %" << fst_idx << " and %"
             << snd_idx << "\n";
    });
    std::iter_swap(stackElements.rbegin(), stackElements.rbegin() + depth);
}

void EVMStackStatus::dup(unsigned depth) {
  unsigned elem = *(stackElements.rbegin() + depth);

  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(elem);
    dbgs() << "  Duplicating " << idx << " at depth " << depth << "\n";
  });

  stackElements.push_back(elem);
}

void EVMStackStatus::pop() {
  LLVM_DEBUG({
    unsigned reg = stackElements.back();
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "  Popping %" << idx << " from stack.\n";
  });
  stackElements.pop_back();
}

void EVMStackStatus::push(unsigned reg) {
  LLVM_DEBUG({
    unsigned idx = Register::virtReg2Index(reg);
    dbgs() << "  Pushing %" << idx << " to top of stack.\n";
  });
  stackElements.push_back(reg);
}


void EVMStackStatus::dump() const {
  LLVM_DEBUG({
    dbgs() << "  Stack :  xRegion_size = " << getSizeOfXRegion() << "\n";
    unsigned counter = 0;
    for (auto i = stackElements.rbegin(), e = stackElements.rend(); i != e; ++i) {
      unsigned idx = Register::virtReg2Index(*i);
      dbgs() << "(" << counter << ", %" << idx << "), ";
      counter ++;
    }
    dbgs() << "\n";
  });
}

unsigned EVMStackStatus::findRegDepth(unsigned reg) const {
  unsigned curHeight = getStackDepth();

  for (unsigned d = 0; d < curHeight; ++d) {
    unsigned stackReg = get(d);
    if (stackReg == reg) {
      LLVM_DEBUG({
        dbgs() << "  Found %" << Register::virtReg2Index(reg)
               << " at depth: " << d << "\n";
      });
      return d;
    }
  }
  llvm_unreachable("Cannot find register on stack");
}

};


#endif