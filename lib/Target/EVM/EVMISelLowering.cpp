//===-- EVMISelLowering.cpp - EVM DAG Lowering Implementation  --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that EVM uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "EVMISelLowering.h"
#include "EVM.h"
#include "EVMMachineFunctionInfo.h"
#include "EVMRegisterInfo.h"
#include "EVMSubtarget.h"
#include "EVMTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "evm-lower"

STATISTIC(NumTailCalls, "Number of tail calls");

EVMTargetLowering::EVMTargetLowering(const TargetMachine &TM,
                                         const EVMSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {

  // Legal register classes:
  addRegisterClass(MVT::i256, &EVM::GPRRegClass);

  // Compute derived properties from the register classes.
  computeRegisterProperties(Subtarget.getRegisterInfo());

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);
  setSchedulingPreference(Sched::RegPressure);
  setStackPointerRegisterToSaveRestore(EVM::SP);



  // TODO: set type legalization actions
  for (auto VT : { MVT::i1, MVT::i8, MVT::i16, MVT::i32,
                   MVT::i64, MVT::i128 }) {
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::SHL_PARTS, VT, Expand);
    setOperationAction(ISD::SRL_PARTS, VT, Expand);
    setOperationAction(ISD::SRA_PARTS, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);

    setOperationAction(ISD::SETCC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);

    // have to do custom for SELECT_CC
    setOperationAction(ISD::SELECT_CC, VT, Expand);

    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Custom);
  }

  setOperationAction(ISD::BR_CC, MVT::i256, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // we don't have trunc stores.
  setTruncStoreAction(MVT::i256, MVT::i8,   Legal);
  setTruncStoreAction(MVT::i256, MVT::i16,  Legal);
  setTruncStoreAction(MVT::i256, MVT::i32,  Legal);
  setTruncStoreAction(MVT::i256, MVT::i64,  Legal);
  setTruncStoreAction(MVT::i256, MVT::i128, Legal);

  // Load extented operations for i1 types must be promoted
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1,  Promote);
  }

  // customize exts
  for (MVT VT : MVT::integer_valuetypes()) {
    // Disable for now.
    //setLoadExtAction(ISD::SEXTLOAD, MVT::i256, VT, Custom);
    /*
    setLoadExtAction(ISD::SEXTLOAD, MVT::i256, VT, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i256, VT, Expand);
    setLoadExtAction(ISD::EXTLOAD,  MVT::i256, VT, Expand);
    */
  }
}

EVT EVMTargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                            EVT VT) const {
  return MVT::i256;
}

bool EVMTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                             const CallInst &I,
                                             MachineFunction &MF,
                                             unsigned Intrinsic) const {
  llvm_unreachable("unimplemented.");
}

bool EVMTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS,
                                                Instruction *I) const {
  llvm_unreachable("unimplemented.");
}

bool EVMTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return true;
}

bool EVMTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return true;
}

bool EVMTargetLowering::isTruncateFree(Type *SrcTy, Type *DstTy) const {
  return true;
}

bool EVMTargetLowering::isTruncateFree(EVT SrcVT, EVT DstVT) const {
  return true;
}

bool EVMTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  return true;
}

bool EVMTargetLowering::isSExtCheaperThanZExt(EVT SrcVT, EVT DstVT) const {
  llvm_unreachable("unimplemented.");
}

static EVMISD::NodeType getReverseCmpOpcode(ISD::CondCode CC) {
  switch (CC) {
    default:
      llvm_unreachable("unimplemented condition code.");
      break;
    case ISD::SETLE:
      return EVMISD::SGT;
    case ISD::SETGE:
      return EVMISD::SLT;
    case ISD::SETULE:
      return EVMISD::GT;
    case ISD::SETUGE:
      return EVMISD::LT;
  }
}


SDValue EVMTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  return DAG.getNode(EVMISD::BRCC, DL, Op.getValueType(), Chain, LHS, RHS,
                     DAG.getConstant(CC, DL, LHS.getValueType()), Dest);

}

SDValue EVMTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  // we are going to pattern match out the i64 type.
  SDValue TargetCC = DAG.getConstant(CC, DL, LHS.getValueType());
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};

  return DAG.getNode(EVMISD::SELECTCC, DL, VTs, Ops);
}


SDValue EVMTargetLowering::LowerSEXTLOAD(SDValue Op, SelectionDAG &DAG) const {
  const LoadSDNode *LD = cast<LoadSDNode>(Op.getNode());
  assert(LD->getExtensionType() == ISD::SEXTLOAD);
  SDLoc DL(Op);

  SDValue Src = Op.getOperand(0);
  int sizeInBytes = Src.getValueSizeInBits() / 8;
  SDValue Bytes = DAG.getConstant(sizeInBytes, DL, Op.getValueType());
  return DAG.getNode(EVMISD::SEXTLOAD, DL, Op.getValueType(), Src);
}

SDValue EVMTargetLowering::LowerSIGN_EXTEND_INREG(SDValue Op, SelectionDAG &DAG) const {
  SDValue Op0 = Op.getOperand(0);
  SDLoc dl(Op);
  assert(Op.getValueType() == MVT::i256 && "Unhandled target sign_extend_inreg.");

  unsigned Width = cast<VTSDNode>(Op.getOperand(1))->getVT().getSizeInBits() / 8;

  return DAG.getNode(EVMISD::SIGNEXTEND, dl, MVT::i256, Op0,
                     DAG.getConstant(32 - Width, dl, MVT::i256));
}

SDValue EVMTargetLowering::LowerOperation(SDValue Op,
                                          SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented lowering operation,");
  case ISD::BR_CC:
    // TODO: this can be used to expand.
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::LOAD:
    return LowerSEXTLOAD(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSIGN_EXTEND_INREG(Op, DAG);
  }
}


void EVMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  llvm_unreachable("unimplemented.");
}

MachineBasicBlock *
EVMTargetLowering::insertSELECTCC(MachineInstr &MI,
                                  MachineBasicBlock *MBB) const {
  const EVMInstrInfo &TII = (const EVMInstrInfo &)*MI.getParent()
                                ->getParent()
                                ->getSubtarget()
                                .getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we insert the diamond
  // control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch
  // on, the true/false values to select between, and a branch opcode
  // to use.

  MachineFunction *MF = MBB->getParent();
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  MachineBasicBlock *FallThrough = MBB->getFallThrough();

  // If the current basic block falls through to another basic block,
  // we must insert an unconditional branch to the fallthrough destination
  // if we are to insert basic blocks at the prior fallthrough point.
  if (FallThrough != nullptr) {
    BuildMI(MBB, dl, TII.get(EVM::JUMP_r)).addMBB(FallThrough);
  }

  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I);
  if (I != MF->end()) ++I;
  MF->insert(I, trueMBB);
  MF->insert(I, falseMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  trueMBB->splice(trueMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  trueMBB->transferSuccessorsAndUpdatePHIs(MBB);

    unsigned LHS = MI.getOperand(0).getReg();
    unsigned RHS = MI.getOperand(1).getReg();
    //unsigned TrueV = MI.getOperand(2);
    //unsigned FalseV = MI.getOperand(3);

  // construct conditional jump
  {
    MachineFunction *F = MBB->getParent();
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    const TargetRegisterClass *RC = getRegClassFor(MVT::i256);
    unsigned rvreg = RegInfo.createVirtualRegister(RC);

    int CC = MI.getOperand(3).getImm();
    switch (CC) {
      default:
        llvm_unreachable("unimplemented.");
      case ISD::SETEQ:
        BuildMI(MBB, dl, TII.get(EVM::EQ_r), rvreg).addReg(LHS).addReg(RHS);
        break;
      case ISD::SETNE:
        {
          unsigned reg = RegInfo.createVirtualRegister(RC);
          BuildMI(MBB, dl, TII.get(EVM::EQ_r), reg).addReg(LHS).addReg(RHS);
          BuildMI(MBB, dl, TII.get(EVM::ISZERO_r), rvreg).addReg(reg);
        }
        break;
      case ISD::SETLT:
        BuildMI(MBB, dl, TII.get(EVM::SLT_r), rvreg).addReg(LHS).addReg(RHS);
        break;
      case ISD::SETLE:
        {
          unsigned reg = RegInfo.createVirtualRegister(RC);
          BuildMI(MBB, dl, TII.get(EVM::SGT_r), reg).addReg(LHS).addReg(RHS);
          BuildMI(MBB, dl, TII.get(EVM::ISZERO_r), rvreg).addReg(reg);
        }
        break;
      case ISD::SETGT:
        BuildMI(MBB, dl, TII.get(EVM::SGT_r), rvreg).addReg(LHS).addReg(RHS);
        break;
      case ISD::SETGE:
        {
          unsigned reg = RegInfo.createVirtualRegister(RC);
          BuildMI(MBB, dl, TII.get(EVM::SLT_r), reg).addReg(LHS).addReg(RHS);
          BuildMI(MBB, dl, TII.get(EVM::ISZERO_r), rvreg).addReg(reg);
        }
        break;
      case ISD::SETULT:
        BuildMI(MBB, dl, TII.get(EVM::LT_r), rvreg).addReg(LHS).addReg(RHS);
        break;
      case ISD::SETULE:
        {
          unsigned reg = RegInfo.createVirtualRegister(RC);
          BuildMI(MBB, dl, TII.get(EVM::GT_r), reg).addReg(LHS).addReg(RHS);
          BuildMI(MBB, dl, TII.get(EVM::ISZERO_r), rvreg).addReg(reg);
        }
        break;
      case ISD::SETUGT:
        BuildMI(MBB, dl, TII.get(EVM::GT_r), rvreg).addReg(LHS).addReg(RHS);
        break;
      case ISD::SETUGE:
        {
          unsigned reg = RegInfo.createVirtualRegister(RC);
          BuildMI(MBB, dl, TII.get(EVM::LT_r), reg).addReg(LHS).addReg(RHS);
          BuildMI(MBB, dl, TII.get(EVM::ISZERO_r), rvreg).addReg(reg);
        }
        break;
    }

    BuildMI(MBB, dl, TII.get(EVM::JUMPI_r), rvreg).addMBB(trueMBB);
  }


  BuildMI(MBB, dl, TII.get(EVM::JUMP_r)).addMBB(falseMBB);
  MBB->addSuccessor(falseMBB);
  MBB->addSuccessor(trueMBB);

  // Unconditionally flow back to the true block
  BuildMI(falseMBB, dl, TII.get(EVM::JUMP_r)).addMBB(trueMBB);
  falseMBB->addSuccessor(trueMBB);

  // Set up the Phi node to determine where we came from
  BuildMI(*trueMBB, trueMBB->begin(), dl, TII.get(EVM::PHI), MI.getOperand(0).getReg())
    .addReg(MI.getOperand(1).getReg())
    .addMBB(MBB)
    .addReg(MI.getOperand(2).getReg())
    .addMBB(falseMBB) ;

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return trueMBB;
}

MachineBasicBlock *
EVMTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *MBB) const {
  int Opc = MI.getOpcode();

  switch (Opc) {
    case EVMISD::SELECTCC :
      return insertSELECTCC(MI, MBB);
    default:
      llvm_unreachable("unimplemented.");
  }
}

// Convert Val to a ValVT. Should not be called for CCValAssign::Indirect
// values.
static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL) {
  llvm_unreachable("unimplemented.");
}

#include "EVMGenCallingConv.inc"

// Transform physical registers into virtual registers.
SDValue EVMTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  
  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_EVM);

  // For now, arguments are on the stack.
  for (CCValAssign &VA : ArgLocs) {
    // sanity check
    assert(VA.isMemLoc());

    MVT LocVT = VA.getLocVT();
    int index = VA.getLocMemOffset() / (LocVT.getSizeInBits() / 8);
    /*
    // The stack pointer offset is relative to the caller stack frame.
    int FI = MFI.CreateFixedObject(LocVT.getSizeInBits() / 8,
                                   VA.getLocMemOffset(), true);

    // Create load nodes to retrieve arguments from the stack
    SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    */
    SDValue indexSD = DAG.getConstant(index, DL, MVT::i256);

    SDValue ArgValue = DAG.getNode(EVMISD::ARGUMENT, DL, VA.getValVT(), indexSD);

    InVals.push_back(ArgValue);
  }

  return Chain;
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization.
/// Note: This is modelled after ARM's IsEligibleForTailCallOptimization.
bool EVMTargetLowering::IsEligibleForTailCallOptimization(
  CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
  const SmallVector<CCValAssign, 16> &ArgLocs) const {
  return false;
}

// Lower a call to a callseq_start + CALL + callseq_end chain, and add input
// and output parameter nodes.
SDValue EVMTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  auto &Outs = CLI.Outs;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  auto Layout = MF.getDataLayout();
  bool IsVarArg = CLI.IsVarArg;

  CallingConv::ID CallConv = CLI.CallConv;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_EVM);


  if (IsVarArg) { llvm_unreachable("unimplemented."); }

  bool IsTailCall = CLI.IsTailCall;
  if (IsTailCall) { llvm_unreachable("unimplemented."); }

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::Fast:
  case CallingConv::C:
    break;
  }

  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  if (Ins.size() > 1) {
    llvm_unreachable("unimplemented.");
  }

  // Compute the operands for the CALLn node.
  SmallVector<SDValue, 16> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  SmallVector<EVT, 8> InTys;
  for (const auto &In : Ins) {
    InTys.push_back(In.VT);
  }

  InTys.push_back(MVT::Other);
  SDVTList InTyList = DAG.getVTList(InTys);
  SDValue Res =
      DAG.getNode(EVMISD::CALL, DL, InTyList, Ops);

  if (Ins.empty()) {
    Chain = Res;
  } else {
    InVals.push_back(Res);
    Chain = Res.getValue(1);
  }

  return Chain;
}

bool EVMTargetLowering::CanLowerReturn(
    CallingConv::ID /*CallConv*/, MachineFunction & /*MF*/, bool /*IsVarArg*/,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    LLVMContext & /*Context*/) const {
  // We can't currently handle returning tuples.
  return Outs.size() <= 1;
}

SDValue
EVMTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<SDValue, 4> RetOps(1, Chain);
  RetOps.append(OutVals.begin(), OutVals.end());
  Chain = DAG.getNode(EVMISD::RETURN, DL, MVT::Other, RetOps);
  return Chain;
}

const char *EVMTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (static_cast<EVMISD::NodeType>(Opcode)) {
  case EVMISD::FIRST_NUMBER:
    break;
#define NODE(N)                                                  \
  case EVMISD::N:                                                   \
    return "EVMISD::" #N;
#include "EVMISD.def"
#undef NODE
  }
  return nullptr;
}

std::pair<unsigned, const TargetRegisterClass *>
EVMTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  llvm_unreachable("unimplemented.");
}

Instruction *EVMTargetLowering::emitLeadingFence(IRBuilder<> &Builder,
                                                   Instruction *Inst,
                                                   AtomicOrdering Ord) const {
  llvm_unreachable("unimplemented.");
  return nullptr;
}

Instruction *EVMTargetLowering::emitTrailingFence(IRBuilder<> &Builder,
                                                    Instruction *Inst,
                                                    AtomicOrdering Ord) const {
  llvm_unreachable("unimplemented.");
  return nullptr;
}
