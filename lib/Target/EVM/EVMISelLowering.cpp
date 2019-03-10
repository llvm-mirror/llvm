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

  setOperationAction(ISD::BR_CC, MVT::i256, Custom);

  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

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
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);


  }

  for (MVT VT : MVT::integer_valuetypes()) {
    for (auto N : {ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}) {
      setLoadExtAction(N, VT, MVT::i1, Promote);
      /*
      if (VT != MVT::i8)
        setLoadExtAction(N, VT, MVT::i8, Expand);
      if (VT != MVT::i16)
        setLoadExtAction(N, VT, MVT::i16, Expand);
      if (VT != MVT::i32)
        setLoadExtAction(N, VT, MVT::i32, Expand);
      if (VT != MVT::i64)
        setLoadExtAction(N, VT, MVT::i64, Expand);
      if (VT != MVT::i128)
        setLoadExtAction(N, VT, MVT::i128, Expand);
      if (VT != MVT::i256)
        setLoadExtAction(N, VT, MVT::i256, Expand);
      */
    }
  }


  //llvm_unreachable("unfinished implementation.");
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

/*
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  assert(LHS.getValueType() == MVT::i256 && "LHS of BR_CC must be i256.");
  assert(RHS.getValueType() == MVT::i256 && "RHS of BR_CC must be i256.");

  if (CC == ISD::SETLE  || CC == ISD::SETGE ||
      CC == ISD::SETULE || CC == ISD::SETUGE) {
    NegateCC(LHS, RHS, CC);
  }

  EVMISD::NodeType op;
  switch (CC) {
    default:
      llvm_unreachable("unimplemented condition code.");
      break;
    case ISD::SETLE:
    case ISD::SETGE:
    case ISD::SETULE:
    case ISD::SETUGE:
      assert(false && "invalid condition code.");
      break;
    case ISD::SETGT:
      op = EVMISD::SGT;
      break;
    case ISD::SETLT:
      op = EVMISD::SLT;
      break;
    case ISD::SETUGT:
      op = EVMISD::GT;
      break;
    case ISD::SETULT:
      op = EVMISD::LT;
      break;
    case ISD::SETEQ:
      op = EVMISD::EQ;
      break;
  }

  SDValue Cmp = DAG.getNode(op, DL, MVT::i256, LHS, RHS);

  SDValue TargetCC;
  return DAG.getNode(EVMISD::JUMPI, DL, MVT::Other, Chain,
                     Cmp, Dest);
*/
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
  }
}


void EVMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  llvm_unreachable("unimplemented.");
}


MachineBasicBlock *
EVMTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  llvm_unreachable("unimplemented.");
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
    // The stack pointer offset is relative to the caller stack frame.
    int FI = MFI.CreateFixedObject(LocVT.getSizeInBits() / 8,
                                   VA.getLocMemOffset(), true);
    
    // Create load nodes to retrieve arguments from the stack
    SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue ArgValue = DAG.getLoad(
        LocVT, DL, Chain, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));

    //ArgValue = UnpackFromArgumentSlot(ArgValue, VA, Ins[i].ArgVT, DL, DAG);

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


SDValue
EVMTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &DL, SelectionDAG &DAG) const {
  SmallVector<SDValue, 4> RetOps(1, Chain);
  //RetOps.append(OutVals.begin(), OutVals.end());
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
