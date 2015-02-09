//===-- X86.h - Top-level interface for X86 representation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the x86
// target library, as used by the LLVM JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86_H
#define LLVM_LIB_TARGET_X86_X86_H

#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class ImmutablePass;
class X86TargetMachine;

/// createX86ISelDag - This pass converts a legalized DAG into a
/// X86-specific DAG, ready for instruction scheduling.
///
FunctionPass *createX86ISelDag(X86TargetMachine &TM,
                               CodeGenOpt::Level OptLevel);

/// createX86GlobalBaseRegPass - This pass initializes a global base
/// register for PIC on x86-32.
FunctionPass* createX86GlobalBaseRegPass();

/// createCleanupLocalDynamicTLSPass() - This pass combines multiple accesses
/// to local-dynamic TLS variables so that the TLS base address for the module
/// is only fetched once per execution path through the function.
FunctionPass *createCleanupLocalDynamicTLSPass();

/// createX86FloatingPointStackifierPass - This function returns a pass which
/// converts floating point register references and pseudo instructions into
/// floating point stack references and physical instructions.
///
FunctionPass *createX86FloatingPointStackifierPass();

/// createX86IssueVZeroUpperPass - This pass inserts AVX vzeroupper instructions
/// before each call to avoid transition penalty between functions encoded with
/// AVX and SSE.
FunctionPass *createX86IssueVZeroUpperPass();

/// createX86EmitCodeToMemory - Returns a pass that converts a register
/// allocated function into raw machine code in a dynamically
/// allocated chunk of memory.
///
FunctionPass *createEmitX86CodeToMemory();

/// createX86PadShortFunctions - Return a pass that pads short functions
/// with NOOPs. This will prevent a stall when returning on the Atom.
FunctionPass *createX86PadShortFunctions();
/// createX86FixupLEAs - Return a a pass that selectively replaces
/// certain instructions (like add, sub, inc, dec, some shifts,
/// and some multiplies) by equivalent LEA instructions, in order
/// to eliminate execution delays in some Atom processors.
FunctionPass *createX86FixupLEAs();

/// createX86CallFrameOptimization - Return a pass that optimizes
/// the code-size of x86 call sequences. This is done by replacing
/// esp-relative movs with pushes.
FunctionPass *createX86CallFrameOptimization();

} // End llvm namespace

#endif
