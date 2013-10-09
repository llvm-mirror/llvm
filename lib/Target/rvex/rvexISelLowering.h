//===-- rvexISelLowering.h - rvex DAG Lowering Interface --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that rvex uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef rvexISELLOWERING_H
#define rvexISELLOWERING_H

#include "rvex.h"
#include "rvexSubtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Target/TargetLowering.h"

namespace llvm {
  namespace rvexISD {
    enum NodeType {
      // Start the numbering from where ISD NodeType finishes.
      FIRST_NUMBER = ISD::BUILTIN_OP_END,

      // Jump and link (call)
      JmpLink,

      // Get the Higher 16 bits from a 32-bit immediate
      // No relation with rvex Hi register
      Hi,
      // Get the Lower 16 bits from a 32-bit immediate
      // No relation with rvex Lo register
      Lo,

      Addc,
      Adde,
      Divs,
      Orc,

      Max,
      Maxu,
      Min,
      Minu,


      // Handle gp_rel (small data/bss sections) relocation.
      GPRel,

      // Thread Pointer
      ThreadPointer,
      // Return
      Ret,

      // DivRem(u)
      DivRem,
      DivRemU,    

      Wrapper,
      DynAlloc,
      Sync
    };
  }

  //===--------------------------------------------------------------------===//
  // TargetLowering Implementation
  //===--------------------------------------------------------------------===//

  class rvexTargetLowering : public TargetLowering  {
  public:
    explicit rvexTargetLowering(rvexTargetMachine &TM);

    virtual MVT getShiftAmountTy(EVT LHSTy) const { return MVT::i32; }
    /// LowerOperation - Provide custom lowering hooks for some operations.
    virtual SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const;

    /// getTargetNodeName - This method returns the name of a target specific
    //  DAG node.
    virtual const char *getTargetNodeName(unsigned Opcode) const;

    virtual SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  private:
    // Subtarget Info
    const rvexSubtarget *Subtarget;

    // Lower Operand helpers
    SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            DebugLoc dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals) const;

    // Lower Operand specifics
    SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;

	//- must be exist without function all
    virtual SDValue
      LowerFormalArguments(SDValue Chain,
                           CallingConv::ID CallConv, bool isVarArg,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           DebugLoc dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const;

    virtual SDValue
      LowerCall(TargetLowering::CallLoweringInfo &CLI,
                SmallVectorImpl<SDValue> &InVals) const;

    SDValue LowerAddCG(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSDIV(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUDIV(SDValue Op, SelectionDAG &DAG) const;  

	//- must be exist without function all
    virtual SDValue
      LowerReturn(SDValue Chain,
                  CallingConv::ID CallConv, bool isVarArg,
                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                  const SmallVectorImpl<SDValue> &OutVals,
                  DebugLoc dl, SelectionDAG &DAG) const;

    virtual bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const;
  };
}

#endif // rvexISELLOWERING_H
