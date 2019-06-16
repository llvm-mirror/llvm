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



  for (auto VT : { MVT::i1, MVT::i8, MVT::i16, MVT::i32,
                   MVT::i64, MVT::i128, MVT::i256 }) {
    setOperationAction(ISD::SMIN, VT, Expand);
    setOperationAction(ISD::SMAX, VT, Expand);
    setOperationAction(ISD::UMIN, VT, Expand);
    setOperationAction(ISD::UMAX, VT, Expand);
    setOperationAction(ISD::ABS,  VT, Expand);

    // we don't have complex operations.
    setOperationAction(ISD::ADDC, VT, Expand);
    setOperationAction(ISD::SUBC, VT, Expand);
    setOperationAction(ISD::ADDE, VT, Expand);
    setOperationAction(ISD::SUBE, VT, Expand);
    setOperationAction(ISD::ADDCARRY, VT, Expand);
    setOperationAction(ISD::SUBCARRY, VT, Expand);
    setOperationAction(ISD::SADDO, VT, Expand);
    setOperationAction(ISD::UADDO, VT, Expand);
    setOperationAction(ISD::SSUBO, VT, Expand);
    setOperationAction(ISD::USUBO, VT, Expand);
    setOperationAction(ISD::SMULO, VT, Expand);
    setOperationAction(ISD::UMULO, VT, Expand);
    setOperationAction(ISD::SADDSAT, VT, Expand);
    setOperationAction(ISD::UADDSAT, VT, Expand);
    setOperationAction(ISD::SSUBSAT, VT, Expand);
    setOperationAction(ISD::USUBSAT, VT, Expand);
    setOperationAction(ISD::SMULFIX, VT, Expand);
    setOperationAction(ISD::UMULFIX, VT, Expand);

    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
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
    setOperationAction(ISD::SELECT, VT, Expand);

    setOperationAction(ISD::SELECT_CC, VT, Custom);

    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::BITREVERSE, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Custom);

    setOperationAction(ISD::GlobalAddress, VT, Custom);
    setOperationAction(ISD::ExternalSymbol, VT, Custom);
    setOperationAction(ISD::BlockAddress, VT, Custom);
  }

  setOperationAction(ISD::BR_CC, MVT::i256, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  //setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  setOperationAction(ISD::FrameIndex, MVT::i256, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i256, Custom);

  // FIXME: DYNAMIC_STACKALLOC
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i256, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  // extends
  setOperationAction(ISD::ANY_EXTEND,  MVT::i256, Expand);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i256, Expand);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i256, Custom);

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
  return false;
}

bool EVMTargetLowering::isSExtCheaperThanZExt(EVT SrcVT, EVT DstVT) const {
  return true;
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

SDValue EVMTargetLowering::LowerFrameIndex(SDValue Op,
                                           SelectionDAG &DAG) const {
    int FI = cast<FrameIndexSDNode>(Op)->getIndex();

    // Record the FI so that we know how many frame slots are allocated to
    // frames.
    MachineFunction &MF = DAG.getMachineFunction();
    EVMMachineFunctionInfo &MFI = *MF.getInfo<EVMMachineFunctionInfo>();
    if ((FI + 1) > MFI.getFrameIndexSize()) {
      MFI.setFrameIndexSize(FI + 1);
    }

    return DAG.getTargetFrameIndex(FI, Op.getValueType());
}

SDValue
EVMTargetLowering::LowerGlobalAddress(SDValue Op,
                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const auto *GA = cast<GlobalAddressSDNode>(Op);
  EVT VT = Op.getValueType();
  assert(GA->getTargetFlags() == 0 &&
         "Unexpected target flags on generic GlobalAddressSDNode");

  if (GA->getAddressSpace() != 0) {
    llvm_unreachable("multiple address space unimplemented");
  }

  return DAG.getNode(EVMISD::WRAPPER, DL, VT,
                     DAG.getTargetGlobalAddress(GA->getGlobal(),
                                                DL, VT,
                                                GA->getOffset()));
}

SDValue
EVMTargetLowering::LowerExternalSymbol(SDValue Op,
                                       SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const auto *ES = cast<ExternalSymbolSDNode>(Op);
  EVT VT = Op.getValueType();
  assert(ES->getTargetFlags() == 0 &&
         "Unexpected target flags on generic ExternalSymbolSDNode");

  return DAG.getNode(
      EVMISD::WRAPPER, DL, VT,
      DAG.getTargetExternalSymbol(ES->getSymbol(), MVT::i256));

}

SDValue
EVMTargetLowering::LowerBlockAddress(SDValue Op,
                                     SelectionDAG &DAG) const {
  SDLoc dl(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout());
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT);

  return DAG.getNode(EVMISD::WRAPPER, dl, PtrVT, Result);
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
  case ISD::FrameIndex:
    return LowerFrameIndex(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::SIGN_EXTEND:
    // TODO: `sext` can be efficiently supported.
    // so dont need to expand.
    // consider to use EVMISD::SIGNEXTEND since we need shift value.
    llvm_unreachable("Needs implementation.");
  }
}


void EVMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  SDLoc DL(N);

  llvm_unreachable("not implemented");
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

  // create two MBBs to handle true and false
  MachineBasicBlock *trueMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *falseMBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MachineFunction::iterator I;
  for (I = MF->begin(); I != MF->end() && &(*I) != MBB; ++I);
  if (I != MF->end()) ++I;
  MF->insert(I, falseMBB);
  MF->insert(I, trueMBB);

  // Transfer remaining instructions and all successors of the current
  // block to the block which will contain the Phi node for the
  // select.
  trueMBB->splice(trueMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  trueMBB->transferSuccessorsAndUpdatePHIs(MBB);

    unsigned SelectedValue = MI.getOperand(0).getReg();
    unsigned LHS   = MI.getOperand(1).getReg();
    unsigned RHS   = MI.getOperand(2).getReg();

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

    BuildMI(MBB, dl, TII.get(EVM::JUMPI_r)).addReg(rvreg).addMBB(trueMBB);
  }

  // Finally, add branch to falseMBB 
  BuildMI(MBB, dl, TII.get(EVM::JUMP_r)).addMBB(falseMBB);

  MBB->addSuccessor(falseMBB);
  MBB->addSuccessor(trueMBB);

  // Unconditionally flow back to the true block
  {
    BuildMI(falseMBB, dl, TII.get(EVM::JUMP_r)).addMBB(trueMBB);
    falseMBB->addSuccessor(trueMBB);
  }

  // Set up the Phi node to determine where we came from
  BuildMI(*trueMBB, trueMBB->begin(), dl, TII.get(EVM::PHI), SelectedValue)
    .addReg(LHS).addMBB(MBB)
    .addReg(RHS).addMBB(falseMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return trueMBB;
}

MachineBasicBlock *
EVMTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *MBB) const {
  int Opc = MI.getOpcode();

  switch (Opc) {
    case EVM::Selectcc :
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

  // Instantiate virtual registers for each of the incoming value.
  // unused register will be set to UNDEF.
  SmallVector<SDValue, 16> ArgsChain;
  ArgsChain.push_back(Chain);
  for (const ISD::InputArg &In : Ins) {
    SmallVector<SDValue, 4> Opnds;


    const SDValue &idx = DAG.getTargetConstant(InVals.size(),
                                               DL, MVT::i256);
    Opnds.push_back(idx);
    //Opnds.push_back(Chain);

    const SDValue &StackArg =
       DAG.getNode(EVMISD::STACKARG, DL, MVT::i256, Opnds);

    InVals.push_back(StackArg);
    //ArgsChain.push_back(StackArg);
  }

  //ArgsChain.push_back(Chain);
  //Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, ArgsChain);

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

SDValue EVMTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  auto &Outs = CLI.Outs;
  auto &Ins = CLI.Ins;
  auto &OutVals = CLI.OutVals;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  MachineFunction &MF = DAG.getMachineFunction();
  auto Layout = MF.getDataLayout();
  bool IsVarArg = CLI.IsVarArg;

  CallingConv::ID CallConv = CLI.CallConv;


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

  if (Ins.size() > 1) {
    llvm_unreachable("unimplemented.");
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_EVM);

  // Insert callseq start
  unsigned NumBytes = CCInfo.getNextStackOffset();
  auto PtrVT = getPointerTy(MF.getDataLayout());
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  // Compute the operands for the CALLn node.
  SmallVector<SDValue, 16> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  for (unsigned I = 0; I < Outs.size(); ++I) {
    const ISD::OutputArg &Out = Outs[I];
    SDValue &OutVal = OutVals[I];
    if (Out.Flags.isByVal() && Out.Flags.getByValSize() != 0) {
      auto &MFI = MF.getFrameInfo();
      int FI = MFI.CreateStackObject(Out.Flags.getByValSize(),
          Out.Flags.getByValAlign(),
          /*isSS=*/false);
      SDValue SizeNode =
        DAG.getConstant(Out.Flags.getByValSize(), DL, MVT::i32);
      SDValue FINode = DAG.getFrameIndex(FI, getPointerTy(Layout));
      Chain = DAG.getMemcpy(
          Chain, DL, FINode, OutVal, SizeNode, Out.Flags.getByValAlign(),
          /*isVolatile*/ false, /*AlwaysInline=*/false,
          /*isTailCall*/ false, MachinePointerInfo(), MachinePointerInfo());
      OutVal = FINode;
    }
    //Ops.push_back(OutVal);
  }

  // Add all fixed arguments. Note that for non-varargs calls, NumFixedArgs
  // isn't reliable.
  Ops.append(OutVals.begin(), OutVals.end());


  SmallVector<EVT, 8> InTys;
  for (const auto &In : Ins) {
    InTys.push_back(In.VT);
  }
  InTys.push_back(MVT::Other);
  SDVTList InTyList = DAG.getVTList(InTys);

  unsigned opc = Ins.empty() ? EVMISD::CALLVOID : EVMISD::CALL;
  SDValue Res = DAG.getNode(opc, DL, InTyList, Ops);

  if (Ins.empty()) {
    Chain = Res;
  } else {
    InVals.push_back(Res);
    Chain = Res.getValue(1);
  }

  Chain = DAG.getCALLSEQ_END(
            Chain,
            DAG.getConstant(NumBytes, DL, PtrVT, true),
            DAG.getConstant(0, DL, PtrVT, true),
            opc == EVMISD::CALLVOID ? Chain.getValue(0) : Chain.getValue(1),
            DL);

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
  assert(Outs.size() <= 1 && "EVM can only return up to one value");

  SmallVector<SDValue, 4> RetOps(1, Chain);
  RetOps.append(OutVals.begin(), OutVals.end());

  Chain = DAG.getNode(EVMISD::RET_FLAG, DL, MVT::Other, RetOps);

  return Chain;
}

const char *EVMTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (static_cast<EVMISD::NodeType>(Opcode)) {
  case EVMISD::FIRST_NUMBER:
    break;
#define NODE(N)                                                  \
  case EVMISD::N:                                                \
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
