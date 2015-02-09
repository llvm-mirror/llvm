//===- FuzzerMutate.cpp - Mutate a test input -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Mutate a test input.
//===----------------------------------------------------------------------===//

#include "FuzzerInternal.h"

namespace fuzzer {

static char FlipRandomBit(char X) {
  int Bit = rand() % 8;
  char Mask = 1 << Bit;
  char R;
  if (X & (1 << Bit))
    R = X & ~Mask;
  else
    R = X | Mask;
  assert(R != X);
  return R;
}

static char RandCh() {
  if (rand() % 2) return rand();
  const char *Special = "!*'();:@&=+$,/?%#[]123ABCxyz-`~.";
  return Special[rand() % (sizeof(Special) - 1)];
}

// Mutate U in place.
void Mutate(Unit *U, size_t MaxLen) {
  assert(MaxLen > 0);
  assert(U->size() <= MaxLen);
  if (U->empty()) {
    for (size_t i = 0; i < MaxLen; i++)
      U->push_back(RandCh());
    return;
  }
  assert(!U->empty());
  switch (rand() % 3) {
  case 0:
    if (U->size() > 1) {
      U->erase(U->begin() + rand() % U->size());
      break;
    }
    // Fallthrough
  case 1:
    if (U->size() < MaxLen) {
      U->insert(U->begin() + rand() % U->size(), RandCh());
    } else { // At MaxLen.
      uint8_t Ch = RandCh();
      size_t Idx = rand() % U->size();
      (*U)[Idx] = Ch;
    }
    break;
  default:
    {
      size_t Idx = rand() % U->size();
      (*U)[Idx] = FlipRandomBit((*U)[Idx]);
    }
    break;
  }
  assert(!U->empty());
}

}  // namespace fuzzer
