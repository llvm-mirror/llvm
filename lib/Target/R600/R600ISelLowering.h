//===-- R600ISelLowering.h - R600 DAG Lowering Interface -*- C++ -*--------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief R600 DAG Lowering interface definition
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_R600_R600ISELLOWERING_H
#define LLVM_LIB_TARGET_R600_R600ISELLOWERING_H

#include "AMDGPUISelLowering.h"

namespace llvm {

class R600InstrInfo;

class R600TargetLowering : public AMDGPUTargetLowering {
public:
  R600TargetLowering(TargetMachine &TM, const AMDGPUSubtarget &STI);
  MachineBasicBlock * EmitInstrWithCustomInserter(MachineInstr *MI,
      MachineBasicBlock * BB) const override;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode * N,
                          SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(
                              SDValue Chain,
                              CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::InputArg> &Ins,
                              SDLoc DL, SelectionDAG &DAG,
                              SmallVectorImpl<SDValue> &InVals) const override;
  EVT getSetCCResultType(LLVMContext &, EVT VT) const override;
private:
  unsigned Gen;
  /// Each OpenCL kernel has nine implicit parameters that are stored in the
  /// first nine dwords of a Vertex Buffer.  These implicit parameters are
  /// lowered to load instructions which retrieve the values from the Vertex
  /// Buffer.
  SDValue LowerImplicitParameter(SelectionDAG &DAG, EVT VT,
                                 SDLoc DL, unsigned DwordOffset) const;

  void lowerImplicitParameter(MachineInstr *MI, MachineBasicBlock &BB,
      MachineRegisterInfo & MRI, unsigned dword_offset) const;
  SDValue OptimizeSwizzle(SDValue BuildVector, SDValue Swz[], SelectionDAG &DAG) const;
  SDValue vectorToVerticalVector(SelectionDAG &DAG, SDValue Vector) const;

  SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFPTOUINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerTrig(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSHLParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRXParts(SDValue Op, SelectionDAG &DAG) const;

  SDValue stackPtrToRegIndex(SDValue Ptr, unsigned StackWidth,
                                          SelectionDAG &DAG) const;
  void getStackAddress(unsigned StackWidth, unsigned ElemIdx,
                       unsigned &Channel, unsigned &PtrIncr) const;
  bool isZero(SDValue Op) const;
  SDNode *PostISelFolding(MachineSDNode *N, SelectionDAG &DAG) const override;
};

} // End namespace llvm;

#endif
