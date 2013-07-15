//===-- SystemZAsmPrinter.h - SystemZ LLVM assembly printer ----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SYSTEMZASMPRINTER_H
#define SYSTEMZASMPRINTER_H

#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCStreamer;
class MachineBasicBlock;
class MachineInstr;
class Module;
class raw_ostream;

class LLVM_LIBRARY_VISIBILITY SystemZAsmPrinter : public AsmPrinter {
private:
  const SystemZSubtarget *Subtarget;

public:
  SystemZAsmPrinter(TargetMachine &TM, MCStreamer &Streamer)
    : AsmPrinter(TM, Streamer) {
    Subtarget = &TM.getSubtarget<SystemZSubtarget>();
  }

  // Override AsmPrinter.
  virtual const char *getPassName() const LLVM_OVERRIDE {
    return "SystemZ Assembly Printer";
  }
  virtual void EmitInstruction(const MachineInstr *MI) LLVM_OVERRIDE;
  virtual void EmitMachineConstantPoolValue(MachineConstantPoolValue *MCPV)
    LLVM_OVERRIDE;
  virtual bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                               unsigned AsmVariant, const char *ExtraCode,
                               raw_ostream &OS) LLVM_OVERRIDE;
  virtual bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                     unsigned AsmVariant,
                                     const char *ExtraCode,
                                     raw_ostream &OS) LLVM_OVERRIDE;
  virtual void EmitEndOfAsmFile(Module &M) LLVM_OVERRIDE;
};
} // end namespace llvm

#endif
