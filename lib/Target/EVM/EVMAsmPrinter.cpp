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

  //void EmitStartOfAsmFile(Module &M) override;

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
  //bool runOnMachineFunction(MachineFunction &MF) override;

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

  void appendDelegatecallCheck(Module& M);
  void initializeContext(Module& M);
  void appendFunctionSelector(Module& M);
  void appendMissingFunctions(Module& M);
  void appendCallValueCheck(Module &M);
  void emitCallDataSizeCheck(Module &M, MCSymbol *notFoundTag);
  void appendInternalSelector(Module &M, MCSymbol *notFoundTag);
  void appendCalldataLoad(Module &M);
  void collectDataUnpackerEntryPoints(Module &M);
  void appendCallDataUnpacker(Module &M);

  void appendFunctionWrapper(Function &F);
  void appendReturnValuePacker(Function &M);
  void appendABIDecode(Function &F) const;

  // helper functions
  void appendConditionalJumpTo(MCSymbol* jumpTag) const;
  void appendJumpTo(MCSymbol* jumpTag) const;
  void appendRevert() const;
  void appendConditionalRevert(MCSymbol *S) const;

  void appendLabel(std::string name) const;
  void pushSymbol(MCSymbol *s) const;
  MCSymbol* createSymbol(std::string name) const;

  DenseMap<const Function*, MCSymbol *> funcWrapperMap;
  DenseMap<const Function*, uint32_t> funcHashMap;
  DenseMap<const Function*, MCSymbol *> funcBodyMap;
  std::unique_ptr<MCSubtargetInfo> STI;
  MCContext* ctx;
};
}

MCSymbol* EVMAsmPrinter::createSymbol(std::string name) const {
  assert(!ctx->lookupSymbol(name));
  return ctx->getOrCreateSymbol(name);
}

void EVMAsmPrinter::collectDataUnpackerEntryPoints(Module &M) {
  for (Function &F : M.functions()) {
    uint32_t funcSig = EVM::getFunctionSignature(&F);
    funcHashMap.insert(std::pair<const Function *, uint32_t>(&F, funcSig));
  }
}

/*
bool EVMAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  return AsmPrinter::runOnMachineFunction(MF);
  // emit the function body label before we emit the function.
  MCSymbol* funcBodySymbol = this->funcBodyMap[&MF.getFunction()];
  assert(funcBodySymbol != nullptr);
  OutStreamer->EmitLabel(funcBodySymbol);

  return AsmPrinter::runOnMachineFunction(MF);
}
*/

void EVMAsmPrinter::appendLabel(std::string name) const {
  MCSymbol *symbol = createSymbol(name);
  OutStreamer->EmitLabel(symbol);
}

void EVMAsmPrinter::appendReturnValuePacker(Function &F) {
  OutStreamer->AddComment("return value packer");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);
  // TODO: more cases
  Type* ty = F.getReturnType();

  // we do not support aggregated return types, yet.
  assert(!ty->isAggregateType());

  // empty types, will simply return
  if (ty->getTypeID() == Type::VoidTyID) {
    OutStreamer->AddComment("there are no return values");
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::STOP), *STI);
    return;
  }

  OutStreamer->AddComment("handle return results");
  // TODO: we will simply use 0-32bytes memory to return the results.
  // fetch free memory pointer
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x0), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::MSTORE), *STI);

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(32), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::RETURN), *STI);
}

void EVMAsmPrinter::appendFunctionWrapper(Function &F) {
  // cannot do external linkage in a module, at least for now
  //assert(!F.hasExternalLinkage());

  assert(funcWrapperMap.find(&F) != funcWrapperMap.end());
  OutStreamer->EmitLabel(funcWrapperMap[&F]);

  // Return tag is used to jump out of the function
  MCSymbol *returnTag = createSymbol(F.getName().str() + "_wrapper_return");

  bool hasArguments = std::distance(F.arg_begin(), F.arg_end()) != 0;
  if (hasArguments) {
    OutStreamer->AddComment("handle arguments");

    // Parameter for calldataUnpacker
    unsigned offset = EVMSubtarget::getDataStartOffset();
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(offset),
                                 *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLDATASIZE), *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::SUB), *STI);

    appendABIDecode(F);
  }

  // jump to function body
  OutStreamer->AddComment("jump to function body");
  pushSymbol(returnTag);
  appendJumpTo(funcBodyMap[&F]);
  OutStreamer->EmitLabel(returnTag);
  appendReturnValuePacker(F);
}


void EVMAsmPrinter::appendABIDecode(Function &F) const {
  // TODO: calculate encodedSize
  unsigned encodedSize = EVM::getEncodedSize(F);
  assert(encodedSize < 256); // If it excceds 255 then we will have to use PUSH2

  appendLabel(F.getName().str() + "_abidecode");

  // if lt(len, encodedSize) { revert(0, 0)}
  OutStreamer->AddComment("check encoded size");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(encodedSize),
                               *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::LT), *STI);

  MCSymbol *abidecodeTag = createSymbol(F.getName().str() + "_abidecode_pass");
  appendConditionalJumpTo(abidecodeTag);

  appendRevert();
  OutStreamer->EmitLabel(abidecodeTag);

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP2), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::ADD), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::SWAP1), *STI);
	/// Stack: <input_end> <source_offset>

  // Retain the offset pointer as base_offset, the point from which the data
  // offsets are computed.
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);

  // Return tag is used to jump out of the function
  MCSymbol *returnTag =
      createSymbol(F.getName().str() + "_function_return");

  // TODO: iterate over function parameters
  unsigned argCounter = 0;
  for (auto &arg : F.args()) {
    OutStreamer->AddComment("arg no." + argCounter++);




  }
}

void EVMAsmPrinter::appendInternalSelector(Module &M, MCSymbol *notFoundTag) {
    appendLabel("internal_function_selector");

  // TODO: split can be implemented
  for (Function &F : M.functions()) {
    OutStreamer->AddComment("selecting function: " + F.getName());
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
    // get hash
    assert(funcHashMap.find(&F) != funcHashMap.end());
    uint32_t hash = funcHashMap[&F];
    OutStreamer->AddComment("check function hash.");
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH4).addImm(hash), *STI);
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::EQ), *STI);

    MCSymbol *funcWrapperSymbol = createSymbol(F.getName().str() + "_wrapper");
    funcWrapperMap.insert(std::pair<Function *, MCSymbol *>(&F, funcWrapperSymbol));
    appendConditionalJumpTo(funcWrapperSymbol);
  }

  OutStreamer->AddComment("No funcs matched");
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

void EVMAsmPrinter::pushSymbol(MCSymbol *S) const {
  const MCExpr *SExpr = MCSymbolRefExpr::create(S, MMI->getContext());
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH2).addExpr(SExpr), *STI);
}

void EVMAsmPrinter::appendConditionalJumpTo(MCSymbol *S) const {
  pushSymbol(S);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPI), *STI);
}

void EVMAsmPrinter::appendJumpTo(MCSymbol *S) const {
  pushSymbol(S);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMP), *STI);
}

void EVMAsmPrinter::appendRevert() const {
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x0), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::REVERT), *STI);
}

void EVMAsmPrinter::appendConditionalRevert(MCSymbol *S) const {
  pushSymbol(S);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPI), *STI);
  
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x0), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::REVERT), *STI);
  OutStreamer->EmitLabel(S);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::POP), *STI);
}

void EVMAsmPrinter::appendFunctionSelector(Module &M) {
  OutStreamer->EmitLabel(createSymbol("function_selector"));

  bool needToAddCallValueCheck = true;
  if (!EVMSubtarget::hasPayableFunctions(M) &&
       EVMSubtarget::hasInterfaceFunctions(M)
      ) {
        appendCallValueCheck(M);
        needToAddCallValueCheck = false;
  }

  MCSymbol *notFoundTag = createSymbol("not_found");
  emitCallDataSizeCheck(M, notFoundTag);

  if (EVMSubtarget::hasInterfaceFunctions(M)) {
    // this function does not add anything onto stack.
    collectDataUnpackerEntryPoints(M);

    OutStreamer->AddComment("Retrieve function signature hash");
    appendCalldataLoad(M);
    // now we have the function id at TOS.

    // stack now is: <can-call-non-view-functions>? <funhash>
    appendInternalSelector(M, notFoundTag);
  }

  OutStreamer->EmitLabel(notFoundTag);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST).addImm(0x0), *STI);

  Function* fallback = EVMSubtarget::getFallbackFunction(M);
  if (fallback != nullptr) {
    assert(!EVMSubtarget::moduleIsLibrary(M) && "");

    OutStreamer->AddComment("Fallback function");
    if (!EVMSubtarget::isPayableFunction(fallback) && needToAddCallValueCheck) {
      appendCallValueCheck(M);
    }

    // TODO: emit fallback function here
    llvm_unreachable("need to implement emit of fallback function selection");
    OutStreamer->EmitInstruction(MCInstBuilder(EVM::STOP), *STI);
  } else {
    OutStreamer->AddComment("no fallback function defined, revert.");
    appendRevert();
  }

  // TODO: emit interface function wrappers. At this moment we see all functions
  // as interface functions. consider not generating static-linked functions
  // (and see them as internal functions)
  for (Function &F : M.functions()) {
    MCSymbol *funcBodySymbol =
        MMI->getContext().getOrCreateSymbol(F.getName() + "_body");
    this->funcBodyMap.insert(std::pair<Function*, MCSymbol *>(&F, funcBodySymbol));
    appendFunctionWrapper(F);
  }
}

void EVMAsmPrinter::emitCallDataSizeCheck(Module &M, MCSymbol *notFoundTag) {
  OutStreamer->AddComment("call data check");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::PUSH1).addImm(0x04), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLDATASIZE), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::LT), *STI);

  OutStreamer->AddComment("test if calldata check fails");
  appendConditionalJumpTo(notFoundTag);
}

void EVMAsmPrinter::appendCallValueCheck(Module &M) {
  OutStreamer->AddComment("callvalue check");
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::CALLVALUE), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::DUP1), *STI);
  OutStreamer->EmitInstruction(MCInstBuilder(EVM::ISZERO), *STI);

  MCSymbol *cvpass = createSymbol("cvcheck_pass");
  OutStreamer->AddComment("if fails, revert");
  appendConditionalRevert(cvpass);
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

void EVMAsmPrinter::emitInitializeFreeMemoryPointer() const {
  OutStreamer->AddComment("initialize free memory pointer");
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

  appendConditionalJumpTo(CallDataUnpackerLabel);

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
  
  appendJumpTo(this->funcBodyMap[&F]);
}

void EVMAsmPrinter::emitFunctionWrapper(const Function &F) {
  MCSymbol *funcWrapperLabel = this->funcWrapperMap[&F];
  //OutStreamer->EmitLabel(funcWrapperLabel);

  OutStreamer->EmitInstruction(MCInstBuilder(EVM::JUMPDEST), *STI);

  // This is used for inner jumping (between nonpayable check and unpacker)
  MCSymbol *S = createSymbol(F.getName().str() + "_calldataunpacker");

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

/*
void EVMAsmPrinter::EmitStartOfAsmFile(Module &M) {
  STI = std::unique_ptr<MCSubtargetInfo>(TM.getTarget().createMCSubtargetInfo(
      TM.getTargetTriple().str(), TM.getTargetCPU(),
      TM.getTargetFeatureString()));
  ctx = &MMI->getContext();
  //AsmPrinter::EmitStartOfAsmFile(M);
  //return;
  // Emit the skeleton of the asm file in text section.
  OutStreamer->SwitchSection(getObjFileLowering().getTextSection());

  OutStreamer->EmitLabel(createSymbol("contract_creation"));

  // TODO
  bool isLibrary = false;
  if (isLibrary) {
    appendDelegatecallCheck(M);
  }

  initializeContext(M);

  appendFunctionSelector(M);

  //appendMissingFunctions(M);

  appendLabel("function_bodies");
}
*/

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
