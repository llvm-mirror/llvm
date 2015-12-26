//- WebAssemblyISelLowering.h - WebAssembly DAG Lowering Interface -*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file defines the interfaces that WebAssembly uses to lower LLVM
/// code into a selection DAG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYISELLOWERING_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYISELLOWERING_H

#include "llvm/Target/TargetLowering.h"

namespace llvm {

namespace WebAssemblyISD {

enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
#define HANDLE_NODETYPE(NODE) NODE,
#include "WebAssemblyISD.def"
#undef HANDLE_NODETYPE
};

} // end namespace WebAssemblyISD

class WebAssemblySubtarget;
class WebAssemblyTargetMachine;

class WebAssemblyTargetLowering final : public TargetLowering {
public:
  WebAssemblyTargetLowering(const TargetMachine &TM,
                            const WebAssemblySubtarget &STI);

private:
  /// Keep a pointer to the WebAssemblySubtarget around so that we can make the
  /// right decision when generating code for different targets.
  const WebAssemblySubtarget *Subtarget;

  FastISel *createFastISel(FunctionLoweringInfo &FuncInfo,
                           const TargetLibraryInfo *LibInfo) const override;
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;
  const char *getTargetNodeName(unsigned Opcode) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  bool isCheapToSpeculateCttz() const override;
  bool isCheapToSpeculateCtlz() const override;
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS) const override;

  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, SDLoc dl,
                      SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               SDLoc DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  // Custom lowering hooks.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue LowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
};

namespace WebAssembly {
FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                         const TargetLibraryInfo *libInfo);
} // end namespace WebAssembly

} // end namespace llvm

#endif
