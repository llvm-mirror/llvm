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
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetLowering.h"
#include <deque>

namespace llvm {
  class rvexSubtarget;

  namespace rvexISD {
    enum NodeType {
      // Start the numbering from where ISD NodeType finishes.
      FIRST_NUMBER = ISD::BUILTIN_OP_END,

      // Jump and link (call)
      JmpLink,
      Call,
      LinkCall,

      // Get the Higher 16 bits from a 32-bit immediate
      // No relation with rvex Hi register
      Hi,
      Lo,


      Addc,
      Adde,
      Divs,
      Orc,

      TargetGlobal,

      Max,
      Maxu,
      Min,
      Minu,
      Slct,

      Mpyllu,
      Mpylhu,
      Mpyhhu,
      Mpyll,
      Mpylh,
      Mpyhh,
      Mpyl,
      Mpyh,

      // Extend in reg ops
      SXTB,
      SXTH,
      ZXTB,
      ZXTH,

      BR,
      BRF,


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

    /// LowerOperation - Provide custom lowering hooks for some operations.
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// getTargetNodeName - This method returns the name of a target specific
    //  DAG node.
    const char *getTargetNodeName(unsigned Opcode) const override;

    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    /// Returns true if a cast between SrcAS and DestAS is a noop.
    bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const override {
      // Addrspacecasts are always noops on this platform.
      return true;
    }


  private:
    // Subtarget Info
    const rvexSubtarget *Subtarget;

    EVT getSetCCResultType(LLVMContext &Context, EVT VT) const;

    // Lower Operand helpers
    SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            SDLoc dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals,
                            const SDNode *CallNode, const Type *RetTy) const;

    // Lower Operand specifics
    SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    
    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;


	//- must be exist without function all
    SDValue
      LowerFormalArguments(SDValue Chain,
                           CallingConv::ID CallConv, bool isVarArg,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           SDLoc dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const override;

    SDValue
      LowerCall(TargetLowering::CallLoweringInfo &CLI,
                SmallVectorImpl<SDValue> &InVals) const override;

    SDValue LowerAddCG(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSUBE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSDIV(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUDIV(SDValue Op, SelectionDAG &DAG) const;  
    SDValue LowerMULHS(SDValue Op, SelectionDAG &DAG) const;  
    SDValue LowerMULHU(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstant(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerZeroExtend(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSelect(SDValue Op, SelectionDAG &DAG) const;
    
    SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;

	//- must be exist without function all
    SDValue
      LowerReturn(SDValue Chain,
                  CallingConv::ID CallConv, bool isVarArg,
                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                  const SmallVectorImpl<SDValue> &OutVals,
                  SDLoc dl, SelectionDAG &DAG) const override;

    SDValue passArgOnStack(SDValue StackPtr, unsigned Offset, SDValue Chain,
                           SDValue Arg, SDLoc DL, bool IsTailCall,
                           SelectionDAG &DAG) const;      

    bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  protected:

    SDValue getAddrLocal(SDValue Op, SelectionDAG &DAG, bool HasMips64) const;

    SDValue getAddrGlobal(SDValue Op, SelectionDAG &DAG, unsigned Flag) const;    
    /// This function fills Ops, which is the list of operands that will later
    /// be used when a function call node is created. It also generates
    /// copyToReg nodes to set up argument registers.
    void
    getOpndList(SmallVectorImpl<SDValue> &Ops,
                std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
                bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
                CallLoweringInfo &CLI, SDValue Callee, SDValue Chain) const;

    /// ByValArgInfo - Byval argument information.
    struct ByValArgInfo {
      unsigned FirstIdx; // Index of the first register used.
      unsigned NumRegs;  // Number of registers used for this argument.
      unsigned Address;  // Offset of the stack area used to pass this argument.

      ByValArgInfo() : FirstIdx(0), NumRegs(0), Address(0) {}
    };

    /// rvexCC - This class provides methods used to analyze formal and call
    /// arguments and inquire about calling convention information.
    class rvexCC {
    public:
      rvexCC(CallingConv::ID CallConv, bool IsO32, CCState &Info);

      void analyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                               bool IsVarArg, bool IsSoftFloat,
                               const SDNode *CallNode,
                               std::vector<ArgListEntry> &FuncArgs);
      void analyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                                  bool IsSoftFloat,
                                  Function::const_arg_iterator FuncArg);

      void analyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins,
                             bool IsSoftFloat, const SDNode *CallNode,
                             const Type *RetTy) const;

      void analyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                         bool IsSoftFloat, const Type *RetTy) const;

      const CCState &getCCInfo() const { return CCInfo; }

      /// hasByValArg - Returns true if function has byval arguments.
      bool hasByValArg() const { return !ByValArgs.empty(); }

      /// regSize - Size (in number of bits) of integer registers.
      unsigned regSize() const { return IsO32 ? 4 : 8; }

      /// numIntArgRegs - Number of integer registers available for calls.
      unsigned numIntArgRegs() const;

      /// reservedArgArea - The size of the area the caller reserves for
      /// register arguments. This is 16-byte if ABI is O32.
      unsigned reservedArgArea() const;

      /// Return pointer to array of integer argument registers.
      const uint16_t *intArgRegs() const;

      typedef SmallVector<ByValArgInfo, 2>::const_iterator byval_iterator;
      byval_iterator byval_begin() const { return ByValArgs.begin(); }
      byval_iterator byval_end() const { return ByValArgs.end(); }

    private:
      void handleByValArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          ISD::ArgFlagsTy ArgFlags);

      /// useRegsForByval - Returns true if the calling convention allows the
      /// use of registers to pass byval arguments.
      bool useRegsForByval() const { return CallConv != CallingConv::Fast; }

      /// Return the function that analyzes fixed argument list functions.
      llvm::CCAssignFn *fixedArgFn() const;

      /// Return the function that analyzes variable argument list functions.
      llvm::CCAssignFn *varArgFn() const;

      const uint16_t *shadowRegs() const;

      void allocateRegs(ByValArgInfo &ByVal, unsigned ByValSize,
                        unsigned Align);

      /// Return the type of the register which is used to pass an argument or
      /// return a value. This function returns f64 if the argument is an i64
      /// value which has been generated as a result of softening an f128 value.
      /// Otherwise, it just returns VT.
      MVT getRegVT(MVT VT, const Type *OrigTy, const SDNode *CallNode,
                   bool IsSoftFloat) const;

      template<typename Ty>
      void analyzeReturn(const SmallVectorImpl<Ty> &RetVals, bool IsSoftFloat,
                         const SDNode *CallNode, const Type *RetTy) const;

      CCState &CCInfo;
      CallingConv::ID CallConv;
      bool IsO32;
      SmallVector<ByValArgInfo, 2> ByValArgs;
    };

    /// copyByValArg - Copy argument registers which were used to pass a byval
    /// argument to the stack. Create a stack frame object for the byval
    /// argument.
    void copyByValRegs(SDValue Chain, SDLoc DL,
                       std::vector<SDValue> &OutChains, SelectionDAG &DAG,
                       const ISD::ArgFlagsTy &Flags,
                       SmallVectorImpl<SDValue> &InVals,
                       const Argument *FuncArg,
                       const rvexCC &CC, const ByValArgInfo &ByVal) const;

    /// passByValArg - Pass a byval argument in registers or on stack.
    void passByValArg(SDValue Chain, SDLoc DL,
                      std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
                      SmallVector<SDValue, 8> &MemOpChains, SDValue StackPtr,
                      MachineFrameInfo *MFI, SelectionDAG &DAG, SDValue Arg,
                      const rvexCC &CC, const ByValArgInfo &ByVal,
                      const ISD::ArgFlagsTy &Flags, bool isLittle) const;

    /// writeVarArgRegs - Write variable function arguments passed in registers
    /// to the stack. Also create a stack frame object for the first variable
    /// argument.
    void writeVarArgRegs(std::vector<SDValue> &OutChains, const rvexCC &CC,
                         SDValue Chain, SDLoc DL, SelectionDAG &DAG) const; 
    bool
      CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                     bool isVarArg,
                     const SmallVectorImpl<ISD::OutputArg> &Outs,
                     LLVMContext &Context) const override;

    bool isEligibleForTailCallOptimization(const rvexCC &rvexCCInfo,
                                      unsigned NextStackOffset) const;                                                    
  };
}

#endif // rvexISELLOWERING_H
