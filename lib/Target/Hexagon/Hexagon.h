//=-- Hexagon.h - Top-level interface for Hexagon representation --*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Hexagon back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGON_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGON_H

#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class FunctionPass;
  class ModulePass;
  class TargetMachine;
  class MachineInstr;
  class HexagonMCInst;
  class HexagonAsmPrinter;
  class HexagonTargetMachine;
  class raw_ostream;

  FunctionPass *createHexagonISelDag(HexagonTargetMachine &TM,
                                     CodeGenOpt::Level OptLevel);
  FunctionPass *createHexagonDelaySlotFillerPass(const TargetMachine &TM);
  FunctionPass *createHexagonFPMoverPass(const TargetMachine &TM);
  FunctionPass *createHexagonRemoveExtendArgs(const HexagonTargetMachine &TM);
  FunctionPass *createHexagonCFGOptimizer();

  FunctionPass *createHexagonSplitTFRCondSets();
  FunctionPass *createHexagonSplitConst32AndConst64();
  FunctionPass *createHexagonExpandPredSpillCode();
  FunctionPass *createHexagonHardwareLoops();
  FunctionPass *createHexagonPeephole();
  FunctionPass *createHexagonFixupHwLoops();
  FunctionPass *createHexagonNewValueJump();
  FunctionPass *createHexagonCopyToCombine();
  FunctionPass *createHexagonPacketizer();
  FunctionPass *createHexagonNewValueJump();

/* TODO: object output.
  MCCodeEmitter *createHexagonMCCodeEmitter(const Target &,
                                            const TargetMachine &TM,
                                            MCContext &Ctx);
*/
/* TODO: assembler input.
  TargetAsmBackend *createHexagonAsmBackend(const Target &,
                                                  const std::string &);
*/
  void HexagonLowerToMC(const MachineInstr *MI, HexagonMCInst &MCI,
                        HexagonAsmPrinter &AP);
} // end namespace llvm;

#define Hexagon_POINTER_SIZE 4

#define Hexagon_PointerSize (Hexagon_POINTER_SIZE)
#define Hexagon_PointerSize_Bits (Hexagon_POINTER_SIZE * 8)
#define Hexagon_WordSize Hexagon_PointerSize
#define Hexagon_WordSize_Bits Hexagon_PointerSize_Bits

// allocframe saves LR and FP on stack before allocating
// a new stack frame. This takes 8 bytes.
#define HEXAGON_LRFP_SIZE 8

// Normal instruction size (in bytes).
#define HEXAGON_INSTR_SIZE 4

// Maximum number of words and instructions in a packet.
#define HEXAGON_PACKET_SIZE 4

#endif
