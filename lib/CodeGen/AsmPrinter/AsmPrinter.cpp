//===-- AsmPrinter.cpp - Common AsmPrinter code ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AsmPrinter class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/AsmPrinter.h"
#include "DwarfDebug.h"
#include "DwarfException.h"
#include "Win64Exception.h"
#include "WinCodeViewLineTables.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/JumpInstrTableInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GCMetadataPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Timer.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetSubtargetInfo.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

static const char *const DWARFGroupName = "DWARF Emission";
static const char *const DbgTimerName = "Debug Info Emission";
static const char *const EHTimerName = "DWARF Exception Writer";
static const char *const CodeViewLineTablesGroupName = "CodeView Line Tables";

STATISTIC(EmittedInsts, "Number of machine instrs printed");

char AsmPrinter::ID = 0;

typedef DenseMap<GCStrategy*, std::unique_ptr<GCMetadataPrinter>> gcp_map_type;
static gcp_map_type &getGCMap(void *&P) {
  if (!P)
    P = new gcp_map_type();
  return *(gcp_map_type*)P;
}


/// getGVAlignmentLog2 - Return the alignment to use for the specified global
/// value in log2 form.  This rounds up to the preferred alignment if possible
/// and legal.
static unsigned getGVAlignmentLog2(const GlobalValue *GV, const DataLayout &TD,
                                   unsigned InBits = 0) {
  unsigned NumBits = 0;
  if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV))
    NumBits = TD.getPreferredAlignmentLog(GVar);

  // If InBits is specified, round it to it.
  if (InBits > NumBits)
    NumBits = InBits;

  // If the GV has a specified alignment, take it into account.
  if (GV->getAlignment() == 0)
    return NumBits;

  unsigned GVAlign = Log2_32(GV->getAlignment());

  // If the GVAlign is larger than NumBits, or if we are required to obey
  // NumBits because the GV has an assigned section, obey it.
  if (GVAlign > NumBits || GV->hasSection())
    NumBits = GVAlign;
  return NumBits;
}

AsmPrinter::AsmPrinter(TargetMachine &tm, std::unique_ptr<MCStreamer> Streamer)
    : MachineFunctionPass(ID), TM(tm), MAI(tm.getMCAsmInfo()),
      MII(tm.getSubtargetImpl()->getInstrInfo()),
      OutContext(Streamer->getContext()), OutStreamer(*Streamer.release()),
      LastMI(nullptr), LastFn(0), Counter(~0U), SetCounter(0) {
  DD = nullptr; MMI = nullptr; LI = nullptr; MF = nullptr;
  CurrentFnSym = CurrentFnSymForSize = nullptr;
  GCMetadataPrinters = nullptr;
  VerboseAsm = OutStreamer.isVerboseAsm();
}

AsmPrinter::~AsmPrinter() {
  assert(!DD && Handlers.empty() && "Debug/EH info didn't get finalized");

  if (GCMetadataPrinters) {
    gcp_map_type &GCMap = getGCMap(GCMetadataPrinters);

    delete &GCMap;
    GCMetadataPrinters = nullptr;
  }

  delete &OutStreamer;
}

/// getFunctionNumber - Return a unique ID for the current function.
///
unsigned AsmPrinter::getFunctionNumber() const {
  return MF->getFunctionNumber();
}

const TargetLoweringObjectFile &AsmPrinter::getObjFileLowering() const {
  return *TM.getObjFileLowering();
}

/// getDataLayout - Return information about data layout.
const DataLayout &AsmPrinter::getDataLayout() const {
  return *TM.getDataLayout();
}

const MCSubtargetInfo &AsmPrinter::getSubtargetInfo() const {
  return TM.getSubtarget<MCSubtargetInfo>();
}

void AsmPrinter::EmitToStreamer(MCStreamer &S, const MCInst &Inst) {
  S.EmitInstruction(Inst, getSubtargetInfo());
}

StringRef AsmPrinter::getTargetTriple() const {
  return TM.getTargetTriple();
}

/// getCurrentSection() - Return the current section we are emitting to.
const MCSection *AsmPrinter::getCurrentSection() const {
  return OutStreamer.getCurrentSection().first;
}



void AsmPrinter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
  AU.addRequired<MachineModuleInfo>();
  AU.addRequired<GCModuleInfo>();
  if (isVerbose())
    AU.addRequired<MachineLoopInfo>();
}

bool AsmPrinter::doInitialization(Module &M) {
  MMI = getAnalysisIfAvailable<MachineModuleInfo>();
  MMI->AnalyzeModule(M);

  // Initialize TargetLoweringObjectFile.
  const_cast<TargetLoweringObjectFile&>(getObjFileLowering())
    .Initialize(OutContext, TM);

  OutStreamer.InitSections(false);

  Mang = new Mangler(TM.getDataLayout());

  // Emit the version-min deplyment target directive if needed.
  //
  // FIXME: If we end up with a collection of these sorts of Darwin-specific
  // or ELF-specific things, it may make sense to have a platform helper class
  // that will work with the target helper class. For now keep it here, as the
  // alternative is duplicated code in each of the target asm printers that
  // use the directive, where it would need the same conditionalization
  // anyway.
  Triple TT(getTargetTriple());
  if (TT.isOSDarwin()) {
    unsigned Major, Minor, Update;
    TT.getOSVersion(Major, Minor, Update);
    // If there is a version specified, Major will be non-zero.
    if (Major)
      OutStreamer.EmitVersionMin((TT.isMacOSX() ?
                                  MCVM_OSXVersionMin : MCVM_IOSVersionMin),
                                 Major, Minor, Update);
  }

  // Allow the target to emit any magic that it wants at the start of the file.
  EmitStartOfAsmFile(M);

  // Very minimal debug info. It is ignored if we emit actual debug info. If we
  // don't, this at least helps the user find where a global came from.
  if (MAI->hasSingleParameterDotFile()) {
    // .file "foo.c"
    OutStreamer.EmitFileDirective(M.getModuleIdentifier());
  }

  GCModuleInfo *MI = getAnalysisIfAvailable<GCModuleInfo>();
  assert(MI && "AsmPrinter didn't require GCModuleInfo?");
  for (auto &I : *MI)
    if (GCMetadataPrinter *MP = GetOrCreateGCPrinter(*I))
      MP->beginAssembly(M, *MI, *this);

  // Emit module-level inline asm if it exists.
  if (!M.getModuleInlineAsm().empty()) {
    OutStreamer.AddComment("Start of file scope inline assembly");
    OutStreamer.AddBlankLine();
    EmitInlineAsm(M.getModuleInlineAsm()+"\n");
    OutStreamer.AddComment("End of file scope inline assembly");
    OutStreamer.AddBlankLine();
  }

  if (MAI->doesSupportDebugInformation()) {
    bool skip_dwarf = false;
    if (Triple(TM.getTargetTriple()).isKnownWindowsMSVCEnvironment()) {
      Handlers.push_back(HandlerInfo(new WinCodeViewLineTables(this),
                                     DbgTimerName,
                                     CodeViewLineTablesGroupName));
      // FIXME: Don't emit DWARF debug info if there's at least one function
      // with AddressSanitizer instrumentation.
      // This is a band-aid fix for PR22032.
      for (auto &F : M.functions()) {
        if (F.hasFnAttribute(Attribute::SanitizeAddress)) {
          skip_dwarf = true;
          break;
        }
      }
    }
    if (!skip_dwarf) {
      DD = new DwarfDebug(this, &M);
      Handlers.push_back(HandlerInfo(DD, DbgTimerName, DWARFGroupName));
    }
  }

  EHStreamer *ES = nullptr;
  switch (MAI->getExceptionHandlingType()) {
  case ExceptionHandling::None:
    break;
  case ExceptionHandling::SjLj:
  case ExceptionHandling::DwarfCFI:
    ES = new DwarfCFIException(this);
    break;
  case ExceptionHandling::ARM:
    ES = new ARMException(this);
    break;
  case ExceptionHandling::WinEH:
    switch (MAI->getWinEHEncodingType()) {
    default: llvm_unreachable("unsupported unwinding information encoding");
    case WinEH::EncodingType::Itanium:
      ES = new Win64Exception(this);
      break;
    }
    break;
  }
  if (ES)
    Handlers.push_back(HandlerInfo(ES, EHTimerName, DWARFGroupName));
  return false;
}

static bool canBeHidden(const GlobalValue *GV, const MCAsmInfo &MAI) {
  if (!MAI.hasWeakDefCanBeHiddenDirective())
    return false;

  return canBeOmittedFromSymbolTable(GV);
}

void AsmPrinter::EmitLinkage(const GlobalValue *GV, MCSymbol *GVSym) const {
  GlobalValue::LinkageTypes Linkage = GV->getLinkage();
  switch (Linkage) {
  case GlobalValue::CommonLinkage:
  case GlobalValue::LinkOnceAnyLinkage:
  case GlobalValue::LinkOnceODRLinkage:
  case GlobalValue::WeakAnyLinkage:
  case GlobalValue::WeakODRLinkage:
    if (MAI->hasWeakDefDirective()) {
      // .globl _foo
      OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Global);

      if (!canBeHidden(GV, *MAI))
        // .weak_definition _foo
        OutStreamer.EmitSymbolAttribute(GVSym, MCSA_WeakDefinition);
      else
        OutStreamer.EmitSymbolAttribute(GVSym, MCSA_WeakDefAutoPrivate);
    } else if (MAI->hasLinkOnceDirective()) {
      // .globl _foo
      OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Global);
      //NOTE: linkonce is handled by the section the symbol was assigned to.
    } else {
      // .weak _foo
      OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Weak);
    }
    return;
  case GlobalValue::AppendingLinkage:
    // FIXME: appending linkage variables should go into a section of
    // their name or something.  For now, just emit them as external.
  case GlobalValue::ExternalLinkage:
    // If external or appending, declare as a global symbol.
    // .globl _foo
    OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Global);
    return;
  case GlobalValue::PrivateLinkage:
  case GlobalValue::InternalLinkage:
    return;
  case GlobalValue::AvailableExternallyLinkage:
    llvm_unreachable("Should never emit this");
  case GlobalValue::ExternalWeakLinkage:
    llvm_unreachable("Don't know how to emit these");
  }
  llvm_unreachable("Unknown linkage type!");
}

void AsmPrinter::getNameWithPrefix(SmallVectorImpl<char> &Name,
                                   const GlobalValue *GV) const {
  TM.getNameWithPrefix(Name, GV, *Mang);
}

MCSymbol *AsmPrinter::getSymbol(const GlobalValue *GV) const {
  return TM.getSymbol(GV, *Mang);
}

/// EmitGlobalVariable - Emit the specified global variable to the .s file.
void AsmPrinter::EmitGlobalVariable(const GlobalVariable *GV) {
  if (GV->hasInitializer()) {
    // Check to see if this is a special global used by LLVM, if so, emit it.
    if (EmitSpecialLLVMGlobal(GV))
      return;

    if (isVerbose()) {
      GV->printAsOperand(OutStreamer.GetCommentOS(),
                     /*PrintType=*/false, GV->getParent());
      OutStreamer.GetCommentOS() << '\n';
    }
  }

  MCSymbol *GVSym = getSymbol(GV);
  EmitVisibility(GVSym, GV->getVisibility(), !GV->isDeclaration());

  if (!GV->hasInitializer())   // External globals require no extra code.
    return;

  GVSym->redefineIfPossible();
  if (GVSym->isDefined() || GVSym->isVariable())
    report_fatal_error("symbol '" + Twine(GVSym->getName()) +
                       "' is already defined");

  if (MAI->hasDotTypeDotSizeDirective())
    OutStreamer.EmitSymbolAttribute(GVSym, MCSA_ELF_TypeObject);

  SectionKind GVKind = TargetLoweringObjectFile::getKindForGlobal(GV, TM);

  const DataLayout *DL = TM.getDataLayout();
  uint64_t Size = DL->getTypeAllocSize(GV->getType()->getElementType());

  // If the alignment is specified, we *must* obey it.  Overaligning a global
  // with a specified alignment is a prompt way to break globals emitted to
  // sections and expected to be contiguous (e.g. ObjC metadata).
  unsigned AlignLog = getGVAlignmentLog2(GV, *DL);

  for (const HandlerInfo &HI : Handlers) {
    NamedRegionTimer T(HI.TimerName, HI.TimerGroupName, TimePassesIsEnabled);
    HI.Handler->setSymbolSize(GVSym, Size);
  }

  // Handle common and BSS local symbols (.lcomm).
  if (GVKind.isCommon() || GVKind.isBSSLocal()) {
    if (Size == 0) Size = 1;   // .comm Foo, 0 is undefined, avoid it.
    unsigned Align = 1 << AlignLog;

    // Handle common symbols.
    if (GVKind.isCommon()) {
      if (!getObjFileLowering().getCommDirectiveSupportsAlignment())
        Align = 0;

      // .comm _foo, 42, 4
      OutStreamer.EmitCommonSymbol(GVSym, Size, Align);
      return;
    }

    // Handle local BSS symbols.
    if (MAI->hasMachoZeroFillDirective()) {
      const MCSection *TheSection =
        getObjFileLowering().SectionForGlobal(GV, GVKind, *Mang, TM);
      // .zerofill __DATA, __bss, _foo, 400, 5
      OutStreamer.EmitZerofill(TheSection, GVSym, Size, Align);
      return;
    }

    // Use .lcomm only if it supports user-specified alignment.
    // Otherwise, while it would still be correct to use .lcomm in some
    // cases (e.g. when Align == 1), the external assembler might enfore
    // some -unknown- default alignment behavior, which could cause
    // spurious differences between external and integrated assembler.
    // Prefer to simply fall back to .local / .comm in this case.
    if (MAI->getLCOMMDirectiveAlignmentType() != LCOMM::NoAlignment) {
      // .lcomm _foo, 42
      OutStreamer.EmitLocalCommonSymbol(GVSym, Size, Align);
      return;
    }

    if (!getObjFileLowering().getCommDirectiveSupportsAlignment())
      Align = 0;

    // .local _foo
    OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Local);
    // .comm _foo, 42, 4
    OutStreamer.EmitCommonSymbol(GVSym, Size, Align);
    return;
  }

  const MCSection *TheSection =
    getObjFileLowering().SectionForGlobal(GV, GVKind, *Mang, TM);

  // Handle the zerofill directive on darwin, which is a special form of BSS
  // emission.
  if (GVKind.isBSSExtern() && MAI->hasMachoZeroFillDirective()) {
    if (Size == 0) Size = 1;  // zerofill of 0 bytes is undefined.

    // .globl _foo
    OutStreamer.EmitSymbolAttribute(GVSym, MCSA_Global);
    // .zerofill __DATA, __common, _foo, 400, 5
    OutStreamer.EmitZerofill(TheSection, GVSym, Size, 1 << AlignLog);
    return;
  }

  // Handle thread local data for mach-o which requires us to output an
  // additional structure of data and mangle the original symbol so that we
  // can reference it later.
  //
  // TODO: This should become an "emit thread local global" method on TLOF.
  // All of this macho specific stuff should be sunk down into TLOFMachO and
  // stuff like "TLSExtraDataSection" should no longer be part of the parent
  // TLOF class.  This will also make it more obvious that stuff like
  // MCStreamer::EmitTBSSSymbol is macho specific and only called from macho
  // specific code.
  if (GVKind.isThreadLocal() && MAI->hasMachoTBSSDirective()) {
    // Emit the .tbss symbol
    MCSymbol *MangSym =
      OutContext.GetOrCreateSymbol(GVSym->getName() + Twine("$tlv$init"));

    if (GVKind.isThreadBSS()) {
      TheSection = getObjFileLowering().getTLSBSSSection();
      OutStreamer.EmitTBSSSymbol(TheSection, MangSym, Size, 1 << AlignLog);
    } else if (GVKind.isThreadData()) {
      OutStreamer.SwitchSection(TheSection);

      EmitAlignment(AlignLog, GV);
      OutStreamer.EmitLabel(MangSym);

      EmitGlobalConstant(GV->getInitializer());
    }

    OutStreamer.AddBlankLine();

    // Emit the variable struct for the runtime.
    const MCSection *TLVSect
      = getObjFileLowering().getTLSExtraDataSection();

    OutStreamer.SwitchSection(TLVSect);
    // Emit the linkage here.
    EmitLinkage(GV, GVSym);
    OutStreamer.EmitLabel(GVSym);

    // Three pointers in size:
    //   - __tlv_bootstrap - used to make sure support exists
    //   - spare pointer, used when mapped by the runtime
    //   - pointer to mangled symbol above with initializer
    unsigned PtrSize = DL->getPointerTypeSize(GV->getType());
    OutStreamer.EmitSymbolValue(GetExternalSymbolSymbol("_tlv_bootstrap"),
                                PtrSize);
    OutStreamer.EmitIntValue(0, PtrSize);
    OutStreamer.EmitSymbolValue(MangSym, PtrSize);

    OutStreamer.AddBlankLine();
    return;
  }

  OutStreamer.SwitchSection(TheSection);

  EmitLinkage(GV, GVSym);
  EmitAlignment(AlignLog, GV);

  OutStreamer.EmitLabel(GVSym);

  EmitGlobalConstant(GV->getInitializer());

  if (MAI->hasDotTypeDotSizeDirective())
    // .size foo, 42
    OutStreamer.EmitELFSize(GVSym, MCConstantExpr::Create(Size, OutContext));

  OutStreamer.AddBlankLine();
}

/// EmitFunctionHeader - This method emits the header for the current
/// function.
void AsmPrinter::EmitFunctionHeader() {
  // Print out constants referenced by the function
  EmitConstantPool();

  // Print the 'header' of function.
  const Function *F = MF->getFunction();

  OutStreamer.SwitchSection(
      getObjFileLowering().SectionForGlobal(F, *Mang, TM));
  EmitVisibility(CurrentFnSym, F->getVisibility());

  EmitLinkage(F, CurrentFnSym);
  EmitAlignment(MF->getAlignment(), F);

  if (MAI->hasDotTypeDotSizeDirective())
    OutStreamer.EmitSymbolAttribute(CurrentFnSym, MCSA_ELF_TypeFunction);

  if (isVerbose()) {
    F->printAsOperand(OutStreamer.GetCommentOS(),
                   /*PrintType=*/false, F->getParent());
    OutStreamer.GetCommentOS() << '\n';
  }

  // Emit the prefix data.
  if (F->hasPrefixData())
    EmitGlobalConstant(F->getPrefixData());

  // Emit the CurrentFnSym.  This is a virtual function to allow targets to
  // do their wild and crazy things as required.
  EmitFunctionEntryLabel();

  // If the function had address-taken blocks that got deleted, then we have
  // references to the dangling symbols.  Emit them at the start of the function
  // so that we don't get references to undefined symbols.
  std::vector<MCSymbol*> DeadBlockSyms;
  MMI->takeDeletedSymbolsForFunction(F, DeadBlockSyms);
  for (unsigned i = 0, e = DeadBlockSyms.size(); i != e; ++i) {
    OutStreamer.AddComment("Address taken block that was later removed");
    OutStreamer.EmitLabel(DeadBlockSyms[i]);
  }

  // Emit pre-function debug and/or EH information.
  for (const HandlerInfo &HI : Handlers) {
    NamedRegionTimer T(HI.TimerName, HI.TimerGroupName, TimePassesIsEnabled);
    HI.Handler->beginFunction(MF);
  }

  // Emit the prologue data.
  if (F->hasPrologueData())
    EmitGlobalConstant(F->getPrologueData());
}

/// EmitFunctionEntryLabel - Emit the label that is the entrypoint for the
/// function.  This can be overridden by targets as required to do custom stuff.
void AsmPrinter::EmitFunctionEntryLabel() {
  CurrentFnSym->redefineIfPossible();

  // The function label could have already been emitted if two symbols end up
  // conflicting due to asm renaming.  Detect this and emit an error.
  if (CurrentFnSym->isVariable())
    report_fatal_error("'" + Twine(CurrentFnSym->getName()) +
                       "' is a protected alias");
  if (CurrentFnSym->isDefined())
    report_fatal_error("'" + Twine(CurrentFnSym->getName()) +
                       "' label emitted multiple times to assembly file");

  return OutStreamer.EmitLabel(CurrentFnSym);
}

/// emitComments - Pretty-print comments for instructions.
static void emitComments(const MachineInstr &MI, raw_ostream &CommentOS) {
  const MachineFunction *MF = MI.getParent()->getParent();
  const TargetMachine &TM = MF->getTarget();

  // Check for spills and reloads
  int FI;

  const MachineFrameInfo *FrameInfo = MF->getFrameInfo();

  // We assume a single instruction only has a spill or reload, not
  // both.
  const MachineMemOperand *MMO;
  if (TM.getSubtargetImpl()->getInstrInfo()->isLoadFromStackSlotPostFE(&MI,
                                                                       FI)) {
    if (FrameInfo->isSpillSlotObjectIndex(FI)) {
      MMO = *MI.memoperands_begin();
      CommentOS << MMO->getSize() << "-byte Reload\n";
    }
  } else if (TM.getSubtargetImpl()->getInstrInfo()->hasLoadFromStackSlot(
                 &MI, MMO, FI)) {
    if (FrameInfo->isSpillSlotObjectIndex(FI))
      CommentOS << MMO->getSize() << "-byte Folded Reload\n";
  } else if (TM.getSubtargetImpl()->getInstrInfo()->isStoreToStackSlotPostFE(
                 &MI, FI)) {
    if (FrameInfo->isSpillSlotObjectIndex(FI)) {
      MMO = *MI.memoperands_begin();
      CommentOS << MMO->getSize() << "-byte Spill\n";
    }
  } else if (TM.getSubtargetImpl()->getInstrInfo()->hasStoreToStackSlot(
                 &MI, MMO, FI)) {
    if (FrameInfo->isSpillSlotObjectIndex(FI))
      CommentOS << MMO->getSize() << "-byte Folded Spill\n";
  }

  // Check for spill-induced copies
  if (MI.getAsmPrinterFlag(MachineInstr::ReloadReuse))
    CommentOS << " Reload Reuse\n";
}

/// emitImplicitDef - This method emits the specified machine instruction
/// that is an implicit def.
void AsmPrinter::emitImplicitDef(const MachineInstr *MI) const {
  unsigned RegNo = MI->getOperand(0).getReg();
  OutStreamer.AddComment(
      Twine("implicit-def: ") +
      TM.getSubtargetImpl()->getRegisterInfo()->getName(RegNo));
  OutStreamer.AddBlankLine();
}

static void emitKill(const MachineInstr *MI, AsmPrinter &AP) {
  std::string Str = "kill:";
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &Op = MI->getOperand(i);
    assert(Op.isReg() && "KILL instruction must have only register operands");
    Str += ' ';
    Str += AP.TM.getSubtargetImpl()->getRegisterInfo()->getName(Op.getReg());
    Str += (Op.isDef() ? "<def>" : "<kill>");
  }
  AP.OutStreamer.AddComment(Str);
  AP.OutStreamer.AddBlankLine();
}

/// emitDebugValueComment - This method handles the target-independent form
/// of DBG_VALUE, returning true if it was able to do so.  A false return
/// means the target will need to handle MI in EmitInstruction.
static bool emitDebugValueComment(const MachineInstr *MI, AsmPrinter &AP) {
  // This code handles only the 4-operand target-independent form.
  if (MI->getNumOperands() != 4)
    return false;

  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  OS << "DEBUG_VALUE: ";

  DIVariable V = MI->getDebugVariable();
  if (V.getContext().isSubprogram()) {
    StringRef Name = DISubprogram(V.getContext()).getDisplayName();
    if (!Name.empty())
      OS << Name << ":";
  }
  OS << V.getName();

  DIExpression Expr = MI->getDebugExpression();
  if (Expr.isVariablePiece())
    OS << " [piece offset=" << Expr.getPieceOffset()
       << " size=" << Expr.getPieceSize() << "]";
  OS << " <- ";

  // The second operand is only an offset if it's an immediate.
  bool Deref = MI->getOperand(0).isReg() && MI->getOperand(1).isImm();
  int64_t Offset = Deref ? MI->getOperand(1).getImm() : 0;

  // Register or immediate value. Register 0 means undef.
  if (MI->getOperand(0).isFPImm()) {
    APFloat APF = APFloat(MI->getOperand(0).getFPImm()->getValueAPF());
    if (MI->getOperand(0).getFPImm()->getType()->isFloatTy()) {
      OS << (double)APF.convertToFloat();
    } else if (MI->getOperand(0).getFPImm()->getType()->isDoubleTy()) {
      OS << APF.convertToDouble();
    } else {
      // There is no good way to print long double.  Convert a copy to
      // double.  Ah well, it's only a comment.
      bool ignored;
      APF.convert(APFloat::IEEEdouble, APFloat::rmNearestTiesToEven,
                  &ignored);
      OS << "(long double) " << APF.convertToDouble();
    }
  } else if (MI->getOperand(0).isImm()) {
    OS << MI->getOperand(0).getImm();
  } else if (MI->getOperand(0).isCImm()) {
    MI->getOperand(0).getCImm()->getValue().print(OS, false /*isSigned*/);
  } else {
    unsigned Reg;
    if (MI->getOperand(0).isReg()) {
      Reg = MI->getOperand(0).getReg();
    } else {
      assert(MI->getOperand(0).isFI() && "Unknown operand type");
      const TargetFrameLowering *TFI =
          AP.TM.getSubtargetImpl()->getFrameLowering();
      Offset += TFI->getFrameIndexReference(*AP.MF,
                                            MI->getOperand(0).getIndex(), Reg);
      Deref = true;
    }
    if (Reg == 0) {
      // Suppress offset, it is not meaningful here.
      OS << "undef";
      // NOTE: Want this comment at start of line, don't emit with AddComment.
      AP.OutStreamer.emitRawComment(OS.str());
      return true;
    }
    if (Deref)
      OS << '[';
    OS << AP.TM.getSubtargetImpl()->getRegisterInfo()->getName(Reg);
  }

  if (Deref)
    OS << '+' << Offset << ']';

  // NOTE: Want this comment at start of line, don't emit with AddComment.
  AP.OutStreamer.emitRawComment(OS.str());
  return true;
}

AsmPrinter::CFIMoveType AsmPrinter::needsCFIMoves() {
  if (MAI->getExceptionHandlingType() == ExceptionHandling::DwarfCFI &&
      MF->getFunction()->needsUnwindTableEntry())
    return CFI_M_EH;

  if (MMI->hasDebugInfo())
    return CFI_M_Debug;

  return CFI_M_None;
}

bool AsmPrinter::needsSEHMoves() {
  return MAI->usesWindowsCFI() && MF->getFunction()->needsUnwindTableEntry();
}

void AsmPrinter::emitCFIInstruction(const MachineInstr &MI) {
  ExceptionHandling ExceptionHandlingType = MAI->getExceptionHandlingType();
  if (ExceptionHandlingType != ExceptionHandling::DwarfCFI &&
      ExceptionHandlingType != ExceptionHandling::ARM)
    return;

  if (needsCFIMoves() == CFI_M_None)
    return;

  const MachineModuleInfo &MMI = MF->getMMI();
  const std::vector<MCCFIInstruction> &Instrs = MMI.getFrameInstructions();
  unsigned CFIIndex = MI.getOperand(0).getCFIIndex();
  const MCCFIInstruction &CFI = Instrs[CFIIndex];
  emitCFIInstruction(CFI);
}

void AsmPrinter::emitFrameAlloc(const MachineInstr &MI) {
  // The operands are the MCSymbol and the frame offset of the allocation.
  MCSymbol *FrameAllocSym = MI.getOperand(0).getMCSymbol();
  int FrameOffset = MI.getOperand(1).getImm();

  // Emit a symbol assignment.
  OutStreamer.EmitAssignment(FrameAllocSym,
                             MCConstantExpr::Create(FrameOffset, OutContext));
}

/// EmitFunctionBody - This method emits the body and trailer for a
/// function.
void AsmPrinter::EmitFunctionBody() {
  // Emit target-specific gunk before the function body.
  EmitFunctionBodyStart();

  bool ShouldPrintDebugScopes = MMI->hasDebugInfo();

  // Print out code for the function.
  bool HasAnyRealCode = false;
  for (auto &MBB : *MF) {
    // Print a label for the basic block.
    EmitBasicBlockStart(MBB);
    for (auto &MI : MBB) {

      // Print the assembly for the instruction.
      if (!MI.isPosition() && !MI.isImplicitDef() && !MI.isKill() &&
          !MI.isDebugValue()) {
        HasAnyRealCode = true;
        ++EmittedInsts;
      }

      if (ShouldPrintDebugScopes) {
        for (const HandlerInfo &HI : Handlers) {
          NamedRegionTimer T(HI.TimerName, HI.TimerGroupName,
                             TimePassesIsEnabled);
          HI.Handler->beginInstruction(&MI);
        }
      }

      if (isVerbose())
        emitComments(MI, OutStreamer.GetCommentOS());

      switch (MI.getOpcode()) {
      case TargetOpcode::CFI_INSTRUCTION:
        emitCFIInstruction(MI);
        break;

      case TargetOpcode::FRAME_ALLOC:
        emitFrameAlloc(MI);
        break;

      case TargetOpcode::EH_LABEL:
      case TargetOpcode::GC_LABEL:
        OutStreamer.EmitLabel(MI.getOperand(0).getMCSymbol());
        break;
      case TargetOpcode::INLINEASM:
        EmitInlineAsm(&MI);
        break;
      case TargetOpcode::DBG_VALUE:
        if (isVerbose()) {
          if (!emitDebugValueComment(&MI, *this))
            EmitInstruction(&MI);
        }
        break;
      case TargetOpcode::IMPLICIT_DEF:
        if (isVerbose()) emitImplicitDef(&MI);
        break;
      case TargetOpcode::KILL:
        if (isVerbose()) emitKill(&MI, *this);
        break;
      default:
        EmitInstruction(&MI);
        break;
      }

      if (ShouldPrintDebugScopes) {
        for (const HandlerInfo &HI : Handlers) {
          NamedRegionTimer T(HI.TimerName, HI.TimerGroupName,
                             TimePassesIsEnabled);
          HI.Handler->endInstruction();
        }
      }
    }

    EmitBasicBlockEnd(MBB);
  }

  // If the function is empty and the object file uses .subsections_via_symbols,
  // then we need to emit *something* to the function body to prevent the
  // labels from collapsing together.  Just emit a noop.
  if ((MAI->hasSubsectionsViaSymbols() && !HasAnyRealCode)) {
    MCInst Noop;
    TM.getSubtargetImpl()->getInstrInfo()->getNoopForMachoTarget(Noop);
    OutStreamer.AddComment("avoids zero-length function");

    // Targets can opt-out of emitting the noop here by leaving the opcode
    // unspecified.
    if (Noop.getOpcode())
      OutStreamer.EmitInstruction(Noop, getSubtargetInfo());
  }

  const Function *F = MF->getFunction();
  for (const auto &BB : *F) {
    if (!BB.hasAddressTaken())
      continue;
    MCSymbol *Sym = GetBlockAddressSymbol(&BB);
    if (Sym->isDefined())
      continue;
    OutStreamer.AddComment("Address of block that was removed by CodeGen");
    OutStreamer.EmitLabel(Sym);
  }

  // Emit target-specific gunk after the function body.
  EmitFunctionBodyEnd();

  // If the target wants a .size directive for the size of the function, emit
  // it.
  if (MAI->hasDotTypeDotSizeDirective()) {
    // Create a symbol for the end of function, so we can get the size as
    // difference between the function label and the temp label.
    MCSymbol *FnEndLabel = OutContext.CreateTempSymbol();
    OutStreamer.EmitLabel(FnEndLabel);

    const MCExpr *SizeExp =
      MCBinaryExpr::CreateSub(MCSymbolRefExpr::Create(FnEndLabel, OutContext),
                              MCSymbolRefExpr::Create(CurrentFnSymForSize,
                                                      OutContext),
                              OutContext);
    OutStreamer.EmitELFSize(CurrentFnSym, SizeExp);
  }

  // Emit post-function debug and/or EH information.
  for (const HandlerInfo &HI : Handlers) {
    NamedRegionTimer T(HI.TimerName, HI.TimerGroupName, TimePassesIsEnabled);
    HI.Handler->endFunction(MF);
  }
  MMI->EndFunction();

  // Print out jump tables referenced by the function.
  EmitJumpTableInfo();

  OutStreamer.AddBlankLine();
}

bool AsmPrinter::doFinalization(Module &M) {
  // Emit global variables.
  for (const auto &G : M.globals())
    EmitGlobalVariable(&G);

  // Emit visibility info for declarations
  for (const Function &F : M) {
    if (!F.isDeclaration())
      continue;
    GlobalValue::VisibilityTypes V = F.getVisibility();
    if (V == GlobalValue::DefaultVisibility)
      continue;

    MCSymbol *Name = getSymbol(&F);
    EmitVisibility(Name, V, false);
  }

  // Get information about jump-instruction tables to print.
  JumpInstrTableInfo *JITI = getAnalysisIfAvailable<JumpInstrTableInfo>();

  if (JITI && !JITI->getTables().empty()) {
    unsigned Arch = Triple(getTargetTriple()).getArch();
    bool IsThumb = (Arch == Triple::thumb || Arch == Triple::thumbeb);
    MCInst TrapInst;
    TM.getSubtargetImpl()->getInstrInfo()->getTrap(TrapInst);
    unsigned LogAlignment = llvm::Log2_64(JITI->entryByteAlignment());

    // Emit the right section for these functions.
    OutStreamer.SwitchSection(OutContext.getObjectFileInfo()->getTextSection());
    for (const auto &KV : JITI->getTables()) {
      uint64_t Count = 0;
      for (const auto &FunPair : KV.second) {
        // Emit the function labels to make this be a function entry point.
        MCSymbol *FunSym =
          OutContext.GetOrCreateSymbol(FunPair.second->getName());
        EmitAlignment(LogAlignment);
        if (IsThumb)
          OutStreamer.EmitThumbFunc(FunSym);
        if (MAI->hasDotTypeDotSizeDirective())
          OutStreamer.EmitSymbolAttribute(FunSym, MCSA_ELF_TypeFunction);
        OutStreamer.EmitLabel(FunSym);

        // Emit the jump instruction to transfer control to the original
        // function.
        MCInst JumpToFun;
        MCSymbol *TargetSymbol =
          OutContext.GetOrCreateSymbol(FunPair.first->getName());
        const MCSymbolRefExpr *TargetSymRef =
          MCSymbolRefExpr::Create(TargetSymbol, MCSymbolRefExpr::VK_PLT,
                                  OutContext);
        TM.getSubtargetImpl()->getInstrInfo()->getUnconditionalBranch(
            JumpToFun, TargetSymRef);
        OutStreamer.EmitInstruction(JumpToFun, getSubtargetInfo());
        ++Count;
      }

      // Emit enough padding instructions to fill up to the next power of two.
      uint64_t Remaining = NextPowerOf2(Count) - Count;
      for (uint64_t C = 0; C < Remaining; ++C) {
        EmitAlignment(LogAlignment);
        OutStreamer.EmitInstruction(TrapInst, getSubtargetInfo());
      }

    }
  }

  // Emit module flags.
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);
  if (!ModuleFlags.empty())
    getObjFileLowering().emitModuleFlags(OutStreamer, ModuleFlags, *Mang, TM);

  // Make sure we wrote out everything we need.
  OutStreamer.Flush();

  // Finalize debug and EH information.
  for (const HandlerInfo &HI : Handlers) {
    NamedRegionTimer T(HI.TimerName, HI.TimerGroupName,
                       TimePassesIsEnabled);
    HI.Handler->endModule();
    delete HI.Handler;
  }
  Handlers.clear();
  DD = nullptr;

  // If the target wants to know about weak references, print them all.
  if (MAI->getWeakRefDirective()) {
    // FIXME: This is not lazy, it would be nice to only print weak references
    // to stuff that is actually used.  Note that doing so would require targets
    // to notice uses in operands (due to constant exprs etc).  This should
    // happen with the MC stuff eventually.

    // Print out module-level global variables here.
    for (const auto &G : M.globals()) {
      if (!G.hasExternalWeakLinkage())
        continue;
      OutStreamer.EmitSymbolAttribute(getSymbol(&G), MCSA_WeakReference);
    }

    for (const auto &F : M) {
      if (!F.hasExternalWeakLinkage())
        continue;
      OutStreamer.EmitSymbolAttribute(getSymbol(&F), MCSA_WeakReference);
    }
  }

  OutStreamer.AddBlankLine();
  for (const auto &Alias : M.aliases()) {
    MCSymbol *Name = getSymbol(&Alias);

    if (Alias.hasExternalLinkage() || !MAI->getWeakRefDirective())
      OutStreamer.EmitSymbolAttribute(Name, MCSA_Global);
    else if (Alias.hasWeakLinkage() || Alias.hasLinkOnceLinkage())
      OutStreamer.EmitSymbolAttribute(Name, MCSA_WeakReference);
    else
      assert(Alias.hasLocalLinkage() && "Invalid alias linkage");

    EmitVisibility(Name, Alias.getVisibility());

    // Emit the directives as assignments aka .set:
    OutStreamer.EmitAssignment(Name, lowerConstant(Alias.getAliasee()));
  }

  GCModuleInfo *MI = getAnalysisIfAvailable<GCModuleInfo>();
  assert(MI && "AsmPrinter didn't require GCModuleInfo?");
  for (GCModuleInfo::iterator I = MI->end(), E = MI->begin(); I != E; )
    if (GCMetadataPrinter *MP = GetOrCreateGCPrinter(**--I))
      MP->finishAssembly(M, *MI, *this);

  // Emit llvm.ident metadata in an '.ident' directive.
  EmitModuleIdents(M);

  // Emit __morestack address if needed for indirect calls.
  if (MMI->usesMorestackAddr()) {
    const MCSection *ReadOnlySection =
        getObjFileLowering().getSectionForConstant(SectionKind::getReadOnly(),
                                                   /*C=*/nullptr);
    OutStreamer.SwitchSection(ReadOnlySection);

    MCSymbol *AddrSymbol =
        OutContext.GetOrCreateSymbol(StringRef("__morestack_addr"));
    OutStreamer.EmitLabel(AddrSymbol);

    unsigned PtrSize = TM.getDataLayout()->getPointerSize(0);
    OutStreamer.EmitSymbolValue(GetExternalSymbolSymbol("__morestack"),
                                PtrSize);
  }

  // If we don't have any trampolines, then we don't require stack memory
  // to be executable. Some targets have a directive to declare this.
  Function *InitTrampolineIntrinsic = M.getFunction("llvm.init.trampoline");
  if (!InitTrampolineIntrinsic || InitTrampolineIntrinsic->use_empty())
    if (const MCSection *S = MAI->getNonexecutableStackSection(OutContext))
      OutStreamer.SwitchSection(S);

  // Allow the target to emit any magic that it wants at the end of the file,
  // after everything else has gone out.
  EmitEndOfAsmFile(M);

  delete Mang; Mang = nullptr;
  MMI = nullptr;

  OutStreamer.Finish();
  OutStreamer.reset();

  return false;
}

void AsmPrinter::SetupMachineFunction(MachineFunction &MF) {
  this->MF = &MF;
  // Get the function symbol.
  CurrentFnSym = getSymbol(MF.getFunction());
  CurrentFnSymForSize = CurrentFnSym;

  if (isVerbose())
    LI = &getAnalysis<MachineLoopInfo>();
}

namespace {
  // SectionCPs - Keep track the alignment, constpool entries per Section.
  struct SectionCPs {
    const MCSection *S;
    unsigned Alignment;
    SmallVector<unsigned, 4> CPEs;
    SectionCPs(const MCSection *s, unsigned a) : S(s), Alignment(a) {}
  };
}

/// EmitConstantPool - Print to the current output stream assembly
/// representations of the constants in the constant pool MCP. This is
/// used to print out constants which have been "spilled to memory" by
/// the code generator.
///
void AsmPrinter::EmitConstantPool() {
  const MachineConstantPool *MCP = MF->getConstantPool();
  const std::vector<MachineConstantPoolEntry> &CP = MCP->getConstants();
  if (CP.empty()) return;

  // Calculate sections for constant pool entries. We collect entries to go into
  // the same section together to reduce amount of section switch statements.
  SmallVector<SectionCPs, 4> CPSections;
  for (unsigned i = 0, e = CP.size(); i != e; ++i) {
    const MachineConstantPoolEntry &CPE = CP[i];
    unsigned Align = CPE.getAlignment();

    SectionKind Kind =
        CPE.getSectionKind(TM.getDataLayout());

    const Constant *C = nullptr;
    if (!CPE.isMachineConstantPoolEntry())
      C = CPE.Val.ConstVal;

    const MCSection *S = getObjFileLowering().getSectionForConstant(Kind, C);

    // The number of sections are small, just do a linear search from the
    // last section to the first.
    bool Found = false;
    unsigned SecIdx = CPSections.size();
    while (SecIdx != 0) {
      if (CPSections[--SecIdx].S == S) {
        Found = true;
        break;
      }
    }
    if (!Found) {
      SecIdx = CPSections.size();
      CPSections.push_back(SectionCPs(S, Align));
    }

    if (Align > CPSections[SecIdx].Alignment)
      CPSections[SecIdx].Alignment = Align;
    CPSections[SecIdx].CPEs.push_back(i);
  }

  // Now print stuff into the calculated sections.
  const MCSection *CurSection = nullptr;
  unsigned Offset = 0;
  for (unsigned i = 0, e = CPSections.size(); i != e; ++i) {
    for (unsigned j = 0, ee = CPSections[i].CPEs.size(); j != ee; ++j) {
      unsigned CPI = CPSections[i].CPEs[j];
      MCSymbol *Sym = GetCPISymbol(CPI);
      if (!Sym->isUndefined())
        continue;

      if (CurSection != CPSections[i].S) {
        OutStreamer.SwitchSection(CPSections[i].S);
        EmitAlignment(Log2_32(CPSections[i].Alignment));
        CurSection = CPSections[i].S;
        Offset = 0;
      }

      MachineConstantPoolEntry CPE = CP[CPI];

      // Emit inter-object padding for alignment.
      unsigned AlignMask = CPE.getAlignment() - 1;
      unsigned NewOffset = (Offset + AlignMask) & ~AlignMask;
      OutStreamer.EmitZeros(NewOffset - Offset);

      Type *Ty = CPE.getType();
      Offset = NewOffset +
               TM.getDataLayout()->getTypeAllocSize(Ty);

      OutStreamer.EmitLabel(Sym);
      if (CPE.isMachineConstantPoolEntry())
        EmitMachineConstantPoolValue(CPE.Val.MachineCPVal);
      else
        EmitGlobalConstant(CPE.Val.ConstVal);
    }
  }
}

/// EmitJumpTableInfo - Print assembly representations of the jump tables used
/// by the current function to the current output stream.
///
void AsmPrinter::EmitJumpTableInfo() {
  const DataLayout *DL = MF->getTarget().getDataLayout();
  const MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
  if (!MJTI) return;
  if (MJTI->getEntryKind() == MachineJumpTableInfo::EK_Inline) return;
  const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
  if (JT.empty()) return;

  // Pick the directive to use to print the jump table entries, and switch to
  // the appropriate section.
  const Function *F = MF->getFunction();
  bool JTInDiffSection = false;
  if (// In PIC mode, we need to emit the jump table to the same section as the
      // function body itself, otherwise the label differences won't make sense.
      // FIXME: Need a better predicate for this: what about custom entries?
      MJTI->getEntryKind() == MachineJumpTableInfo::EK_LabelDifference32 ||
      // We should also do if the section name is NULL or function is declared
      // in discardable section
      // FIXME: this isn't the right predicate, should be based on the MCSection
      // for the function.
      F->isWeakForLinker()) {
    OutStreamer.SwitchSection(
        getObjFileLowering().SectionForGlobal(F, *Mang, TM));
  } else {
    // Otherwise, drop it in the readonly section.
    const MCSection *ReadOnlySection =
        getObjFileLowering().getSectionForConstant(SectionKind::getReadOnly(),
                                                   /*C=*/nullptr);
    OutStreamer.SwitchSection(ReadOnlySection);
    JTInDiffSection = true;
  }

  EmitAlignment(Log2_32(
      MJTI->getEntryAlignment(*TM.getDataLayout())));

  // Jump tables in code sections are marked with a data_region directive
  // where that's supported.
  if (!JTInDiffSection)
    OutStreamer.EmitDataRegion(MCDR_DataRegionJT32);

  for (unsigned JTI = 0, e = JT.size(); JTI != e; ++JTI) {
    const std::vector<MachineBasicBlock*> &JTBBs = JT[JTI].MBBs;

    // If this jump table was deleted, ignore it.
    if (JTBBs.empty()) continue;

    // For the EK_LabelDifference32 entry, if using .set avoids a relocation,
    /// emit a .set directive for each unique entry.
    if (MJTI->getEntryKind() == MachineJumpTableInfo::EK_LabelDifference32 &&
        MAI->doesSetDirectiveSuppressesReloc()) {
      SmallPtrSet<const MachineBasicBlock*, 16> EmittedSets;
      const TargetLowering *TLI = TM.getSubtargetImpl()->getTargetLowering();
      const MCExpr *Base = TLI->getPICJumpTableRelocBaseExpr(MF,JTI,OutContext);
      for (unsigned ii = 0, ee = JTBBs.size(); ii != ee; ++ii) {
        const MachineBasicBlock *MBB = JTBBs[ii];
        if (!EmittedSets.insert(MBB).second)
          continue;

        // .set LJTSet, LBB32-base
        const MCExpr *LHS =
          MCSymbolRefExpr::Create(MBB->getSymbol(), OutContext);
        OutStreamer.EmitAssignment(GetJTSetSymbol(JTI, MBB->getNumber()),
                                MCBinaryExpr::CreateSub(LHS, Base, OutContext));
      }
    }

    // On some targets (e.g. Darwin) we want to emit two consecutive labels
    // before each jump table.  The first label is never referenced, but tells
    // the assembler and linker the extents of the jump table object.  The
    // second label is actually referenced by the code.
    if (JTInDiffSection && DL->hasLinkerPrivateGlobalPrefix())
      // FIXME: This doesn't have to have any specific name, just any randomly
      // named and numbered 'l' label would work.  Simplify GetJTISymbol.
      OutStreamer.EmitLabel(GetJTISymbol(JTI, true));

    OutStreamer.EmitLabel(GetJTISymbol(JTI));

    for (unsigned ii = 0, ee = JTBBs.size(); ii != ee; ++ii)
      EmitJumpTableEntry(MJTI, JTBBs[ii], JTI);
  }
  if (!JTInDiffSection)
    OutStreamer.EmitDataRegion(MCDR_DataRegionEnd);
}

/// EmitJumpTableEntry - Emit a jump table entry for the specified MBB to the
/// current stream.
void AsmPrinter::EmitJumpTableEntry(const MachineJumpTableInfo *MJTI,
                                    const MachineBasicBlock *MBB,
                                    unsigned UID) const {
  assert(MBB && MBB->getNumber() >= 0 && "Invalid basic block");
  const MCExpr *Value = nullptr;
  switch (MJTI->getEntryKind()) {
  case MachineJumpTableInfo::EK_Inline:
    llvm_unreachable("Cannot emit EK_Inline jump table entry");
  case MachineJumpTableInfo::EK_Custom32:
    Value =
        TM.getSubtargetImpl()->getTargetLowering()->LowerCustomJumpTableEntry(
            MJTI, MBB, UID, OutContext);
    break;
  case MachineJumpTableInfo::EK_BlockAddress:
    // EK_BlockAddress - Each entry is a plain address of block, e.g.:
    //     .word LBB123
    Value = MCSymbolRefExpr::Create(MBB->getSymbol(), OutContext);
    break;
  case MachineJumpTableInfo::EK_GPRel32BlockAddress: {
    // EK_GPRel32BlockAddress - Each entry is an address of block, encoded
    // with a relocation as gp-relative, e.g.:
    //     .gprel32 LBB123
    MCSymbol *MBBSym = MBB->getSymbol();
    OutStreamer.EmitGPRel32Value(MCSymbolRefExpr::Create(MBBSym, OutContext));
    return;
  }

  case MachineJumpTableInfo::EK_GPRel64BlockAddress: {
    // EK_GPRel64BlockAddress - Each entry is an address of block, encoded
    // with a relocation as gp-relative, e.g.:
    //     .gpdword LBB123
    MCSymbol *MBBSym = MBB->getSymbol();
    OutStreamer.EmitGPRel64Value(MCSymbolRefExpr::Create(MBBSym, OutContext));
    return;
  }

  case MachineJumpTableInfo::EK_LabelDifference32: {
    // Each entry is the address of the block minus the address of the jump
    // table. This is used for PIC jump tables where gprel32 is not supported.
    // e.g.:
    //      .word LBB123 - LJTI1_2
    // If the .set directive avoids relocations, this is emitted as:
    //      .set L4_5_set_123, LBB123 - LJTI1_2
    //      .word L4_5_set_123
    if (MAI->doesSetDirectiveSuppressesReloc()) {
      Value = MCSymbolRefExpr::Create(GetJTSetSymbol(UID, MBB->getNumber()),
                                      OutContext);
      break;
    }
    Value = MCSymbolRefExpr::Create(MBB->getSymbol(), OutContext);
    const TargetLowering *TLI = TM.getSubtargetImpl()->getTargetLowering();
    const MCExpr *Base = TLI->getPICJumpTableRelocBaseExpr(MF, UID, OutContext);
    Value = MCBinaryExpr::CreateSub(Value, Base, OutContext);
    break;
  }
  }

  assert(Value && "Unknown entry kind!");

  unsigned EntrySize =
      MJTI->getEntrySize(*TM.getDataLayout());
  OutStreamer.EmitValue(Value, EntrySize);
}


/// EmitSpecialLLVMGlobal - Check to see if the specified global is a
/// special global used by LLVM.  If so, emit it and return true, otherwise
/// do nothing and return false.
bool AsmPrinter::EmitSpecialLLVMGlobal(const GlobalVariable *GV) {
  if (GV->getName() == "llvm.used") {
    if (MAI->hasNoDeadStrip())    // No need to emit this at all.
      EmitLLVMUsedList(cast<ConstantArray>(GV->getInitializer()));
    return true;
  }

  // Ignore debug and non-emitted data.  This handles llvm.compiler.used.
  if (StringRef(GV->getSection()) == "llvm.metadata" ||
      GV->hasAvailableExternallyLinkage())
    return true;

  if (!GV->hasAppendingLinkage()) return false;

  assert(GV->hasInitializer() && "Not a special LLVM global!");

  if (GV->getName() == "llvm.global_ctors") {
    EmitXXStructorList(GV->getInitializer(), /* isCtor */ true);

    if (TM.getRelocationModel() == Reloc::Static &&
        MAI->hasStaticCtorDtorReferenceInStaticMode()) {
      StringRef Sym(".constructors_used");
      OutStreamer.EmitSymbolAttribute(OutContext.GetOrCreateSymbol(Sym),
                                      MCSA_Reference);
    }
    return true;
  }

  if (GV->getName() == "llvm.global_dtors") {
    EmitXXStructorList(GV->getInitializer(), /* isCtor */ false);

    if (TM.getRelocationModel() == Reloc::Static &&
        MAI->hasStaticCtorDtorReferenceInStaticMode()) {
      StringRef Sym(".destructors_used");
      OutStreamer.EmitSymbolAttribute(OutContext.GetOrCreateSymbol(Sym),
                                      MCSA_Reference);
    }
    return true;
  }

  return false;
}

/// EmitLLVMUsedList - For targets that define a MAI::UsedDirective, mark each
/// global in the specified llvm.used list for which emitUsedDirectiveFor
/// is true, as being used with this directive.
void AsmPrinter::EmitLLVMUsedList(const ConstantArray *InitList) {
  // Should be an array of 'i8*'.
  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i) {
    const GlobalValue *GV =
      dyn_cast<GlobalValue>(InitList->getOperand(i)->stripPointerCasts());
    if (GV)
      OutStreamer.EmitSymbolAttribute(getSymbol(GV), MCSA_NoDeadStrip);
  }
}

namespace {
struct Structor {
  Structor() : Priority(0), Func(nullptr), ComdatKey(nullptr) {}
  int Priority;
  llvm::Constant *Func;
  llvm::GlobalValue *ComdatKey;
};
} // end namespace

/// EmitXXStructorList - Emit the ctor or dtor list taking into account the init
/// priority.
void AsmPrinter::EmitXXStructorList(const Constant *List, bool isCtor) {
  // Should be an array of '{ int, void ()* }' structs.  The first value is the
  // init priority.
  if (!isa<ConstantArray>(List)) return;

  // Sanity check the structors list.
  const ConstantArray *InitList = dyn_cast<ConstantArray>(List);
  if (!InitList) return; // Not an array!
  StructType *ETy = dyn_cast<StructType>(InitList->getType()->getElementType());
  // FIXME: Only allow the 3-field form in LLVM 4.0.
  if (!ETy || ETy->getNumElements() < 2 || ETy->getNumElements() > 3)
    return; // Not an array of two or three elements!
  if (!isa<IntegerType>(ETy->getTypeAtIndex(0U)) ||
      !isa<PointerType>(ETy->getTypeAtIndex(1U))) return; // Not (int, ptr).
  if (ETy->getNumElements() == 3 && !isa<PointerType>(ETy->getTypeAtIndex(2U)))
    return; // Not (int, ptr, ptr).

  // Gather the structors in a form that's convenient for sorting by priority.
  SmallVector<Structor, 8> Structors;
  for (Value *O : InitList->operands()) {
    ConstantStruct *CS = dyn_cast<ConstantStruct>(O);
    if (!CS) continue; // Malformed.
    if (CS->getOperand(1)->isNullValue())
      break;  // Found a null terminator, skip the rest.
    ConstantInt *Priority = dyn_cast<ConstantInt>(CS->getOperand(0));
    if (!Priority) continue; // Malformed.
    Structors.push_back(Structor());
    Structor &S = Structors.back();
    S.Priority = Priority->getLimitedValue(65535);
    S.Func = CS->getOperand(1);
    if (ETy->getNumElements() == 3 && !CS->getOperand(2)->isNullValue())
      S.ComdatKey = dyn_cast<GlobalValue>(CS->getOperand(2)->stripPointerCasts());
  }

  // Emit the function pointers in the target-specific order
  const DataLayout *DL = TM.getDataLayout();
  unsigned Align = Log2_32(DL->getPointerPrefAlignment());
  std::stable_sort(Structors.begin(), Structors.end(),
                   [](const Structor &L,
                      const Structor &R) { return L.Priority < R.Priority; });
  for (Structor &S : Structors) {
    const TargetLoweringObjectFile &Obj = getObjFileLowering();
    const MCSymbol *KeySym = nullptr;
    if (GlobalValue *GV = S.ComdatKey) {
      if (GV->hasAvailableExternallyLinkage())
        // If the associated variable is available_externally, some other TU
        // will provide its dynamic initializer.
        continue;

      KeySym = getSymbol(GV);
    }
    const MCSection *OutputSection =
        (isCtor ? Obj.getStaticCtorSection(S.Priority, KeySym)
                : Obj.getStaticDtorSection(S.Priority, KeySym));
    OutStreamer.SwitchSection(OutputSection);
    if (OutStreamer.getCurrentSection() != OutStreamer.getPreviousSection())
      EmitAlignment(Align);
    EmitXXStructor(S.Func);
  }
}

void AsmPrinter::EmitModuleIdents(Module &M) {
  if (!MAI->hasIdentDirective())
    return;

  if (const NamedMDNode *NMD = M.getNamedMetadata("llvm.ident")) {
    for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
      const MDNode *N = NMD->getOperand(i);
      assert(N->getNumOperands() == 1 &&
             "llvm.ident metadata entry can have only one operand");
      const MDString *S = cast<MDString>(N->getOperand(0));
      OutStreamer.EmitIdent(S->getString());
    }
  }
}

//===--------------------------------------------------------------------===//
// Emission and print routines
//

/// EmitInt8 - Emit a byte directive and value.
///
void AsmPrinter::EmitInt8(int Value) const {
  OutStreamer.EmitIntValue(Value, 1);
}

/// EmitInt16 - Emit a short directive and value.
///
void AsmPrinter::EmitInt16(int Value) const {
  OutStreamer.EmitIntValue(Value, 2);
}

/// EmitInt32 - Emit a long directive and value.
///
void AsmPrinter::EmitInt32(int Value) const {
  OutStreamer.EmitIntValue(Value, 4);
}

/// Emit something like ".long Hi-Lo" where the size in bytes of the directive
/// is specified by Size and Hi/Lo specify the labels. This implicitly uses
/// .set if it avoids relocations.
void AsmPrinter::EmitLabelDifference(const MCSymbol *Hi, const MCSymbol *Lo,
                                     unsigned Size) const {
  // Get the Hi-Lo expression.
  const MCExpr *Diff =
    MCBinaryExpr::CreateSub(MCSymbolRefExpr::Create(Hi, OutContext),
                            MCSymbolRefExpr::Create(Lo, OutContext),
                            OutContext);

  if (!MAI->doesSetDirectiveSuppressesReloc()) {
    OutStreamer.EmitValue(Diff, Size);
    return;
  }

  // Otherwise, emit with .set (aka assignment).
  MCSymbol *SetLabel = GetTempSymbol("set", SetCounter++);
  OutStreamer.EmitAssignment(SetLabel, Diff);
  OutStreamer.EmitSymbolValue(SetLabel, Size);
}

/// EmitLabelPlusOffset - Emit something like ".long Label+Offset"
/// where the size in bytes of the directive is specified by Size and Label
/// specifies the label.  This implicitly uses .set if it is available.
void AsmPrinter::EmitLabelPlusOffset(const MCSymbol *Label, uint64_t Offset,
                                     unsigned Size,
                                     bool IsSectionRelative) const {
  if (MAI->needsDwarfSectionOffsetDirective() && IsSectionRelative) {
    OutStreamer.EmitCOFFSecRel32(Label);
    return;
  }

  // Emit Label+Offset (or just Label if Offset is zero)
  const MCExpr *Expr = MCSymbolRefExpr::Create(Label, OutContext);
  if (Offset)
    Expr = MCBinaryExpr::CreateAdd(
        Expr, MCConstantExpr::Create(Offset, OutContext), OutContext);

  OutStreamer.EmitValue(Expr, Size);
}

//===----------------------------------------------------------------------===//

// EmitAlignment - Emit an alignment directive to the specified power of
// two boundary.  For example, if you pass in 3 here, you will get an 8
// byte alignment.  If a global value is specified, and if that global has
// an explicit alignment requested, it will override the alignment request
// if required for correctness.
//
void AsmPrinter::EmitAlignment(unsigned NumBits, const GlobalObject *GV) const {
  if (GV)
    NumBits = getGVAlignmentLog2(GV, *TM.getDataLayout(),
                                 NumBits);

  if (NumBits == 0) return;   // 1-byte aligned: no need to emit alignment.

  assert(NumBits <
             static_cast<unsigned>(std::numeric_limits<unsigned>::digits) &&
         "undefined behavior");
  if (getCurrentSection()->getKind().isText())
    OutStreamer.EmitCodeAlignment(1u << NumBits);
  else
    OutStreamer.EmitValueToAlignment(1u << NumBits);
}

//===----------------------------------------------------------------------===//
// Constant emission.
//===----------------------------------------------------------------------===//

const MCExpr *AsmPrinter::lowerConstant(const Constant *CV) {
  MCContext &Ctx = OutContext;

  if (CV->isNullValue() || isa<UndefValue>(CV))
    return MCConstantExpr::Create(0, Ctx);

  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CV))
    return MCConstantExpr::Create(CI->getZExtValue(), Ctx);

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(CV))
    return MCSymbolRefExpr::Create(getSymbol(GV), Ctx);

  if (const BlockAddress *BA = dyn_cast<BlockAddress>(CV))
    return MCSymbolRefExpr::Create(GetBlockAddressSymbol(BA), Ctx);

  const ConstantExpr *CE = dyn_cast<ConstantExpr>(CV);
  if (!CE) {
    llvm_unreachable("Unknown constant value to lower!");
  }

  if (const MCExpr *RelocExpr
      = getObjFileLowering().getExecutableRelativeSymbol(CE, *Mang, TM))
    return RelocExpr;

  switch (CE->getOpcode()) {
  default:
    // If the code isn't optimized, there may be outstanding folding
    // opportunities. Attempt to fold the expression using DataLayout as a
    // last resort before giving up.
    if (Constant *C = ConstantFoldConstantExpression(
            CE, TM.getDataLayout()))
      if (C != CE)
        return lowerConstant(C);

    // Otherwise report the problem to the user.
    {
      std::string S;
      raw_string_ostream OS(S);
      OS << "Unsupported expression in static initializer: ";
      CE->printAsOperand(OS, /*PrintType=*/false,
                     !MF ? nullptr : MF->getFunction()->getParent());
      report_fatal_error(OS.str());
    }
  case Instruction::GetElementPtr: {
    const DataLayout &DL = *TM.getDataLayout();

    // Generate a symbolic expression for the byte address
    APInt OffsetAI(DL.getPointerTypeSizeInBits(CE->getType()), 0);
    cast<GEPOperator>(CE)->accumulateConstantOffset(DL, OffsetAI);

    const MCExpr *Base = lowerConstant(CE->getOperand(0));
    if (!OffsetAI)
      return Base;

    int64_t Offset = OffsetAI.getSExtValue();
    return MCBinaryExpr::CreateAdd(Base, MCConstantExpr::Create(Offset, Ctx),
                                   Ctx);
  }

  case Instruction::Trunc:
    // We emit the value and depend on the assembler to truncate the generated
    // expression properly.  This is important for differences between
    // blockaddress labels.  Since the two labels are in the same function, it
    // is reasonable to treat their delta as a 32-bit value.
    // FALL THROUGH.
  case Instruction::BitCast:
    return lowerConstant(CE->getOperand(0));

  case Instruction::IntToPtr: {
    const DataLayout &DL = *TM.getDataLayout();

    // Handle casts to pointers by changing them into casts to the appropriate
    // integer type.  This promotes constant folding and simplifies this code.
    Constant *Op = CE->getOperand(0);
    Op = ConstantExpr::getIntegerCast(Op, DL.getIntPtrType(CV->getType()),
                                      false/*ZExt*/);
    return lowerConstant(Op);
  }

  case Instruction::PtrToInt: {
    const DataLayout &DL = *TM.getDataLayout();

    // Support only foldable casts to/from pointers that can be eliminated by
    // changing the pointer to the appropriately sized integer type.
    Constant *Op = CE->getOperand(0);
    Type *Ty = CE->getType();

    const MCExpr *OpExpr = lowerConstant(Op);

    // We can emit the pointer value into this slot if the slot is an
    // integer slot equal to the size of the pointer.
    if (DL.getTypeAllocSize(Ty) == DL.getTypeAllocSize(Op->getType()))
      return OpExpr;

    // Otherwise the pointer is smaller than the resultant integer, mask off
    // the high bits so we are sure to get a proper truncation if the input is
    // a constant expr.
    unsigned InBits = DL.getTypeAllocSizeInBits(Op->getType());
    const MCExpr *MaskExpr = MCConstantExpr::Create(~0ULL >> (64-InBits), Ctx);
    return MCBinaryExpr::CreateAnd(OpExpr, MaskExpr, Ctx);
  }

  // The MC library also has a right-shift operator, but it isn't consistently
  // signed or unsigned between different targets.
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::Shl:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    const MCExpr *LHS = lowerConstant(CE->getOperand(0));
    const MCExpr *RHS = lowerConstant(CE->getOperand(1));
    switch (CE->getOpcode()) {
    default: llvm_unreachable("Unknown binary operator constant cast expr");
    case Instruction::Add: return MCBinaryExpr::CreateAdd(LHS, RHS, Ctx);
    case Instruction::Sub: return MCBinaryExpr::CreateSub(LHS, RHS, Ctx);
    case Instruction::Mul: return MCBinaryExpr::CreateMul(LHS, RHS, Ctx);
    case Instruction::SDiv: return MCBinaryExpr::CreateDiv(LHS, RHS, Ctx);
    case Instruction::SRem: return MCBinaryExpr::CreateMod(LHS, RHS, Ctx);
    case Instruction::Shl: return MCBinaryExpr::CreateShl(LHS, RHS, Ctx);
    case Instruction::And: return MCBinaryExpr::CreateAnd(LHS, RHS, Ctx);
    case Instruction::Or:  return MCBinaryExpr::CreateOr (LHS, RHS, Ctx);
    case Instruction::Xor: return MCBinaryExpr::CreateXor(LHS, RHS, Ctx);
    }
  }
  }
}

static void emitGlobalConstantImpl(const Constant *C, AsmPrinter &AP);

/// isRepeatedByteSequence - Determine whether the given value is
/// composed of a repeated sequence of identical bytes and return the
/// byte value.  If it is not a repeated sequence, return -1.
static int isRepeatedByteSequence(const ConstantDataSequential *V) {
  StringRef Data = V->getRawDataValues();
  assert(!Data.empty() && "Empty aggregates should be CAZ node");
  char C = Data[0];
  for (unsigned i = 1, e = Data.size(); i != e; ++i)
    if (Data[i] != C) return -1;
  return static_cast<uint8_t>(C); // Ensure 255 is not returned as -1.
}


/// isRepeatedByteSequence - Determine whether the given value is
/// composed of a repeated sequence of identical bytes and return the
/// byte value.  If it is not a repeated sequence, return -1.
static int isRepeatedByteSequence(const Value *V, TargetMachine &TM) {

  if (const ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getBitWidth() > 64) return -1;

    uint64_t Size =
        TM.getDataLayout()->getTypeAllocSize(V->getType());
    uint64_t Value = CI->getZExtValue();

    // Make sure the constant is at least 8 bits long and has a power
    // of 2 bit width.  This guarantees the constant bit width is
    // always a multiple of 8 bits, avoiding issues with padding out
    // to Size and other such corner cases.
    if (CI->getBitWidth() < 8 || !isPowerOf2_64(CI->getBitWidth())) return -1;

    uint8_t Byte = static_cast<uint8_t>(Value);

    for (unsigned i = 1; i < Size; ++i) {
      Value >>= 8;
      if (static_cast<uint8_t>(Value) != Byte) return -1;
    }
    return Byte;
  }
  if (const ConstantArray *CA = dyn_cast<ConstantArray>(V)) {
    // Make sure all array elements are sequences of the same repeated
    // byte.
    assert(CA->getNumOperands() != 0 && "Should be a CAZ");
    int Byte = isRepeatedByteSequence(CA->getOperand(0), TM);
    if (Byte == -1) return -1;

    for (unsigned i = 1, e = CA->getNumOperands(); i != e; ++i) {
      int ThisByte = isRepeatedByteSequence(CA->getOperand(i), TM);
      if (ThisByte == -1) return -1;
      if (Byte != ThisByte) return -1;
    }
    return Byte;
  }

  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(V))
    return isRepeatedByteSequence(CDS);

  return -1;
}

static void emitGlobalConstantDataSequential(const ConstantDataSequential *CDS,
                                             AsmPrinter &AP){

  // See if we can aggregate this into a .fill, if so, emit it as such.
  int Value = isRepeatedByteSequence(CDS, AP.TM);
  if (Value != -1) {
    uint64_t Bytes =
        AP.TM.getDataLayout()->getTypeAllocSize(
            CDS->getType());
    // Don't emit a 1-byte object as a .fill.
    if (Bytes > 1)
      return AP.OutStreamer.EmitFill(Bytes, Value);
  }

  // If this can be emitted with .ascii/.asciz, emit it as such.
  if (CDS->isString())
    return AP.OutStreamer.EmitBytes(CDS->getAsString());

  // Otherwise, emit the values in successive locations.
  unsigned ElementByteSize = CDS->getElementByteSize();
  if (isa<IntegerType>(CDS->getElementType())) {
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      if (AP.isVerbose())
        AP.OutStreamer.GetCommentOS() << format("0x%" PRIx64 "\n",
                                                CDS->getElementAsInteger(i));
      AP.OutStreamer.EmitIntValue(CDS->getElementAsInteger(i),
                                  ElementByteSize);
    }
  } else if (ElementByteSize == 4) {
    // FP Constants are printed as integer constants to avoid losing
    // precision.
    assert(CDS->getElementType()->isFloatTy());
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      union {
        float F;
        uint32_t I;
      };

      F = CDS->getElementAsFloat(i);
      if (AP.isVerbose())
        AP.OutStreamer.GetCommentOS() << "float " << F << '\n';
      AP.OutStreamer.EmitIntValue(I, 4);
    }
  } else {
    assert(CDS->getElementType()->isDoubleTy());
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      union {
        double F;
        uint64_t I;
      };

      F = CDS->getElementAsDouble(i);
      if (AP.isVerbose())
        AP.OutStreamer.GetCommentOS() << "double " << F << '\n';
      AP.OutStreamer.EmitIntValue(I, 8);
    }
  }

  const DataLayout &DL = *AP.TM.getDataLayout();
  unsigned Size = DL.getTypeAllocSize(CDS->getType());
  unsigned EmittedSize = DL.getTypeAllocSize(CDS->getType()->getElementType()) *
                        CDS->getNumElements();
  if (unsigned Padding = Size - EmittedSize)
    AP.OutStreamer.EmitZeros(Padding);

}

static void emitGlobalConstantArray(const ConstantArray *CA, AsmPrinter &AP) {
  // See if we can aggregate some values.  Make sure it can be
  // represented as a series of bytes of the constant value.
  int Value = isRepeatedByteSequence(CA, AP.TM);

  if (Value != -1) {
    uint64_t Bytes =
        AP.TM.getDataLayout()->getTypeAllocSize(
            CA->getType());
    AP.OutStreamer.EmitFill(Bytes, Value);
  }
  else {
    for (unsigned i = 0, e = CA->getNumOperands(); i != e; ++i)
      emitGlobalConstantImpl(CA->getOperand(i), AP);
  }
}

static void emitGlobalConstantVector(const ConstantVector *CV, AsmPrinter &AP) {
  for (unsigned i = 0, e = CV->getType()->getNumElements(); i != e; ++i)
    emitGlobalConstantImpl(CV->getOperand(i), AP);

  const DataLayout &DL = *AP.TM.getDataLayout();
  unsigned Size = DL.getTypeAllocSize(CV->getType());
  unsigned EmittedSize = DL.getTypeAllocSize(CV->getType()->getElementType()) *
                         CV->getType()->getNumElements();
  if (unsigned Padding = Size - EmittedSize)
    AP.OutStreamer.EmitZeros(Padding);
}

static void emitGlobalConstantStruct(const ConstantStruct *CS, AsmPrinter &AP) {
  // Print the fields in successive locations. Pad to align if needed!
  const DataLayout *DL = AP.TM.getDataLayout();
  unsigned Size = DL->getTypeAllocSize(CS->getType());
  const StructLayout *Layout = DL->getStructLayout(CS->getType());
  uint64_t SizeSoFar = 0;
  for (unsigned i = 0, e = CS->getNumOperands(); i != e; ++i) {
    const Constant *Field = CS->getOperand(i);

    // Check if padding is needed and insert one or more 0s.
    uint64_t FieldSize = DL->getTypeAllocSize(Field->getType());
    uint64_t PadSize = ((i == e-1 ? Size : Layout->getElementOffset(i+1))
                        - Layout->getElementOffset(i)) - FieldSize;
    SizeSoFar += FieldSize + PadSize;

    // Now print the actual field value.
    emitGlobalConstantImpl(Field, AP);

    // Insert padding - this may include padding to increase the size of the
    // current field up to the ABI size (if the struct is not packed) as well
    // as padding to ensure that the next field starts at the right offset.
    AP.OutStreamer.EmitZeros(PadSize);
  }
  assert(SizeSoFar == Layout->getSizeInBytes() &&
         "Layout of constant struct may be incorrect!");
}

static void emitGlobalConstantFP(const ConstantFP *CFP, AsmPrinter &AP) {
  APInt API = CFP->getValueAPF().bitcastToAPInt();

  // First print a comment with what we think the original floating-point value
  // should have been.
  if (AP.isVerbose()) {
    SmallString<8> StrVal;
    CFP->getValueAPF().toString(StrVal);

    if (CFP->getType())
      CFP->getType()->print(AP.OutStreamer.GetCommentOS());
    else
      AP.OutStreamer.GetCommentOS() << "Printing <null> Type";
    AP.OutStreamer.GetCommentOS() << ' ' << StrVal << '\n';
  }

  // Now iterate through the APInt chunks, emitting them in endian-correct
  // order, possibly with a smaller chunk at beginning/end (e.g. for x87 80-bit
  // floats).
  unsigned NumBytes = API.getBitWidth() / 8;
  unsigned TrailingBytes = NumBytes % sizeof(uint64_t);
  const uint64_t *p = API.getRawData();

  // PPC's long double has odd notions of endianness compared to how LLVM
  // handles it: p[0] goes first for *big* endian on PPC.
  if (AP.TM.getDataLayout()->isBigEndian() &&
      !CFP->getType()->isPPC_FP128Ty()) {
    int Chunk = API.getNumWords() - 1;

    if (TrailingBytes)
      AP.OutStreamer.EmitIntValue(p[Chunk--], TrailingBytes);

    for (; Chunk >= 0; --Chunk)
      AP.OutStreamer.EmitIntValue(p[Chunk], sizeof(uint64_t));
  } else {
    unsigned Chunk;
    for (Chunk = 0; Chunk < NumBytes / sizeof(uint64_t); ++Chunk)
      AP.OutStreamer.EmitIntValue(p[Chunk], sizeof(uint64_t));

    if (TrailingBytes)
      AP.OutStreamer.EmitIntValue(p[Chunk], TrailingBytes);
  }

  // Emit the tail padding for the long double.
  const DataLayout &DL = *AP.TM.getDataLayout();
  AP.OutStreamer.EmitZeros(DL.getTypeAllocSize(CFP->getType()) -
                           DL.getTypeStoreSize(CFP->getType()));
}

static void emitGlobalConstantLargeInt(const ConstantInt *CI, AsmPrinter &AP) {
  const DataLayout *DL = AP.TM.getDataLayout();
  unsigned BitWidth = CI->getBitWidth();

  // Copy the value as we may massage the layout for constants whose bit width
  // is not a multiple of 64-bits.
  APInt Realigned(CI->getValue());
  uint64_t ExtraBits = 0;
  unsigned ExtraBitsSize = BitWidth & 63;

  if (ExtraBitsSize) {
    // The bit width of the data is not a multiple of 64-bits.
    // The extra bits are expected to be at the end of the chunk of the memory.
    // Little endian:
    // * Nothing to be done, just record the extra bits to emit.
    // Big endian:
    // * Record the extra bits to emit.
    // * Realign the raw data to emit the chunks of 64-bits.
    if (DL->isBigEndian()) {
      // Basically the structure of the raw data is a chunk of 64-bits cells:
      //    0        1         BitWidth / 64
      // [chunk1][chunk2] ... [chunkN].
      // The most significant chunk is chunkN and it should be emitted first.
      // However, due to the alignment issue chunkN contains useless bits.
      // Realign the chunks so that they contain only useless information:
      // ExtraBits     0       1       (BitWidth / 64) - 1
      //       chu[nk1 chu][nk2 chu] ... [nkN-1 chunkN]
      ExtraBits = Realigned.getRawData()[0] &
        (((uint64_t)-1) >> (64 - ExtraBitsSize));
      Realigned = Realigned.lshr(ExtraBitsSize);
    } else
      ExtraBits = Realigned.getRawData()[BitWidth / 64];
  }

  // We don't expect assemblers to support integer data directives
  // for more than 64 bits, so we emit the data in at most 64-bit
  // quantities at a time.
  const uint64_t *RawData = Realigned.getRawData();
  for (unsigned i = 0, e = BitWidth / 64; i != e; ++i) {
    uint64_t Val = DL->isBigEndian() ? RawData[e - i - 1] : RawData[i];
    AP.OutStreamer.EmitIntValue(Val, 8);
  }

  if (ExtraBitsSize) {
    // Emit the extra bits after the 64-bits chunks.

    // Emit a directive that fills the expected size.
    uint64_t Size = AP.TM.getDataLayout()->getTypeAllocSize(
        CI->getType());
    Size -= (BitWidth / 64) * 8;
    assert(Size && Size * 8 >= ExtraBitsSize &&
           (ExtraBits & (((uint64_t)-1) >> (64 - ExtraBitsSize)))
           == ExtraBits && "Directive too small for extra bits.");
    AP.OutStreamer.EmitIntValue(ExtraBits, Size);
  }
}

static void emitGlobalConstantImpl(const Constant *CV, AsmPrinter &AP) {
  const DataLayout *DL = AP.TM.getDataLayout();
  uint64_t Size = DL->getTypeAllocSize(CV->getType());
  if (isa<ConstantAggregateZero>(CV) || isa<UndefValue>(CV))
    return AP.OutStreamer.EmitZeros(Size);

  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CV)) {
    switch (Size) {
    case 1:
    case 2:
    case 4:
    case 8:
      if (AP.isVerbose())
        AP.OutStreamer.GetCommentOS() << format("0x%" PRIx64 "\n",
                                                CI->getZExtValue());
      AP.OutStreamer.EmitIntValue(CI->getZExtValue(), Size);
      return;
    default:
      emitGlobalConstantLargeInt(CI, AP);
      return;
    }
  }

  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CV))
    return emitGlobalConstantFP(CFP, AP);

  if (isa<ConstantPointerNull>(CV)) {
    AP.OutStreamer.EmitIntValue(0, Size);
    return;
  }

  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(CV))
    return emitGlobalConstantDataSequential(CDS, AP);

  if (const ConstantArray *CVA = dyn_cast<ConstantArray>(CV))
    return emitGlobalConstantArray(CVA, AP);

  if (const ConstantStruct *CVS = dyn_cast<ConstantStruct>(CV))
    return emitGlobalConstantStruct(CVS, AP);

  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(CV)) {
    // Look through bitcasts, which might not be able to be MCExpr'ized (e.g. of
    // vectors).
    if (CE->getOpcode() == Instruction::BitCast)
      return emitGlobalConstantImpl(CE->getOperand(0), AP);

    if (Size > 8) {
      // If the constant expression's size is greater than 64-bits, then we have
      // to emit the value in chunks. Try to constant fold the value and emit it
      // that way.
      Constant *New = ConstantFoldConstantExpression(CE, DL);
      if (New && New != CE)
        return emitGlobalConstantImpl(New, AP);
    }
  }

  if (const ConstantVector *V = dyn_cast<ConstantVector>(CV))
    return emitGlobalConstantVector(V, AP);

  // Otherwise, it must be a ConstantExpr.  Lower it to an MCExpr, then emit it
  // thread the streamer with EmitValue.
  AP.OutStreamer.EmitValue(AP.lowerConstant(CV), Size);
}

/// EmitGlobalConstant - Print a general LLVM constant to the .s file.
void AsmPrinter::EmitGlobalConstant(const Constant *CV) {
  uint64_t Size =
      TM.getDataLayout()->getTypeAllocSize(CV->getType());
  if (Size)
    emitGlobalConstantImpl(CV, *this);
  else if (MAI->hasSubsectionsViaSymbols()) {
    // If the global has zero size, emit a single byte so that two labels don't
    // look like they are at the same location.
    OutStreamer.EmitIntValue(0, 1);
  }
}

void AsmPrinter::EmitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) {
  // Target doesn't support this yet!
  llvm_unreachable("Target does not support EmitMachineConstantPoolValue");
}

void AsmPrinter::printOffset(int64_t Offset, raw_ostream &OS) const {
  if (Offset > 0)
    OS << '+' << Offset;
  else if (Offset < 0)
    OS << Offset;
}

//===----------------------------------------------------------------------===//
// Symbol Lowering Routines.
//===----------------------------------------------------------------------===//

/// GetTempSymbol - Return the MCSymbol corresponding to the assembler
/// temporary label with the specified stem and unique ID.
MCSymbol *AsmPrinter::GetTempSymbol(Twine Name, unsigned ID) const {
  const DataLayout *DL = TM.getDataLayout();
  return OutContext.GetOrCreateSymbol(Twine(DL->getPrivateGlobalPrefix()) +
                                      Name + Twine(ID));
}

/// GetTempSymbol - Return an assembler temporary label with the specified
/// stem.
MCSymbol *AsmPrinter::GetTempSymbol(Twine Name) const {
  const DataLayout *DL = TM.getDataLayout();
  return OutContext.GetOrCreateSymbol(Twine(DL->getPrivateGlobalPrefix())+
                                      Name);
}


MCSymbol *AsmPrinter::GetBlockAddressSymbol(const BlockAddress *BA) const {
  return MMI->getAddrLabelSymbol(BA->getBasicBlock());
}

MCSymbol *AsmPrinter::GetBlockAddressSymbol(const BasicBlock *BB) const {
  return MMI->getAddrLabelSymbol(BB);
}

/// GetCPISymbol - Return the symbol for the specified constant pool entry.
MCSymbol *AsmPrinter::GetCPISymbol(unsigned CPID) const {
  const DataLayout *DL = TM.getDataLayout();
  return OutContext.GetOrCreateSymbol
    (Twine(DL->getPrivateGlobalPrefix()) + "CPI" + Twine(getFunctionNumber())
     + "_" + Twine(CPID));
}

/// GetJTISymbol - Return the symbol for the specified jump table entry.
MCSymbol *AsmPrinter::GetJTISymbol(unsigned JTID, bool isLinkerPrivate) const {
  return MF->getJTISymbol(JTID, OutContext, isLinkerPrivate);
}

/// GetJTSetSymbol - Return the symbol for the specified jump table .set
/// FIXME: privatize to AsmPrinter.
MCSymbol *AsmPrinter::GetJTSetSymbol(unsigned UID, unsigned MBBID) const {
  const DataLayout *DL = TM.getDataLayout();
  return OutContext.GetOrCreateSymbol
  (Twine(DL->getPrivateGlobalPrefix()) + Twine(getFunctionNumber()) + "_" +
   Twine(UID) + "_set_" + Twine(MBBID));
}

MCSymbol *AsmPrinter::getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                                   StringRef Suffix) const {
  return getObjFileLowering().getSymbolWithGlobalValueBase(GV, Suffix, *Mang,
                                                           TM);
}

/// GetExternalSymbolSymbol - Return the MCSymbol for the specified
/// ExternalSymbol.
MCSymbol *AsmPrinter::GetExternalSymbolSymbol(StringRef Sym) const {
  SmallString<60> NameStr;
  Mang->getNameWithPrefix(NameStr, Sym);
  return OutContext.GetOrCreateSymbol(NameStr.str());
}



/// PrintParentLoopComment - Print comments about parent loops of this one.
static void PrintParentLoopComment(raw_ostream &OS, const MachineLoop *Loop,
                                   unsigned FunctionNumber) {
  if (!Loop) return;
  PrintParentLoopComment(OS, Loop->getParentLoop(), FunctionNumber);
  OS.indent(Loop->getLoopDepth()*2)
    << "Parent Loop BB" << FunctionNumber << "_"
    << Loop->getHeader()->getNumber()
    << " Depth=" << Loop->getLoopDepth() << '\n';
}


/// PrintChildLoopComment - Print comments about child loops within
/// the loop for this basic block, with nesting.
static void PrintChildLoopComment(raw_ostream &OS, const MachineLoop *Loop,
                                  unsigned FunctionNumber) {
  // Add child loop information
  for (const MachineLoop *CL : *Loop) {
    OS.indent(CL->getLoopDepth()*2)
      << "Child Loop BB" << FunctionNumber << "_"
      << CL->getHeader()->getNumber() << " Depth " << CL->getLoopDepth()
      << '\n';
    PrintChildLoopComment(OS, CL, FunctionNumber);
  }
}

/// emitBasicBlockLoopComments - Pretty-print comments for basic blocks.
static void emitBasicBlockLoopComments(const MachineBasicBlock &MBB,
                                       const MachineLoopInfo *LI,
                                       const AsmPrinter &AP) {
  // Add loop depth information
  const MachineLoop *Loop = LI->getLoopFor(&MBB);
  if (!Loop) return;

  MachineBasicBlock *Header = Loop->getHeader();
  assert(Header && "No header for loop");

  // If this block is not a loop header, just print out what is the loop header
  // and return.
  if (Header != &MBB) {
    AP.OutStreamer.AddComment("  in Loop: Header=BB" +
                              Twine(AP.getFunctionNumber())+"_" +
                              Twine(Loop->getHeader()->getNumber())+
                              " Depth="+Twine(Loop->getLoopDepth()));
    return;
  }

  // Otherwise, it is a loop header.  Print out information about child and
  // parent loops.
  raw_ostream &OS = AP.OutStreamer.GetCommentOS();

  PrintParentLoopComment(OS, Loop->getParentLoop(), AP.getFunctionNumber());

  OS << "=>";
  OS.indent(Loop->getLoopDepth()*2-2);

  OS << "This ";
  if (Loop->empty())
    OS << "Inner ";
  OS << "Loop Header: Depth=" + Twine(Loop->getLoopDepth()) << '\n';

  PrintChildLoopComment(OS, Loop, AP.getFunctionNumber());
}


/// EmitBasicBlockStart - This method prints the label for the specified
/// MachineBasicBlock, an alignment (if present) and a comment describing
/// it if appropriate.
void AsmPrinter::EmitBasicBlockStart(const MachineBasicBlock &MBB) const {
  // Emit an alignment directive for this block, if needed.
  if (unsigned Align = MBB.getAlignment())
    EmitAlignment(Align);

  // If the block has its address taken, emit any labels that were used to
  // reference the block.  It is possible that there is more than one label
  // here, because multiple LLVM BB's may have been RAUW'd to this block after
  // the references were generated.
  if (MBB.hasAddressTaken()) {
    const BasicBlock *BB = MBB.getBasicBlock();
    if (isVerbose())
      OutStreamer.AddComment("Block address taken");

    std::vector<MCSymbol*> Symbols = MMI->getAddrLabelSymbolToEmit(BB);
    for (auto *Sym : Symbols)
      OutStreamer.EmitLabel(Sym);
  }

  // Print some verbose block comments.
  if (isVerbose()) {
    if (const BasicBlock *BB = MBB.getBasicBlock())
      if (BB->hasName())
        OutStreamer.AddComment("%" + BB->getName());
    emitBasicBlockLoopComments(MBB, LI, *this);
  }

  // Print the main label for the block.
  if (MBB.pred_empty() || isBlockOnlyReachableByFallthrough(&MBB)) {
    if (isVerbose()) {
      // NOTE: Want this comment at start of line, don't emit with AddComment.
      OutStreamer.emitRawComment(" BB#" + Twine(MBB.getNumber()) + ":", false);
    }
  } else {
    OutStreamer.EmitLabel(MBB.getSymbol());
  }
}

void AsmPrinter::EmitVisibility(MCSymbol *Sym, unsigned Visibility,
                                bool IsDefinition) const {
  MCSymbolAttr Attr = MCSA_Invalid;

  switch (Visibility) {
  default: break;
  case GlobalValue::HiddenVisibility:
    if (IsDefinition)
      Attr = MAI->getHiddenVisibilityAttr();
    else
      Attr = MAI->getHiddenDeclarationVisibilityAttr();
    break;
  case GlobalValue::ProtectedVisibility:
    Attr = MAI->getProtectedVisibilityAttr();
    break;
  }

  if (Attr != MCSA_Invalid)
    OutStreamer.EmitSymbolAttribute(Sym, Attr);
}

/// isBlockOnlyReachableByFallthough - Return true if the basic block has
/// exactly one predecessor and the control transfer mechanism between
/// the predecessor and this block is a fall-through.
bool AsmPrinter::
isBlockOnlyReachableByFallthrough(const MachineBasicBlock *MBB) const {
  // If this is a landing pad, it isn't a fall through.  If it has no preds,
  // then nothing falls through to it.
  if (MBB->isLandingPad() || MBB->pred_empty())
    return false;

  // If there isn't exactly one predecessor, it can't be a fall through.
  if (MBB->pred_size() > 1)
    return false;

  // The predecessor has to be immediately before this block.
  MachineBasicBlock *Pred = *MBB->pred_begin();
  if (!Pred->isLayoutSuccessor(MBB))
    return false;

  // If the block is completely empty, then it definitely does fall through.
  if (Pred->empty())
    return true;

  // Check the terminators in the previous blocks
  for (const auto &MI : Pred->terminators()) {
    // If it is not a simple branch, we are in a table somewhere.
    if (!MI.isBranch() || MI.isIndirectBranch())
      return false;

    // If we are the operands of one of the branches, this is not a fall
    // through. Note that targets with delay slots will usually bundle
    // terminators with the delay slot instruction.
    for (ConstMIBundleOperands OP(&MI); OP.isValid(); ++OP) {
      if (OP->isJTI())
        return false;
      if (OP->isMBB() && OP->getMBB() == MBB)
        return false;
    }
  }

  return true;
}



GCMetadataPrinter *AsmPrinter::GetOrCreateGCPrinter(GCStrategy &S) {
  if (!S.usesMetadata())
    return nullptr;

  assert(!S.useStatepoints() && "statepoints do not currently support custom"
         " stackmap formats, please see the documentation for a description of"
         " the default format.  If you really need a custom serialized format,"
         " please file a bug");

  gcp_map_type &GCMap = getGCMap(GCMetadataPrinters);
  gcp_map_type::iterator GCPI = GCMap.find(&S);
  if (GCPI != GCMap.end())
    return GCPI->second.get();

  const char *Name = S.getName().c_str();

  for (GCMetadataPrinterRegistry::iterator
         I = GCMetadataPrinterRegistry::begin(),
         E = GCMetadataPrinterRegistry::end(); I != E; ++I)
    if (strcmp(Name, I->getName()) == 0) {
      std::unique_ptr<GCMetadataPrinter> GMP = I->instantiate();
      GMP->S = &S;
      auto IterBool = GCMap.insert(std::make_pair(&S, std::move(GMP)));
      return IterBool.first->second.get();
    }

  report_fatal_error("no GCMetadataPrinter registered for GC: " + Twine(Name));
}

/// Pin vtable to this file.
AsmPrinterHandler::~AsmPrinterHandler() {}
