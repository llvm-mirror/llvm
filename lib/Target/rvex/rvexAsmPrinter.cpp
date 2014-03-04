//===-- rvexAsmPrinter.cpp - rvex LLVM Assembly Printer -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format rvex assembly language.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "rvex-asm-printer"
#include "rvexAsmPrinter.h"
#include "rvex.h"
#include "rvexInstrInfo.h"
#include "InstPrinter/rvexInstPrinter.h"
#include "MCTargetDesc/rvexBaseInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

bool rvexAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  rvexFI = MF.getInfo<rvexFunctionInfo>();
  AsmPrinter::runOnMachineFunction(MF);
  return true;
}

//- EmitInstruction() must exists or will have run time error.
void rvexAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  SmallString<128> Str;
  raw_svector_ostream OS(Str);

  if(MI->isBundle()) {
    std::vector<const MachineInstr*> BundleMIs;

    unsigned int IgnoreCount = 0;
    MachineBasicBlock::const_instr_iterator MII = MI;
    MachineBasicBlock::const_instr_iterator MBBe = MI->getParent()->instr_end();
    ++MII;
    while(MII != MBBe && MII->isInsideBundle()) {
      const MachineInstr *MInst = MII;
      if(MInst->getOpcode() == TargetOpcode::DBG_VALUE ||
         MInst->getOpcode() == TargetOpcode::IMPLICIT_DEF) {
        IgnoreCount++;
      } else {
        BundleMIs.push_back(MInst);
      }
      ++MII;
    }

    unsigned Size = BundleMIs.size();
    assert((Size+IgnoreCount) == MI->getBundleSize() && "Corrupt Bundle!");

    
    for(unsigned Index = 0; Index < Size; ++Index) {
      const MachineInstr *BMI = BundleMIs[Index];
      //OutStreamer.EmitRawText(StringRef("\tc0"));

      MCInst TmpInst0;
      MCInstLowering.Lower(BMI, TmpInst0);
      OutStreamer.EmitInstruction(TmpInst0);
      // TmpInst0.dump();

      // int i;

      // for (i = 0; i < TmpInst0.getNumOperands(); i++)
      // {
      //   if (TmpInst0.getOperand(i).isImm())
      //     // DEBUG(errs() << "Imm operand found!\n");
      // }
    }

    OutStreamer.EmitRawText(StringRef(";;\n\n"));

  } else {
    //OutStreamer.EmitRawText(StringRef("\tc0"));
    MCInst TmpInst0;
    // MI->dump();
    MCInstLowering.Lower(MI, TmpInst0);
    OutStreamer.EmitInstruction(TmpInst0);
    OutStreamer.EmitRawText(StringRef(";;\n\n"));
  }

}

//===----------------------------------------------------------------------===//
//
//  rvex Asm Directives
//
//  -- Frame directive "frame Stackpointer, Stacksize, RARegister"
//  Describe the stack frame.
//
//  -- Mask directives "(f)mask  bitmask, offset"
//  Tells the assembler which registers are saved and where.
//  bitmask - contain a little endian bitset indicating which registers are
//            saved on function prologue (e.g. with a 0x80000000 mask, the
//            assembler knows the register 31 (RA) is saved at prologue.
//  offset  - the position before stack pointer subtraction indicating where
//            the first saved register on prologue is located. (e.g. with a
//
//  Consider the following function prologue:
//
//    .frame  $fp,48,$ra
//    .mask   0xc0000000,-8
//       addiu $sp, $sp, -48
//       st $ra, 40($sp)
//       st $fp, 36($sp)
//
//    With a 0xc0000000 mask, the assembler knows the register 31 (RA) and
//    30 (FP) are saved at prologue. As the save order on prologue is from
//    left to right, RA is saved first. A -8 offset means that after the
//    stack pointer subtration, the first register in the mask (RA) will be
//    saved at address 48-8=40.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Mask directives
//===----------------------------------------------------------------------===//
//  .frame  $sp,8,$lr
//->  .mask   0x00000000,0
//  .set  noreorder
//  .set  nomacro

// Create a bitmask with all callee saved registers for CPU or Floating Point
// registers. For CPU registers consider RA, GP and FP for saving if necessary.
void rvexAsmPrinter::printSavedRegsBitmask(raw_ostream &O) {
  // CPU and FPU Saved Registers Bitmasks
  unsigned CPUBitmask = 0;
  int CPUTopSavedRegOff;

  // Set the CPU and FPU Bitmasks
  const MachineFrameInfo *MFI = MF->getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();
  // size of stack area to which FP callee-saved regs are saved.
  unsigned CPURegSize = rvex::CPURegsRegClass.getSize();
  unsigned i = 0, e = CSI.size();

  // Set CPU Bitmask.
  for (; i != e; ++i) {
    unsigned Reg = CSI[i].getReg();

    //unsigned RegNum = rvexII::getrvexRegisterNumbering(Reg);
    //CPUBitmask |= (1 << RegNum);

    // Not sure if this hack works. Normally getrvexregisternumbering is used to determine
    // the correct Reg number but considering Reg is already a number is this step really required?
    DEBUG(errs() << "reg number:" << Reg << "\n");
    CPUBitmask |= (1 << Reg);
  }

  CPUTopSavedRegOff = CPUBitmask ? -CPURegSize : 0;

  // Print CPUBitmask
  O << "\t.mask \t"; printHex32(CPUBitmask, O);
  O << ',' << CPUTopSavedRegOff << '\n';
}

// Print a 32 bit hex number with all numbers.
void rvexAsmPrinter::printHex32(unsigned Value, raw_ostream &O) {
  O << "0x";
  for (int i = 7; i >= 0; i--)
    O.write_hex((Value & (0xF << (i*4))) >> (i*4));
}

//===----------------------------------------------------------------------===//
// Frame and Set directives
//===----------------------------------------------------------------------===//
//->  .frame  $sp,8,$lr
//  .mask   0x00000000,0
//  .set  noreorder
//  .set  nomacro
/// Frame Directive
void rvexAsmPrinter::emitFrameDirective() {
//  const TargetRegisterInfo &RI = *TM.getRegisterInfo();

/*  if (OutStreamer.hasRawTextSupport())
    OutStreamer.EmitRawText("\t.frame\t$" +
           StringRef(rvexInstPrinter::getRegisterName(stackReg)).lower() +
           "," + Twine(stackSize) + ",$" +
           StringRef(rvexInstPrinter::getRegisterName(returnReg)).lower());*/
}

/// Emit Set directives.
const char *rvexAsmPrinter::getCurrentABIString() const {
  switch (Subtarget->getTargetABI()) {
  case rvexSubtarget::O32:  return "abi32";
  default: llvm_unreachable("Unknown rvex ABI");;
  }
}

//    .type main,@function
//->    .ent  main                    # @main
//  main:
void rvexAsmPrinter::EmitFunctionEntryLabel() {
  if (OutStreamer.hasRawTextSupport())
  {
    OutStreamer.EmitRawText(StringRef("\n\t.section .bss"));
    EmitAlignment(5);
    OutStreamer.EmitRawText(StringRef("\t.section .data"));
    EmitAlignment(5);
    OutStreamer.EmitRawText(StringRef("\t.section .text"));
    OutStreamer.EmitRawText(StringRef("\t.proc"));
    OutStreamer.EmitRawText(StringRef("\t.entry caller")); //TODO enhance entry caller
    OutStreamer.EmitRawText(Twine(CurrentFnSym->getName())+"::");
  }
  
}


//  .frame  $sp,8,$pc
//  .mask   0x00000000,0
//->  .set  noreorder
//->  .set  nomacro
/// EmitFunctionBodyStart - Targets can override this to emit stuff before
/// the first basic block in the function.
void rvexAsmPrinter::EmitFunctionBodyStart() {
  MCInstLowering.Initialize(Mang, &MF->getContext());

  //emitFrameDirective();


}

//->  .set  macro
//->  .set  reorder
//->  .end  main
/// EmitFunctionBodyEnd - Targets can override this to emit stuff after
/// the last basic block in the function.
void rvexAsmPrinter::EmitFunctionBodyEnd() {
  // There are instruction for this macros, but they must
  // always be at the function end, and we can't emit and
  // break with BB logic.
  OutStreamer.EmitRawText(StringRef("\t.endp"));
/*  if (OutStreamer.hasRawTextSupport()) {
    OutStreamer.EmitRawText(StringRef("\t.set\tmacro"));
    OutStreamer.EmitRawText(StringRef("\t.set\treorder"));
    OutStreamer.EmitRawText("\t.end\t" + Twine(CurrentFnSym->getName()));
  }*/
}

//  .section .mdebug.abi32
//  .previous
void rvexAsmPrinter::EmitStartOfAsmFile(Module &M) {




}

MachineLocation
rvexAsmPrinter::getDebugValueLocation(const MachineInstr *MI) const {
  // Handles frame addresses emitted in rvexInstrInfo::emitFrameIndexDebugValue.
  assert(MI->getNumOperands() == 4 && "Invalid no. of machine operands!");
  assert(MI->getOperand(0).isReg() && MI->getOperand(1).isImm() &&
         "Unexpected MachineOperand types");
  return MachineLocation(MI->getOperand(0).getReg(),
                         MI->getOperand(1).getImm());
}

void rvexAsmPrinter::PrintDebugValueComment(const MachineInstr *MI,
                                           raw_ostream &OS) {
  // TODO: implement
  OS << "PrintDebugValueComment()";
}

static MCInstPrinter *creatervexMCInstPrinter(const Target &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI,
                                                 const MCSubtargetInfo &STI) {
  if (SyntaxVariant == 0)
    return(new rvexInstPrinter(MAI, MII, MRI));
  else
   return NULL;
}

// Force static initialization.
extern "C" void LLVMInitializervexAsmPrinter() {
  RegisterAsmPrinter<rvexAsmPrinter> X(ThervexTarget);
  TargetRegistry::RegisterMCInstPrinter(ThervexTarget,
                                        creatervexMCInstPrinter);
}