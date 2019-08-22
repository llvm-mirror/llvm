//===-- EVMAsmPrinter.cpp - EVM LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the EVM assembly language.
//
//===----------------------------------------------------------------------===//

#include "EVM.h"
#include "EVMMCInstLower.h"
#include "InstPrinter/EVMInstPrinter.h"
#include "EVMTargetMachine.h"
#include "EVMUtils.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

#include "llvm/MC/MCInstBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "evm-asm-printer"

namespace {
class EVMAsmPrinter : public AsmPrinter {
public:
  explicit EVMAsmPrinter(TargetMachine &TM,
                           std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  void EmitStartOfAsmFile(Module &M) override;
  //void EmitEndOfAsmFile(Module &M) override;

  //void EmitFunctionEntryLabel() override;

  StringRef getPassName() const override { return "EVM Assembly Printer"; }

  void EmitInstruction(const MachineInstr *MI) override;

  void printOperand(const MachineInstr *MI, unsigned OpNo, raw_ostream &OS);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

  // void EmitToStreamer(MCStreamer &S, const MCInst &Inst);
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

  // Wrapper needed for tblgenned pseudo lowering.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const {
    return LowerEVMMachineOperandToMCOperand(MO, MCOp, *this);
    }
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  void emitInitializeFreeMemoryPointer() const;
  void emitNonPayableCheck(const Function &F, MCSymbol *CallDataUnpackerLabel) const;
  void emitRetrieveConstructorParameters() const;
  void emitConstructorBody() const;
  void emitCopyRuntimeCodeToMemory() const;
  void emitReturnRuntimeCode() const;
  void emitContractParameters() const;
  void emitFunctionWrapper(const Function &F);
  void emitCallDataUnpacker(const Function &F, MCSymbol *CallDataUnpackerLabel);
  void emitMemoryReturner(const Function &F) const;
  void genearteFunctionBodyLabels(Module &M);

  void appendDelegatecallCheck(Module& M);
  void initializeContext(Module& M);
  void appendFunctionSelector(Module& M);
  void appendMissingFunctions(Module& M);
  void appendCallValueCheck(Module &M);
  void emitCallDataCheck(Module &M, MCSymbol *notFoundTag);
  void appendInternalSelector(Module &M, MCSymbol *notFoundTag);
  void appendCalldataLoad(Module &M);
  void collectDataUnpackerEntryPoints(Module &M);
  void appendCallDataUnpacker(Module &M);

  void appendFunctionWrappers(Module &M);
  void appendReturnValuePacker(Function &M);

  // helper functions
  void appendConditionalJumpTo(MCSymbol* jumpTag) const;
  void appendJumpTo(MCSymbol* jumpTag) const;
  void appendRevert() const;

  DenseMap<const Function*, MCSymbol *> funcWrapperMap;
  DenseMap<const Function*, uint32_t> funcHashMap;
  DenseMap<const Function*, MCSymbol *> funcBodyMap;
  std::unique_ptr<MCSubtargetInfo> STI;
};
}

void EVMAsmPrinter::collectDataUnpackerEntryPoints(Module &M) {
  for (Function &F : M.functions()) {
    
  }
  llvm_unreachable("unimplemented");
}

bool EVMAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // emit the function body label before we emit the function.
  MCSymbol* funcBodySymbol = this->funcBodyMap[&MF.getFunction()];
  assert(funcBodySymbol != nullptr);
  OutStreamer->EmitLabel(funcBodySymbol);

  return AsmPrinter::runOnMachineFunction(MF);
}

void EVMAsmPrinter::appendReturnValuePacker(Function &F) {
  // TODO: more cases
  Type* ty = F.getReturnType();

  // empty types, will simply return
  if (ty->getTypeID() == Type::VoidTyID) {
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::STOP), *STI);
    return;
  }

  // fetch free memory pointer
  OutStreamer->EmitInstruction(
      MCInstBuilder(EVM::PUSH2).addImm(EVMSubtarget::getFreeMemoryPointer()),
      *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MLOAD), *STI);

  // toSizeAfterFreeMemoryPointer
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP2), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SUB), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::RETURN), *STI);
  llvm_unreachable("Unimplemented");
}

void EVMAsmPrinter::appendFunctionWrappers(Module &M) {
  // TODO: more cases
  for (Function &F : M.functions()) {

    // Return tag is used to jump out of the function
    MCSymbol *returnTag =
        MMI->getContext().getOrCreateSymbol(F.getName() + "_return");

    if (std::distance(F.arg_begin(), F.arg_end()) != 0) {
      unsigned offset = EVMSubtarget::getDataStartOffset();
      OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(offset),
                                   *STI);
      OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
      OutStreamer->EmitInstruction(MCInstBuilder(EVM::SUB), *STI);

      // jump to function body:
      assert(funcBodyMap.find(&F) != funcBodyMap.end());
      MCSymbol *bodySymbol = funcBodyMap[&F];
      const MCExpr *bodyExpr =
          MCSymbolRefExpr::create(bodySymbol, MMI->getContext());
      OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(bodyExpr),
                                   *STI);
      OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMP), *STI);
    }

    OutStreamer->EmitLabel(returnTag);

		// Return tag and input parameters get consumed.
    // TODO

    appendReturnValuePacker(F);
  }
}

void EVMAsmPrinter::appendInternalSelector(Module &M, MCSymbol *notFoundTag) {
  // TODO: split can be implemented
  for (Function &F : M.functions()) {
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(1), *STI);
    // get hash
    assert(funcHashMap.find(&F) != funcHashMap.end());
    uint32_t hash = funcHashMap[&F];
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH4).addImm(hash), *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::EQ), *STI);

    // get entryp point
    assert(funcWrapperMap.find(&F) != funcWrapperMap.end());
    MCSymbol *funcWrapperSymbol = this->funcWrapperMap[&F];
    appendConditionalJumpTo(funcWrapperSymbol);
  }

  appendJumpTo(notFoundTag);
}

void EVMAsmPrinter::appendMissingFunctions(Module &M) {
  llvm_unreachable("unimplemented");
}

void EVMAsmPrinter::appendCalldataLoad(Module &M) {
  unsigned offset = EVMSubtarget::getDataStartOffset();
  // CALLDATALOAD @ dataStartOffset
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(offset), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLDATALOAD), *STI);

  // calculate right shift amount
  unsigned shrAmount = (32 - offset) * 8;
  // at this moment, num of bytes to load == 4
  assert(shrAmount == 224);

  // right shift
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(shrAmount), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SHR), *STI);
}

void EVMAsmPrinter::appendConditionalJumpTo(MCSymbol *S) const {
  const MCExpr *se =
      MCSymbolRefExpr::create(S, MMI->getContext());
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(se), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPI), *STI);
}

void EVMAsmPrinter::appendJumpTo(MCSymbol *S) const {
  const MCExpr *se =
      MCSymbolRefExpr::create(S, MMI->getContext());
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(se), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMP), *STI);
}

void EVMAsmPrinter::appendRevert() const {
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x0), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::REVERT), *STI);
}

void EVMAsmPrinter::appendFunctionSelector(Module &M) {
  bool needToAddCallValueCheck = true;
  if (!EVMSubtarget::hasPayableFunctions(M) &&
      !EVMSubtarget::hasInterfaceFunctions(M) &&
      !EVMSubtarget::moduleIsLibrary(M)) {
        appendCallValueCheck(M);
        needToAddCallValueCheck = false;
  }

  MCSymbol *notFoundTag = MMI->getContext().getOrCreateSymbol("NotFound");
  emitCallDataCheck(M, notFoundTag);

  if (EVMSubtarget::hasInterfaceFunctions(M)) {
    OutStreamer->AddComment("Retrieve function signature hash");
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x0), *STI);
    appendCalldataLoad(M);

    // TOOD: collect DataUnpackerEntryPoints.
    collectDataUnpackerEntryPoints(M);
    // stack now is: <can-call-non-view-functions>? <funhash>

    // appendInternalSelector(callDataUnpackerEntryPoints, sortedIDs, notFoundTag);
    appendInternalSelector(M, notFoundTag);
  }

  OutStreamer->EmitLabel(notFoundTag);

  Function* fallback = EVMSubtarget::getFallbackFunction(M);
  if (fallback != nullptr) {
    assert(!EVMSubtarget::moduleIsLibrary(M) && "");

    OutStreamer->AddComment("Fallback function");
    if (!EVMSubtarget::isPayableFunction(fallback) && needToAddCallValueCheck) {
      appendCallValueCheck(M);
    }

    // TODO: emit fallback function here
    llvm_unreachable("need to implement emit of fallback function");
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::STOP), *STI);
  } else {
    OutStreamer->AddComment("no fallback function, revert.");
    appendRevert();
  }

  // TODO: emit interface function wrappers. At this moment we see all functions
  // as interface functions. consider not generating static-linked functions
  // (and see them as internal functions)
}

void EVMAsmPrinter::emitCallDataCheck(Module &M, MCSymbol *notFoundTag) {
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x04), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLDATASIZE), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::LT), *STI);
  appendConditionalJumpTo(notFoundTag);
}

void EVMAsmPrinter::appendCallValueCheck(Module &M) {
  OutStreamer->AddComment("Callvalue check");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLVALUE), *STI);

  MCSymbol *fallthrough_tag =
      MMI->getContext().getOrCreateSymbol("cv_check_false");

  OutStreamer->EmitInstruction(
      MCInstBuilder(EVM::PUSH2)
          .addExpr(MCSymbolRefExpr::create(fallthrough_tag, MMI->getContext())),
      *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPI), *STI);

  appendRevert();

  OutStreamer->EmitLabel(fallthrough_tag);
}

void EVMAsmPrinter::emitMemoryReturner(const Function &F) const {
  // if the function does not return anything, skip.
  Type* retType = F.getReturnType();
  if (retType->isVoidTy()) {
    return;
  }

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);

  emitInitializeFreeMemoryPointer();

  // TODO:

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SUB), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x20), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::ADD), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::RETURN), *STI);
}

void EVMAsmPrinter::genearteFunctionBodyLabels(Module &M) {
  for (const auto &F : M) {
    MCSymbol *funcBodyLabel =
      MMI->getContext().getOrCreateSymbol(F.getName() + "_funcbody");
    this->funcBodyMap.insert(
        std::pair<const Function *, MCSymbol *>(&F, funcBodyLabel));
  }
}

void EVMAsmPrinter::emitInitializeFreeMemoryPointer() const {
  OutStreamer->AddComment("Free memory pointer");
  // Initialize the Free memory pointer
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x80), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x40), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MSTORE), *STI);
}

void EVMAsmPrinter::emitNonPayableCheck(const Function &F,
                                        MCSymbol *CallDataUnpackerLabel) const {
  OutStreamer->AddComment("non-payable check");

  // Non-payable check
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLVALUE), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::ISZERO), *STI);

  const MCExpr *cdulabel =
      MCSymbolRefExpr::create(CallDataUnpackerLabel,MMI->getContext());

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(cdulabel), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPI), *STI);

  appendRevert();
}

void EVMAsmPrinter::emitRetrieveConstructorParameters() const {
  // this reads 
  OutStreamer->AddComment("Retrieve constructor parameters");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::POP), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x40), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MLOAD), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x20), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);

  // this PUSH2 should read in the beginning of data section (data section is
  // after text section) and in data section we store contract operands
  // TODO:
  
  // TODO: enable PUSH2
  //OutStreamer->AddComment("Parameter");
  //MCExpr* params; // TODO
  //OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(params), *STI);

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP4), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CODECOPY), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP2), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::ADD), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x40), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP2), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MSTORE), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MLOAD), *STI);
}

void EVMAsmPrinter::emitConstructorBody() const {
  OutStreamer->AddComment("Constructor body");
  // TODO: fill in Constructor Body
  // we might want to look for the constructor body in the function list. 
}

void EVMAsmPrinter::emitCopyRuntimeCodeToMemory() const {
  OutStreamer->AddComment("Copy runtime Code to memory");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addImm(0x01d1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addImm(0x0046), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x00), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CODECOPY), *STI);
}

void EVMAsmPrinter::emitReturnRuntimeCode() const {
  OutStreamer->AddComment("Return from runtime code");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x00), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::RETURN), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::STOP), *STI);
}

void EVMAsmPrinter::emitContractParameters() const {
  OutStreamer->AddComment("Contract parameters");
  // TODO
}

void EVMAsmPrinter::emitCallDataUnpacker(const Function &F,
                                         MCSymbol *CallDataUnpackerLabel) {
  OutStreamer->AddComment("Call data unpacker");
  OutStreamer->EmitLabel(CallDataUnpackerLabel);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::POP), *STI);

  // Call data unpacker needs to push parameters on to stack
  for (auto &arg : F.args()) {
    // TODO: special handling for addres types (masking out unnecessary bits)
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x04), *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLDATALOAD), *STI);
  }
  
  MCSymbol *funcBodyLabel = this->funcBodyMap[&F];
  const MCExpr* funcBodyTag =
      MCSymbolRefExpr::create(funcBodyLabel, MMI->getContext());

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(funcBodyTag), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMP), *STI);
}

void EVMAsmPrinter::emitFunctionWrapper(const Function &F) {
  OutStreamer->AddComment("Function wrapper(s)");

  MCSymbol *funcWrapperLabel = this->funcWrapperMap[&F];
  OutStreamer->EmitLabel(funcWrapperLabel);

  // This is used for inner jumping (between nonpayable check and unpacker)
  MCSymbol *S =
      MMI->getContext().getOrCreateSymbol(F.getName() + "_calldataunpacker");

  emitNonPayableCheck(F, S);
  emitCallDataUnpacker(F, S);
}

void EVMAsmPrinter::initializeContext(Module &M) {
  // TODO: check Solidity:
  // libsolidity/codegen/ContractCompiler.cpp
  emitInitializeFreeMemoryPointer();
}

void EVMAsmPrinter::appendDelegatecallCheck(Module &M) {
  llvm_unreachable("unimplemented");
}

void EVMAsmPrinter::EmitStartOfAsmFile(Module &M) {
  STI = std::unique_ptr<MCSubtargetInfo>(TM.getTarget().createMCSubtargetInfo(
      TM.getTargetTriple().str(), TM.getTargetCPU(),
      TM.getTargetFeatureString()));
  //AsmPrinter::EmitStartOfAsmFile(M);
  //return;
  // Emit the skeleton of the asm file in text section.
  OutStreamer->SwitchSection(getObjFileLowering().getTextSection());

  OutStreamer->AddComment("Contract creation");

  // TODO
  bool isLibrary = false;
  if (isLibrary) {
    appendDelegatecallCheck(M);
  }

  initializeContext(M);

  appendFunctionSelector(M);

  appendMissingFunctions(M);
}

//void EVMAsmPrinter::EmitEndOfAsmFile(Module &M) { }

void EVMAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  EVMMCInstLower MCInstLowering(OutContext, *this);
  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

void EVMAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNo,
                                 raw_ostream &OS) {
  const MachineOperand &MO = MI->getOperand(OpNo);

  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    break;
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    break;
  case MachineOperand::MO_CImmediate:
    OS << MO.getCImm();
    break;
  case MachineOperand::MO_MachineBasicBlock:
    OS << *MO.getMBB()->getSymbol();
    break;
  case MachineOperand::MO_BlockAddress: {
    MCSymbol *Sym = GetBlockAddressSymbol(MO.getBlockAddress());
    Sym->print(OS, MAI);
    break;
  }
  default:
    llvm_unreachable("Not implemented yet!");
  }
}

bool EVMAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                    const char *ExtraCode, raw_ostream &OS) {
  printOperand(MI, OpNo, OS);
  return true;
}

bool EVMAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &OS) {

  return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, OS);
}

// Force static initialization.
extern "C" void LLVMInitializeEVMAsmPrinter() {
  RegisterAsmPrinter<EVMAsmPrinter> Y(getTheEVMTarget());
}
