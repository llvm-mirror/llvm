//===-- rvexAsmPrinter.h - rvex LLVM Assembly Printer ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// rvex Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef rvexASMPRINTER_H
#define rvexASMPRINTER_H

#include "rvexMachineFunction.h"
#include "rvexMCInstLower.h"
#include "rvexSubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"

#include "llvm/Support/raw_ostream.h"


namespace llvm {
class MCStreamer;
class MachineInstr;
class MachineBasicBlock;
class Module;
class raw_ostream;

class LLVM_LIBRARY_VISIBILITY rvexAsmPrinter : public AsmPrinter {

  void EmitInstrWithMacroNoAT(const MachineInstr *MI);

public:

  const rvexSubtarget *Subtarget;
  const rvexFunctionInfo *rvexFI;
  rvexMCInstLower MCInstLowering;

  explicit rvexAsmPrinter(TargetMachine &TM,  MCStreamer &Streamer)
    : AsmPrinter(TM, Streamer), MCInstLowering(*this) {
    Subtarget = &TM.getSubtarget<rvexSubtarget>();
  }

  virtual const char *getPassName() const {
    return "rvex Assembly Printer";
  }

  virtual bool runOnMachineFunction(MachineFunction &MF);

//- EmitInstruction() must exists or will have run time error.

  void printInstruction(const MachineInstr *MI, raw_ostream &OS);

  void EmitInstruction(const MachineInstr *MI);
  void printSavedRegsBitmask(raw_ostream &O);
  void printHex32(unsigned int Value, raw_ostream &O);
  void emitFrameDirective();
  const char *getCurrentABIString() const;
  virtual void EmitFunctionEntryLabel();
  virtual void EmitFunctionBodyStart();
  virtual void EmitFunctionBodyEnd();
  void EmitStartOfAsmFile(Module &M);
  virtual MachineLocation getDebugValueLocation(const MachineInstr *MI) const;
  void PrintDebugValueComment(const MachineInstr *MI, raw_ostream &OS);
};
}

#endif

