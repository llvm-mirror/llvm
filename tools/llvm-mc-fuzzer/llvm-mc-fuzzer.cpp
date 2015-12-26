//===--- llvm-mc-fuzzer.cpp - Fuzzer for the MC layer ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Disassembler.h"
#include "llvm-c/Target.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "FuzzerInterface.h"

using namespace llvm;

const unsigned AssemblyTextBufSize = 80;

enum ActionType {
  AC_Assemble,
  AC_Disassemble
};

static cl::opt<ActionType>
Action(cl::desc("Action to perform:"),
       cl::init(AC_Assemble),
       cl::values(clEnumValN(AC_Assemble, "assemble",
                             "Assemble a .s file (default)"),
                  clEnumValN(AC_Disassemble, "disassemble",
                             "Disassemble strings of hex bytes"),
                  clEnumValEnd));

static cl::opt<std::string>
    TripleName("triple", cl::desc("Target triple to assemble for, "
                                  "see -version for available targets"));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::init(""));

// This is useful for variable-length instruction sets.
static cl::opt<unsigned> InsnLimit(
    "insn-limit",
    cl::desc("Limit the number of instructions to process (0 for no limit)"),
    cl::value_desc("count"), cl::init(0));

static cl::list<std::string>
    MAttrs("mattr", cl::CommaSeparated,
           cl::desc("Target specific attributes (-mattr=help for details)"),
           cl::value_desc("a1,+a2,-a3,..."));
// The feature string derived from -mattr's values.
std::string FeaturesStr;

static cl::list<std::string>
    FuzzerArgv("fuzzer-args", cl::Positional,
               cl::desc("Options to pass to the fuzzer"), cl::ZeroOrMore,
               cl::PositionalEatsArgs);

int DisassembleOneInput(const uint8_t *Data, size_t Size) {
  char AssemblyText[AssemblyTextBufSize];

  std::vector<uint8_t> DataCopy(Data, Data + Size);

  LLVMDisasmContextRef Ctx = LLVMCreateDisasmCPUFeatures(
      TripleName.c_str(), MCPU.c_str(), FeaturesStr.c_str(), nullptr, 0,
      nullptr, nullptr);
  assert(Ctx);
  uint8_t *p = DataCopy.data();
  unsigned Consumed;
  unsigned InstructionsProcessed = 0;
  do {
    Consumed = LLVMDisasmInstruction(Ctx, p, Size, 0, AssemblyText,
                                     AssemblyTextBufSize);
    Size -= Consumed;
    p += Consumed;

    InstructionsProcessed ++;
    if (InsnLimit != 0 && InstructionsProcessed < InsnLimit)
      break;
  } while (Consumed != 0);
  LLVMDisasmDispose(Ctx);
  return 0;
}

int main(int argc, char **argv) {
  // The command line is unusual compared to other fuzzers due to the need to
  // specify the target. Options like -triple, -mcpu, and -mattr work like
  // their counterparts in llvm-mc, while -fuzzer-args collects options for the
  // fuzzer itself.
  //
  // Examples:
  //
  // Fuzz the big-endian MIPS32R6 disassembler using 100,000 inputs of up to
  // 4-bytes each and use the contents of ./corpus as the test corpus:
  //   llvm-mc-fuzzer -triple mips-linux-gnu -mcpu=mips32r6 -disassemble \
  //       -fuzzer-args -max_len=4 -runs=100000 ./corpus
  //
  // Infinitely fuzz the little-endian MIPS64R2 disassembler with the MSA
  // feature enabled using up to 64-byte inputs:
  //   llvm-mc-fuzzer -triple mipsel-linux-gnu -mcpu=mips64r2 -mattr=msa \
  //       -disassemble -fuzzer-args ./corpus
  //
  // If your aim is to find instructions that are not tested, then it is
  // advisable to constrain the maximum input size to a single instruction
  // using -max_len as in the first example. This results in a test corpus of
  // individual instructions that test unique paths. Without this constraint,
  // there will be considerable redundancy in the corpus.

  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllDisassemblers();

  cl::ParseCommandLineOptions(argc, argv);

  // Package up features to be passed to target/subtarget
  // We have to pass it via a global since the callback doesn't
  // permit any user data.
  if (MAttrs.size()) {
    SubtargetFeatures Features;
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  if (Action == AC_Assemble)
    errs() << "error: -assemble is not implemented\n";
  else if (Action == AC_Disassemble)
    return fuzzer::FuzzerDriver(argc, argv, DisassembleOneInput);

  llvm_unreachable("Unknown action");
  return 1;
}
