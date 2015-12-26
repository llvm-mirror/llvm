//===-- BPFISelLowering.cpp - BPF DAG Lowering Implementation  ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that BPF uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "BPFISelLowering.h"
#include "BPF.h"
#include "BPFTargetMachine.h"
#include "BPFSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
using namespace llvm;

#define DEBUG_TYPE "bpf-lower"

namespace {

// Diagnostic information for unimplemented or unsupported feature reporting.
class DiagnosticInfoUnsupported : public DiagnosticInfo {
private:
  // Debug location where this diagnostic is triggered.
  DebugLoc DLoc;
  const Twine &Description;
  const Function &Fn;
  SDValue Value;

  static int KindID;

  static int getKindID() {
    if (KindID == 0)
      KindID = llvm::getNextAvailablePluginDiagnosticKind();
    return KindID;
  }

public:
  DiagnosticInfoUnsupported(SDLoc DLoc, const Function &Fn, const Twine &Desc,
                            SDValue Value)
      : DiagnosticInfo(getKindID(), DS_Error), DLoc(DLoc.getDebugLoc()),
        Description(Desc), Fn(Fn), Value(Value) {}

  void print(DiagnosticPrinter &DP) const override {
    std::string Str;
    raw_string_ostream OS(Str);

    if (DLoc) {
      auto DIL = DLoc.get();
      StringRef Filename = DIL->getFilename();
      unsigned Line = DIL->getLine();
      unsigned Column = DIL->getColumn();
      OS << Filename << ':' << Line << ':' << Column << ' ';
    }

    OS << "in function " << Fn.getName() << ' ' << *Fn.getFunctionType() << '\n'
       << Description;
    if (Value)
      Value->print(OS);
    OS << '\n';
    OS.flush();
    DP << Str;
  }

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == getKindID();
  }
};

int DiagnosticInfoUnsupported::KindID = 0;
}

BPFTargetLowering::BPFTargetLowering(const TargetMachine &TM,
                                     const BPFSubtarget &STI)
    : TargetLowering(TM) {

  // Set up the register classes.
  addRegisterClass(MVT::i64, &BPF::GPRRegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(BPF::R11);

  setOperationAction(ISD::BR_CC, MVT::i64, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::SETCC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Custom);

  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::SREM, MVT::i64, Expand);
  setOperationAction(ISD::UREM, MVT::i64, Expand);

  setOperationAction(ISD::MULHU, MVT::i64, Expand);
  setOperationAction(ISD::MULHS, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  setOperationAction(ISD::ADDC, MVT::i64, Expand);
  setOperationAction(ISD::ADDE, MVT::i64, Expand);
  setOperationAction(ISD::SUBC, MVT::i64, Expand);
  setOperationAction(ISD::SUBE, MVT::i64, Expand);

  setOperationAction(ISD::ROTR, MVT::i64, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);

  setOperationAction(ISD::CTTZ, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ, MVT::i64, Custom);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i64, Custom);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i64, Custom);
  setOperationAction(ISD::CTPOP, MVT::i64, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Expand);

  // Extended load operations for i1 types must be promoted
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);

    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i16, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i32, Expand);
  }

  setBooleanContents(ZeroOrOneBooleanContent);

  // Function alignments (log2)
  setMinFunctionAlignment(3);
  setPrefFunctionAlignment(3);

  // inline memcpy() for kernel to see explicit copy
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 128;
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 128;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 128;
}

SDValue BPFTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  default:
    llvm_unreachable("unimplemented operand");
  }
}

// Calling Convention Implementation
#include "BPFGenCallingConv.inc"

SDValue BPFTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, SDLoc DL, SelectionDAG &DAG,
    SmallVectorImpl<SDValue> &InVals) const {
  switch (CallConv) {
  default:
    llvm_unreachable("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    break;
  }

  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_BPF64);

  for (auto &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      // Arguments passed in registers
      EVT RegVT = VA.getLocVT();
      switch (RegVT.getSimpleVT().SimpleTy) {
      default: {
        errs() << "LowerFormalArguments Unhandled argument type: "
               << RegVT.getSimpleVT().SimpleTy << '\n';
        llvm_unreachable(0);
      }
      case MVT::i64:
        unsigned VReg = RegInfo.createVirtualRegister(&BPF::GPRRegClass);
        RegInfo.addLiveIn(VA.getLocReg(), VReg);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, RegVT);

        // If this is an 8/16/32-bit value, it is really passed promoted to 64
        // bits. Insert an assert[sz]ext to capture this, then truncate to the
        // right size.
        if (VA.getLocInfo() == CCValAssign::SExt)
          ArgValue = DAG.getNode(ISD::AssertSext, DL, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          ArgValue = DAG.getNode(ISD::AssertZext, DL, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));

        if (VA.getLocInfo() != CCValAssign::Full)
          ArgValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgValue);

        InVals.push_back(ArgValue);
      }
    } else {
      DiagnosticInfoUnsupported Err(DL, *MF.getFunction(),
                                    "defined with too many args", SDValue());
      DAG.getContext()->diagnose(Err);
    }
  }

  if (IsVarArg || MF.getFunction()->hasStructRetAttr()) {
    DiagnosticInfoUnsupported Err(
        DL, *MF.getFunction(),
        "functions with VarArgs or StructRet are not supported", SDValue());
    DAG.getContext()->diagnose(Err);
  }

  return Chain;
}

SDValue BPFTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                     SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  auto &Outs = CLI.Outs;
  auto &OutVals = CLI.OutVals;
  auto &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();

  // BPF target does not support tail call optimization.
  IsTailCall = false;

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::Fast:
  case CallingConv::C:
    break;
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeCallOperands(Outs, CC_BPF64);

  unsigned NumBytes = CCInfo.getNextStackOffset();

  if (Outs.size() >= 6) {
    DiagnosticInfoUnsupported Err(CLI.DL, *MF.getFunction(),
                                  "too many args to ", Callee);
    DAG.getContext()->diagnose(Err);
  }

  for (auto &Arg : Outs) {
    ISD::ArgFlagsTy Flags = Arg.Flags;
    if (!Flags.isByVal())
      continue;

    DiagnosticInfoUnsupported Err(CLI.DL, *MF.getFunction(),
                                  "pass by value not supported ", Callee);
    DAG.getContext()->diagnose(Err);
  }

  auto PtrVT = getPointerTy(MF.getDataLayout());
  Chain = DAG.getCALLSEQ_START(
      Chain, DAG.getConstant(NumBytes, CLI.DL, PtrVT, true), CLI.DL);

  SmallVector<std::pair<unsigned, SDValue>, 5> RegsToPass;

  // Walk arg assignments
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, CLI.DL, VA.getLocVT(), Arg);
      break;
    }

    // Push arguments into RegsToPass vector
    if (VA.isRegLoc())
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    else
      llvm_unreachable("call arg pass bug");
  }

  SDValue InFlag;

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InFlag in
  // necessary since all emitted instructions must be stuck together.
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, CLI.DL, Reg.first, Reg.second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), CLI.DL, PtrVT,
                                        G->getOffset(), 0);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, 0);

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain = DAG.getNode(BPFISD::CALL, CLI.DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(
      Chain, DAG.getConstant(NumBytes, CLI.DL, PtrVT, true),
      DAG.getConstant(0, CLI.DL, PtrVT, true), InFlag, CLI.DL);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, IsVarArg, Ins, CLI.DL, DAG,
                         InVals);
}

SDValue
BPFTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               SDLoc DL, SelectionDAG &DAG) const {

  // CCValAssign - represent the assignment of the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;
  MachineFunction &MF = DAG.getMachineFunction();

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());

  if (MF.getFunction()->getReturnType()->isAggregateType()) {
    DiagnosticInfoUnsupported Err(DL, *MF.getFunction(),
                                  "only integer returns supported", SDValue());
    DAG.getContext()->diagnose(Err);
  }

  // Analize return values.
  CCInfo.AnalyzeReturn(Outs, RetCC_BPF64);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Flag);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  unsigned Opc = BPFISD::RET_FLAG;
  RetOps[0] = Chain; // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(Opc, DL, MVT::Other, RetOps);
}

SDValue BPFTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, SDLoc DL, SelectionDAG &DAG,
    SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());

  if (Ins.size() >= 2) {
    DiagnosticInfoUnsupported Err(DL, *MF.getFunction(),
                                  "only small returns supported", SDValue());
    DAG.getContext()->diagnose(Err);
  }

  CCInfo.AnalyzeCallResult(Ins, RetCC_BPF64);

  // Copy all of the result registers out of their specified physreg.
  for (auto &Val : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, DL, Val.getLocReg(),
                               Val.getValVT(), InFlag).getValue(1);
    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

static void NegateCC(SDValue &LHS, SDValue &RHS, ISD::CondCode &CC) {
  switch (CC) {
  default:
    break;
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETLT:
  case ISD::SETLE:
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
    break;
  }
}

SDValue BPFTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  NegateCC(LHS, RHS, CC);

  return DAG.getNode(BPFISD::BR_CC, DL, Op.getValueType(), Chain, LHS, RHS,
                     DAG.getConstant(CC, DL, MVT::i64), Dest);
}

SDValue BPFTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  NegateCC(LHS, RHS, CC);

  SDValue TargetCC = DAG.getConstant(CC, DL, MVT::i64);

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};

  return DAG.getNode(BPFISD::SELECT_CC, DL, VTs, Ops);
}

const char *BPFTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((BPFISD::NodeType)Opcode) {
  case BPFISD::FIRST_NUMBER:
    break;
  case BPFISD::RET_FLAG:
    return "BPFISD::RET_FLAG";
  case BPFISD::CALL:
    return "BPFISD::CALL";
  case BPFISD::SELECT_CC:
    return "BPFISD::SELECT_CC";
  case BPFISD::BR_CC:
    return "BPFISD::BR_CC";
  case BPFISD::Wrapper:
    return "BPFISD::Wrapper";
  }
  return nullptr;
}

SDValue BPFTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i64);

  return DAG.getNode(BPFISD::Wrapper, DL, MVT::i64, GA);
}

MachineBasicBlock *
BPFTargetLowering::EmitInstrWithCustomInserter(MachineInstr *MI,
                                               MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();

  assert(MI->getOpcode() == BPF::Select && "Unexpected instr type to insert");

  // To "insert" a SELECT instruction, we actually have to insert the diamond
  // control-flow pattern.  The incoming instruction knows the destination vreg
  // to set, the condition code register to branch on, the true/false values to
  // select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  // ThisMBB:
  // ...
  //  TrueVal = ...
  //  jmp_XX r1, r2 goto Copy1MBB
  //  fallthrough --> Copy0MBB
  MachineBasicBlock *ThisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *Copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *Copy1MBB = F->CreateMachineBasicBlock(LLVM_BB);

  F->insert(I, Copy0MBB);
  F->insert(I, Copy1MBB);
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi node for the select.
  Copy1MBB->splice(Copy1MBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
  Copy1MBB->transferSuccessorsAndUpdatePHIs(BB);
  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(Copy0MBB);
  BB->addSuccessor(Copy1MBB);

  // Insert Branch if Flag
  unsigned LHS = MI->getOperand(1).getReg();
  unsigned RHS = MI->getOperand(2).getReg();
  int CC = MI->getOperand(3).getImm();
  switch (CC) {
  case ISD::SETGT:
    BuildMI(BB, DL, TII.get(BPF::JSGT_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  case ISD::SETUGT:
    BuildMI(BB, DL, TII.get(BPF::JUGT_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  case ISD::SETGE:
    BuildMI(BB, DL, TII.get(BPF::JSGE_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  case ISD::SETUGE:
    BuildMI(BB, DL, TII.get(BPF::JUGE_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  case ISD::SETEQ:
    BuildMI(BB, DL, TII.get(BPF::JEQ_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  case ISD::SETNE:
    BuildMI(BB, DL, TII.get(BPF::JNE_rr))
        .addReg(LHS)
        .addReg(RHS)
        .addMBB(Copy1MBB);
    break;
  default:
    report_fatal_error("unimplemented select CondCode " + Twine(CC));
  }

  // Copy0MBB:
  //  %FalseValue = ...
  //  # fallthrough to Copy1MBB
  BB = Copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(Copy1MBB);

  // Copy1MBB:
  //  %Result = phi [ %FalseValue, Copy0MBB ], [ %TrueValue, ThisMBB ]
  // ...
  BB = Copy1MBB;
  BuildMI(*BB, BB->begin(), DL, TII.get(BPF::PHI), MI->getOperand(0).getReg())
      .addReg(MI->getOperand(5).getReg())
      .addMBB(Copy0MBB)
      .addReg(MI->getOperand(4).getReg())
      .addMBB(ThisMBB);

  MI->eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}
