// Simple test for a fuzzer.
// The fuzzer must find the string "Hi!" preceded by a magic value.
// Uses UserSuppliedFuzzer which ensures that the magic is present.
#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <iostream>

#include "FuzzerInterface.h"

static const uint64_t kMagic = 8860221463604ULL;

class MyFuzzer : public fuzzer::UserSuppliedFuzzer {
 public:
  MyFuzzer(fuzzer::FuzzerRandomBase *Rand)
      : fuzzer::UserSuppliedFuzzer(Rand) {}
  int TargetFunction(const uint8_t *Data, size_t Size) {
    if (Size <= 10) return 0;
    if (memcmp(Data, &kMagic, sizeof(kMagic))) return 0;
    // It's hard to get here w/o advanced fuzzing techniques (e.g. cmp tracing).
    // So, we simply 'fix' the data in the custom mutator.
    if (Data[8] == 'H') {
      if (Data[9] == 'i') {
        if (Data[10] == '!') {
          std::cout << "BINGO; Found the target, exiting\n";
          exit(1);
        }
      }
    }
    return 0;
  }
  // Custom mutator.
  virtual size_t Mutate(uint8_t *Data, size_t Size, size_t MaxSize) {
    assert(MaxSize > sizeof(kMagic));
    if (Size < sizeof(kMagic))
      Size = sizeof(kMagic);
    // "Fix" the data, then mutate.
    memcpy(Data, &kMagic, std::min(MaxSize, sizeof(kMagic)));
    return fuzzer::UserSuppliedFuzzer::Mutate(
        Data + sizeof(kMagic), Size - sizeof(kMagic), MaxSize - sizeof(kMagic));
  }
  // No need to redefine CrossOver() here.
};

int main(int argc, char **argv) {
  fuzzer::FuzzerRandomLibc Rand(0);
  MyFuzzer F(&Rand);
  fuzzer::FuzzerDriver(argc, argv, F);
}
