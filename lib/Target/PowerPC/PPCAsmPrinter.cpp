//===-- PPCAsmPrinter.cpp - Print machine instrs to PowerPC assembly ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to PowerPC assembly language. This printer is
// the output mechanism used by `llc'.
//
// Documentation at http://developer.apple.com/documentation/DeveloperTools/
// Reference/Assembler/ASMIntroduction/chapter_1_section_1.html
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "InstPrinter/PPCInstPrinter.h"
#include "MCTargetDesc/PPCMCExpr.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "PPCTargetStreamer.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetRegisterInfo.h"
using namespace llvm;

#define DEBUG_TYPE "asmprinter"

namespace {
  class PPCAsmPrinter : public AsmPrinter {
  protected:
    MapVector<MCSymbol*, MCSymbol*> TOC;
    const PPCSubtarget &Subtarget;
    uint64_t TOCLabelID;
    StackMaps SM;
  public:
    explicit PPCAsmPrinter(TargetMachine &TM,
                           std::unique_ptr<MCStreamer> Streamer)
        : AsmPrinter(TM, std::move(Streamer)),
          Subtarget(TM.getSubtarget<PPCSubtarget>()), TOCLabelID(0), SM(*this) {
    }

    const char *getPassName() const override {
      return "PowerPC Assembly Printer";
    }

    MCSymbol *lookUpOrCreateTOCEntry(MCSymbol *Sym);

    void EmitInstruction(const MachineInstr *MI) override;

    void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &O);

    bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                         unsigned AsmVariant, const char *ExtraCode,
                         raw_ostream &O) override;
    bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                               unsigned AsmVariant, const char *ExtraCode,
                               raw_ostream &O) override;

    void EmitEndOfAsmFile(Module &M) override;

    void LowerSTACKMAP(MCStreamer &OutStreamer, StackMaps &SM,
                       const MachineInstr &MI);
    void LowerPATCHPOINT(MCStreamer &OutStreamer, StackMaps &SM,
                         const MachineInstr &MI);
  };

  /// PPCLinuxAsmPrinter - PowerPC assembly printer, customized for Linux
  class PPCLinuxAsmPrinter : public PPCAsmPrinter {
  public:
    explicit PPCLinuxAsmPrinter(TargetMachine &TM,
                                std::unique_ptr<MCStreamer> Streamer)
        : PPCAsmPrinter(TM, std::move(Streamer)) {}

    const char *getPassName() const override {
      return "Linux PPC Assembly Printer";
    }

    bool doFinalization(Module &M) override;
    void EmitStartOfAsmFile(Module &M) override;

    void EmitFunctionEntryLabel() override;

    void EmitFunctionBodyStart() override;
    void EmitFunctionBodyEnd() override;
  };

  /// PPCDarwinAsmPrinter - PowerPC assembly printer, customized for Darwin/Mac
  /// OS X
  class PPCDarwinAsmPrinter : public PPCAsmPrinter {
  public:
    explicit PPCDarwinAsmPrinter(TargetMachine &TM,
                                 std::unique_ptr<MCStreamer> Streamer)
        : PPCAsmPrinter(TM, std::move(Streamer)) {}

    const char *getPassName() const override {
      return "Darwin PPC Assembly Printer";
    }

    bool doFinalization(Module &M) override;
    void EmitStartOfAsmFile(Module &M) override;

    void EmitFunctionStubs(const MachineModuleInfoMachO::SymbolListTy &Stubs);
  };
} // end of anonymous namespace

/// stripRegisterPrefix - This method strips the character prefix from a
/// register name so that only the number is left.  Used by for linux asm.
static const char *stripRegisterPrefix(const char *RegName) {
  switch (RegName[0]) {
    case 'r':
    case 'f':
    case 'v':
      if (RegName[1] == 's')
        return RegName + 2;
      return RegName + 1;
    case 'c': if (RegName[1] == 'r') return RegName + 2;
  }
  
  return RegName;
}

void PPCAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &O) {
  const DataLayout *DL = TM.getDataLayout();
  const MachineOperand &MO = MI->getOperand(OpNo);
  
  switch (MO.getType()) {
  case MachineOperand::MO_Register: {
    const char *RegName = PPCInstPrinter::getRegisterName(MO.getReg());
    // Linux assembler (Others?) does not take register mnemonics.
    // FIXME - What about special registers used in mfspr/mtspr?
    if (!Subtarget.isDarwin()) RegName = stripRegisterPrefix(RegName);
    O << RegName;
    return;
  }
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;

  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    return;
  case MachineOperand::MO_ConstantPoolIndex:
    O << DL->getPrivateGlobalPrefix() << "CPI" << getFunctionNumber()
      << '_' << MO.getIndex();
    return;
  case MachineOperand::MO_BlockAddress:
    O << *GetBlockAddressSymbol(MO.getBlockAddress());
    return;
  case MachineOperand::MO_GlobalAddress: {
    // Computing the address of a global symbol, not calling it.
    const GlobalValue *GV = MO.getGlobal();
    MCSymbol *SymToPrint;

    // External or weakly linked global variables need non-lazily-resolved stubs
    if (TM.getRelocationModel() != Reloc::Static &&
        (GV->isDeclaration() || GV->isWeakForLinker())) {
      if (!GV->hasHiddenVisibility()) {
        SymToPrint = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
        MachineModuleInfoImpl::StubValueTy &StubSym = 
          MMI->getObjFileInfo<MachineModuleInfoMachO>()
            .getGVStubEntry(SymToPrint);
        if (!StubSym.getPointer())
          StubSym = MachineModuleInfoImpl::
            StubValueTy(getSymbol(GV), !GV->hasInternalLinkage());
      } else if (GV->isDeclaration() || GV->hasCommonLinkage() ||
                 GV->hasAvailableExternallyLinkage()) {
        SymToPrint = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
        
        MachineModuleInfoImpl::StubValueTy &StubSym = 
          MMI->getObjFileInfo<MachineModuleInfoMachO>().
                    getHiddenGVStubEntry(SymToPrint);
        if (!StubSym.getPointer())
          StubSym = MachineModuleInfoImpl::
            StubValueTy(getSymbol(GV), !GV->hasInternalLinkage());
      } else {
        SymToPrint = getSymbol(GV);
      }
    } else {
      SymToPrint = getSymbol(GV);
    }
    
    O << *SymToPrint;

    printOffset(MO.getOffset(), O);
    return;
  }

  default:
    O << "<unknown operand type: " << (unsigned)MO.getType() << ">";
    return;
  }
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool PPCAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                    unsigned AsmVariant,
                                    const char *ExtraCode, raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, AsmVariant, ExtraCode, O);
    case 'c': // Don't print "$" before a global var name or constant.
      break; // PPC never has a prefix.
    case 'L': // Write second word of DImode reference.
      // Verify that this operand has two consecutive registers.
      if (!MI->getOperand(OpNo).isReg() ||
          OpNo+1 == MI->getNumOperands() ||
          !MI->getOperand(OpNo+1).isReg())
        return true;
      ++OpNo;   // Return the high-part.
      break;
    case 'I':
      // Write 'i' if an integer constant, otherwise nothing.  Used to print
      // addi vs add, etc.
      if (MI->getOperand(OpNo).isImm())
        O << "i";
      return false;
    }
  }

  printOperand(MI, OpNo, O);
  return false;
}

// At the moment, all inline asm memory operands are a single register.
// In any case, the output of this routine should always be just one
// assembler operand.

bool PPCAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                          unsigned AsmVariant,
                                          const char *ExtraCode,
                                          raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default: return true;  // Unknown modifier.
    case 'y': // A memory reference for an X-form instruction
      {
        const char *RegName = "r0";
        if (!Subtarget.isDarwin()) RegName = stripRegisterPrefix(RegName);
        O << RegName << ", ";
        printOperand(MI, OpNo, O);
        return false;
      }
    case 'U': // Print 'u' for update form.
    case 'X': // Print 'x' for indexed form.
      {
	// FIXME: Currently for PowerPC memory operands are always loaded
	// into a register, so we never get an update or indexed form.
	// This is bad even for offset forms, since even if we know we
	// have a value in -16(r1), we will generate a load into r<n>
	// and then load from 0(r<n>).  Until that issue is fixed,
	// tolerate 'U' and 'X' but don't output anything.
	assert(MI->getOperand(OpNo).isReg());
	return false;
      }
    }
  }

  assert(MI->getOperand(OpNo).isReg());
  O << "0(";
  printOperand(MI, OpNo, O);
  O << ")";
  return false;
}


/// lookUpOrCreateTOCEntry -- Given a symbol, look up whether a TOC entry
/// exists for it.  If not, create one.  Then return a symbol that references
/// the TOC entry.
MCSymbol *PPCAsmPrinter::lookUpOrCreateTOCEntry(MCSymbol *Sym) {
  const DataLayout *DL = TM.getDataLayout();
  MCSymbol *&TOCEntry = TOC[Sym];

  // To avoid name clash check if the name already exists.
  while (!TOCEntry) {
    if (OutContext.LookupSymbol(Twine(DL->getPrivateGlobalPrefix()) +
                                "C" + Twine(TOCLabelID++)) == nullptr) {
      TOCEntry = GetTempSymbol("C", TOCLabelID);
    }
  }

  return TOCEntry;
}

void PPCAsmPrinter::EmitEndOfAsmFile(Module &M) {
  SM.serializeToStackMapSection();
}

void PPCAsmPrinter::LowerSTACKMAP(MCStreamer &OutStreamer, StackMaps &SM,
                                  const MachineInstr &MI) {
  unsigned NumNOPBytes = MI.getOperand(1).getImm();

  SM.recordStackMap(MI);
  assert(NumNOPBytes % 4 == 0 && "Invalid number of NOP bytes requested!");

  // Scan ahead to trim the shadow.
  const MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::const_iterator MII(MI);
  ++MII;
  while (NumNOPBytes > 0) {
    if (MII == MBB.end() || MII->isCall() ||
        MII->getOpcode() == PPC::DBG_VALUE ||
        MII->getOpcode() == TargetOpcode::PATCHPOINT ||
        MII->getOpcode() == TargetOpcode::STACKMAP)
      break;
    ++MII;
    NumNOPBytes -= 4;
  }

  // Emit nops.
  for (unsigned i = 0; i < NumNOPBytes; i += 4)
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::NOP));
}

// Lower a patchpoint of the form:
// [<def>], <id>, <numBytes>, <target>, <numArgs>
void PPCAsmPrinter::LowerPATCHPOINT(MCStreamer &OutStreamer, StackMaps &SM,
                                    const MachineInstr &MI) {
  SM.recordPatchPoint(MI);
  PatchPointOpers Opers(&MI);

  int64_t CallTarget = Opers.getMetaOper(PatchPointOpers::TargetPos).getImm();
  unsigned EncodedBytes = 0;
  if (CallTarget) {
    assert((CallTarget & 0xFFFFFFFFFFFF) == CallTarget &&
           "High 16 bits of call target should be zero.");
    unsigned ScratchReg = MI.getOperand(Opers.getNextScratchIdx()).getReg();
    EncodedBytes = 6*4;
    // Materialize the jump address:
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::LI8)
                                    .addReg(ScratchReg)
                                    .addImm((CallTarget >> 32) & 0xFFFF));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::RLDIC)
                                    .addReg(ScratchReg)
                                    .addReg(ScratchReg)
                                    .addImm(32).addImm(16));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ORIS8)
                                    .addReg(ScratchReg)
                                    .addReg(ScratchReg)
                                    .addImm((CallTarget >> 16) & 0xFFFF));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ORI8)
                                    .addReg(ScratchReg)
                                    .addReg(ScratchReg)
                                    .addImm(CallTarget & 0xFFFF));

    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MTCTR8).addReg(ScratchReg));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BCTRL8));
  }

  // Emit padding.
  unsigned NumBytes = Opers.getMetaOper(PatchPointOpers::NBytesPos).getImm();
  assert(NumBytes >= EncodedBytes &&
         "Patchpoint can't request size less than the length of a call.");
  assert((NumBytes - EncodedBytes) % 4 == 0 &&
         "Invalid number of NOP bytes requested!");
  for (unsigned i = EncodedBytes; i < NumBytes; i += 4)
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::NOP));
}

/// EmitInstruction -- Print out a single PowerPC MI in Darwin syntax to
/// the current output stream.
///
void PPCAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  MCInst TmpInst;
  bool isPPC64 = Subtarget.isPPC64();
  bool isDarwin = Triple(TM.getTargetTriple()).isOSDarwin();
  const Module *M = MF->getFunction()->getParent();
  PICLevel::Level PL = M->getPICLevel();
  
  // Lower multi-instruction pseudo operations.
  switch (MI->getOpcode()) {
  default: break;
  case TargetOpcode::DBG_VALUE:
    llvm_unreachable("Should be handled target independently");
  case TargetOpcode::STACKMAP:
    return LowerSTACKMAP(OutStreamer, SM, *MI);
  case TargetOpcode::PATCHPOINT:
    return LowerPATCHPOINT(OutStreamer, SM, *MI);

  case PPC::MoveGOTtoLR: {
    // Transform %LR = MoveGOTtoLR
    // Into this: bl _GLOBAL_OFFSET_TABLE_@local-4
    // _GLOBAL_OFFSET_TABLE_@local-4 (instruction preceding
    // _GLOBAL_OFFSET_TABLE_) has exactly one instruction:
    //      blrl
    // This will return the pointer to _GLOBAL_OFFSET_TABLE_@local
    MCSymbol *GOTSymbol =
      OutContext.GetOrCreateSymbol(StringRef("_GLOBAL_OFFSET_TABLE_"));
    const MCExpr *OffsExpr =
      MCBinaryExpr::CreateSub(MCSymbolRefExpr::Create(GOTSymbol,
                                                      MCSymbolRefExpr::VK_PPC_LOCAL,
                                                      OutContext),
                              MCConstantExpr::Create(4, OutContext),
                              OutContext);

    // Emit the 'bl'.
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BL).addExpr(OffsExpr));
    return;
  }
  case PPC::MovePCtoLR:
  case PPC::MovePCtoLR8: {
    // Transform %LR = MovePCtoLR
    // Into this, where the label is the PIC base: 
    //     bl L1$pb
    // L1$pb:
    MCSymbol *PICBase = MF->getPICBaseSymbol();
    
    // Emit the 'bl'.
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BL)
      // FIXME: We would like an efficient form for this, so we don't have to do
      // a lot of extra uniquing.
      .addExpr(MCSymbolRefExpr::Create(PICBase, OutContext)));
    
    // Emit the label.
    OutStreamer.EmitLabel(PICBase);
    return;
  }
  case PPC::UpdateGBR: {
    // Transform %Rd = UpdateGBR(%Rt, %Ri)
    // Into: lwz %Rt, .L0$poff - .L0$pb(%Ri)
    //       add %Rd, %Rt, %Ri
    // Get the offset from the GOT Base Register to the GOT
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);
    MCSymbol *PICOffset =
      MF->getInfo<PPCFunctionInfo>()->getPICOffsetSymbol();
    TmpInst.setOpcode(PPC::LWZ);
    const MCExpr *Exp =
      MCSymbolRefExpr::Create(PICOffset, MCSymbolRefExpr::VK_None, OutContext);
    const MCExpr *PB =
      MCSymbolRefExpr::Create(MF->getPICBaseSymbol(),
                              MCSymbolRefExpr::VK_None,
                              OutContext);
    const MCOperand TR = TmpInst.getOperand(1);
    const MCOperand PICR = TmpInst.getOperand(0);

    // Step 1: lwz %Rt, .L$poff - .L$pb(%Ri)
    TmpInst.getOperand(1) =
        MCOperand::CreateExpr(MCBinaryExpr::CreateSub(Exp, PB, OutContext));
    TmpInst.getOperand(0) = TR;
    TmpInst.getOperand(2) = PICR;
    EmitToStreamer(OutStreamer, TmpInst);

    TmpInst.setOpcode(PPC::ADD4);
    TmpInst.getOperand(0) = PICR;
    TmpInst.getOperand(1) = TR;
    TmpInst.getOperand(2) = PICR;
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
  case PPC::LWZtoc: {
    // Transform %R3 = LWZtoc <ga:@min1>, %R2
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to LWZ, and the global address operand to be a
    // reference to the GOT entry we will synthesize later.
    TmpInst.setOpcode(PPC::LWZ);
    const MachineOperand &MO = MI->getOperand(1);

    // Map symbol -> label of TOC entry
    assert(MO.isGlobal() || MO.isCPI() || MO.isJTI() || MO.isBlockAddress());
    MCSymbol *MOSymbol = nullptr;
    if (MO.isGlobal())
      MOSymbol = getSymbol(MO.getGlobal());
    else if (MO.isCPI())
      MOSymbol = GetCPISymbol(MO.getIndex());
    else if (MO.isJTI())
      MOSymbol = GetJTISymbol(MO.getIndex());
    else if (MO.isBlockAddress())
      MOSymbol = GetBlockAddressSymbol(MO.getBlockAddress());

    if (PL == PICLevel::Small) {
      const MCExpr *Exp =
        MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_GOT,
                                OutContext);
      TmpInst.getOperand(1) = MCOperand::CreateExpr(Exp);
    } else {
      MCSymbol *TOCEntry = lookUpOrCreateTOCEntry(MOSymbol);

      const MCExpr *Exp =
        MCSymbolRefExpr::Create(TOCEntry, MCSymbolRefExpr::VK_None,
                                OutContext);
      const MCExpr *PB =
        MCSymbolRefExpr::Create(OutContext.GetOrCreateSymbol(Twine(".LTOC")),
                                                             OutContext);
      Exp = MCBinaryExpr::CreateSub(Exp, PB, OutContext);
      TmpInst.getOperand(1) = MCOperand::CreateExpr(Exp);
    }
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
  case PPC::LDtocJTI:
  case PPC::LDtocCPT:
  case PPC::LDtocBA:
  case PPC::LDtoc: {
    // Transform %X3 = LDtoc <ga:@min1>, %X2
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to LD, and the global address operand to be a
    // reference to the TOC entry we will synthesize later.
    TmpInst.setOpcode(PPC::LD);
    const MachineOperand &MO = MI->getOperand(1);

    // Map symbol -> label of TOC entry
    assert(MO.isGlobal() || MO.isCPI() || MO.isJTI() || MO.isBlockAddress());
    MCSymbol *MOSymbol = nullptr;
    if (MO.isGlobal())
      MOSymbol = getSymbol(MO.getGlobal());
    else if (MO.isCPI())
      MOSymbol = GetCPISymbol(MO.getIndex());
    else if (MO.isJTI())
      MOSymbol = GetJTISymbol(MO.getIndex());
    else if (MO.isBlockAddress())
      MOSymbol = GetBlockAddressSymbol(MO.getBlockAddress());

    MCSymbol *TOCEntry = lookUpOrCreateTOCEntry(MOSymbol);

    const MCExpr *Exp =
      MCSymbolRefExpr::Create(TOCEntry, MCSymbolRefExpr::VK_PPC_TOC,
                              OutContext);
    TmpInst.getOperand(1) = MCOperand::CreateExpr(Exp);
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
      
  case PPC::ADDIStocHA: {
    // Transform %Xd = ADDIStocHA %X2, <ga:@sym>
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to ADDIS8.  If the global address is external, has
    // common linkage, is a non-local function address, or is a jump table
    // address, then generate a TOC entry and reference that.  Otherwise
    // reference the symbol directly.
    TmpInst.setOpcode(PPC::ADDIS8);
    const MachineOperand &MO = MI->getOperand(2);
    assert((MO.isGlobal() || MO.isCPI() || MO.isJTI() ||
            MO.isBlockAddress()) &&
           "Invalid operand for ADDIStocHA!");
    MCSymbol *MOSymbol = nullptr;
    bool IsExternal = false;
    bool IsNonLocalFunction = false;
    bool IsCommon = false;
    bool IsAvailExt = false;

    if (MO.isGlobal()) {
      const GlobalValue *GV = MO.getGlobal();
      MOSymbol = getSymbol(GV);
      IsExternal = GV->isDeclaration();
      IsCommon = GV->hasCommonLinkage();
      IsNonLocalFunction = GV->getType()->getElementType()->isFunctionTy() &&
        (GV->isDeclaration() || GV->isWeakForLinker());
      IsAvailExt = GV->hasAvailableExternallyLinkage();
    } else if (MO.isCPI())
      MOSymbol = GetCPISymbol(MO.getIndex());
    else if (MO.isJTI())
      MOSymbol = GetJTISymbol(MO.getIndex());
    else if (MO.isBlockAddress())
      MOSymbol = GetBlockAddressSymbol(MO.getBlockAddress());

    if (IsExternal || IsNonLocalFunction || IsCommon || IsAvailExt ||
        MO.isJTI() || MO.isBlockAddress() ||
        TM.getCodeModel() == CodeModel::Large)
      MOSymbol = lookUpOrCreateTOCEntry(MOSymbol);

    const MCExpr *Exp =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_TOC_HA,
                              OutContext);
    TmpInst.getOperand(2) = MCOperand::CreateExpr(Exp);
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
  case PPC::LDtocL: {
    // Transform %Xd = LDtocL <ga:@sym>, %Xs
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to LD.  If the global address is external, has
    // common linkage, or is a jump table address, then reference the
    // associated TOC entry.  Otherwise reference the symbol directly.
    TmpInst.setOpcode(PPC::LD);
    const MachineOperand &MO = MI->getOperand(1);
    assert((MO.isGlobal() || MO.isCPI() || MO.isJTI() ||
            MO.isBlockAddress()) &&
           "Invalid operand for LDtocL!");
    MCSymbol *MOSymbol = nullptr;

    if (MO.isJTI())
      MOSymbol = lookUpOrCreateTOCEntry(GetJTISymbol(MO.getIndex()));
    else if (MO.isBlockAddress()) {
      MOSymbol = GetBlockAddressSymbol(MO.getBlockAddress());
      MOSymbol = lookUpOrCreateTOCEntry(MOSymbol);
    }
    else if (MO.isCPI()) {
      MOSymbol = GetCPISymbol(MO.getIndex());
      if (TM.getCodeModel() == CodeModel::Large)
        MOSymbol = lookUpOrCreateTOCEntry(MOSymbol);
    }
    else if (MO.isGlobal()) {
      const GlobalValue *GValue = MO.getGlobal();
      MOSymbol = getSymbol(GValue);
      if (GValue->getType()->getElementType()->isFunctionTy() ||
          GValue->isDeclaration() || GValue->hasCommonLinkage() ||
          GValue->hasAvailableExternallyLinkage() ||
          TM.getCodeModel() == CodeModel::Large)
        MOSymbol = lookUpOrCreateTOCEntry(MOSymbol);
    }

    const MCExpr *Exp =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_TOC_LO,
                              OutContext);
    TmpInst.getOperand(1) = MCOperand::CreateExpr(Exp);
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
  case PPC::ADDItocL: {
    // Transform %Xd = ADDItocL %Xs, <ga:@sym>
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to ADDI8.  If the global address is external, then
    // generate a TOC entry and reference that.  Otherwise reference the
    // symbol directly.
    TmpInst.setOpcode(PPC::ADDI8);
    const MachineOperand &MO = MI->getOperand(2);
    assert((MO.isGlobal() || MO.isCPI()) && "Invalid operand for ADDItocL");
    MCSymbol *MOSymbol = nullptr;
    bool IsExternal = false;
    bool IsNonLocalFunction = false;

    if (MO.isGlobal()) {
      const GlobalValue *GV = MO.getGlobal();
      MOSymbol = getSymbol(GV);
      IsExternal = GV->isDeclaration();
      IsNonLocalFunction = GV->getType()->getElementType()->isFunctionTy() &&
        (GV->isDeclaration() || GV->isWeakForLinker());
    } else if (MO.isCPI())
      MOSymbol = GetCPISymbol(MO.getIndex());

    if (IsNonLocalFunction || IsExternal ||
        TM.getCodeModel() == CodeModel::Large)
      MOSymbol = lookUpOrCreateTOCEntry(MOSymbol);

    const MCExpr *Exp =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_TOC_LO,
                              OutContext);
    TmpInst.getOperand(2) = MCOperand::CreateExpr(Exp);
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }
  case PPC::ADDISgotTprelHA: {
    // Transform: %Xd = ADDISgotTprelHA %X2, <ga:@sym>
    // Into:      %Xd = ADDIS8 %X2, sym@got@tlsgd@ha
    assert(Subtarget.isPPC64() && "Not supported for 32-bit PowerPC");
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymGotTprel =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_GOT_TPREL_HA,
                              OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS8)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(1).getReg())
                                .addExpr(SymGotTprel));
    return;
  }
  case PPC::LDgotTprelL:
  case PPC::LDgotTprelL32: {
    // Transform %Xd = LDgotTprelL <ga:@sym>, %Xs
    LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);

    // Change the opcode to LD.
    TmpInst.setOpcode(isPPC64 ? PPC::LD : PPC::LWZ);
    const MachineOperand &MO = MI->getOperand(1);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *Exp =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_GOT_TPREL_LO,
                              OutContext);
    TmpInst.getOperand(1) = MCOperand::CreateExpr(Exp);
    EmitToStreamer(OutStreamer, TmpInst);
    return;
  }

  case PPC::PPC32PICGOT: {
    MCSymbol *GOTSymbol = OutContext.GetOrCreateSymbol(StringRef("_GLOBAL_OFFSET_TABLE_"));
    MCSymbol *GOTRef = OutContext.CreateTempSymbol();
    MCSymbol *NextInstr = OutContext.CreateTempSymbol();

    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BL)
      // FIXME: We would like an efficient form for this, so we don't have to do
      // a lot of extra uniquing.
      .addExpr(MCSymbolRefExpr::Create(NextInstr, OutContext)));
    const MCExpr *OffsExpr =
      MCBinaryExpr::CreateSub(MCSymbolRefExpr::Create(GOTSymbol, OutContext),
                                MCSymbolRefExpr::Create(GOTRef, OutContext),
        OutContext);
    OutStreamer.EmitLabel(GOTRef);
    OutStreamer.EmitValue(OffsExpr, 4);
    OutStreamer.EmitLabel(NextInstr);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MFLR)
                                .addReg(MI->getOperand(0).getReg()));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::LWZ)
                                .addReg(MI->getOperand(1).getReg())
                                .addImm(0)
                                .addReg(MI->getOperand(0).getReg()));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADD4)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(1).getReg())
                                .addReg(MI->getOperand(0).getReg()));
    return;
  }
  case PPC::PPC32GOT: {
    MCSymbol *GOTSymbol = OutContext.GetOrCreateSymbol(StringRef("_GLOBAL_OFFSET_TABLE_"));
    const MCExpr *SymGotTlsL =
      MCSymbolRefExpr::Create(GOTSymbol, MCSymbolRefExpr::VK_PPC_LO,
                              OutContext);
    const MCExpr *SymGotTlsHA =                               
      MCSymbolRefExpr::Create(GOTSymbol, MCSymbolRefExpr::VK_PPC_HA,
                              OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::LI)
                                .addReg(MI->getOperand(0).getReg())
                                .addExpr(SymGotTlsL));
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(0).getReg())
                                .addExpr(SymGotTlsHA));
    return;
  }
  case PPC::ADDIStlsgdHA: {
    // Transform: %Xd = ADDIStlsgdHA %X2, <ga:@sym>
    // Into:      %Xd = ADDIS8 %X2, sym@got@tlsgd@ha
    assert(Subtarget.isPPC64() && "Not supported for 32-bit PowerPC");
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymGotTlsGD =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_GOT_TLSGD_HA,
                              OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS8)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(1).getReg())
                                .addExpr(SymGotTlsGD));
    return;
  }
  case PPC::ADDItlsgdL:
    // Transform: %Xd = ADDItlsgdL %Xs, <ga:@sym>
    // Into:      %Xd = ADDI8 %Xs, sym@got@tlsgd@l
  case PPC::ADDItlsgdL32: {
    // Transform: %Rd = ADDItlsgdL32 %Rs, <ga:@sym>
    // Into:      %Rd = ADDI %Rs, sym@got@tlsgd
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymGotTlsGD =
      MCSymbolRefExpr::Create(MOSymbol, Subtarget.isPPC64() ?
                                         MCSymbolRefExpr::VK_PPC_GOT_TLSGD_LO :
                                         MCSymbolRefExpr::VK_PPC_GOT_TLSGD,
                              OutContext);
    EmitToStreamer(OutStreamer,
                   MCInstBuilder(Subtarget.isPPC64() ? PPC::ADDI8 : PPC::ADDI)
                   .addReg(MI->getOperand(0).getReg())
                   .addReg(MI->getOperand(1).getReg())
                   .addExpr(SymGotTlsGD));
    return;
  }
  case PPC::ADDIStlsldHA: {
    // Transform: %Xd = ADDIStlsldHA %X2, <ga:@sym>
    // Into:      %Xd = ADDIS8 %X2, sym@got@tlsld@ha
    assert(Subtarget.isPPC64() && "Not supported for 32-bit PowerPC");
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymGotTlsLD =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_GOT_TLSLD_HA,
                              OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS8)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(1).getReg())
                                .addExpr(SymGotTlsLD));
    return;
  }
  case PPC::ADDItlsldL:
    // Transform: %Xd = ADDItlsldL %Xs, <ga:@sym>
    // Into:      %Xd = ADDI8 %Xs, sym@got@tlsld@l
  case PPC::ADDItlsldL32: {
    // Transform: %Rd = ADDItlsldL32 %Rs, <ga:@sym>
    // Into:      %Rd = ADDI %Rs, sym@got@tlsld
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymGotTlsLD =
      MCSymbolRefExpr::Create(MOSymbol, Subtarget.isPPC64() ?
                                         MCSymbolRefExpr::VK_PPC_GOT_TLSLD_LO :
                                         MCSymbolRefExpr::VK_PPC_GOT_TLSLD,
                              OutContext);
    EmitToStreamer(OutStreamer,
                   MCInstBuilder(Subtarget.isPPC64() ? PPC::ADDI8 : PPC::ADDI)
                   .addReg(MI->getOperand(0).getReg())
                   .addReg(MI->getOperand(1).getReg())
                   .addExpr(SymGotTlsLD));
    return;
  }
  case PPC::ADDISdtprelHA:
    // Transform: %Xd = ADDISdtprelHA %X3, <ga:@sym>
    // Into:      %Xd = ADDIS8 %X3, sym@dtprel@ha
  case PPC::ADDISdtprelHA32: {
    // Transform: %Rd = ADDISdtprelHA32 %R3, <ga:@sym>
    // Into:      %Rd = ADDIS %R3, sym@dtprel@ha
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymDtprel =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_DTPREL_HA,
                              OutContext);
    EmitToStreamer(OutStreamer,
                   MCInstBuilder(Subtarget.isPPC64() ? PPC::ADDIS8 : PPC::ADDIS)
                   .addReg(MI->getOperand(0).getReg())
                   .addReg(Subtarget.isPPC64() ? PPC::X3 : PPC::R3)
                   .addExpr(SymDtprel));
    return;
  }
  case PPC::ADDIdtprelL:
    // Transform: %Xd = ADDIdtprelL %Xs, <ga:@sym>
    // Into:      %Xd = ADDI8 %Xs, sym@dtprel@l
  case PPC::ADDIdtprelL32: {
    // Transform: %Rd = ADDIdtprelL32 %Rs, <ga:@sym>
    // Into:      %Rd = ADDI %Rs, sym@dtprel@l
    const MachineOperand &MO = MI->getOperand(2);
    const GlobalValue *GValue = MO.getGlobal();
    MCSymbol *MOSymbol = getSymbol(GValue);
    const MCExpr *SymDtprel =
      MCSymbolRefExpr::Create(MOSymbol, MCSymbolRefExpr::VK_PPC_DTPREL_LO,
                              OutContext);
    EmitToStreamer(OutStreamer,
                   MCInstBuilder(Subtarget.isPPC64() ? PPC::ADDI8 : PPC::ADDI)
                   .addReg(MI->getOperand(0).getReg())
                   .addReg(MI->getOperand(1).getReg())
                   .addExpr(SymDtprel));
    return;
  }
  case PPC::MFOCRF:
  case PPC::MFOCRF8:
    if (!Subtarget.hasMFOCRF()) {
      // Transform: %R3 = MFOCRF %CR7
      // Into:      %R3 = MFCR   ;; cr7
      unsigned NewOpcode =
        MI->getOpcode() == PPC::MFOCRF ? PPC::MFCR : PPC::MFCR8;
      OutStreamer.AddComment(PPCInstPrinter::
                             getRegisterName(MI->getOperand(1).getReg()));
      EmitToStreamer(OutStreamer, MCInstBuilder(NewOpcode)
                                  .addReg(MI->getOperand(0).getReg()));
      return;
    }
    break;
  case PPC::MTOCRF:
  case PPC::MTOCRF8:
    if (!Subtarget.hasMFOCRF()) {
      // Transform: %CR7 = MTOCRF %R3
      // Into:      MTCRF mask, %R3 ;; cr7
      unsigned NewOpcode =
        MI->getOpcode() == PPC::MTOCRF ? PPC::MTCRF : PPC::MTCRF8;
      unsigned Mask = 0x80 >> OutContext.getRegisterInfo()
                              ->getEncodingValue(MI->getOperand(0).getReg());
      OutStreamer.AddComment(PPCInstPrinter::
                             getRegisterName(MI->getOperand(0).getReg()));
      EmitToStreamer(OutStreamer, MCInstBuilder(NewOpcode)
                                  .addImm(Mask)
                                  .addReg(MI->getOperand(1).getReg()));
      return;
    }
    break;
  case PPC::LD:
  case PPC::STD:
  case PPC::LWA_32:
  case PPC::LWA: {
    // Verify alignment is legal, so we don't create relocations
    // that can't be supported.
    // FIXME:  This test is currently disabled for Darwin.  The test
    // suite shows a handful of test cases that fail this check for
    // Darwin.  Those need to be investigated before this sanity test
    // can be enabled for those subtargets.
    if (!Subtarget.isDarwin()) {
      unsigned OpNum = (MI->getOpcode() == PPC::STD) ? 2 : 1;
      const MachineOperand &MO = MI->getOperand(OpNum);
      if (MO.isGlobal() && MO.getGlobal()->getAlignment() < 4)
        llvm_unreachable("Global must be word-aligned for LD, STD, LWA!");
    }
    // Now process the instruction normally.
    break;
  }
  }

  LowerPPCMachineInstrToMCInst(MI, TmpInst, *this, isDarwin);
  EmitToStreamer(OutStreamer, TmpInst);
}

void PPCLinuxAsmPrinter::EmitStartOfAsmFile(Module &M) {
  if (Subtarget.isELFv2ABI()) {
    PPCTargetStreamer *TS =
      static_cast<PPCTargetStreamer *>(OutStreamer.getTargetStreamer());

    if (TS)
      TS->emitAbiVersion(2);
  }

  if (Subtarget.isPPC64() || TM.getRelocationModel() != Reloc::PIC_)
    return AsmPrinter::EmitStartOfAsmFile(M);

  if (M.getPICLevel() == PICLevel::Small)
    return AsmPrinter::EmitStartOfAsmFile(M);

  OutStreamer.SwitchSection(OutContext.getELFSection(
      ".got2", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC));

  MCSymbol *TOCSym = OutContext.GetOrCreateSymbol(Twine(".LTOC"));
  MCSymbol *CurrentPos = OutContext.CreateTempSymbol();

  OutStreamer.EmitLabel(CurrentPos);

  // The GOT pointer points to the middle of the GOT, in order to reference the
  // entire 64kB range.  0x8000 is the midpoint.
  const MCExpr *tocExpr =
    MCBinaryExpr::CreateAdd(MCSymbolRefExpr::Create(CurrentPos, OutContext),
                            MCConstantExpr::Create(0x8000, OutContext),
                            OutContext);

  OutStreamer.EmitAssignment(TOCSym, tocExpr);

  OutStreamer.SwitchSection(getObjFileLowering().getTextSection());
}

void PPCLinuxAsmPrinter::EmitFunctionEntryLabel() {
  // linux/ppc32 - Normal entry label.
  if (!Subtarget.isPPC64() && 
      (TM.getRelocationModel() != Reloc::PIC_ || 
       MF->getFunction()->getParent()->getPICLevel() == PICLevel::Small))
    return AsmPrinter::EmitFunctionEntryLabel();

  if (!Subtarget.isPPC64()) {
    const PPCFunctionInfo *PPCFI = MF->getInfo<PPCFunctionInfo>();
  	if (PPCFI->usesPICBase()) {
      MCSymbol *RelocSymbol = PPCFI->getPICOffsetSymbol();
      MCSymbol *PICBase = MF->getPICBaseSymbol();
      OutStreamer.EmitLabel(RelocSymbol);

      const MCExpr *OffsExpr =
        MCBinaryExpr::CreateSub(
          MCSymbolRefExpr::Create(OutContext.GetOrCreateSymbol(Twine(".LTOC")),
                                                               OutContext),
                                  MCSymbolRefExpr::Create(PICBase, OutContext),
          OutContext);
      OutStreamer.EmitValue(OffsExpr, 4);
      OutStreamer.EmitLabel(CurrentFnSym);
      return;
    } else
      return AsmPrinter::EmitFunctionEntryLabel();
  }

  // ELFv2 ABI - Normal entry label.
  if (Subtarget.isELFv2ABI())
    return AsmPrinter::EmitFunctionEntryLabel();

  // Emit an official procedure descriptor.
  MCSectionSubPair Current = OutStreamer.getCurrentSection();
  const MCSectionELF *Section = OutStreamer.getContext().getELFSection(
      ".opd", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
  OutStreamer.SwitchSection(Section);
  OutStreamer.EmitLabel(CurrentFnSym);
  OutStreamer.EmitValueToAlignment(8);
  MCSymbol *Symbol1 = 
    OutContext.GetOrCreateSymbol(".L." + Twine(CurrentFnSym->getName()));
  // Generates a R_PPC64_ADDR64 (from FK_DATA_8) relocation for the function
  // entry point.
  OutStreamer.EmitValue(MCSymbolRefExpr::Create(Symbol1, OutContext),
			8 /*size*/);
  MCSymbol *Symbol2 = OutContext.GetOrCreateSymbol(StringRef(".TOC."));
  // Generates a R_PPC64_TOC relocation for TOC base insertion.
  OutStreamer.EmitValue(MCSymbolRefExpr::Create(Symbol2,
                        MCSymbolRefExpr::VK_PPC_TOCBASE, OutContext),
                        8/*size*/);
  // Emit a null environment pointer.
  OutStreamer.EmitIntValue(0, 8 /* size */);
  OutStreamer.SwitchSection(Current.first, Current.second);

  MCSymbol *RealFnSym = OutContext.GetOrCreateSymbol(
                          ".L." + Twine(CurrentFnSym->getName()));
  OutStreamer.EmitLabel(RealFnSym);
  CurrentFnSymForSize = RealFnSym;
}


bool PPCLinuxAsmPrinter::doFinalization(Module &M) {
  const DataLayout *TD = TM.getDataLayout();

  bool isPPC64 = TD->getPointerSizeInBits() == 64;

  PPCTargetStreamer &TS =
      static_cast<PPCTargetStreamer &>(*OutStreamer.getTargetStreamer());

  if (!TOC.empty()) {
    const MCSectionELF *Section;
    
    if (isPPC64)
      Section = OutStreamer.getContext().getELFSection(
          ".toc", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
        else
          Section = OutStreamer.getContext().getELFSection(
              ".got2", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
    OutStreamer.SwitchSection(Section);

    for (MapVector<MCSymbol*, MCSymbol*>::iterator I = TOC.begin(),
         E = TOC.end(); I != E; ++I) {
      OutStreamer.EmitLabel(I->second);
      MCSymbol *S = I->first;
      if (isPPC64)
        TS.emitTCEntry(*S);
      else
        OutStreamer.EmitSymbolValue(S, 4);
    }
  }

  MachineModuleInfoELF &MMIELF =
    MMI->getObjFileInfo<MachineModuleInfoELF>();

  MachineModuleInfoELF::SymbolListTy Stubs = MMIELF.GetGVStubList();
  if (!Stubs.empty()) {
    OutStreamer.SwitchSection(getObjFileLowering().getDataSection());
    for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
      // L_foo$stub:
      OutStreamer.EmitLabel(Stubs[i].first);
      //   .long _foo
      OutStreamer.EmitValue(MCSymbolRefExpr::Create(Stubs[i].second.getPointer(),
                                                    OutContext),
                            isPPC64 ? 8 : 4/*size*/);
    }

    Stubs.clear();
    OutStreamer.AddBlankLine();
  }

  return AsmPrinter::doFinalization(M);
}

/// EmitFunctionBodyStart - Emit a global entry point prefix for ELFv2.
void PPCLinuxAsmPrinter::EmitFunctionBodyStart() {
  // In the ELFv2 ABI, in functions that use the TOC register, we need to
  // provide two entry points.  The ABI guarantees that when calling the
  // local entry point, r2 is set up by the caller to contain the TOC base
  // for this function, and when calling the global entry point, r12 is set
  // up by the caller to hold the address of the global entry point.  We
  // thus emit a prefix sequence along the following lines:
  //
  // func:
  //         # global entry point
  //         addis r2,r12,(.TOC.-func)@ha
  //         addi  r2,r2,(.TOC.-func)@l
  //         .localentry func, .-func
  //         # local entry point, followed by function body
  //
  // This ensures we have r2 set up correctly while executing the function
  // body, no matter which entry point is called.
  if (Subtarget.isELFv2ABI()
      // Only do all that if the function uses r2 in the first place.
      && !MF->getRegInfo().use_empty(PPC::X2)) {

    MCSymbol *GlobalEntryLabel = OutContext.CreateTempSymbol();
    OutStreamer.EmitLabel(GlobalEntryLabel);
    const MCSymbolRefExpr *GlobalEntryLabelExp =
      MCSymbolRefExpr::Create(GlobalEntryLabel, OutContext);

    MCSymbol *TOCSymbol = OutContext.GetOrCreateSymbol(StringRef(".TOC."));
    const MCExpr *TOCDeltaExpr =
      MCBinaryExpr::CreateSub(MCSymbolRefExpr::Create(TOCSymbol, OutContext),
                              GlobalEntryLabelExp, OutContext);

    const MCExpr *TOCDeltaHi =
      PPCMCExpr::CreateHa(TOCDeltaExpr, false, OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS)
                                .addReg(PPC::X2)
                                .addReg(PPC::X12)
                                .addExpr(TOCDeltaHi));

    const MCExpr *TOCDeltaLo =
      PPCMCExpr::CreateLo(TOCDeltaExpr, false, OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDI)
                                .addReg(PPC::X2)
                                .addReg(PPC::X2)
                                .addExpr(TOCDeltaLo));

    MCSymbol *LocalEntryLabel = OutContext.CreateTempSymbol();
    OutStreamer.EmitLabel(LocalEntryLabel);
    const MCSymbolRefExpr *LocalEntryLabelExp =
       MCSymbolRefExpr::Create(LocalEntryLabel, OutContext);
    const MCExpr *LocalOffsetExp =
      MCBinaryExpr::CreateSub(LocalEntryLabelExp,
                              GlobalEntryLabelExp, OutContext);

    PPCTargetStreamer *TS =
      static_cast<PPCTargetStreamer *>(OutStreamer.getTargetStreamer());

    if (TS)
      TS->emitLocalEntry(CurrentFnSym, LocalOffsetExp);
  }
}

/// EmitFunctionBodyEnd - Print the traceback table before the .size
/// directive.
///
void PPCLinuxAsmPrinter::EmitFunctionBodyEnd() {
  // Only the 64-bit target requires a traceback table.  For now,
  // we only emit the word of zeroes that GDB requires to find
  // the end of the function, and zeroes for the eight-byte
  // mandatory fields.
  // FIXME: We should fill in the eight-byte mandatory fields as described in
  // the PPC64 ELF ABI (this is a low-priority item because GDB does not
  // currently make use of these fields).
  if (Subtarget.isPPC64()) {
    OutStreamer.EmitIntValue(0, 4/*size*/);
    OutStreamer.EmitIntValue(0, 8/*size*/);
  }
}

void PPCDarwinAsmPrinter::EmitStartOfAsmFile(Module &M) {
  static const char *const CPUDirectives[] = {
    "",
    "ppc",
    "ppc440",
    "ppc601",
    "ppc602",
    "ppc603",
    "ppc7400",
    "ppc750",
    "ppc970",
    "ppcA2",
    "ppce500mc",
    "ppce5500",
    "power3",
    "power4",
    "power5",
    "power5x",
    "power6",
    "power6x",
    "power7",
    "ppc64",
    "ppc64le"
  };

  unsigned Directive = Subtarget.getDarwinDirective();
  if (Subtarget.hasMFOCRF() && Directive < PPC::DIR_970)
    Directive = PPC::DIR_970;
  if (Subtarget.hasAltivec() && Directive < PPC::DIR_7400)
    Directive = PPC::DIR_7400;
  if (Subtarget.isPPC64() && Directive < PPC::DIR_64)
    Directive = PPC::DIR_64;
  assert(Directive <= PPC::DIR_64 && "Directive out of range.");

  assert(Directive < array_lengthof(CPUDirectives) &&
         "CPUDirectives[] might not be up-to-date!");
  PPCTargetStreamer &TStreamer =
      *static_cast<PPCTargetStreamer *>(OutStreamer.getTargetStreamer());
  TStreamer.emitMachine(CPUDirectives[Directive]);

  // Prime text sections so they are adjacent.  This reduces the likelihood a
  // large data or debug section causes a branch to exceed 16M limit.
  const TargetLoweringObjectFileMachO &TLOFMacho = 
    static_cast<const TargetLoweringObjectFileMachO &>(getObjFileLowering());
  OutStreamer.SwitchSection(TLOFMacho.getTextCoalSection());
  if (TM.getRelocationModel() == Reloc::PIC_) {
    OutStreamer.SwitchSection(
           OutContext.getMachOSection("__TEXT", "__picsymbolstub1",
                                      MachO::S_SYMBOL_STUBS |
                                      MachO::S_ATTR_PURE_INSTRUCTIONS,
                                      32, SectionKind::getText()));
  } else if (TM.getRelocationModel() == Reloc::DynamicNoPIC) {
    OutStreamer.SwitchSection(
           OutContext.getMachOSection("__TEXT","__symbol_stub1",
                                      MachO::S_SYMBOL_STUBS |
                                      MachO::S_ATTR_PURE_INSTRUCTIONS,
                                      16, SectionKind::getText()));
  }
  OutStreamer.SwitchSection(getObjFileLowering().getTextSection());
}

static MCSymbol *GetLazyPtr(MCSymbol *Sym, MCContext &Ctx) {
  // Remove $stub suffix, add $lazy_ptr.
  StringRef NoStub = Sym->getName().substr(0, Sym->getName().size()-5);
  return Ctx.GetOrCreateSymbol(NoStub + "$lazy_ptr");
}

static MCSymbol *GetAnonSym(MCSymbol *Sym, MCContext &Ctx) {
  // Add $tmp suffix to $stub, yielding $stub$tmp.
  return Ctx.GetOrCreateSymbol(Sym->getName() + "$tmp");
}

void PPCDarwinAsmPrinter::
EmitFunctionStubs(const MachineModuleInfoMachO::SymbolListTy &Stubs) {
  bool isPPC64 = TM.getDataLayout()->getPointerSizeInBits() == 64;
  bool isDarwin = Subtarget.isDarwin();
  
  const TargetLoweringObjectFileMachO &TLOFMacho = 
    static_cast<const TargetLoweringObjectFileMachO &>(getObjFileLowering());

  // .lazy_symbol_pointer
  const MCSection *LSPSection = TLOFMacho.getLazySymbolPointerSection();
  
  // Output stubs for dynamically-linked functions
  if (TM.getRelocationModel() == Reloc::PIC_) {
    const MCSection *StubSection = 
    OutContext.getMachOSection("__TEXT", "__picsymbolstub1",
                               MachO::S_SYMBOL_STUBS |
                               MachO::S_ATTR_PURE_INSTRUCTIONS,
                               32, SectionKind::getText());
    for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
      OutStreamer.SwitchSection(StubSection);
      EmitAlignment(4);
      
      MCSymbol *Stub = Stubs[i].first;
      MCSymbol *RawSym = Stubs[i].second.getPointer();
      MCSymbol *LazyPtr = GetLazyPtr(Stub, OutContext);
      MCSymbol *AnonSymbol = GetAnonSym(Stub, OutContext);
                                           
      OutStreamer.EmitLabel(Stub);
      OutStreamer.EmitSymbolAttribute(RawSym, MCSA_IndirectSymbol);

      const MCExpr *Anon = MCSymbolRefExpr::Create(AnonSymbol, OutContext);
      const MCExpr *LazyPtrExpr = MCSymbolRefExpr::Create(LazyPtr, OutContext);
      const MCExpr *Sub =
        MCBinaryExpr::CreateSub(LazyPtrExpr, Anon, OutContext);

      // mflr r0
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MFLR).addReg(PPC::R0));
      // bcl 20, 31, AnonSymbol
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BCLalways).addExpr(Anon));
      OutStreamer.EmitLabel(AnonSymbol);
      // mflr r11
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MFLR).addReg(PPC::R11));
      // addis r11, r11, ha16(LazyPtr - AnonSymbol)
      const MCExpr *SubHa16 = PPCMCExpr::CreateHa(Sub, isDarwin, OutContext);
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::ADDIS)
        .addReg(PPC::R11)
        .addReg(PPC::R11)
        .addExpr(SubHa16));
      // mtlr r0
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MTLR).addReg(PPC::R0));

      // ldu r12, lo16(LazyPtr - AnonSymbol)(r11)
      // lwzu r12, lo16(LazyPtr - AnonSymbol)(r11)
      const MCExpr *SubLo16 = PPCMCExpr::CreateLo(Sub, isDarwin, OutContext);
      EmitToStreamer(OutStreamer, MCInstBuilder(isPPC64 ? PPC::LDU : PPC::LWZU)
        .addReg(PPC::R12)
        .addExpr(SubLo16).addExpr(SubLo16)
        .addReg(PPC::R11));
      // mtctr r12
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MTCTR).addReg(PPC::R12));
      // bctr
      EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BCTR));

      OutStreamer.SwitchSection(LSPSection);
      OutStreamer.EmitLabel(LazyPtr);
      OutStreamer.EmitSymbolAttribute(RawSym, MCSA_IndirectSymbol);

      MCSymbol *DyldStubBindingHelper =
        OutContext.GetOrCreateSymbol(StringRef("dyld_stub_binding_helper"));
      if (isPPC64) {
        // .quad dyld_stub_binding_helper
        OutStreamer.EmitSymbolValue(DyldStubBindingHelper, 8);
      } else {
        // .long dyld_stub_binding_helper
        OutStreamer.EmitSymbolValue(DyldStubBindingHelper, 4);
      }
    }
    OutStreamer.AddBlankLine();
    return;
  }
  
  const MCSection *StubSection =
    OutContext.getMachOSection("__TEXT","__symbol_stub1",
                               MachO::S_SYMBOL_STUBS |
                               MachO::S_ATTR_PURE_INSTRUCTIONS,
                               16, SectionKind::getText());
  for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
    MCSymbol *Stub = Stubs[i].first;
    MCSymbol *RawSym = Stubs[i].second.getPointer();
    MCSymbol *LazyPtr = GetLazyPtr(Stub, OutContext);
    const MCExpr *LazyPtrExpr = MCSymbolRefExpr::Create(LazyPtr, OutContext);

    OutStreamer.SwitchSection(StubSection);
    EmitAlignment(4);
    OutStreamer.EmitLabel(Stub);
    OutStreamer.EmitSymbolAttribute(RawSym, MCSA_IndirectSymbol);

    // lis r11, ha16(LazyPtr)
    const MCExpr *LazyPtrHa16 =
      PPCMCExpr::CreateHa(LazyPtrExpr, isDarwin, OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::LIS)
      .addReg(PPC::R11)
      .addExpr(LazyPtrHa16));

    // ldu r12, lo16(LazyPtr)(r11)
    // lwzu r12, lo16(LazyPtr)(r11)
    const MCExpr *LazyPtrLo16 =
      PPCMCExpr::CreateLo(LazyPtrExpr, isDarwin, OutContext);
    EmitToStreamer(OutStreamer, MCInstBuilder(isPPC64 ? PPC::LDU : PPC::LWZU)
      .addReg(PPC::R12)
      .addExpr(LazyPtrLo16).addExpr(LazyPtrLo16)
      .addReg(PPC::R11));

    // mtctr r12
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::MTCTR).addReg(PPC::R12));
    // bctr
    EmitToStreamer(OutStreamer, MCInstBuilder(PPC::BCTR));

    OutStreamer.SwitchSection(LSPSection);
    OutStreamer.EmitLabel(LazyPtr);
    OutStreamer.EmitSymbolAttribute(RawSym, MCSA_IndirectSymbol);

    MCSymbol *DyldStubBindingHelper =
      OutContext.GetOrCreateSymbol(StringRef("dyld_stub_binding_helper"));
    if (isPPC64) {
      // .quad dyld_stub_binding_helper
      OutStreamer.EmitSymbolValue(DyldStubBindingHelper, 8);
    } else {
      // .long dyld_stub_binding_helper
      OutStreamer.EmitSymbolValue(DyldStubBindingHelper, 4);
    }
  }
  
  OutStreamer.AddBlankLine();
}


bool PPCDarwinAsmPrinter::doFinalization(Module &M) {
  bool isPPC64 = TM.getDataLayout()->getPointerSizeInBits() == 64;

  // Darwin/PPC always uses mach-o.
  const TargetLoweringObjectFileMachO &TLOFMacho = 
    static_cast<const TargetLoweringObjectFileMachO &>(getObjFileLowering());
  MachineModuleInfoMachO &MMIMacho =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();
  
  MachineModuleInfoMachO::SymbolListTy Stubs = MMIMacho.GetFnStubList();
  if (!Stubs.empty())
    EmitFunctionStubs(Stubs);

  if (MAI->doesSupportExceptionHandling() && MMI) {
    // Add the (possibly multiple) personalities to the set of global values.
    // Only referenced functions get into the Personalities list.
    const std::vector<const Function*> &Personalities = MMI->getPersonalities();
    for (std::vector<const Function*>::const_iterator I = Personalities.begin(),
         E = Personalities.end(); I != E; ++I) {
      if (*I) {
        MCSymbol *NLPSym = getSymbolWithGlobalValueBase(*I, "$non_lazy_ptr");
        MachineModuleInfoImpl::StubValueTy &StubSym =
          MMIMacho.getGVStubEntry(NLPSym);
        StubSym = MachineModuleInfoImpl::StubValueTy(getSymbol(*I), true);
      }
    }
  }

  // Output stubs for dynamically-linked functions.
  Stubs = MMIMacho.GetGVStubList();
  
  // Output macho stubs for external and common global variables.
  if (!Stubs.empty()) {
    // Switch with ".non_lazy_symbol_pointer" directive.
    OutStreamer.SwitchSection(TLOFMacho.getNonLazySymbolPointerSection());
    EmitAlignment(isPPC64 ? 3 : 2);
    
    for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
      // L_foo$stub:
      OutStreamer.EmitLabel(Stubs[i].first);
      //   .indirect_symbol _foo
      MachineModuleInfoImpl::StubValueTy &MCSym = Stubs[i].second;
      OutStreamer.EmitSymbolAttribute(MCSym.getPointer(), MCSA_IndirectSymbol);

      if (MCSym.getInt())
        // External to current translation unit.
        OutStreamer.EmitIntValue(0, isPPC64 ? 8 : 4/*size*/);
      else
        // Internal to current translation unit.
        //
        // When we place the LSDA into the TEXT section, the type info pointers
        // need to be indirect and pc-rel. We accomplish this by using NLPs.
        // However, sometimes the types are local to the file. So we need to
        // fill in the value for the NLP in those cases.
        OutStreamer.EmitValue(MCSymbolRefExpr::Create(MCSym.getPointer(),
                                                      OutContext),
                              isPPC64 ? 8 : 4/*size*/);
    }

    Stubs.clear();
    OutStreamer.AddBlankLine();
  }

  Stubs = MMIMacho.GetHiddenGVStubList();
  if (!Stubs.empty()) {
    OutStreamer.SwitchSection(getObjFileLowering().getDataSection());
    EmitAlignment(isPPC64 ? 3 : 2);
    
    for (unsigned i = 0, e = Stubs.size(); i != e; ++i) {
      // L_foo$stub:
      OutStreamer.EmitLabel(Stubs[i].first);
      //   .long _foo
      OutStreamer.EmitValue(MCSymbolRefExpr::
                            Create(Stubs[i].second.getPointer(),
                                   OutContext),
                            isPPC64 ? 8 : 4/*size*/);
    }

    Stubs.clear();
    OutStreamer.AddBlankLine();
  }

  // Funny Darwin hack: This flag tells the linker that no global symbols
  // contain code that falls through to other global symbols (e.g. the obvious
  // implementation of multiple entry points).  If this doesn't occur, the
  // linker can safely perform dead code stripping.  Since LLVM never generates
  // code that does this, it is always safe to set.
  OutStreamer.EmitAssemblerFlag(MCAF_SubsectionsViaSymbols);

  return AsmPrinter::doFinalization(M);
}

/// createPPCAsmPrinterPass - Returns a pass that prints the PPC assembly code
/// for a MachineFunction to the given output stream, in a format that the
/// Darwin assembler can deal with.
///
static AsmPrinter *
createPPCAsmPrinterPass(TargetMachine &tm,
                        std::unique_ptr<MCStreamer> &&Streamer) {
  const PPCSubtarget *Subtarget = &tm.getSubtarget<PPCSubtarget>();

  if (Subtarget->isDarwin())
    return new PPCDarwinAsmPrinter(tm, std::move(Streamer));
  return new PPCLinuxAsmPrinter(tm, std::move(Streamer));
}

// Force static initialization.
extern "C" void LLVMInitializePowerPCAsmPrinter() { 
  TargetRegistry::RegisterAsmPrinter(ThePPC32Target, createPPCAsmPrinterPass);
  TargetRegistry::RegisterAsmPrinter(ThePPC64Target, createPPCAsmPrinterPass);
  TargetRegistry::RegisterAsmPrinter(ThePPC64LETarget, createPPCAsmPrinterPass);
}
