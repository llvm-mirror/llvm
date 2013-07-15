//===-- LegalizeDAG.cpp - Implement SelectionDAG::Legalize ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAG::Legalize method.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/DebugInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
/// SelectionDAGLegalize - This takes an arbitrary SelectionDAG as input and
/// hacks on it until the target machine can handle it.  This involves
/// eliminating value sizes the machine cannot handle (promoting small sizes to
/// large sizes or splitting up large values into small values) as well as
/// eliminating operations the machine cannot handle.
///
/// This code also does a small amount of optimization and recognition of idioms
/// as part of its processing.  For example, if a target does not support a
/// 'setcc' instruction efficiently, but does support 'brcc' instruction, this
/// will attempt merge setcc and brc instructions into brcc's.
///
namespace {
class SelectionDAGLegalize : public SelectionDAG::DAGUpdateListener {
  const TargetMachine &TM;
  const TargetLowering &TLI;
  SelectionDAG &DAG;

  /// LegalizePosition - The iterator for walking through the node list.
  SelectionDAG::allnodes_iterator LegalizePosition;

  /// LegalizedNodes - The set of nodes which have already been legalized.
  SmallPtrSet<SDNode *, 16> LegalizedNodes;

  EVT getSetCCResultType(EVT VT) const {
    return TLI.getSetCCResultType(*DAG.getContext(), VT);
  }

  // Libcall insertion helpers.

public:
  explicit SelectionDAGLegalize(SelectionDAG &DAG);

  void LegalizeDAG();

private:
  /// LegalizeOp - Legalizes the given operation.
  void LegalizeOp(SDNode *Node);

  SDValue OptimizeFloatStore(StoreSDNode *ST);

  void LegalizeLoadOps(SDNode *Node);
  void LegalizeStoreOps(SDNode *Node);

  /// PerformInsertVectorEltInMemory - Some target cannot handle a variable
  /// insertion index for the INSERT_VECTOR_ELT instruction.  In this case, it
  /// is necessary to spill the vector being inserted into to memory, perform
  /// the insert there, and then read the result back.
  SDValue PerformInsertVectorEltInMemory(SDValue Vec, SDValue Val,
                                         SDValue Idx, SDLoc dl);
  SDValue ExpandINSERT_VECTOR_ELT(SDValue Vec, SDValue Val,
                                  SDValue Idx, SDLoc dl);

  /// ShuffleWithNarrowerEltType - Return a vector shuffle operation which
  /// performs the same shuffe in terms of order or result bytes, but on a type
  /// whose vector element type is narrower than the original shuffle type.
  /// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
  SDValue ShuffleWithNarrowerEltType(EVT NVT, EVT VT, SDLoc dl,
                                     SDValue N1, SDValue N2,
                                     ArrayRef<int> Mask) const;

  void LegalizeSetCCCondCode(EVT VT, SDValue &LHS, SDValue &RHS, SDValue &CC,
                             SDLoc dl);

  SDValue ExpandLibCall(RTLIB::Libcall LC, SDNode *Node, bool isSigned);
  SDValue ExpandLibCall(RTLIB::Libcall LC, EVT RetVT, const SDValue *Ops,
                        unsigned NumOps, bool isSigned, SDLoc dl);

  std::pair<SDValue, SDValue> ExpandChainLibCall(RTLIB::Libcall LC,
                                                 SDNode *Node, bool isSigned);
  SDValue ExpandFPLibCall(SDNode *Node, RTLIB::Libcall Call_F32,
                          RTLIB::Libcall Call_F64, RTLIB::Libcall Call_F80,
                          RTLIB::Libcall Call_F128,
                          RTLIB::Libcall Call_PPCF128);
  SDValue ExpandIntLibCall(SDNode *Node, bool isSigned,
                           RTLIB::Libcall Call_I8,
                           RTLIB::Libcall Call_I16,
                           RTLIB::Libcall Call_I32,
                           RTLIB::Libcall Call_I64,
                           RTLIB::Libcall Call_I128);
  void ExpandDivRemLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandSinCosLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  SDValue EmitStackConvert(SDValue SrcOp, EVT SlotVT, EVT DestVT, SDLoc dl);
  SDValue ExpandBUILD_VECTOR(SDNode *Node);
  SDValue ExpandSCALAR_TO_VECTOR(SDNode *Node);
  void ExpandDYNAMIC_STACKALLOC(SDNode *Node,
                                SmallVectorImpl<SDValue> &Results);
  SDValue ExpandFCOPYSIGN(SDNode *Node);
  SDValue ExpandLegalINT_TO_FP(bool isSigned, SDValue LegalOp, EVT DestVT,
                               SDLoc dl);
  SDValue PromoteLegalINT_TO_FP(SDValue LegalOp, EVT DestVT, bool isSigned,
                                SDLoc dl);
  SDValue PromoteLegalFP_TO_INT(SDValue LegalOp, EVT DestVT, bool isSigned,
                                SDLoc dl);

  SDValue ExpandBSWAP(SDValue Op, SDLoc dl);
  SDValue ExpandBitCount(unsigned Opc, SDValue Op, SDLoc dl);

  SDValue ExpandExtractFromVectorThroughStack(SDValue Op);
  SDValue ExpandInsertToVectorThroughStack(SDValue Op);
  SDValue ExpandVectorBuildThroughStack(SDNode* Node);

  SDValue ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP);

  std::pair<SDValue, SDValue> ExpandAtomic(SDNode *Node);

  void ExpandNode(SDNode *Node);
  void PromoteNode(SDNode *Node);

  void ForgetNode(SDNode *N) {
    LegalizedNodes.erase(N);
    if (LegalizePosition == SelectionDAG::allnodes_iterator(N))
      ++LegalizePosition;
  }

public:
  // DAGUpdateListener implementation.
  virtual void NodeDeleted(SDNode *N, SDNode *E) {
    ForgetNode(N);
  }
  virtual void NodeUpdated(SDNode *N) {}

  // Node replacement helpers
  void ReplacedNode(SDNode *N) {
    if (N->use_empty()) {
      DAG.RemoveDeadNode(N);
    } else {
      ForgetNode(N);
    }
  }
  void ReplaceNode(SDNode *Old, SDNode *New) {
    DAG.ReplaceAllUsesWith(Old, New);
    ReplacedNode(Old);
  }
  void ReplaceNode(SDValue Old, SDValue New) {
    DAG.ReplaceAllUsesWith(Old, New);
    ReplacedNode(Old.getNode());
  }
  void ReplaceNode(SDNode *Old, const SDValue *New) {
    DAG.ReplaceAllUsesWith(Old, New);
    ReplacedNode(Old);
  }
};
}

/// ShuffleWithNarrowerEltType - Return a vector shuffle operation which
/// performs the same shuffe in terms of order or result bytes, but on a type
/// whose vector element type is narrower than the original shuffle type.
/// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
SDValue
SelectionDAGLegalize::ShuffleWithNarrowerEltType(EVT NVT, EVT VT,  SDLoc dl,
                                                 SDValue N1, SDValue N2,
                                                 ArrayRef<int> Mask) const {
  unsigned NumMaskElts = VT.getVectorNumElements();
  unsigned NumDestElts = NVT.getVectorNumElements();
  unsigned NumEltsGrowth = NumDestElts / NumMaskElts;

  assert(NumEltsGrowth && "Cannot promote to vector type with fewer elts!");

  if (NumEltsGrowth == 1)
    return DAG.getVectorShuffle(NVT, dl, N1, N2, &Mask[0]);

  SmallVector<int, 8> NewMask;
  for (unsigned i = 0; i != NumMaskElts; ++i) {
    int Idx = Mask[i];
    for (unsigned j = 0; j != NumEltsGrowth; ++j) {
      if (Idx < 0)
        NewMask.push_back(-1);
      else
        NewMask.push_back(Idx * NumEltsGrowth + j);
    }
  }
  assert(NewMask.size() == NumDestElts && "Non-integer NumEltsGrowth?");
  assert(TLI.isShuffleMaskLegal(NewMask, NVT) && "Shuffle not legal?");
  return DAG.getVectorShuffle(NVT, dl, N1, N2, &NewMask[0]);
}

SelectionDAGLegalize::SelectionDAGLegalize(SelectionDAG &dag)
  : SelectionDAG::DAGUpdateListener(dag),
    TM(dag.getTarget()), TLI(dag.getTargetLoweringInfo()),
    DAG(dag) {
}

void SelectionDAGLegalize::LegalizeDAG() {
  DAG.AssignTopologicalOrder();

  // Visit all the nodes. We start in topological order, so that we see
  // nodes with their original operands intact. Legalization can produce
  // new nodes which may themselves need to be legalized. Iterate until all
  // nodes have been legalized.
  for (;;) {
    bool AnyLegalized = false;
    for (LegalizePosition = DAG.allnodes_end();
         LegalizePosition != DAG.allnodes_begin(); ) {
      --LegalizePosition;

      SDNode *N = LegalizePosition;
      if (LegalizedNodes.insert(N)) {
        AnyLegalized = true;
        LegalizeOp(N);
      }
    }
    if (!AnyLegalized)
      break;

  }

  // Remove dead nodes now.
  DAG.RemoveDeadNodes();
}

/// ExpandConstantFP - Expands the ConstantFP node to an integer constant or
/// a load from the constant pool.
SDValue
SelectionDAGLegalize::ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP) {
  bool Extend = false;
  SDLoc dl(CFP);

  // If a FP immediate is precise when represented as a float and if the
  // target can do an extending load from float to double, we put it into
  // the constant pool as a float, even if it's is statically typed as a
  // double.  This shrinks FP constants and canonicalizes them for targets where
  // an FP extending load is the same cost as a normal load (such as on the x87
  // fp stack or PPC FP unit).
  EVT VT = CFP->getValueType(0);
  ConstantFP *LLVMC = const_cast<ConstantFP*>(CFP->getConstantFPValue());
  if (!UseCP) {
    assert((VT == MVT::f64 || VT == MVT::f32) && "Invalid type expansion");
    return DAG.getConstant(LLVMC->getValueAPF().bitcastToAPInt(),
                           (VT == MVT::f64) ? MVT::i64 : MVT::i32);
  }

  EVT OrigVT = VT;
  EVT SVT = VT;
  while (SVT != MVT::f32) {
    SVT = (MVT::SimpleValueType)(SVT.getSimpleVT().SimpleTy - 1);
    if (ConstantFPSDNode::isValueValidForType(SVT, CFP->getValueAPF()) &&
        // Only do this if the target has a native EXTLOAD instruction from
        // smaller type.
        TLI.isLoadExtLegal(ISD::EXTLOAD, SVT) &&
        TLI.ShouldShrinkFPConstant(OrigVT)) {
      Type *SType = SVT.getTypeForEVT(*DAG.getContext());
      LLVMC = cast<ConstantFP>(ConstantExpr::getFPTrunc(LLVMC, SType));
      VT = SVT;
      Extend = true;
    }
  }

  SDValue CPIdx = DAG.getConstantPool(LLVMC, TLI.getPointerTy());
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
  if (Extend) {
    SDValue Result =
      DAG.getExtLoad(ISD::EXTLOAD, dl, OrigVT,
                     DAG.getEntryNode(),
                     CPIdx, MachinePointerInfo::getConstantPool(),
                     VT, false, false, Alignment);
    return Result;
  }
  SDValue Result =
    DAG.getLoad(OrigVT, dl, DAG.getEntryNode(), CPIdx,
                MachinePointerInfo::getConstantPool(), false, false, false,
                Alignment);
  return Result;
}

/// ExpandUnalignedStore - Expands an unaligned store to 2 half-size stores.
static void ExpandUnalignedStore(StoreSDNode *ST, SelectionDAG &DAG,
                                 const TargetLowering &TLI,
                                 SelectionDAGLegalize *DAGLegalize) {
  assert(ST->getAddressingMode() == ISD::UNINDEXED &&
         "unaligned indexed stores not implemented!");
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  SDValue Val = ST->getValue();
  EVT VT = Val.getValueType();
  int Alignment = ST->getAlignment();
  SDLoc dl(ST);
  if (ST->getMemoryVT().isFloatingPoint() ||
      ST->getMemoryVT().isVector()) {
    EVT intVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
    if (TLI.isTypeLegal(intVT)) {
      // Expand to a bitconvert of the value to the integer type of the
      // same size, then a (misaligned) int store.
      // FIXME: Does not handle truncating floating point stores!
      SDValue Result = DAG.getNode(ISD::BITCAST, dl, intVT, Val);
      Result = DAG.getStore(Chain, dl, Result, Ptr, ST->getPointerInfo(),
                           ST->isVolatile(), ST->isNonTemporal(), Alignment);
      DAGLegalize->ReplaceNode(SDValue(ST, 0), Result);
      return;
    }
    // Do a (aligned) store to a stack slot, then copy from the stack slot
    // to the final destination using (unaligned) integer loads and stores.
    EVT StoredVT = ST->getMemoryVT();
    MVT RegVT =
      TLI.getRegisterType(*DAG.getContext(),
                          EVT::getIntegerVT(*DAG.getContext(),
                                            StoredVT.getSizeInBits()));
    unsigned StoredBytes = StoredVT.getSizeInBits() / 8;
    unsigned RegBytes = RegVT.getSizeInBits() / 8;
    unsigned NumRegs = (StoredBytes + RegBytes - 1) / RegBytes;

    // Make sure the stack slot is also aligned for the register type.
    SDValue StackPtr = DAG.CreateStackTemporary(StoredVT, RegVT);

    // Perform the original store, only redirected to the stack slot.
    SDValue Store = DAG.getTruncStore(Chain, dl,
                                      Val, StackPtr, MachinePointerInfo(),
                                      StoredVT, false, false, 0);
    SDValue Increment = DAG.getConstant(RegBytes, TLI.getPointerTy());
    SmallVector<SDValue, 8> Stores;
    unsigned Offset = 0;

    // Do all but one copies using the full register width.
    for (unsigned i = 1; i < NumRegs; i++) {
      // Load one integer register's worth from the stack slot.
      SDValue Load = DAG.getLoad(RegVT, dl, Store, StackPtr,
                                 MachinePointerInfo(),
                                 false, false, false, 0);
      // Store it to the final location.  Remember the store.
      Stores.push_back(DAG.getStore(Load.getValue(1), dl, Load, Ptr,
                                  ST->getPointerInfo().getWithOffset(Offset),
                                    ST->isVolatile(), ST->isNonTemporal(),
                                    MinAlign(ST->getAlignment(), Offset)));
      // Increment the pointers.
      Offset += RegBytes;
      StackPtr = DAG.getNode(ISD::ADD, dl, StackPtr.getValueType(), StackPtr,
                             Increment);
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr, Increment);
    }

    // The last store may be partial.  Do a truncating store.  On big-endian
    // machines this requires an extending load from the stack slot to ensure
    // that the bits are in the right place.
    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(),
                                  8 * (StoredBytes - Offset));

    // Load from the stack slot.
    SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, RegVT, Store, StackPtr,
                                  MachinePointerInfo(),
                                  MemVT, false, false, 0);

    Stores.push_back(DAG.getTruncStore(Load.getValue(1), dl, Load, Ptr,
                                       ST->getPointerInfo()
                                         .getWithOffset(Offset),
                                       MemVT, ST->isVolatile(),
                                       ST->isNonTemporal(),
                                       MinAlign(ST->getAlignment(), Offset)));
    // The order of the stores doesn't matter - say it with a TokenFactor.
    SDValue Result =
      DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &Stores[0],
                  Stores.size());
    DAGLegalize->ReplaceNode(SDValue(ST, 0), Result);
    return;
  }
  assert(ST->getMemoryVT().isInteger() &&
         !ST->getMemoryVT().isVector() &&
         "Unaligned store of unknown type.");
  // Get the half-size VT
  EVT NewStoredVT = ST->getMemoryVT().getHalfSizedIntegerVT(*DAG.getContext());
  int NumBits = NewStoredVT.getSizeInBits();
  int IncrementSize = NumBits / 8;

  // Divide the stored value in two parts.
  SDValue ShiftAmount = DAG.getConstant(NumBits,
                                      TLI.getShiftAmountTy(Val.getValueType()));
  SDValue Lo = Val;
  SDValue Hi = DAG.getNode(ISD::SRL, dl, VT, Val, ShiftAmount);

  // Store the two parts
  SDValue Store1, Store2;
  Store1 = DAG.getTruncStore(Chain, dl, TLI.isLittleEndian()?Lo:Hi, Ptr,
                             ST->getPointerInfo(), NewStoredVT,
                             ST->isVolatile(), ST->isNonTemporal(), Alignment);
  Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                    DAG.getConstant(IncrementSize, TLI.getPointerTy()));
  Alignment = MinAlign(Alignment, IncrementSize);
  Store2 = DAG.getTruncStore(Chain, dl, TLI.isLittleEndian()?Hi:Lo, Ptr,
                             ST->getPointerInfo().getWithOffset(IncrementSize),
                             NewStoredVT, ST->isVolatile(), ST->isNonTemporal(),
                             Alignment);

  SDValue Result =
    DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Store1, Store2);
  DAGLegalize->ReplaceNode(SDValue(ST, 0), Result);
}

/// ExpandUnalignedLoad - Expands an unaligned load to 2 half-size loads.
static void
ExpandUnalignedLoad(LoadSDNode *LD, SelectionDAG &DAG,
                    const TargetLowering &TLI,
                    SDValue &ValResult, SDValue &ChainResult) {
  assert(LD->getAddressingMode() == ISD::UNINDEXED &&
         "unaligned indexed loads not implemented!");
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  EVT VT = LD->getValueType(0);
  EVT LoadedVT = LD->getMemoryVT();
  SDLoc dl(LD);
  if (VT.isFloatingPoint() || VT.isVector()) {
    EVT intVT = EVT::getIntegerVT(*DAG.getContext(), LoadedVT.getSizeInBits());
    if (TLI.isTypeLegal(intVT) && TLI.isTypeLegal(LoadedVT)) {
      // Expand to a (misaligned) integer load of the same size,
      // then bitconvert to floating point or vector.
      SDValue newLoad = DAG.getLoad(intVT, dl, Chain, Ptr, LD->getPointerInfo(),
                                    LD->isVolatile(),
                                    LD->isNonTemporal(),
                                    LD->isInvariant(), LD->getAlignment());
      SDValue Result = DAG.getNode(ISD::BITCAST, dl, LoadedVT, newLoad);
      if (LoadedVT != VT)
        Result = DAG.getNode(VT.isFloatingPoint() ? ISD::FP_EXTEND :
                             ISD::ANY_EXTEND, dl, VT, Result);

      ValResult = Result;
      ChainResult = Chain;
      return;
    }

    // Copy the value to a (aligned) stack slot using (unaligned) integer
    // loads and stores, then do a (aligned) load from the stack slot.
    MVT RegVT = TLI.getRegisterType(*DAG.getContext(), intVT);
    unsigned LoadedBytes = LoadedVT.getSizeInBits() / 8;
    unsigned RegBytes = RegVT.getSizeInBits() / 8;
    unsigned NumRegs = (LoadedBytes + RegBytes - 1) / RegBytes;

    // Make sure the stack slot is also aligned for the register type.
    SDValue StackBase = DAG.CreateStackTemporary(LoadedVT, RegVT);

    SDValue Increment = DAG.getConstant(RegBytes, TLI.getPointerTy());
    SmallVector<SDValue, 8> Stores;
    SDValue StackPtr = StackBase;
    unsigned Offset = 0;

    // Do all but one copies using the full register width.
    for (unsigned i = 1; i < NumRegs; i++) {
      // Load one integer register's worth from the original location.
      SDValue Load = DAG.getLoad(RegVT, dl, Chain, Ptr,
                                 LD->getPointerInfo().getWithOffset(Offset),
                                 LD->isVolatile(), LD->isNonTemporal(),
                                 LD->isInvariant(),
                                 MinAlign(LD->getAlignment(), Offset));
      // Follow the load with a store to the stack slot.  Remember the store.
      Stores.push_back(DAG.getStore(Load.getValue(1), dl, Load, StackPtr,
                                    MachinePointerInfo(), false, false, 0));
      // Increment the pointers.
      Offset += RegBytes;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr, Increment);
      StackPtr = DAG.getNode(ISD::ADD, dl, StackPtr.getValueType(), StackPtr,
                             Increment);
    }

    // The last copy may be partial.  Do an extending load.
    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(),
                                  8 * (LoadedBytes - Offset));
    SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, RegVT, Chain, Ptr,
                                  LD->getPointerInfo().getWithOffset(Offset),
                                  MemVT, LD->isVolatile(),
                                  LD->isNonTemporal(),
                                  MinAlign(LD->getAlignment(), Offset));
    // Follow the load with a store to the stack slot.  Remember the store.
    // On big-endian machines this requires a truncating store to ensure
    // that the bits end up in the right place.
    Stores.push_back(DAG.getTruncStore(Load.getValue(1), dl, Load, StackPtr,
                                       MachinePointerInfo(), MemVT,
                                       false, false, 0));

    // The order of the stores doesn't matter - say it with a TokenFactor.
    SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &Stores[0],
                             Stores.size());

    // Finally, perform the original load only redirected to the stack slot.
    Load = DAG.getExtLoad(LD->getExtensionType(), dl, VT, TF, StackBase,
                          MachinePointerInfo(), LoadedVT, false, false, 0);

    // Callers expect a MERGE_VALUES node.
    ValResult = Load;
    ChainResult = TF;
    return;
  }
  assert(LoadedVT.isInteger() && !LoadedVT.isVector() &&
         "Unaligned load of unsupported type.");

  // Compute the new VT that is half the size of the old one.  This is an
  // integer MVT.
  unsigned NumBits = LoadedVT.getSizeInBits();
  EVT NewLoadedVT;
  NewLoadedVT = EVT::getIntegerVT(*DAG.getContext(), NumBits/2);
  NumBits >>= 1;

  unsigned Alignment = LD->getAlignment();
  unsigned IncrementSize = NumBits / 8;
  ISD::LoadExtType HiExtType = LD->getExtensionType();

  // If the original load is NON_EXTLOAD, the hi part load must be ZEXTLOAD.
  if (HiExtType == ISD::NON_EXTLOAD)
    HiExtType = ISD::ZEXTLOAD;

  // Load the value in two parts
  SDValue Lo, Hi;
  if (TLI.isLittleEndian()) {
    Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, VT, Chain, Ptr, LD->getPointerInfo(),
                        NewLoadedVT, LD->isVolatile(),
                        LD->isNonTemporal(), Alignment);
    Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                      DAG.getConstant(IncrementSize, TLI.getPointerTy()));
    Hi = DAG.getExtLoad(HiExtType, dl, VT, Chain, Ptr,
                        LD->getPointerInfo().getWithOffset(IncrementSize),
                        NewLoadedVT, LD->isVolatile(),
                        LD->isNonTemporal(), MinAlign(Alignment,IncrementSize));
  } else {
    Hi = DAG.getExtLoad(HiExtType, dl, VT, Chain, Ptr, LD->getPointerInfo(),
                        NewLoadedVT, LD->isVolatile(),
                        LD->isNonTemporal(), Alignment);
    Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                      DAG.getConstant(IncrementSize, TLI.getPointerTy()));
    Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, VT, Chain, Ptr,
                        LD->getPointerInfo().getWithOffset(IncrementSize),
                        NewLoadedVT, LD->isVolatile(),
                        LD->isNonTemporal(), MinAlign(Alignment,IncrementSize));
  }

  // aggregate the two parts
  SDValue ShiftAmount = DAG.getConstant(NumBits,
                                       TLI.getShiftAmountTy(Hi.getValueType()));
  SDValue Result = DAG.getNode(ISD::SHL, dl, VT, Hi, ShiftAmount);
  Result = DAG.getNode(ISD::OR, dl, VT, Result, Lo);

  SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                             Hi.getValue(1));

  ValResult = Result;
  ChainResult = TF;
}

/// PerformInsertVectorEltInMemory - Some target cannot handle a variable
/// insertion index for the INSERT_VECTOR_ELT instruction.  In this case, it
/// is necessary to spill the vector being inserted into to memory, perform
/// the insert there, and then read the result back.
SDValue SelectionDAGLegalize::
PerformInsertVectorEltInMemory(SDValue Vec, SDValue Val, SDValue Idx,
                               SDLoc dl) {
  SDValue Tmp1 = Vec;
  SDValue Tmp2 = Val;
  SDValue Tmp3 = Idx;

  // If the target doesn't support this, we have to spill the input vector
  // to a temporary stack slot, update the element, then reload it.  This is
  // badness.  We could also load the value into a vector register (either
  // with a "move to register" or "extload into register" instruction, then
  // permute it into place, if the idx is a constant and if the idx is
  // supported by the target.
  EVT VT    = Tmp1.getValueType();
  EVT EltVT = VT.getVectorElementType();
  EVT IdxVT = Tmp3.getValueType();
  EVT PtrVT = TLI.getPointerTy();
  SDValue StackPtr = DAG.CreateStackTemporary(VT);

  int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();

  // Store the vector.
  SDValue Ch = DAG.getStore(DAG.getEntryNode(), dl, Tmp1, StackPtr,
                            MachinePointerInfo::getFixedStack(SPFI),
                            false, false, 0);

  // Truncate or zero extend offset to target pointer type.
  unsigned CastOpc = IdxVT.bitsGT(PtrVT) ? ISD::TRUNCATE : ISD::ZERO_EXTEND;
  Tmp3 = DAG.getNode(CastOpc, dl, PtrVT, Tmp3);
  // Add the offset to the index.
  unsigned EltSize = EltVT.getSizeInBits()/8;
  Tmp3 = DAG.getNode(ISD::MUL, dl, IdxVT, Tmp3,DAG.getConstant(EltSize, IdxVT));
  SDValue StackPtr2 = DAG.getNode(ISD::ADD, dl, IdxVT, Tmp3, StackPtr);
  // Store the scalar value.
  Ch = DAG.getTruncStore(Ch, dl, Tmp2, StackPtr2, MachinePointerInfo(), EltVT,
                         false, false, 0);
  // Load the updated vector.
  return DAG.getLoad(VT, dl, Ch, StackPtr,
                     MachinePointerInfo::getFixedStack(SPFI), false, false,
                     false, 0);
}


SDValue SelectionDAGLegalize::
ExpandINSERT_VECTOR_ELT(SDValue Vec, SDValue Val, SDValue Idx, SDLoc dl) {
  if (ConstantSDNode *InsertPos = dyn_cast<ConstantSDNode>(Idx)) {
    // SCALAR_TO_VECTOR requires that the type of the value being inserted
    // match the element type of the vector being created, except for
    // integers in which case the inserted value can be over width.
    EVT EltVT = Vec.getValueType().getVectorElementType();
    if (Val.getValueType() == EltVT ||
        (EltVT.isInteger() && Val.getValueType().bitsGE(EltVT))) {
      SDValue ScVec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl,
                                  Vec.getValueType(), Val);

      unsigned NumElts = Vec.getValueType().getVectorNumElements();
      // We generate a shuffle of InVec and ScVec, so the shuffle mask
      // should be 0,1,2,3,4,5... with the appropriate element replaced with
      // elt 0 of the RHS.
      SmallVector<int, 8> ShufOps;
      for (unsigned i = 0; i != NumElts; ++i)
        ShufOps.push_back(i != InsertPos->getZExtValue() ? i : NumElts);

      return DAG.getVectorShuffle(Vec.getValueType(), dl, Vec, ScVec,
                                  &ShufOps[0]);
    }
  }
  return PerformInsertVectorEltInMemory(Vec, Val, Idx, dl);
}

SDValue SelectionDAGLegalize::OptimizeFloatStore(StoreSDNode* ST) {
  // Turn 'store float 1.0, Ptr' -> 'store int 0x12345678, Ptr'
  // FIXME: We shouldn't do this for TargetConstantFP's.
  // FIXME: move this to the DAG Combiner!  Note that we can't regress due
  // to phase ordering between legalized code and the dag combiner.  This
  // probably means that we need to integrate dag combiner and legalizer
  // together.
  // We generally can't do this one for long doubles.
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  unsigned Alignment = ST->getAlignment();
  bool isVolatile = ST->isVolatile();
  bool isNonTemporal = ST->isNonTemporal();
  SDLoc dl(ST);
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(ST->getValue())) {
    if (CFP->getValueType(0) == MVT::f32 &&
        TLI.isTypeLegal(MVT::i32)) {
      SDValue Con = DAG.getConstant(CFP->getValueAPF().
                                      bitcastToAPInt().zextOrTrunc(32),
                              MVT::i32);
      return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(),
                          isVolatile, isNonTemporal, Alignment);
    }

    if (CFP->getValueType(0) == MVT::f64) {
      // If this target supports 64-bit registers, do a single 64-bit store.
      if (TLI.isTypeLegal(MVT::i64)) {
        SDValue Con = DAG.getConstant(CFP->getValueAPF().bitcastToAPInt().
                                  zextOrTrunc(64), MVT::i64);
        return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(),
                            isVolatile, isNonTemporal, Alignment);
      }

      if (TLI.isTypeLegal(MVT::i32) && !ST->isVolatile()) {
        // Otherwise, if the target supports 32-bit registers, use 2 32-bit
        // stores.  If the target supports neither 32- nor 64-bits, this
        // xform is certainly not worth it.
        const APInt &IntVal =CFP->getValueAPF().bitcastToAPInt();
        SDValue Lo = DAG.getConstant(IntVal.trunc(32), MVT::i32);
        SDValue Hi = DAG.getConstant(IntVal.lshr(32).trunc(32), MVT::i32);
        if (TLI.isBigEndian()) std::swap(Lo, Hi);

        Lo = DAG.getStore(Chain, dl, Lo, Ptr, ST->getPointerInfo(), isVolatile,
                          isNonTemporal, Alignment);
        Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                            DAG.getIntPtrConstant(4));
        Hi = DAG.getStore(Chain, dl, Hi, Ptr,
                          ST->getPointerInfo().getWithOffset(4),
                          isVolatile, isNonTemporal, MinAlign(Alignment, 4U));

        return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
      }
    }
  }
  return SDValue(0, 0);
}

void SelectionDAGLegalize::LegalizeStoreOps(SDNode *Node) {
    StoreSDNode *ST = cast<StoreSDNode>(Node);
    SDValue Chain = ST->getChain();
    SDValue Ptr = ST->getBasePtr();
    SDLoc dl(Node);

    unsigned Alignment = ST->getAlignment();
    bool isVolatile = ST->isVolatile();
    bool isNonTemporal = ST->isNonTemporal();

    if (!ST->isTruncatingStore()) {
      if (SDNode *OptStore = OptimizeFloatStore(ST).getNode()) {
        ReplaceNode(ST, OptStore);
        return;
      }

      {
        SDValue Value = ST->getValue();
        MVT VT = Value.getSimpleValueType();
        switch (TLI.getOperationAction(ISD::STORE, VT)) {
        default: llvm_unreachable("This action is not supported yet!");
        case TargetLowering::Legal:
          // If this is an unaligned store and the target doesn't support it,
          // expand it.
          if (!TLI.allowsUnalignedMemoryAccesses(ST->getMemoryVT())) {
            Type *Ty = ST->getMemoryVT().getTypeForEVT(*DAG.getContext());
            unsigned ABIAlignment= TLI.getDataLayout()->getABITypeAlignment(Ty);
            if (ST->getAlignment() < ABIAlignment)
              ExpandUnalignedStore(cast<StoreSDNode>(Node),
                                   DAG, TLI, this);
          }
          break;
        case TargetLowering::Custom: {
          SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
          if (Res.getNode())
            ReplaceNode(SDValue(Node, 0), Res);
          return;
        }
        case TargetLowering::Promote: {
          MVT NVT = TLI.getTypeToPromoteTo(ISD::STORE, VT);
          assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
                 "Can only promote stores to same size type");
          Value = DAG.getNode(ISD::BITCAST, dl, NVT, Value);
          SDValue Result =
            DAG.getStore(Chain, dl, Value, Ptr,
                         ST->getPointerInfo(), isVolatile,
                         isNonTemporal, Alignment);
          ReplaceNode(SDValue(Node, 0), Result);
          break;
        }
        }
        return;
      }
    } else {
      SDValue Value = ST->getValue();

      EVT StVT = ST->getMemoryVT();
      unsigned StWidth = StVT.getSizeInBits();

      if (StWidth != StVT.getStoreSizeInBits()) {
        // Promote to a byte-sized store with upper bits zero if not
        // storing an integral number of bytes.  For example, promote
        // TRUNCSTORE:i1 X -> TRUNCSTORE:i8 (and X, 1)
        EVT NVT = EVT::getIntegerVT(*DAG.getContext(),
                                    StVT.getStoreSizeInBits());
        Value = DAG.getZeroExtendInReg(Value, dl, StVT);
        SDValue Result =
          DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                            NVT, isVolatile, isNonTemporal, Alignment);
        ReplaceNode(SDValue(Node, 0), Result);
      } else if (StWidth & (StWidth - 1)) {
        // If not storing a power-of-2 number of bits, expand as two stores.
        assert(!StVT.isVector() && "Unsupported truncstore!");
        unsigned RoundWidth = 1 << Log2_32(StWidth);
        assert(RoundWidth < StWidth);
        unsigned ExtraWidth = StWidth - RoundWidth;
        assert(ExtraWidth < RoundWidth);
        assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
               "Store size not an integral number of bytes!");
        EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
        EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
        SDValue Lo, Hi;
        unsigned IncrementSize;

        if (TLI.isLittleEndian()) {
          // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 X, TRUNCSTORE@+2:i8 (srl X, 16)
          // Store the bottom RoundWidth bits.
          Lo = DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                                 RoundVT,
                                 isVolatile, isNonTemporal, Alignment);

          // Store the remaining ExtraWidth bits.
          IncrementSize = RoundWidth / 8;
          Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                             DAG.getIntPtrConstant(IncrementSize));
          Hi = DAG.getNode(ISD::SRL, dl, Value.getValueType(), Value,
                           DAG.getConstant(RoundWidth,
                                    TLI.getShiftAmountTy(Value.getValueType())));
          Hi = DAG.getTruncStore(Chain, dl, Hi, Ptr,
                             ST->getPointerInfo().getWithOffset(IncrementSize),
                                 ExtraVT, isVolatile, isNonTemporal,
                                 MinAlign(Alignment, IncrementSize));
        } else {
          // Big endian - avoid unaligned stores.
          // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 (srl X, 8), TRUNCSTORE@+2:i8 X
          // Store the top RoundWidth bits.
          Hi = DAG.getNode(ISD::SRL, dl, Value.getValueType(), Value,
                           DAG.getConstant(ExtraWidth,
                                    TLI.getShiftAmountTy(Value.getValueType())));
          Hi = DAG.getTruncStore(Chain, dl, Hi, Ptr, ST->getPointerInfo(),
                                 RoundVT, isVolatile, isNonTemporal, Alignment);

          // Store the remaining ExtraWidth bits.
          IncrementSize = RoundWidth / 8;
          Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                             DAG.getIntPtrConstant(IncrementSize));
          Lo = DAG.getTruncStore(Chain, dl, Value, Ptr,
                              ST->getPointerInfo().getWithOffset(IncrementSize),
                                 ExtraVT, isVolatile, isNonTemporal,
                                 MinAlign(Alignment, IncrementSize));
        }

        // The order of the stores doesn't matter.
        SDValue Result = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
        ReplaceNode(SDValue(Node, 0), Result);
      } else {
        switch (TLI.getTruncStoreAction(ST->getValue().getSimpleValueType(),
                                        StVT.getSimpleVT())) {
        default: llvm_unreachable("This action is not supported yet!");
        case TargetLowering::Legal:
          // If this is an unaligned store and the target doesn't support it,
          // expand it.
          if (!TLI.allowsUnalignedMemoryAccesses(ST->getMemoryVT())) {
            Type *Ty = ST->getMemoryVT().getTypeForEVT(*DAG.getContext());
            unsigned ABIAlignment= TLI.getDataLayout()->getABITypeAlignment(Ty);
            if (ST->getAlignment() < ABIAlignment)
              ExpandUnalignedStore(cast<StoreSDNode>(Node), DAG, TLI, this);
          }
          break;
        case TargetLowering::Custom: {
          SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
          if (Res.getNode())
            ReplaceNode(SDValue(Node, 0), Res);
          return;
        }
        case TargetLowering::Expand:
          assert(!StVT.isVector() &&
                 "Vector Stores are handled in LegalizeVectorOps");

          // TRUNCSTORE:i16 i32 -> STORE i16
          assert(TLI.isTypeLegal(StVT) &&
                 "Do not know how to expand this store!");
          Value = DAG.getNode(ISD::TRUNCATE, dl, StVT, Value);
          SDValue Result =
            DAG.getStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                         isVolatile, isNonTemporal, Alignment);
          ReplaceNode(SDValue(Node, 0), Result);
          break;
        }
      }
    }
}

void SelectionDAGLegalize::LegalizeLoadOps(SDNode *Node) {
  LoadSDNode *LD = cast<LoadSDNode>(Node);
  SDValue Chain = LD->getChain();  // The chain.
  SDValue Ptr = LD->getBasePtr();  // The base pointer.
  SDValue Value;                   // The value returned by the load op.
  SDLoc dl(Node);

  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD) {
    MVT VT = Node->getSimpleValueType(0);
    SDValue RVal = SDValue(Node, 0);
    SDValue RChain = SDValue(Node, 1);

    switch (TLI.getOperationAction(Node->getOpcode(), VT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal:
      // If this is an unaligned load and the target doesn't support it,
      // expand it.
      if (!TLI.allowsUnalignedMemoryAccesses(LD->getMemoryVT())) {
        Type *Ty = LD->getMemoryVT().getTypeForEVT(*DAG.getContext());
        unsigned ABIAlignment =
          TLI.getDataLayout()->getABITypeAlignment(Ty);
        if (LD->getAlignment() < ABIAlignment){
          ExpandUnalignedLoad(cast<LoadSDNode>(Node), DAG, TLI, RVal, RChain);
        }
      }
      break;
    case TargetLowering::Custom: {
      SDValue Res = TLI.LowerOperation(RVal, DAG);
      if (Res.getNode()) {
        RVal = Res;
        RChain = Res.getValue(1);
      }
      break;
    }
    case TargetLowering::Promote: {
      MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
      assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
             "Can only promote loads to same size type");

      SDValue Res = DAG.getLoad(NVT, dl, Chain, Ptr, LD->getPointerInfo(),
                         LD->isVolatile(), LD->isNonTemporal(),
                         LD->isInvariant(), LD->getAlignment());
      RVal = DAG.getNode(ISD::BITCAST, dl, VT, Res);
      RChain = Res.getValue(1);
      break;
    }
    }
    if (RChain.getNode() != Node) {
      assert(RVal.getNode() != Node && "Load must be completely replaced");
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), RVal);
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), RChain);
      ReplacedNode(Node);
    }
    return;
  }

  EVT SrcVT = LD->getMemoryVT();
  unsigned SrcWidth = SrcVT.getSizeInBits();
  unsigned Alignment = LD->getAlignment();
  bool isVolatile = LD->isVolatile();
  bool isNonTemporal = LD->isNonTemporal();

  if (SrcWidth != SrcVT.getStoreSizeInBits() &&
      // Some targets pretend to have an i1 loading operation, and actually
      // load an i8.  This trick is correct for ZEXTLOAD because the top 7
      // bits are guaranteed to be zero; it helps the optimizers understand
      // that these bits are zero.  It is also useful for EXTLOAD, since it
      // tells the optimizers that those bits are undefined.  It would be
      // nice to have an effective generic way of getting these benefits...
      // Until such a way is found, don't insist on promoting i1 here.
      (SrcVT != MVT::i1 ||
       TLI.getLoadExtAction(ExtType, MVT::i1) == TargetLowering::Promote)) {
    // Promote to a byte-sized load if not loading an integral number of
    // bytes.  For example, promote EXTLOAD:i20 -> EXTLOAD:i24.
    unsigned NewWidth = SrcVT.getStoreSizeInBits();
    EVT NVT = EVT::getIntegerVT(*DAG.getContext(), NewWidth);
    SDValue Ch;

    // The extra bits are guaranteed to be zero, since we stored them that
    // way.  A zext load from NVT thus automatically gives zext from SrcVT.

    ISD::LoadExtType NewExtType =
      ExtType == ISD::ZEXTLOAD ? ISD::ZEXTLOAD : ISD::EXTLOAD;

    SDValue Result =
      DAG.getExtLoad(NewExtType, dl, Node->getValueType(0),
                     Chain, Ptr, LD->getPointerInfo(),
                     NVT, isVolatile, isNonTemporal, Alignment);

    Ch = Result.getValue(1); // The chain.

    if (ExtType == ISD::SEXTLOAD)
      // Having the top bits zero doesn't help when sign extending.
      Result = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                           Result.getValueType(),
                           Result, DAG.getValueType(SrcVT));
    else if (ExtType == ISD::ZEXTLOAD || NVT == Result.getValueType())
      // All the top bits are guaranteed to be zero - inform the optimizers.
      Result = DAG.getNode(ISD::AssertZext, dl,
                           Result.getValueType(), Result,
                           DAG.getValueType(SrcVT));

    Value = Result;
    Chain = Ch;
  } else if (SrcWidth & (SrcWidth - 1)) {
    // If not loading a power-of-2 number of bits, expand as two loads.
    assert(!SrcVT.isVector() && "Unsupported extload!");
    unsigned RoundWidth = 1 << Log2_32(SrcWidth);
    assert(RoundWidth < SrcWidth);
    unsigned ExtraWidth = SrcWidth - RoundWidth;
    assert(ExtraWidth < RoundWidth);
    assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
           "Load size not an integral number of bytes!");
    EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
    EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
    SDValue Lo, Hi, Ch;
    unsigned IncrementSize;

    if (TLI.isLittleEndian()) {
      // EXTLOAD:i24 -> ZEXTLOAD:i16 | (shl EXTLOAD@+2:i8, 16)
      // Load the bottom RoundWidth bits.
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, Node->getValueType(0),
                          Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, isVolatile,
                          isNonTemporal, Alignment);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                         DAG.getIntPtrConstant(IncrementSize));
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, isVolatile, isNonTemporal,
                          MinAlign(Alignment, IncrementSize));

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(ISD::SHL, dl, Hi.getValueType(), Hi,
                       DAG.getConstant(RoundWidth,
                                       TLI.getShiftAmountTy(Hi.getValueType())));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    } else {
      // Big endian - avoid unaligned loads.
      // EXTLOAD:i24 -> (shl EXTLOAD:i16, 8) | ZEXTLOAD@+2:i8
      // Load the top RoundWidth bits.
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, isVolatile,
                          isNonTemporal, Alignment);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                         DAG.getIntPtrConstant(IncrementSize));
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD,
                          dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, isVolatile, isNonTemporal,
                          MinAlign(Alignment, IncrementSize));

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(ISD::SHL, dl, Hi.getValueType(), Hi,
                       DAG.getConstant(ExtraWidth,
                                       TLI.getShiftAmountTy(Hi.getValueType())));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    }

    Chain = Ch;
  } else {
    bool isCustom = false;
    switch (TLI.getLoadExtAction(ExtType, SrcVT.getSimpleVT())) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Custom:
             isCustom = true;
             // FALLTHROUGH
    case TargetLowering::Legal: {
             Value = SDValue(Node, 0);
             Chain = SDValue(Node, 1);

             if (isCustom) {
               SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
               if (Res.getNode()) {
                 Value = Res;
                 Chain = Res.getValue(1);
               }
             } else {
               // If this is an unaligned load and the target doesn't support it,
               // expand it.
               if (!TLI.allowsUnalignedMemoryAccesses(LD->getMemoryVT())) {
                 Type *Ty =
                   LD->getMemoryVT().getTypeForEVT(*DAG.getContext());
                 unsigned ABIAlignment =
                   TLI.getDataLayout()->getABITypeAlignment(Ty);
                 if (LD->getAlignment() < ABIAlignment){
                   ExpandUnalignedLoad(cast<LoadSDNode>(Node),
                                       DAG, TLI, Value, Chain);
                 }
               }
             }
             break;
    }
    case TargetLowering::Expand:
             if (!TLI.isLoadExtLegal(ISD::EXTLOAD, SrcVT) && TLI.isTypeLegal(SrcVT)) {
               SDValue Load = DAG.getLoad(SrcVT, dl, Chain, Ptr,
                                          LD->getPointerInfo(),
                                          LD->isVolatile(), LD->isNonTemporal(),
                                          LD->isInvariant(), LD->getAlignment());
               unsigned ExtendOp;
               switch (ExtType) {
               case ISD::EXTLOAD:
                 ExtendOp = (SrcVT.isFloatingPoint() ?
                             ISD::FP_EXTEND : ISD::ANY_EXTEND);
                 break;
               case ISD::SEXTLOAD: ExtendOp = ISD::SIGN_EXTEND; break;
               case ISD::ZEXTLOAD: ExtendOp = ISD::ZERO_EXTEND; break;
               default: llvm_unreachable("Unexpected extend load type!");
               }
               Value = DAG.getNode(ExtendOp, dl, Node->getValueType(0), Load);
               Chain = Load.getValue(1);
               break;
             }

             assert(!SrcVT.isVector() &&
                    "Vector Loads are handled in LegalizeVectorOps");

             // FIXME: This does not work for vectors on most targets.  Sign- and
             // zero-extend operations are currently folded into extending loads,
             // whether they are legal or not, and then we end up here without any
             // support for legalizing them.
             assert(ExtType != ISD::EXTLOAD &&
                    "EXTLOAD should always be supported!");
             // Turn the unsupported load into an EXTLOAD followed by an explicit
             // zero/sign extend inreg.
             SDValue Result = DAG.getExtLoad(ISD::EXTLOAD, dl, Node->getValueType(0),
                                             Chain, Ptr, LD->getPointerInfo(), SrcVT,
                                             LD->isVolatile(), LD->isNonTemporal(),
                                             LD->getAlignment());
             SDValue ValRes;
             if (ExtType == ISD::SEXTLOAD)
               ValRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                                    Result.getValueType(),
                                    Result, DAG.getValueType(SrcVT));
             else
               ValRes = DAG.getZeroExtendInReg(Result, dl, SrcVT.getScalarType());
             Value = ValRes;
             Chain = Result.getValue(1);
             break;
    }
  }

  // Since loads produce two values, make sure to remember that we legalized
  // both of them.
  if (Chain.getNode() != Node) {
    assert(Value.getNode() != Node && "Load must be completely replaced");
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Value);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    ReplacedNode(Node);
  }
}

/// LegalizeOp - Return a legal replacement for the given operation, with
/// all legal operands.
void SelectionDAGLegalize::LegalizeOp(SDNode *Node) {
  if (Node->getOpcode() == ISD::TargetConstant) // Allow illegal target nodes.
    return;

  for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
    assert(TLI.getTypeAction(*DAG.getContext(), Node->getValueType(i)) ==
             TargetLowering::TypeLegal &&
           "Unexpected illegal type!");

  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i)
    assert((TLI.getTypeAction(*DAG.getContext(),
                              Node->getOperand(i).getValueType()) ==
              TargetLowering::TypeLegal ||
            Node->getOperand(i).getOpcode() == ISD::TargetConstant) &&
           "Unexpected illegal type!");

  // Figure out the correct action; the way to query this varies by opcode
  TargetLowering::LegalizeAction Action = TargetLowering::Legal;
  bool SimpleFinishLegalizing = true;
  switch (Node->getOpcode()) {
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
  case ISD::STACKSAVE:
    Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::VAARG:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getValueType(0));
    if (Action != TargetLowering::Promote)
      Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::EXTRACT_VECTOR_ELT:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(0).getValueType());
    break;
  case ISD::FP_ROUND_INREG:
  case ISD::SIGN_EXTEND_INREG: {
    EVT InnerType = cast<VTSDNode>(Node->getOperand(1))->getVT();
    Action = TLI.getOperationAction(Node->getOpcode(), InnerType);
    break;
  }
  case ISD::ATOMIC_STORE: {
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(2).getValueType());
    break;
  }
  case ISD::SELECT_CC:
  case ISD::SETCC:
  case ISD::BR_CC: {
    unsigned CCOperand = Node->getOpcode() == ISD::SELECT_CC ? 4 :
                         Node->getOpcode() == ISD::SETCC ? 2 : 1;
    unsigned CompareOperand = Node->getOpcode() == ISD::BR_CC ? 2 : 0;
    MVT OpVT = Node->getOperand(CompareOperand).getSimpleValueType();
    ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(CCOperand))->get();
    Action = TLI.getCondCodeAction(CCCode, OpVT);
    if (Action == TargetLowering::Legal) {
      if (Node->getOpcode() == ISD::SELECT_CC)
        Action = TLI.getOperationAction(Node->getOpcode(),
                                        Node->getValueType(0));
      else
        Action = TLI.getOperationAction(Node->getOpcode(), OpVT);
    }
    break;
  }
  case ISD::LOAD:
  case ISD::STORE:
    // FIXME: Model these properly.  LOAD and STORE are complicated, and
    // STORE expects the unlegalized operand in some cases.
    SimpleFinishLegalizing = false;
    break;
  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    // FIXME: This shouldn't be necessary.  These nodes have special properties
    // dealing with the recursive nature of legalization.  Removing this
    // special case should be done as part of making LegalizeDAG non-recursive.
    SimpleFinishLegalizing = false;
    break;
  case ISD::EXTRACT_ELEMENT:
  case ISD::FLT_ROUNDS_:
  case ISD::SADDO:
  case ISD::SSUBO:
  case ISD::UADDO:
  case ISD::USUBO:
  case ISD::SMULO:
  case ISD::UMULO:
  case ISD::FPOWI:
  case ISD::MERGE_VALUES:
  case ISD::EH_RETURN:
  case ISD::FRAME_TO_ARGS_OFFSET:
  case ISD::EH_SJLJ_SETJMP:
  case ISD::EH_SJLJ_LONGJMP:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be expanded.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Expand;
    break;
  case ISD::INIT_TRAMPOLINE:
  case ISD::ADJUST_TRAMPOLINE:
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be custom-lowered.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Custom;
    break;
  case ISD::DEBUGTRAP:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Expand) {
      // replace ISD::DEBUGTRAP with ISD::TRAP
      SDValue NewVal;
      NewVal = DAG.getNode(ISD::TRAP, SDLoc(Node), Node->getVTList(),
                           Node->getOperand(0));
      ReplaceNode(Node, NewVal.getNode());
      LegalizeOp(NewVal.getNode());
      return;
    }
    break;

  default:
    if (Node->getOpcode() >= ISD::BUILTIN_OP_END) {
      Action = TargetLowering::Legal;
    } else {
      Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    }
    break;
  }

  if (SimpleFinishLegalizing) {
    SDNode *NewNode = Node;
    switch (Node->getOpcode()) {
    default: break;
    case ISD::SHL:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::ROTL:
    case ISD::ROTR:
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      if (!Node->getOperand(1).getValueType().isVector()) {
        SDValue SAO =
          DAG.getShiftAmountOperand(Node->getOperand(0).getValueType(),
                                    Node->getOperand(1));
        HandleSDNode Handle(SAO);
        LegalizeOp(SAO.getNode());
        NewNode = DAG.UpdateNodeOperands(Node, Node->getOperand(0),
                                         Handle.getValue());
      }
      break;
    case ISD::SRL_PARTS:
    case ISD::SRA_PARTS:
    case ISD::SHL_PARTS:
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      if (!Node->getOperand(2).getValueType().isVector()) {
        SDValue SAO =
          DAG.getShiftAmountOperand(Node->getOperand(0).getValueType(),
                                    Node->getOperand(2));
        HandleSDNode Handle(SAO);
        LegalizeOp(SAO.getNode());
        NewNode = DAG.UpdateNodeOperands(Node, Node->getOperand(0),
                                         Node->getOperand(1),
                                         Handle.getValue());
      }
      break;
    }

    if (NewNode != Node) {
      DAG.ReplaceAllUsesWith(Node, NewNode);
      for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
        DAG.TransferDbgValues(SDValue(Node, i), SDValue(NewNode, i));
      ReplacedNode(Node);
      Node = NewNode;
    }
    switch (Action) {
    case TargetLowering::Legal:
      return;
    case TargetLowering::Custom: {
      // FIXME: The handling for custom lowering with multiple results is
      // a complete mess.
      SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
      if (Res.getNode()) {
        SmallVector<SDValue, 8> ResultVals;
        for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i) {
          if (e == 1)
            ResultVals.push_back(Res);
          else
            ResultVals.push_back(Res.getValue(i));
        }
        if (Res.getNode() != Node || Res.getResNo() != 0) {
          DAG.ReplaceAllUsesWith(Node, ResultVals.data());
          for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
            DAG.TransferDbgValues(SDValue(Node, i), ResultVals[i]);
          ReplacedNode(Node);
        }
        return;
      }
    }
      // FALL THROUGH
    case TargetLowering::Expand:
      ExpandNode(Node);
      return;
    case TargetLowering::Promote:
      PromoteNode(Node);
      return;
    }
  }

  switch (Node->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "NODE: ";
    Node->dump( &DAG);
    dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to legalize this operator!");

  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    break;
  case ISD::LOAD: {
    return LegalizeLoadOps(Node);
  }
  case ISD::STORE: {
    return LegalizeStoreOps(Node);
  }
  }
}

SDValue SelectionDAGLegalize::ExpandExtractFromVectorThroughStack(SDValue Op) {
  SDValue Vec = Op.getOperand(0);
  SDValue Idx = Op.getOperand(1);
  SDLoc dl(Op);
  // Store the value to a temporary stack slot, then LOAD the returned part.
  SDValue StackPtr = DAG.CreateStackTemporary(Vec.getValueType());
  SDValue Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr,
                            MachinePointerInfo(), false, false, 0);

  // Add the offset to the index.
  unsigned EltSize =
      Vec.getValueType().getVectorElementType().getSizeInBits()/8;
  Idx = DAG.getNode(ISD::MUL, dl, Idx.getValueType(), Idx,
                    DAG.getConstant(EltSize, Idx.getValueType()));

  if (Idx.getValueType().bitsGT(TLI.getPointerTy()))
    Idx = DAG.getNode(ISD::TRUNCATE, dl, TLI.getPointerTy(), Idx);
  else
    Idx = DAG.getNode(ISD::ZERO_EXTEND, dl, TLI.getPointerTy(), Idx);

  StackPtr = DAG.getNode(ISD::ADD, dl, Idx.getValueType(), Idx, StackPtr);

  if (Op.getValueType().isVector())
    return DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr,MachinePointerInfo(),
                       false, false, false, 0);
  return DAG.getExtLoad(ISD::EXTLOAD, dl, Op.getValueType(), Ch, StackPtr,
                        MachinePointerInfo(),
                        Vec.getValueType().getVectorElementType(),
                        false, false, 0);
}

SDValue SelectionDAGLegalize::ExpandInsertToVectorThroughStack(SDValue Op) {
  assert(Op.getValueType().isVector() && "Non-vector insert subvector!");

  SDValue Vec  = Op.getOperand(0);
  SDValue Part = Op.getOperand(1);
  SDValue Idx  = Op.getOperand(2);
  SDLoc dl(Op);

  // Store the value to a temporary stack slot, then LOAD the returned part.

  SDValue StackPtr = DAG.CreateStackTemporary(Vec.getValueType());
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(FI);

  // First store the whole vector.
  SDValue Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo,
                            false, false, 0);

  // Then store the inserted part.

  // Add the offset to the index.
  unsigned EltSize =
      Vec.getValueType().getVectorElementType().getSizeInBits()/8;

  Idx = DAG.getNode(ISD::MUL, dl, Idx.getValueType(), Idx,
                    DAG.getConstant(EltSize, Idx.getValueType()));

  if (Idx.getValueType().bitsGT(TLI.getPointerTy()))
    Idx = DAG.getNode(ISD::TRUNCATE, dl, TLI.getPointerTy(), Idx);
  else
    Idx = DAG.getNode(ISD::ZERO_EXTEND, dl, TLI.getPointerTy(), Idx);

  SDValue SubStackPtr = DAG.getNode(ISD::ADD, dl, Idx.getValueType(), Idx,
                                    StackPtr);

  // Store the subvector.
  Ch = DAG.getStore(DAG.getEntryNode(), dl, Part, SubStackPtr,
                    MachinePointerInfo(), false, false, 0);

  // Finally, load the updated vector.
  return DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr, PtrInfo,
                     false, false, false, 0);
}

SDValue SelectionDAGLegalize::ExpandVectorBuildThroughStack(SDNode* Node) {
  // We can't handle this case efficiently.  Allocate a sufficiently
  // aligned object on the stack, store each element into it, then load
  // the result as a vector.
  // Create the stack frame object.
  EVT VT = Node->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(Node);
  SDValue FIPtr = DAG.CreateStackTemporary(VT);
  int FI = cast<FrameIndexSDNode>(FIPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(FI);

  // Emit a store of each element to the stack slot.
  SmallVector<SDValue, 8> Stores;
  unsigned TypeByteSize = EltVT.getSizeInBits() / 8;
  // Store (in the right endianness) the elements to memory.
  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i) {
    // Ignore undef elements.
    if (Node->getOperand(i).getOpcode() == ISD::UNDEF) continue;

    unsigned Offset = TypeByteSize*i;

    SDValue Idx = DAG.getConstant(Offset, FIPtr.getValueType());
    Idx = DAG.getNode(ISD::ADD, dl, FIPtr.getValueType(), FIPtr, Idx);

    // If the destination vector element type is narrower than the source
    // element type, only store the bits necessary.
    if (EltVT.bitsLT(Node->getOperand(i).getValueType().getScalarType())) {
      Stores.push_back(DAG.getTruncStore(DAG.getEntryNode(), dl,
                                         Node->getOperand(i), Idx,
                                         PtrInfo.getWithOffset(Offset),
                                         EltVT, false, false, 0));
    } else
      Stores.push_back(DAG.getStore(DAG.getEntryNode(), dl,
                                    Node->getOperand(i), Idx,
                                    PtrInfo.getWithOffset(Offset),
                                    false, false, 0));
  }

  SDValue StoreChain;
  if (!Stores.empty())    // Not all undef elements?
    StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                             &Stores[0], Stores.size());
  else
    StoreChain = DAG.getEntryNode();

  // Result is a load from the stack slot.
  return DAG.getLoad(VT, dl, StoreChain, FIPtr, PtrInfo,
                     false, false, false, 0);
}

SDValue SelectionDAGLegalize::ExpandFCOPYSIGN(SDNode* Node) {
  SDLoc dl(Node);
  SDValue Tmp1 = Node->getOperand(0);
  SDValue Tmp2 = Node->getOperand(1);

  // Get the sign bit of the RHS.  First obtain a value that has the same
  // sign as the sign bit, i.e. negative if and only if the sign bit is 1.
  SDValue SignBit;
  EVT FloatVT = Tmp2.getValueType();
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), FloatVT.getSizeInBits());
  if (TLI.isTypeLegal(IVT)) {
    // Convert to an integer with the same sign bit.
    SignBit = DAG.getNode(ISD::BITCAST, dl, IVT, Tmp2);
  } else {
    // Store the float to memory, then load the sign part out as an integer.
    MVT LoadTy = TLI.getPointerTy();
    // First create a temporary that is aligned for both the load and store.
    SDValue StackPtr = DAG.CreateStackTemporary(FloatVT, LoadTy);
    // Then store the float to it.
    SDValue Ch =
      DAG.getStore(DAG.getEntryNode(), dl, Tmp2, StackPtr, MachinePointerInfo(),
                   false, false, 0);
    if (TLI.isBigEndian()) {
      assert(FloatVT.isByteSized() && "Unsupported floating point type!");
      // Load out a legal integer with the same sign bit as the float.
      SignBit = DAG.getLoad(LoadTy, dl, Ch, StackPtr, MachinePointerInfo(),
                            false, false, false, 0);
    } else { // Little endian
      SDValue LoadPtr = StackPtr;
      // The float may be wider than the integer we are going to load.  Advance
      // the pointer so that the loaded integer will contain the sign bit.
      unsigned Strides = (FloatVT.getSizeInBits()-1)/LoadTy.getSizeInBits();
      unsigned ByteOffset = (Strides * LoadTy.getSizeInBits()) / 8;
      LoadPtr = DAG.getNode(ISD::ADD, dl, LoadPtr.getValueType(),
                            LoadPtr, DAG.getIntPtrConstant(ByteOffset));
      // Load a legal integer containing the sign bit.
      SignBit = DAG.getLoad(LoadTy, dl, Ch, LoadPtr, MachinePointerInfo(),
                            false, false, false, 0);
      // Move the sign bit to the top bit of the loaded integer.
      unsigned BitShift = LoadTy.getSizeInBits() -
        (FloatVT.getSizeInBits() - 8 * ByteOffset);
      assert(BitShift < LoadTy.getSizeInBits() && "Pointer advanced wrong?");
      if (BitShift)
        SignBit = DAG.getNode(ISD::SHL, dl, LoadTy, SignBit,
                              DAG.getConstant(BitShift,
                                 TLI.getShiftAmountTy(SignBit.getValueType())));
    }
  }
  // Now get the sign bit proper, by seeing whether the value is negative.
  SignBit = DAG.getSetCC(dl, getSetCCResultType(SignBit.getValueType()),
                         SignBit, DAG.getConstant(0, SignBit.getValueType()),
                         ISD::SETLT);
  // Get the absolute value of the result.
  SDValue AbsVal = DAG.getNode(ISD::FABS, dl, Tmp1.getValueType(), Tmp1);
  // Select between the nabs and abs value based on the sign bit of
  // the input.
  return DAG.getSelect(dl, AbsVal.getValueType(), SignBit,
                       DAG.getNode(ISD::FNEG, dl, AbsVal.getValueType(), AbsVal),
                       AbsVal);
}

void SelectionDAGLegalize::ExpandDYNAMIC_STACKALLOC(SDNode* Node,
                                           SmallVectorImpl<SDValue> &Results) {
  unsigned SPReg = TLI.getStackPointerRegisterToSaveRestore();
  assert(SPReg && "Target cannot require DYNAMIC_STACKALLOC expansion and"
          " not tell us which reg is the stack pointer!");
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  SDValue Tmp1 = SDValue(Node, 0);
  SDValue Tmp2 = SDValue(Node, 1);
  SDValue Tmp3 = Node->getOperand(2);
  SDValue Chain = Tmp1.getOperand(0);

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, DAG.getIntPtrConstant(0, true),
                               SDLoc(Node));

  SDValue Size  = Tmp2.getOperand(1);
  SDValue SP = DAG.getCopyFromReg(Chain, dl, SPReg, VT);
  Chain = SP.getValue(1);
  unsigned Align = cast<ConstantSDNode>(Tmp3)->getZExtValue();
  unsigned StackAlign = TM.getFrameLowering()->getStackAlignment();
  if (Align > StackAlign)
    SP = DAG.getNode(ISD::AND, dl, VT, SP,
                      DAG.getConstant(-(uint64_t)Align, VT));
  Tmp1 = DAG.getNode(ISD::SUB, dl, VT, SP, Size);       // Value
  Chain = DAG.getCopyToReg(Chain, dl, SPReg, Tmp1);     // Output chain

  Tmp2 = DAG.getCALLSEQ_END(Chain,  DAG.getIntPtrConstant(0, true),
                            DAG.getIntPtrConstant(0, true), SDValue(),
                            SDLoc(Node));

  Results.push_back(Tmp1);
  Results.push_back(Tmp2);
}

/// LegalizeSetCCCondCode - Legalize a SETCC with given LHS and RHS and
/// condition code CC on the current target. This routine expands SETCC with
/// illegal condition code into AND / OR of multiple SETCC values.
void SelectionDAGLegalize::LegalizeSetCCCondCode(EVT VT,
                                                 SDValue &LHS, SDValue &RHS,
                                                 SDValue &CC,
                                                 SDLoc dl) {
  MVT OpVT = LHS.getSimpleValueType();
  ISD::CondCode CCCode = cast<CondCodeSDNode>(CC)->get();
  switch (TLI.getCondCodeAction(CCCode, OpVT)) {
  default: llvm_unreachable("Unknown condition code action!");
  case TargetLowering::Legal:
    // Nothing to do.
    break;
  case TargetLowering::Expand: {
    ISD::CondCode CC1 = ISD::SETCC_INVALID, CC2 = ISD::SETCC_INVALID;
    ISD::CondCode InvCC = ISD::SETCC_INVALID;
    unsigned Opc = 0;
    switch (CCCode) {
    default: llvm_unreachable("Don't know how to expand this condition!");
    case ISD::SETO:
        assert(TLI.getCondCodeAction(ISD::SETOEQ, OpVT)
            == TargetLowering::Legal
            && "If SETO is expanded, SETOEQ must be legal!");
        CC1 = ISD::SETOEQ; CC2 = ISD::SETOEQ; Opc = ISD::AND; break;
    case ISD::SETUO:
        assert(TLI.getCondCodeAction(ISD::SETUNE, OpVT)
            == TargetLowering::Legal
            && "If SETUO is expanded, SETUNE must be legal!");
        CC1 = ISD::SETUNE; CC2 = ISD::SETUNE; Opc = ISD::OR;  break;
    case ISD::SETOEQ:
    case ISD::SETOGT:
    case ISD::SETOGE:
    case ISD::SETOLT:
    case ISD::SETOLE:
    case ISD::SETONE:
    case ISD::SETUEQ:
    case ISD::SETUNE:
    case ISD::SETUGT:
    case ISD::SETUGE:
    case ISD::SETULT:
    case ISD::SETULE:
        // If we are floating point, assign and break, otherwise fall through.
        if (!OpVT.isInteger()) {
          // We can use the 4th bit to tell if we are the unordered
          // or ordered version of the opcode.
          CC2 = ((unsigned)CCCode & 0x8U) ? ISD::SETUO : ISD::SETO;
          Opc = ((unsigned)CCCode & 0x8U) ? ISD::OR : ISD::AND;
          CC1 = (ISD::CondCode)(((int)CCCode & 0x7) | 0x10);
          break;
        }
        // Fallthrough if we are unsigned integer.
    case ISD::SETLE:
    case ISD::SETGT:
    case ISD::SETGE:
    case ISD::SETLT:
    case ISD::SETNE:
    case ISD::SETEQ:
      InvCC = ISD::getSetCCSwappedOperands(CCCode);
      if (TLI.getCondCodeAction(InvCC, OpVT) == TargetLowering::Expand) {
        // We only support using the inverted operation and not a
        // different manner of supporting expanding these cases.
        llvm_unreachable("Don't know how to expand this condition!");
      }
      LHS = DAG.getSetCC(dl, VT, RHS, LHS, InvCC);
      RHS = SDValue();
      CC = SDValue();
      return;
    }

    SDValue SetCC1, SetCC2;
    if (CCCode != ISD::SETO && CCCode != ISD::SETUO) {
      // If we aren't the ordered or unorder operation,
      // then the pattern is (LHS CC1 RHS) Opc (LHS CC2 RHS).
      SetCC1 = DAG.getSetCC(dl, VT, LHS, RHS, CC1);
      SetCC2 = DAG.getSetCC(dl, VT, LHS, RHS, CC2);
    } else {
      // Otherwise, the pattern is (LHS CC1 LHS) Opc (RHS CC2 RHS)
      SetCC1 = DAG.getSetCC(dl, VT, LHS, LHS, CC1);
      SetCC2 = DAG.getSetCC(dl, VT, RHS, RHS, CC2);
    }
    LHS = DAG.getNode(Opc, dl, VT, SetCC1, SetCC2);
    RHS = SDValue();
    CC  = SDValue();
    break;
  }
  }
}

/// EmitStackConvert - Emit a store/load combination to the stack.  This stores
/// SrcOp to a stack slot of type SlotVT, truncating it if needed.  It then does
/// a load from the stack slot to DestVT, extending it if needed.
/// The resultant code need not be legal.
SDValue SelectionDAGLegalize::EmitStackConvert(SDValue SrcOp,
                                               EVT SlotVT,
                                               EVT DestVT,
                                               SDLoc dl) {
  // Create the stack frame object.
  unsigned SrcAlign =
    TLI.getDataLayout()->getPrefTypeAlignment(SrcOp.getValueType().
                                              getTypeForEVT(*DAG.getContext()));
  SDValue FIPtr = DAG.CreateStackTemporary(SlotVT, SrcAlign);

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(FIPtr);
  int SPFI = StackPtrFI->getIndex();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(SPFI);

  unsigned SrcSize = SrcOp.getValueType().getSizeInBits();
  unsigned SlotSize = SlotVT.getSizeInBits();
  unsigned DestSize = DestVT.getSizeInBits();
  Type *DestType = DestVT.getTypeForEVT(*DAG.getContext());
  unsigned DestAlign = TLI.getDataLayout()->getPrefTypeAlignment(DestType);

  // Emit a store to the stack slot.  Use a truncstore if the input value is
  // later than DestVT.
  SDValue Store;

  if (SrcSize > SlotSize)
    Store = DAG.getTruncStore(DAG.getEntryNode(), dl, SrcOp, FIPtr,
                              PtrInfo, SlotVT, false, false, SrcAlign);
  else {
    assert(SrcSize == SlotSize && "Invalid store");
    Store = DAG.getStore(DAG.getEntryNode(), dl, SrcOp, FIPtr,
                         PtrInfo, false, false, SrcAlign);
  }

  // Result is a load from the stack slot.
  if (SlotSize == DestSize)
    return DAG.getLoad(DestVT, dl, Store, FIPtr, PtrInfo,
                       false, false, false, DestAlign);

  assert(SlotSize < DestSize && "Unknown extension!");
  return DAG.getExtLoad(ISD::EXTLOAD, dl, DestVT, Store, FIPtr,
                        PtrInfo, SlotVT, false, false, DestAlign);
}

SDValue SelectionDAGLegalize::ExpandSCALAR_TO_VECTOR(SDNode *Node) {
  SDLoc dl(Node);
  // Create a vector sized/aligned stack slot, store the value to element #0,
  // then load the whole vector back out.
  SDValue StackPtr = DAG.CreateStackTemporary(Node->getValueType(0));

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(StackPtr);
  int SPFI = StackPtrFI->getIndex();

  SDValue Ch = DAG.getTruncStore(DAG.getEntryNode(), dl, Node->getOperand(0),
                                 StackPtr,
                                 MachinePointerInfo::getFixedStack(SPFI),
                                 Node->getValueType(0).getVectorElementType(),
                                 false, false, 0);
  return DAG.getLoad(Node->getValueType(0), dl, Ch, StackPtr,
                     MachinePointerInfo::getFixedStack(SPFI),
                     false, false, false, 0);
}


/// ExpandBUILD_VECTOR - Expand a BUILD_VECTOR node on targets that don't
/// support the operation, but do support the resultant vector type.
SDValue SelectionDAGLegalize::ExpandBUILD_VECTOR(SDNode *Node) {
  unsigned NumElems = Node->getNumOperands();
  SDValue Value1, Value2;
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT OpVT = Node->getOperand(0).getValueType();
  EVT EltVT = VT.getVectorElementType();

  // If the only non-undef value is the low element, turn this into a
  // SCALAR_TO_VECTOR node.  If this is { X, X, X, X }, determine X.
  bool isOnlyLowElement = true;
  bool MoreThanTwoValues = false;
  bool isConstant = true;
  for (unsigned i = 0; i < NumElems; ++i) {
    SDValue V = Node->getOperand(i);
    if (V.getOpcode() == ISD::UNDEF)
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    if (!Value1.getNode()) {
      Value1 = V;
    } else if (!Value2.getNode()) {
      if (V != Value1)
        Value2 = V;
    } else if (V != Value1 && V != Value2) {
      MoreThanTwoValues = true;
    }
  }

  if (!Value1.getNode())
    return DAG.getUNDEF(VT);

  if (isOnlyLowElement)
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Node->getOperand(0));

  // If all elements are constants, create a load from the constant pool.
  if (isConstant) {
    SmallVector<Constant*, 16> CV;
    for (unsigned i = 0, e = NumElems; i != e; ++i) {
      if (ConstantFPSDNode *V =
          dyn_cast<ConstantFPSDNode>(Node->getOperand(i))) {
        CV.push_back(const_cast<ConstantFP *>(V->getConstantFPValue()));
      } else if (ConstantSDNode *V =
                 dyn_cast<ConstantSDNode>(Node->getOperand(i))) {
        if (OpVT==EltVT)
          CV.push_back(const_cast<ConstantInt *>(V->getConstantIntValue()));
        else {
          // If OpVT and EltVT don't match, EltVT is not legal and the
          // element values have been promoted/truncated earlier.  Undo this;
          // we don't want a v16i8 to become a v16i32 for example.
          const ConstantInt *CI = V->getConstantIntValue();
          CV.push_back(ConstantInt::get(EltVT.getTypeForEVT(*DAG.getContext()),
                                        CI->getZExtValue()));
        }
      } else {
        assert(Node->getOperand(i).getOpcode() == ISD::UNDEF);
        Type *OpNTy = EltVT.getTypeForEVT(*DAG.getContext());
        CV.push_back(UndefValue::get(OpNTy));
      }
    }
    Constant *CP = ConstantVector::get(CV);
    SDValue CPIdx = DAG.getConstantPool(CP, TLI.getPointerTy());
    unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
    return DAG.getLoad(VT, dl, DAG.getEntryNode(), CPIdx,
                       MachinePointerInfo::getConstantPool(),
                       false, false, false, Alignment);
  }

  if (!MoreThanTwoValues) {
    SmallVector<int, 8> ShuffleVec(NumElems, -1);
    for (unsigned i = 0; i < NumElems; ++i) {
      SDValue V = Node->getOperand(i);
      if (V.getOpcode() == ISD::UNDEF)
        continue;
      ShuffleVec[i] = V == Value1 ? 0 : NumElems;
    }
    if (TLI.isShuffleMaskLegal(ShuffleVec, Node->getValueType(0))) {
      // Get the splatted value into the low element of a vector register.
      SDValue Vec1 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value1);
      SDValue Vec2;
      if (Value2.getNode())
        Vec2 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value2);
      else
        Vec2 = DAG.getUNDEF(VT);

      // Return shuffle(LowValVec, undef, <0,0,0,0>)
      return DAG.getVectorShuffle(VT, dl, Vec1, Vec2, ShuffleVec.data());
    }
  }

  // Otherwise, we can't handle this case efficiently.
  return ExpandVectorBuildThroughStack(Node);
}

// ExpandLibCall - Expand a node into a call to a libcall.  If the result value
// does not fit into a register, return the lo part and set the hi part to the
// by-reg argument.  If it does fit into a single register, return the result
// and leave the Hi part unset.
SDValue SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, SDNode *Node,
                                            bool isSigned) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i) {
    EVT ArgVT = Node->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Node->getOperand(i); Entry.Ty = ArgTy;
    Entry.isSExt = isSigned;
    Entry.isZExt = !isSigned;
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy());

  Type *RetTy = Node->getValueType(0).getTypeForEVT(*DAG.getContext());

  // By default, the input chain to this libcall is the entry node of the
  // function. If the libcall is going to be emitted as a tail call then
  // TLI.isUsedByReturnOnly will change it to the right chain if the return
  // node which is being folded has a non-entry input chain.
  SDValue InChain = DAG.getEntryNode();

  // isTailCall may be true since the callee does not reference caller stack
  // frame. Check if it's in the right position.
  SDValue TCChain = InChain;
  bool isTailCall = TLI.isInTailCallPosition(DAG, Node, TCChain);
  if (isTailCall)
    InChain = TCChain;

  TargetLowering::
  CallLoweringInfo CLI(InChain, RetTy, isSigned, !isSigned, false, false,
                    0, TLI.getLibcallCallingConv(LC), isTailCall,
                    /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                    Callee, Args, DAG, SDLoc(Node));
  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);


  if (!CallInfo.second.getNode())
    // It's a tailcall, return the chain (which is the DAG root).
    return DAG.getRoot();

  return CallInfo.first;
}

/// ExpandLibCall - Generate a libcall taking the given operands as arguments
/// and returning a result of type RetVT.
SDValue SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, EVT RetVT,
                                            const SDValue *Ops, unsigned NumOps,
                                            bool isSigned, SDLoc dl) {
  TargetLowering::ArgListTy Args;
  Args.reserve(NumOps);

  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0; i != NumOps; ++i) {
    Entry.Node = Ops[i];
    Entry.Ty = Entry.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Entry.isSExt = isSigned;
    Entry.isZExt = !isSigned;
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy());

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  TargetLowering::
  CallLoweringInfo CLI(DAG.getEntryNode(), RetTy, isSigned, !isSigned, false,
                       false, 0, TLI.getLibcallCallingConv(LC),
                       /*isTailCall=*/false,
                  /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                  Callee, Args, DAG, dl);
  std::pair<SDValue,SDValue> CallInfo = TLI.LowerCallTo(CLI);

  return CallInfo.first;
}

// ExpandChainLibCall - Expand a node into a call to a libcall. Similar to
// ExpandLibCall except that the first operand is the in-chain.
std::pair<SDValue, SDValue>
SelectionDAGLegalize::ExpandChainLibCall(RTLIB::Libcall LC,
                                         SDNode *Node,
                                         bool isSigned) {
  SDValue InChain = Node->getOperand(0);

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 1, e = Node->getNumOperands(); i != e; ++i) {
    EVT ArgVT = Node->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Node->getOperand(i);
    Entry.Ty = ArgTy;
    Entry.isSExt = isSigned;
    Entry.isZExt = !isSigned;
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy());

  Type *RetTy = Node->getValueType(0).getTypeForEVT(*DAG.getContext());
  TargetLowering::
  CallLoweringInfo CLI(InChain, RetTy, isSigned, !isSigned, false, false,
                    0, TLI.getLibcallCallingConv(LC), /*isTailCall=*/false,
                    /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                    Callee, Args, DAG, SDLoc(Node));
  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  return CallInfo;
}

SDValue SelectionDAGLegalize::ExpandFPLibCall(SDNode* Node,
                                              RTLIB::Libcall Call_F32,
                                              RTLIB::Libcall Call_F64,
                                              RTLIB::Libcall Call_F80,
                                              RTLIB::Libcall Call_F128,
                                              RTLIB::Libcall Call_PPCF128) {
  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32: LC = Call_F32; break;
  case MVT::f64: LC = Call_F64; break;
  case MVT::f80: LC = Call_F80; break;
  case MVT::f128: LC = Call_F128; break;
  case MVT::ppcf128: LC = Call_PPCF128; break;
  }
  return ExpandLibCall(LC, Node, false);
}

SDValue SelectionDAGLegalize::ExpandIntLibCall(SDNode* Node, bool isSigned,
                                               RTLIB::Libcall Call_I8,
                                               RTLIB::Libcall Call_I16,
                                               RTLIB::Libcall Call_I32,
                                               RTLIB::Libcall Call_I64,
                                               RTLIB::Libcall Call_I128) {
  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC = Call_I8; break;
  case MVT::i16:  LC = Call_I16; break;
  case MVT::i32:  LC = Call_I32; break;
  case MVT::i64:  LC = Call_I64; break;
  case MVT::i128: LC = Call_I128; break;
  }
  return ExpandLibCall(LC, Node, isSigned);
}

/// isDivRemLibcallAvailable - Return true if divmod libcall is available.
static bool isDivRemLibcallAvailable(SDNode *Node, bool isSigned,
                                     const TargetLowering &TLI) {
  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC= isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16:  LC= isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32:  LC= isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64:  LC= isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  case MVT::i128: LC= isSigned ? RTLIB::SDIVREM_I128:RTLIB::UDIVREM_I128; break;
  }

  return TLI.getLibcallName(LC) != 0;
}

/// useDivRem - Only issue divrem libcall if both quotient and remainder are
/// needed.
static bool useDivRem(SDNode *Node, bool isSigned, bool isDIV) {
  // The other use might have been replaced with a divrem already.
  unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
  unsigned OtherOpcode = 0;
  if (isSigned)
    OtherOpcode = isDIV ? ISD::SREM : ISD::SDIV;
  else
    OtherOpcode = isDIV ? ISD::UREM : ISD::UDIV;

  SDValue Op0 = Node->getOperand(0);
  SDValue Op1 = Node->getOperand(1);
  for (SDNode::use_iterator UI = Op0.getNode()->use_begin(),
         UE = Op0.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User == Node)
      continue;
    if ((User->getOpcode() == OtherOpcode || User->getOpcode() == DivRemOpc) &&
        User->getOperand(0) == Op0 &&
        User->getOperand(1) == Op1)
      return true;
  }
  return false;
}

/// ExpandDivRemLibCall - Issue libcalls to __{u}divmod to compute div / rem
/// pairs.
void
SelectionDAGLegalize::ExpandDivRemLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  unsigned Opcode = Node->getOpcode();
  bool isSigned = Opcode == ISD::SDIVREM;

  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC= isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16:  LC= isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32:  LC= isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64:  LC= isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  case MVT::i128: LC= isSigned ? RTLIB::SDIVREM_I128:RTLIB::UDIVREM_I128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i) {
    EVT ArgVT = Node->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Node->getOperand(i); Entry.Ty = ArgTy;
    Entry.isSExt = isSigned;
    Entry.isZExt = !isSigned;
    Args.push_back(Entry);
  }

  // Also pass the return address of the remainder.
  SDValue FIPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = FIPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.isSExt = isSigned;
  Entry.isZExt = !isSigned;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy());

  SDLoc dl(Node);
  TargetLowering::
  CallLoweringInfo CLI(InChain, RetTy, isSigned, !isSigned, false, false,
                    0, TLI.getLibcallCallingConv(LC), /*isTailCall=*/false,
                    /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                    Callee, Args, DAG, dl);
  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  // Remainder is loaded back from the stack frame.
  SDValue Rem = DAG.getLoad(RetVT, dl, CallInfo.second, FIPtr,
                            MachinePointerInfo(), false, false, false, 0);
  Results.push_back(CallInfo.first);
  Results.push_back(Rem);
}

/// isSinCosLibcallAvailable - Return true if sincos libcall is available.
static bool isSinCosLibcallAvailable(SDNode *Node, const TargetLowering &TLI) {
  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }
  return TLI.getLibcallName(LC) != 0;
}

/// canCombineSinCosLibcall - Return true if sincos libcall is available and
/// can be used to combine sin and cos.
static bool canCombineSinCosLibcall(SDNode *Node, const TargetLowering &TLI,
                                    const TargetMachine &TM) {
  if (!isSinCosLibcallAvailable(Node, TLI))
    return false;
  // GNU sin/cos functions set errno while sincos does not. Therefore
  // combining sin and cos is only safe if unsafe-fpmath is enabled.
  bool isGNU = Triple(TM.getTargetTriple()).getEnvironment() == Triple::GNU;
  if (isGNU && !TM.Options.UnsafeFPMath)
    return false;
  return true;
}

/// useSinCos - Only issue sincos libcall if both sin and cos are
/// needed.
static bool useSinCos(SDNode *Node) {
  unsigned OtherOpcode = Node->getOpcode() == ISD::FSIN
    ? ISD::FCOS : ISD::FSIN;

  SDValue Op0 = Node->getOperand(0);
  for (SDNode::use_iterator UI = Op0.getNode()->use_begin(),
       UE = Op0.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User == Node)
      continue;
    // The other user might have been turned into sincos already.
    if (User->getOpcode() == OtherOpcode || User->getOpcode() == ISD::FSINCOS)
      return true;
  }
  return false;
}

/// ExpandSinCosLibCall - Issue libcalls to sincos to compute sin / cos
/// pairs.
void
SelectionDAGLegalize::ExpandSinCosLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  RTLIB::Libcall LC;
  switch (Node->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  // Pass the argument.
  Entry.Node = Node->getOperand(0);
  Entry.Ty = RetTy;
  Entry.isSExt = false;
  Entry.isZExt = false;
  Args.push_back(Entry);

  // Pass the return address of sin.
  SDValue SinPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = SinPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.isSExt = false;
  Entry.isZExt = false;
  Args.push_back(Entry);

  // Also pass the return address of the cos.
  SDValue CosPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = CosPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.isSExt = false;
  Entry.isZExt = false;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy());

  SDLoc dl(Node);
  TargetLowering::
  CallLoweringInfo CLI(InChain, Type::getVoidTy(*DAG.getContext()),
                       false, false, false, false,
                       0, TLI.getLibcallCallingConv(LC), /*isTailCall=*/false,
                       /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                       Callee, Args, DAG, dl);
  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  Results.push_back(DAG.getLoad(RetVT, dl, CallInfo.second, SinPtr,
                                MachinePointerInfo(), false, false, false, 0));
  Results.push_back(DAG.getLoad(RetVT, dl, CallInfo.second, CosPtr,
                                MachinePointerInfo(), false, false, false, 0));
}

/// ExpandLegalINT_TO_FP - This function is responsible for legalizing a
/// INT_TO_FP operation of the specified operand when the target requests that
/// we expand it.  At this point, we know that the result and operand types are
/// legal for the target.
SDValue SelectionDAGLegalize::ExpandLegalINT_TO_FP(bool isSigned,
                                                   SDValue Op0,
                                                   EVT DestVT,
                                                   SDLoc dl) {
  if (Op0.getValueType() == MVT::i32 && TLI.isTypeLegal(MVT::f64)) {
    // simple 32-bit [signed|unsigned] integer to float/double expansion

    // Get the stack frame index of a 8 byte buffer.
    SDValue StackSlot = DAG.CreateStackTemporary(MVT::f64);

    // word offset constant for Hi/Lo address computation
    SDValue WordOff = DAG.getConstant(sizeof(int), TLI.getPointerTy());
    // set up Hi and Lo (into buffer) address based on endian
    SDValue Hi = StackSlot;
    SDValue Lo = DAG.getNode(ISD::ADD, dl,
                             TLI.getPointerTy(), StackSlot, WordOff);
    if (TLI.isLittleEndian())
      std::swap(Hi, Lo);

    // if signed map to unsigned space
    SDValue Op0Mapped;
    if (isSigned) {
      // constant used to invert sign bit (signed to unsigned mapping)
      SDValue SignBit = DAG.getConstant(0x80000000u, MVT::i32);
      Op0Mapped = DAG.getNode(ISD::XOR, dl, MVT::i32, Op0, SignBit);
    } else {
      Op0Mapped = Op0;
    }
    // store the lo of the constructed double - based on integer input
    SDValue Store1 = DAG.getStore(DAG.getEntryNode(), dl,
                                  Op0Mapped, Lo, MachinePointerInfo(),
                                  false, false, 0);
    // initial hi portion of constructed double
    SDValue InitialHi = DAG.getConstant(0x43300000u, MVT::i32);
    // store the hi of the constructed double - biased exponent
    SDValue Store2 = DAG.getStore(Store1, dl, InitialHi, Hi,
                                  MachinePointerInfo(),
                                  false, false, 0);
    // load the constructed double
    SDValue Load = DAG.getLoad(MVT::f64, dl, Store2, StackSlot,
                               MachinePointerInfo(), false, false, false, 0);
    // FP constant to bias correct the final result
    SDValue Bias = DAG.getConstantFP(isSigned ?
                                     BitsToDouble(0x4330000080000000ULL) :
                                     BitsToDouble(0x4330000000000000ULL),
                                     MVT::f64);
    // subtract the bias
    SDValue Sub = DAG.getNode(ISD::FSUB, dl, MVT::f64, Load, Bias);
    // final result
    SDValue Result;
    // handle final rounding
    if (DestVT == MVT::f64) {
      // do nothing
      Result = Sub;
    } else if (DestVT.bitsLT(MVT::f64)) {
      Result = DAG.getNode(ISD::FP_ROUND, dl, DestVT, Sub,
                           DAG.getIntPtrConstant(0));
    } else if (DestVT.bitsGT(MVT::f64)) {
      Result = DAG.getNode(ISD::FP_EXTEND, dl, DestVT, Sub);
    }
    return Result;
  }
  assert(!isSigned && "Legalize cannot Expand SINT_TO_FP for i64 yet");
  // Code below here assumes !isSigned without checking again.

  // Implementation of unsigned i64 to f64 following the algorithm in
  // __floatundidf in compiler_rt. This implementation has the advantage
  // of performing rounding correctly, both in the default rounding mode
  // and in all alternate rounding modes.
  // TODO: Generalize this for use with other types.
  if (Op0.getValueType() == MVT::i64 && DestVT == MVT::f64) {
    SDValue TwoP52 =
      DAG.getConstant(UINT64_C(0x4330000000000000), MVT::i64);
    SDValue TwoP84PlusTwoP52 =
      DAG.getConstantFP(BitsToDouble(UINT64_C(0x4530000000100000)), MVT::f64);
    SDValue TwoP84 =
      DAG.getConstant(UINT64_C(0x4530000000000000), MVT::i64);

    SDValue Lo = DAG.getZeroExtendInReg(Op0, dl, MVT::i32);
    SDValue Hi = DAG.getNode(ISD::SRL, dl, MVT::i64, Op0,
                             DAG.getConstant(32, MVT::i64));
    SDValue LoOr = DAG.getNode(ISD::OR, dl, MVT::i64, Lo, TwoP52);
    SDValue HiOr = DAG.getNode(ISD::OR, dl, MVT::i64, Hi, TwoP84);
    SDValue LoFlt = DAG.getNode(ISD::BITCAST, dl, MVT::f64, LoOr);
    SDValue HiFlt = DAG.getNode(ISD::BITCAST, dl, MVT::f64, HiOr);
    SDValue HiSub = DAG.getNode(ISD::FSUB, dl, MVT::f64, HiFlt,
                                TwoP84PlusTwoP52);
    return DAG.getNode(ISD::FADD, dl, MVT::f64, LoFlt, HiSub);
  }

  // Implementation of unsigned i64 to f32.
  // TODO: Generalize this for use with other types.
  if (Op0.getValueType() == MVT::i64 && DestVT == MVT::f32) {
    // For unsigned conversions, convert them to signed conversions using the
    // algorithm from the x86_64 __floatundidf in compiler_rt.
    if (!isSigned) {
      SDValue Fast = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, Op0);

      SDValue ShiftConst =
          DAG.getConstant(1, TLI.getShiftAmountTy(Op0.getValueType()));
      SDValue Shr = DAG.getNode(ISD::SRL, dl, MVT::i64, Op0, ShiftConst);
      SDValue AndConst = DAG.getConstant(1, MVT::i64);
      SDValue And = DAG.getNode(ISD::AND, dl, MVT::i64, Op0, AndConst);
      SDValue Or = DAG.getNode(ISD::OR, dl, MVT::i64, And, Shr);

      SDValue SignCvt = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, Or);
      SDValue Slow = DAG.getNode(ISD::FADD, dl, MVT::f32, SignCvt, SignCvt);

      // TODO: This really should be implemented using a branch rather than a
      // select.  We happen to get lucky and machinesink does the right
      // thing most of the time.  This would be a good candidate for a
      //pseudo-op, or, even better, for whole-function isel.
      SDValue SignBitTest = DAG.getSetCC(dl, getSetCCResultType(MVT::i64),
        Op0, DAG.getConstant(0, MVT::i64), ISD::SETLT);
      return DAG.getSelect(dl, MVT::f32, SignBitTest, Slow, Fast);
    }

    // Otherwise, implement the fully general conversion.

    SDValue And = DAG.getNode(ISD::AND, dl, MVT::i64, Op0,
         DAG.getConstant(UINT64_C(0xfffffffffffff800), MVT::i64));
    SDValue Or = DAG.getNode(ISD::OR, dl, MVT::i64, And,
         DAG.getConstant(UINT64_C(0x800), MVT::i64));
    SDValue And2 = DAG.getNode(ISD::AND, dl, MVT::i64, Op0,
         DAG.getConstant(UINT64_C(0x7ff), MVT::i64));
    SDValue Ne = DAG.getSetCC(dl, getSetCCResultType(MVT::i64),
                   And2, DAG.getConstant(UINT64_C(0), MVT::i64), ISD::SETNE);
    SDValue Sel = DAG.getSelect(dl, MVT::i64, Ne, Or, Op0);
    SDValue Ge = DAG.getSetCC(dl, getSetCCResultType(MVT::i64),
                   Op0, DAG.getConstant(UINT64_C(0x0020000000000000), MVT::i64),
                   ISD::SETUGE);
    SDValue Sel2 = DAG.getSelect(dl, MVT::i64, Ge, Sel, Op0);
    EVT SHVT = TLI.getShiftAmountTy(Sel2.getValueType());

    SDValue Sh = DAG.getNode(ISD::SRL, dl, MVT::i64, Sel2,
                             DAG.getConstant(32, SHVT));
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Sh);
    SDValue Fcvt = DAG.getNode(ISD::UINT_TO_FP, dl, MVT::f64, Trunc);
    SDValue TwoP32 =
      DAG.getConstantFP(BitsToDouble(UINT64_C(0x41f0000000000000)), MVT::f64);
    SDValue Fmul = DAG.getNode(ISD::FMUL, dl, MVT::f64, TwoP32, Fcvt);
    SDValue Lo = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Sel2);
    SDValue Fcvt2 = DAG.getNode(ISD::UINT_TO_FP, dl, MVT::f64, Lo);
    SDValue Fadd = DAG.getNode(ISD::FADD, dl, MVT::f64, Fmul, Fcvt2);
    return DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, Fadd,
                       DAG.getIntPtrConstant(0));
  }

  SDValue Tmp1 = DAG.getNode(ISD::SINT_TO_FP, dl, DestVT, Op0);

  SDValue SignSet = DAG.getSetCC(dl, getSetCCResultType(Op0.getValueType()),
                                 Op0, DAG.getConstant(0, Op0.getValueType()),
                                 ISD::SETLT);
  SDValue Zero = DAG.getIntPtrConstant(0), Four = DAG.getIntPtrConstant(4);
  SDValue CstOffset = DAG.getSelect(dl, Zero.getValueType(),
                                    SignSet, Four, Zero);

  // If the sign bit of the integer is set, the large number will be treated
  // as a negative number.  To counteract this, the dynamic code adds an
  // offset depending on the data type.
  uint64_t FF;
  switch (Op0.getValueType().getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unsupported integer type!");
  case MVT::i8 : FF = 0x43800000ULL; break;  // 2^8  (as a float)
  case MVT::i16: FF = 0x47800000ULL; break;  // 2^16 (as a float)
  case MVT::i32: FF = 0x4F800000ULL; break;  // 2^32 (as a float)
  case MVT::i64: FF = 0x5F800000ULL; break;  // 2^64 (as a float)
  }
  if (TLI.isLittleEndian()) FF <<= 32;
  Constant *FudgeFactor = ConstantInt::get(
                                       Type::getInt64Ty(*DAG.getContext()), FF);

  SDValue CPIdx = DAG.getConstantPool(FudgeFactor, TLI.getPointerTy());
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
  CPIdx = DAG.getNode(ISD::ADD, dl, TLI.getPointerTy(), CPIdx, CstOffset);
  Alignment = std::min(Alignment, 4u);
  SDValue FudgeInReg;
  if (DestVT == MVT::f32)
    FudgeInReg = DAG.getLoad(MVT::f32, dl, DAG.getEntryNode(), CPIdx,
                             MachinePointerInfo::getConstantPool(),
                             false, false, false, Alignment);
  else {
    SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, DestVT,
                                  DAG.getEntryNode(), CPIdx,
                                  MachinePointerInfo::getConstantPool(),
                                  MVT::f32, false, false, Alignment);
    HandleSDNode Handle(Load);
    LegalizeOp(Load.getNode());
    FudgeInReg = Handle.getValue();
  }

  return DAG.getNode(ISD::FADD, dl, DestVT, Tmp1, FudgeInReg);
}

/// PromoteLegalINT_TO_FP - This function is responsible for legalizing a
/// *INT_TO_FP operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal UINT_TO_FP or SINT_TO_FP
/// operation that takes a larger input.
SDValue SelectionDAGLegalize::PromoteLegalINT_TO_FP(SDValue LegalOp,
                                                    EVT DestVT,
                                                    bool isSigned,
                                                    SDLoc dl) {
  // First step, figure out the appropriate *INT_TO_FP operation to use.
  EVT NewInTy = LegalOp.getValueType();

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (1) {
    NewInTy = (MVT::SimpleValueType)(NewInTy.getSimpleVT().SimpleTy+1);
    assert(NewInTy.isInteger() && "Ran out of possibilities!");

    // If the target supports SINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(ISD::SINT_TO_FP, NewInTy)) {
      OpToUse = ISD::SINT_TO_FP;
      break;
    }
    if (isSigned) continue;

    // If the target supports UINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(ISD::UINT_TO_FP, NewInTy)) {
      OpToUse = ISD::UINT_TO_FP;
      break;
    }

    // Otherwise, try a larger type.
  }

  // Okay, we found the operation and type to use.  Zero extend our input to the
  // desired type then run the operation on it.
  return DAG.getNode(OpToUse, dl, DestVT,
                     DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                                 dl, NewInTy, LegalOp));
}

/// PromoteLegalFP_TO_INT - This function is responsible for legalizing a
/// FP_TO_*INT operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal FP_TO_UINT or FP_TO_SINT
/// operation that returns a larger result.
SDValue SelectionDAGLegalize::PromoteLegalFP_TO_INT(SDValue LegalOp,
                                                    EVT DestVT,
                                                    bool isSigned,
                                                    SDLoc dl) {
  // First step, figure out the appropriate FP_TO*INT operation to use.
  EVT NewOutTy = DestVT;

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (1) {
    NewOutTy = (MVT::SimpleValueType)(NewOutTy.getSimpleVT().SimpleTy+1);
    assert(NewOutTy.isInteger() && "Ran out of possibilities!");

    if (TLI.isOperationLegalOrCustom(ISD::FP_TO_SINT, NewOutTy)) {
      OpToUse = ISD::FP_TO_SINT;
      break;
    }

    if (TLI.isOperationLegalOrCustom(ISD::FP_TO_UINT, NewOutTy)) {
      OpToUse = ISD::FP_TO_UINT;
      break;
    }

    // Otherwise, try a larger type.
  }


  // Okay, we found the operation and type to use.
  SDValue Operation = DAG.getNode(OpToUse, dl, NewOutTy, LegalOp);

  // Truncate the result of the extended FP_TO_*INT operation to the desired
  // size.
  return DAG.getNode(ISD::TRUNCATE, dl, DestVT, Operation);
}

/// ExpandBSWAP - Open code the operations for BSWAP of the specified operation.
///
SDValue SelectionDAGLegalize::ExpandBSWAP(SDValue Op, SDLoc dl) {
  EVT VT = Op.getValueType();
  EVT SHVT = TLI.getShiftAmountTy(VT);
  SDValue Tmp1, Tmp2, Tmp3, Tmp4, Tmp5, Tmp6, Tmp7, Tmp8;
  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unhandled Expand type in BSWAP!");
  case MVT::i16:
    Tmp2 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, SHVT));
    return DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
  case MVT::i32:
    Tmp4 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(24, SHVT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, SHVT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, SHVT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp3, DAG.getConstant(0xFF0000, VT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(0xFF00, VT));
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    return DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
  case MVT::i64:
    Tmp8 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(56, SHVT));
    Tmp7 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(40, SHVT));
    Tmp6 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(24, SHVT));
    Tmp5 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, SHVT));
    Tmp4 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, SHVT));
    Tmp3 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, SHVT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(40, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(56, SHVT));
    Tmp7 = DAG.getNode(ISD::AND, dl, VT, Tmp7, DAG.getConstant(255ULL<<48, VT));
    Tmp6 = DAG.getNode(ISD::AND, dl, VT, Tmp6, DAG.getConstant(255ULL<<40, VT));
    Tmp5 = DAG.getNode(ISD::AND, dl, VT, Tmp5, DAG.getConstant(255ULL<<32, VT));
    Tmp4 = DAG.getNode(ISD::AND, dl, VT, Tmp4, DAG.getConstant(255ULL<<24, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp3, DAG.getConstant(255ULL<<16, VT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(255ULL<<8 , VT));
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp7);
    Tmp6 = DAG.getNode(ISD::OR, dl, VT, Tmp6, Tmp5);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp6);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
    return DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp4);
  }
}

/// ExpandBitCount - Expand the specified bitcount instruction into operations.
///
SDValue SelectionDAGLegalize::ExpandBitCount(unsigned Opc, SDValue Op,
                                             SDLoc dl) {
  switch (Opc) {
  default: llvm_unreachable("Cannot expand this yet!");
  case ISD::CTPOP: {
    EVT VT = Op.getValueType();
    EVT ShVT = TLI.getShiftAmountTy(VT);
    unsigned Len = VT.getSizeInBits();

    assert(VT.isInteger() && Len <= 128 && Len % 8 == 0 &&
           "CTPOP not implemented for this type.");

    // This is the "best" algorithm from
    // http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel

    SDValue Mask55 = DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x55)), VT);
    SDValue Mask33 = DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x33)), VT);
    SDValue Mask0F = DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x0F)), VT);
    SDValue Mask01 = DAG.getConstant(APInt::getSplat(Len, APInt(8, 0x01)), VT);

    // v = v - ((v >> 1) & 0x55555555...)
    Op = DAG.getNode(ISD::SUB, dl, VT, Op,
                     DAG.getNode(ISD::AND, dl, VT,
                                 DAG.getNode(ISD::SRL, dl, VT, Op,
                                             DAG.getConstant(1, ShVT)),
                                 Mask55));
    // v = (v & 0x33333333...) + ((v >> 2) & 0x33333333...)
    Op = DAG.getNode(ISD::ADD, dl, VT,
                     DAG.getNode(ISD::AND, dl, VT, Op, Mask33),
                     DAG.getNode(ISD::AND, dl, VT,
                                 DAG.getNode(ISD::SRL, dl, VT, Op,
                                             DAG.getConstant(2, ShVT)),
                                 Mask33));
    // v = (v + (v >> 4)) & 0x0F0F0F0F...
    Op = DAG.getNode(ISD::AND, dl, VT,
                     DAG.getNode(ISD::ADD, dl, VT, Op,
                                 DAG.getNode(ISD::SRL, dl, VT, Op,
                                             DAG.getConstant(4, ShVT))),
                     Mask0F);
    // v = (v * 0x01010101...) >> (Len - 8)
    Op = DAG.getNode(ISD::SRL, dl, VT,
                     DAG.getNode(ISD::MUL, dl, VT, Op, Mask01),
                     DAG.getConstant(Len - 8, ShVT));

    return Op;
  }
  case ISD::CTLZ_ZERO_UNDEF:
    // This trivially expands to CTLZ.
    return DAG.getNode(ISD::CTLZ, dl, Op.getValueType(), Op);
  case ISD::CTLZ: {
    // for now, we do this:
    // x = x | (x >> 1);
    // x = x | (x >> 2);
    // ...
    // x = x | (x >>16);
    // x = x | (x >>32); // for 64-bit input
    // return popcount(~x);
    //
    // but see also: http://www.hackersdelight.org/HDcode/nlz.cc
    EVT VT = Op.getValueType();
    EVT ShVT = TLI.getShiftAmountTy(VT);
    unsigned len = VT.getSizeInBits();
    for (unsigned i = 0; (1U << i) <= (len / 2); ++i) {
      SDValue Tmp3 = DAG.getConstant(1ULL << i, ShVT);
      Op = DAG.getNode(ISD::OR, dl, VT, Op,
                       DAG.getNode(ISD::SRL, dl, VT, Op, Tmp3));
    }
    Op = DAG.getNOT(dl, Op, VT);
    return DAG.getNode(ISD::CTPOP, dl, VT, Op);
  }
  case ISD::CTTZ_ZERO_UNDEF:
    // This trivially expands to CTTZ.
    return DAG.getNode(ISD::CTTZ, dl, Op.getValueType(), Op);
  case ISD::CTTZ: {
    // for now, we use: { return popcount(~x & (x - 1)); }
    // unless the target has ctlz but not ctpop, in which case we use:
    // { return 32 - nlz(~x & (x-1)); }
    // see also http://www.hackersdelight.org/HDcode/ntz.cc
    EVT VT = Op.getValueType();
    SDValue Tmp3 = DAG.getNode(ISD::AND, dl, VT,
                               DAG.getNOT(dl, Op, VT),
                               DAG.getNode(ISD::SUB, dl, VT, Op,
                                           DAG.getConstant(1, VT)));
    // If ISD::CTLZ is legal and CTPOP isn't, then do that instead.
    if (!TLI.isOperationLegalOrCustom(ISD::CTPOP, VT) &&
        TLI.isOperationLegalOrCustom(ISD::CTLZ, VT))
      return DAG.getNode(ISD::SUB, dl, VT,
                         DAG.getConstant(VT.getSizeInBits(), VT),
                         DAG.getNode(ISD::CTLZ, dl, VT, Tmp3));
    return DAG.getNode(ISD::CTPOP, dl, VT, Tmp3);
  }
  }
}

std::pair <SDValue, SDValue> SelectionDAGLegalize::ExpandAtomic(SDNode *Node) {
  unsigned Opc = Node->getOpcode();
  MVT VT = cast<AtomicSDNode>(Node)->getMemoryVT().getSimpleVT();
  RTLIB::Libcall LC;

  switch (Opc) {
  default:
    llvm_unreachable("Unhandled atomic intrinsic Expand!");
  case ISD::ATOMIC_SWAP:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_LOCK_TEST_AND_SET_1; break;
    case MVT::i16: LC = RTLIB::SYNC_LOCK_TEST_AND_SET_2; break;
    case MVT::i32: LC = RTLIB::SYNC_LOCK_TEST_AND_SET_4; break;
    case MVT::i64: LC = RTLIB::SYNC_LOCK_TEST_AND_SET_8; break;
    }
    break;
  case ISD::ATOMIC_CMP_SWAP:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_VAL_COMPARE_AND_SWAP_1; break;
    case MVT::i16: LC = RTLIB::SYNC_VAL_COMPARE_AND_SWAP_2; break;
    case MVT::i32: LC = RTLIB::SYNC_VAL_COMPARE_AND_SWAP_4; break;
    case MVT::i64: LC = RTLIB::SYNC_VAL_COMPARE_AND_SWAP_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_ADD:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_ADD_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_ADD_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_ADD_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_ADD_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_SUB:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_SUB_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_SUB_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_SUB_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_SUB_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_AND:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_AND_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_AND_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_AND_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_AND_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_OR:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_OR_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_OR_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_OR_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_OR_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_XOR:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_XOR_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_XOR_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_XOR_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_XOR_8; break;
    }
    break;
  case ISD::ATOMIC_LOAD_NAND:
    switch (VT.SimpleTy) {
    default: llvm_unreachable("Unexpected value type for atomic!");
    case MVT::i8:  LC = RTLIB::SYNC_FETCH_AND_NAND_1; break;
    case MVT::i16: LC = RTLIB::SYNC_FETCH_AND_NAND_2; break;
    case MVT::i32: LC = RTLIB::SYNC_FETCH_AND_NAND_4; break;
    case MVT::i64: LC = RTLIB::SYNC_FETCH_AND_NAND_8; break;
    }
    break;
  }

  return ExpandChainLibCall(LC, Node, false);
}

void SelectionDAGLegalize::ExpandNode(SDNode *Node) {
  SmallVector<SDValue, 8> Results;
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3, Tmp4;
  switch (Node->getOpcode()) {
  case ISD::CTPOP:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    Tmp1 = ExpandBitCount(Node->getOpcode(), Node->getOperand(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::BSWAP:
    Results.push_back(ExpandBSWAP(Node->getOperand(0), dl));
    break;
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
  case ISD::FRAME_TO_ARGS_OFFSET:
    Results.push_back(DAG.getConstant(0, Node->getValueType(0)));
    break;
  case ISD::FLT_ROUNDS_:
    Results.push_back(DAG.getConstant(1, Node->getValueType(0)));
    break;
  case ISD::EH_RETURN:
  case ISD::EH_LABEL:
  case ISD::PREFETCH:
  case ISD::VAEND:
  case ISD::EH_SJLJ_LONGJMP:
    // If the target didn't expand these, there's nothing to do, so just
    // preserve the chain and be done.
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::EH_SJLJ_SETJMP:
    // If the target didn't expand this, just return 'zero' and preserve the
    // chain.
    Results.push_back(DAG.getConstant(0, MVT::i32));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::ATOMIC_FENCE: {
    // If the target didn't lower this, lower it to '__sync_synchronize()' call
    // FIXME: handle "fence singlethread" more efficiently.
    TargetLowering::ArgListTy Args;
    TargetLowering::
    CallLoweringInfo CLI(Node->getOperand(0),
                         Type::getVoidTy(*DAG.getContext()),
                      false, false, false, false, 0, CallingConv::C,
                      /*isTailCall=*/false,
                      /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                      DAG.getExternalSymbol("__sync_synchronize",
                                            TLI.getPointerTy()),
                      Args, DAG, dl);
    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  case ISD::ATOMIC_LOAD: {
    // There is no libcall for atomic load; fake it with ATOMIC_CMP_SWAP.
    SDValue Zero = DAG.getConstant(0, Node->getValueType(0));
    SDValue Swap = DAG.getAtomic(ISD::ATOMIC_CMP_SWAP, dl,
                                 cast<AtomicSDNode>(Node)->getMemoryVT(),
                                 Node->getOperand(0),
                                 Node->getOperand(1), Zero, Zero,
                                 cast<AtomicSDNode>(Node)->getMemOperand(),
                                 cast<AtomicSDNode>(Node)->getOrdering(),
                                 cast<AtomicSDNode>(Node)->getSynchScope());
    Results.push_back(Swap.getValue(0));
    Results.push_back(Swap.getValue(1));
    break;
  }
  case ISD::ATOMIC_STORE: {
    // There is no libcall for atomic store; fake it with ATOMIC_SWAP.
    SDValue Swap = DAG.getAtomic(ISD::ATOMIC_SWAP, dl,
                                 cast<AtomicSDNode>(Node)->getMemoryVT(),
                                 Node->getOperand(0),
                                 Node->getOperand(1), Node->getOperand(2),
                                 cast<AtomicSDNode>(Node)->getMemOperand(),
                                 cast<AtomicSDNode>(Node)->getOrdering(),
                                 cast<AtomicSDNode>(Node)->getSynchScope());
    Results.push_back(Swap.getValue(1));
    break;
  }
  // By default, atomic intrinsics are marked Legal and lowered. Targets
  // which don't support them directly, however, may want libcalls, in which
  // case they mark them Expand, and we get here.
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_CMP_SWAP: {
    std::pair<SDValue, SDValue> Tmp = ExpandAtomic(Node);
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
    break;
  }
  case ISD::DYNAMIC_STACKALLOC:
    ExpandDYNAMIC_STACKALLOC(Node, Results);
    break;
  case ISD::MERGE_VALUES:
    for (unsigned i = 0; i < Node->getNumValues(); i++)
      Results.push_back(Node->getOperand(i));
    break;
  case ISD::UNDEF: {
    EVT VT = Node->getValueType(0);
    if (VT.isInteger())
      Results.push_back(DAG.getConstant(0, VT));
    else {
      assert(VT.isFloatingPoint() && "Unknown value type!");
      Results.push_back(DAG.getConstantFP(0, VT));
    }
    break;
  }
  case ISD::TRAP: {
    // If this operation is not supported, lower it to 'abort()' call
    TargetLowering::ArgListTy Args;
    TargetLowering::
    CallLoweringInfo CLI(Node->getOperand(0),
                         Type::getVoidTy(*DAG.getContext()),
                      false, false, false, false, 0, CallingConv::C,
                      /*isTailCall=*/false,
                      /*doesNotReturn=*/false, /*isReturnValueUsed=*/true,
                      DAG.getExternalSymbol("abort", TLI.getPointerTy()),
                      Args, DAG, dl);
    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  case ISD::FP_ROUND:
  case ISD::BITCAST:
    Tmp1 = EmitStackConvert(Node->getOperand(0), Node->getValueType(0),
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::FP_EXTEND:
    Tmp1 = EmitStackConvert(Node->getOperand(0),
                            Node->getOperand(0).getValueType(),
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::SIGN_EXTEND_INREG: {
    // NOTE: we could fall back on load/store here too for targets without
    // SAR.  However, it is doubtful that any exist.
    EVT ExtraVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
    EVT VT = Node->getValueType(0);
    EVT ShiftAmountTy = TLI.getShiftAmountTy(VT);
    if (VT.isVector())
      ShiftAmountTy = VT;
    unsigned BitsDiff = VT.getScalarType().getSizeInBits() -
                        ExtraVT.getScalarType().getSizeInBits();
    SDValue ShiftCst = DAG.getConstant(BitsDiff, ShiftAmountTy);
    Tmp1 = DAG.getNode(ISD::SHL, dl, Node->getValueType(0),
                       Node->getOperand(0), ShiftCst);
    Tmp1 = DAG.getNode(ISD::SRA, dl, Node->getValueType(0), Tmp1, ShiftCst);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FP_ROUND_INREG: {
    // The only way we can lower this is to turn it into a TRUNCSTORE,
    // EXTLOAD pair, targeting a temporary location (a stack slot).

    // NOTE: there is a choice here between constantly creating new stack
    // slots and always reusing the same one.  We currently always create
    // new ones, as reuse may inhibit scheduling.
    EVT ExtraVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
    Tmp1 = EmitStackConvert(Node->getOperand(0), ExtraVT,
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    Tmp1 = ExpandLegalINT_TO_FP(Node->getOpcode() == ISD::SINT_TO_FP,
                                Node->getOperand(0), Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::FP_TO_UINT: {
    SDValue True, False;
    EVT VT =  Node->getOperand(0).getValueType();
    EVT NVT = Node->getValueType(0);
    APFloat apf(DAG.EVTToAPFloatSemantics(VT),
                APInt::getNullValue(VT.getSizeInBits()));
    APInt x = APInt::getSignBit(NVT.getSizeInBits());
    (void)apf.convertFromAPInt(x, false, APFloat::rmNearestTiesToEven);
    Tmp1 = DAG.getConstantFP(apf, VT);
    Tmp2 = DAG.getSetCC(dl, getSetCCResultType(VT),
                        Node->getOperand(0),
                        Tmp1, ISD::SETLT);
    True = DAG.getNode(ISD::FP_TO_SINT, dl, NVT, Node->getOperand(0));
    False = DAG.getNode(ISD::FP_TO_SINT, dl, NVT,
                        DAG.getNode(ISD::FSUB, dl, VT,
                                    Node->getOperand(0), Tmp1));
    False = DAG.getNode(ISD::XOR, dl, NVT, False,
                        DAG.getConstant(x, NVT));
    Tmp1 = DAG.getSelect(dl, NVT, Tmp2, True, False);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VAARG: {
    const Value *V = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
    EVT VT = Node->getValueType(0);
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    unsigned Align = Node->getConstantOperandVal(3);

    SDValue VAListLoad = DAG.getLoad(TLI.getPointerTy(), dl, Tmp1, Tmp2,
                                     MachinePointerInfo(V),
                                     false, false, false, 0);
    SDValue VAList = VAListLoad;

    if (Align > TLI.getMinStackArgumentAlignment()) {
      assert(((Align & (Align-1)) == 0) && "Expected Align to be a power of 2");

      VAList = DAG.getNode(ISD::ADD, dl, TLI.getPointerTy(), VAList,
                           DAG.getConstant(Align - 1,
                                           TLI.getPointerTy()));

      VAList = DAG.getNode(ISD::AND, dl, TLI.getPointerTy(), VAList,
                           DAG.getConstant(-(int64_t)Align,
                                           TLI.getPointerTy()));
    }

    // Increment the pointer, VAList, to the next vaarg
    Tmp3 = DAG.getNode(ISD::ADD, dl, TLI.getPointerTy(), VAList,
                       DAG.getConstant(TLI.getDataLayout()->
                          getTypeAllocSize(VT.getTypeForEVT(*DAG.getContext())),
                                       TLI.getPointerTy()));
    // Store the incremented VAList to the legalized pointer
    Tmp3 = DAG.getStore(VAListLoad.getValue(1), dl, Tmp3, Tmp2,
                        MachinePointerInfo(V), false, false, 0);
    // Load the actual argument out of the pointer VAList
    Results.push_back(DAG.getLoad(VT, dl, Tmp3, VAList, MachinePointerInfo(),
                                  false, false, false, 0));
    Results.push_back(Results[0].getValue(1));
    break;
  }
  case ISD::VACOPY: {
    // This defaults to loading a pointer from the input and storing it to the
    // output, returning the chain.
    const Value *VD = cast<SrcValueSDNode>(Node->getOperand(3))->getValue();
    const Value *VS = cast<SrcValueSDNode>(Node->getOperand(4))->getValue();
    Tmp1 = DAG.getLoad(TLI.getPointerTy(), dl, Node->getOperand(0),
                       Node->getOperand(2), MachinePointerInfo(VS),
                       false, false, false, 0);
    Tmp1 = DAG.getStore(Tmp1.getValue(1), dl, Tmp1, Node->getOperand(1),
                        MachinePointerInfo(VD), false, false, 0);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT:
    if (Node->getOperand(0).getValueType().getVectorNumElements() == 1)
      // This must be an access of the only element.  Return it.
      Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0),
                         Node->getOperand(0));
    else
      Tmp1 = ExpandExtractFromVectorThroughStack(SDValue(Node, 0));
    Results.push_back(Tmp1);
    break;
  case ISD::EXTRACT_SUBVECTOR:
    Results.push_back(ExpandExtractFromVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::INSERT_SUBVECTOR:
    Results.push_back(ExpandInsertToVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::CONCAT_VECTORS: {
    Results.push_back(ExpandVectorBuildThroughStack(Node));
    break;
  }
  case ISD::SCALAR_TO_VECTOR:
    Results.push_back(ExpandSCALAR_TO_VECTOR(Node));
    break;
  case ISD::INSERT_VECTOR_ELT:
    Results.push_back(ExpandINSERT_VECTOR_ELT(Node->getOperand(0),
                                              Node->getOperand(1),
                                              Node->getOperand(2), dl));
    break;
  case ISD::VECTOR_SHUFFLE: {
    SmallVector<int, 32> NewMask;
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    EVT VT = Node->getValueType(0);
    EVT EltVT = VT.getVectorElementType();
    SDValue Op0 = Node->getOperand(0);
    SDValue Op1 = Node->getOperand(1);
    if (!TLI.isTypeLegal(EltVT)) {

      EVT NewEltVT = TLI.getTypeToTransformTo(*DAG.getContext(), EltVT);

      // BUILD_VECTOR operands are allowed to be wider than the element type.
      // But if NewEltVT is smaller that EltVT the BUILD_VECTOR does not accept it
      if (NewEltVT.bitsLT(EltVT)) {

        // Convert shuffle node.
        // If original node was v4i64 and the new EltVT is i32,
        // cast operands to v8i32 and re-build the mask.

        // Calculate new VT, the size of the new VT should be equal to original.
        EVT NewVT = EVT::getVectorVT(*DAG.getContext(), NewEltVT,
                                      VT.getSizeInBits()/NewEltVT.getSizeInBits());
        assert(NewVT.bitsEq(VT));

        // cast operands to new VT
        Op0 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op0);
        Op1 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op1);

        // Convert the shuffle mask
        unsigned int factor = NewVT.getVectorNumElements()/VT.getVectorNumElements();

        // EltVT gets smaller
        assert(factor > 0);

        for (unsigned i = 0; i < VT.getVectorNumElements(); ++i) {
          if (Mask[i] < 0) {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]);
          }
          else {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]*factor+fi);
          }
        }
        Mask = NewMask;
        VT = NewVT;
      }
      EltVT = NewEltVT;
    }
    unsigned NumElems = VT.getVectorNumElements();
    SmallVector<SDValue, 16> Ops;
    for (unsigned i = 0; i != NumElems; ++i) {
      if (Mask[i] < 0) {
        Ops.push_back(DAG.getUNDEF(EltVT));
        continue;
      }
      unsigned Idx = Mask[i];
      if (Idx < NumElems)
        Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT,
                                  Op0,
                                  DAG.getIntPtrConstant(Idx)));
      else
        Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT,
                                  Op1,
                                  DAG.getIntPtrConstant(Idx - NumElems)));
    }

    Tmp1 = DAG.getNode(ISD::BUILD_VECTOR, dl, VT, &Ops[0], Ops.size());
    // We may have changed the BUILD_VECTOR type. Cast it back to the Node type.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0), Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::EXTRACT_ELEMENT: {
    EVT OpTy = Node->getOperand(0).getValueType();
    if (cast<ConstantSDNode>(Node->getOperand(1))->getZExtValue()) {
      // 1 -> Hi
      Tmp1 = DAG.getNode(ISD::SRL, dl, OpTy, Node->getOperand(0),
                         DAG.getConstant(OpTy.getSizeInBits()/2,
                    TLI.getShiftAmountTy(Node->getOperand(0).getValueType())));
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0), Tmp1);
    } else {
      // 0 -> Lo
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0),
                         Node->getOperand(0));
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::STACKSAVE:
    // Expand to CopyFromReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (unsigned SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyFromReg(Node->getOperand(0), dl, SP,
                                           Node->getValueType(0)));
      Results.push_back(Results[0].getValue(1));
    } else {
      Results.push_back(DAG.getUNDEF(Node->getValueType(0)));
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::STACKRESTORE:
    // Expand to CopyToReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (unsigned SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyToReg(Node->getOperand(0), dl, SP,
                                         Node->getOperand(1)));
    } else {
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::FCOPYSIGN:
    Results.push_back(ExpandFCOPYSIGN(Node));
    break;
  case ISD::FNEG:
    // Expand Y = FNEG(X) ->  Y = SUB -0.0, X
    Tmp1 = DAG.getConstantFP(-0.0, Node->getValueType(0));
    Tmp1 = DAG.getNode(ISD::FSUB, dl, Node->getValueType(0), Tmp1,
                       Node->getOperand(0));
    Results.push_back(Tmp1);
    break;
  case ISD::FABS: {
    // Expand Y = FABS(X) -> Y = (X >u 0.0) ? X : fneg(X).
    EVT VT = Node->getValueType(0);
    Tmp1 = Node->getOperand(0);
    Tmp2 = DAG.getConstantFP(0.0, VT);
    Tmp2 = DAG.getSetCC(dl, getSetCCResultType(Tmp1.getValueType()),
                        Tmp1, Tmp2, ISD::SETUGT);
    Tmp3 = DAG.getNode(ISD::FNEG, dl, VT, Tmp1);
    Tmp1 = DAG.getSelect(dl, VT, Tmp2, Tmp1, Tmp3);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FSQRT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::SQRT_F32, RTLIB::SQRT_F64,
                                      RTLIB::SQRT_F80, RTLIB::SQRT_F128,
                                      RTLIB::SQRT_PPCF128));
    break;
  case ISD::FSIN:
  case ISD::FCOS: {
    EVT VT = Node->getValueType(0);
    bool isSIN = Node->getOpcode() == ISD::FSIN;
    // Turn fsin / fcos into ISD::FSINCOS node if there are a pair of fsin /
    // fcos which share the same operand and both are used.
    if ((TLI.isOperationLegalOrCustom(ISD::FSINCOS, VT) ||
         canCombineSinCosLibcall(Node, TLI, TM))
        && useSinCos(Node)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(ISD::FSINCOS, dl, VTs, Node->getOperand(0));
      if (!isSIN)
        Tmp1 = Tmp1.getValue(1);
      Results.push_back(Tmp1);
    } else if (isSIN) {
      Results.push_back(ExpandFPLibCall(Node, RTLIB::SIN_F32, RTLIB::SIN_F64,
                                        RTLIB::SIN_F80, RTLIB::SIN_F128,
                                        RTLIB::SIN_PPCF128));
    } else {
      Results.push_back(ExpandFPLibCall(Node, RTLIB::COS_F32, RTLIB::COS_F64,
                                        RTLIB::COS_F80, RTLIB::COS_F128,
                                        RTLIB::COS_PPCF128));
    }
    break;
  }
  case ISD::FSINCOS:
    // Expand into sincos libcall.
    ExpandSinCosLibCall(Node, Results);
    break;
  case ISD::FLOG:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG_F32, RTLIB::LOG_F64,
                                      RTLIB::LOG_F80, RTLIB::LOG_F128,
                                      RTLIB::LOG_PPCF128));
    break;
  case ISD::FLOG2:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG2_F32, RTLIB::LOG2_F64,
                                      RTLIB::LOG2_F80, RTLIB::LOG2_F128,
                                      RTLIB::LOG2_PPCF128));
    break;
  case ISD::FLOG10:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG10_F32, RTLIB::LOG10_F64,
                                      RTLIB::LOG10_F80, RTLIB::LOG10_F128,
                                      RTLIB::LOG10_PPCF128));
    break;
  case ISD::FEXP:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP_F32, RTLIB::EXP_F64,
                                      RTLIB::EXP_F80, RTLIB::EXP_F128,
                                      RTLIB::EXP_PPCF128));
    break;
  case ISD::FEXP2:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP2_F32, RTLIB::EXP2_F64,
                                      RTLIB::EXP2_F80, RTLIB::EXP2_F128,
                                      RTLIB::EXP2_PPCF128));
    break;
  case ISD::FTRUNC:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::TRUNC_F32, RTLIB::TRUNC_F64,
                                      RTLIB::TRUNC_F80, RTLIB::TRUNC_F128,
                                      RTLIB::TRUNC_PPCF128));
    break;
  case ISD::FFLOOR:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FLOOR_F32, RTLIB::FLOOR_F64,
                                      RTLIB::FLOOR_F80, RTLIB::FLOOR_F128,
                                      RTLIB::FLOOR_PPCF128));
    break;
  case ISD::FCEIL:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::CEIL_F32, RTLIB::CEIL_F64,
                                      RTLIB::CEIL_F80, RTLIB::CEIL_F128,
                                      RTLIB::CEIL_PPCF128));
    break;
  case ISD::FRINT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::RINT_F32, RTLIB::RINT_F64,
                                      RTLIB::RINT_F80, RTLIB::RINT_F128,
                                      RTLIB::RINT_PPCF128));
    break;
  case ISD::FNEARBYINT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::NEARBYINT_F32,
                                      RTLIB::NEARBYINT_F64,
                                      RTLIB::NEARBYINT_F80,
                                      RTLIB::NEARBYINT_F128,
                                      RTLIB::NEARBYINT_PPCF128));
    break;
  case ISD::FPOWI:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::POWI_F32, RTLIB::POWI_F64,
                                      RTLIB::POWI_F80, RTLIB::POWI_F128,
                                      RTLIB::POWI_PPCF128));
    break;
  case ISD::FPOW:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::POW_F32, RTLIB::POW_F64,
                                      RTLIB::POW_F80, RTLIB::POW_F128,
                                      RTLIB::POW_PPCF128));
    break;
  case ISD::FDIV:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::DIV_F32, RTLIB::DIV_F64,
                                      RTLIB::DIV_F80, RTLIB::DIV_F128,
                                      RTLIB::DIV_PPCF128));
    break;
  case ISD::FREM:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::REM_F32, RTLIB::REM_F64,
                                      RTLIB::REM_F80, RTLIB::REM_F128,
                                      RTLIB::REM_PPCF128));
    break;
  case ISD::FMA:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FMA_F32, RTLIB::FMA_F64,
                                      RTLIB::FMA_F80, RTLIB::FMA_F128,
                                      RTLIB::FMA_PPCF128));
    break;
  case ISD::FP16_TO_FP32:
    Results.push_back(ExpandLibCall(RTLIB::FPEXT_F16_F32, Node, false));
    break;
  case ISD::FP32_TO_FP16:
    Results.push_back(ExpandLibCall(RTLIB::FPROUND_F32_F16, Node, false));
    break;
  case ISD::ConstantFP: {
    ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Node);
    // Check to see if this FP immediate is already legal.
    // If this is a legal constant, turn it into a TargetConstantFP node.
    if (!TLI.isFPImmLegal(CFP->getValueAPF(), Node->getValueType(0)))
      Results.push_back(ExpandConstantFP(CFP, true));
    break;
  }
  case ISD::FSUB: {
    EVT VT = Node->getValueType(0);
    assert(TLI.isOperationLegalOrCustom(ISD::FADD, VT) &&
           TLI.isOperationLegalOrCustom(ISD::FNEG, VT) &&
           "Don't know how to expand this FP subtraction!");
    Tmp1 = DAG.getNode(ISD::FNEG, dl, VT, Node->getOperand(1));
    Tmp1 = DAG.getNode(ISD::FADD, dl, VT, Node->getOperand(0), Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SUB: {
    EVT VT = Node->getValueType(0);
    assert(TLI.isOperationLegalOrCustom(ISD::ADD, VT) &&
           TLI.isOperationLegalOrCustom(ISD::XOR, VT) &&
           "Don't know how to expand this subtraction!");
    Tmp1 = DAG.getNode(ISD::XOR, dl, VT, Node->getOperand(1),
               DAG.getConstant(APInt::getAllOnesValue(VT.getSizeInBits()), VT));
    Tmp1 = DAG.getNode(ISD::ADD, dl, VT, Tmp1, DAG.getConstant(1, VT));
    Results.push_back(DAG.getNode(ISD::ADD, dl, VT, Node->getOperand(0), Tmp1));
    break;
  }
  case ISD::UREM:
  case ISD::SREM: {
    EVT VT = Node->getValueType(0);
    bool isSigned = Node->getOpcode() == ISD::SREM;
    unsigned DivOpc = isSigned ? ISD::SDIV : ISD::UDIV;
    unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
    Tmp2 = Node->getOperand(0);
    Tmp3 = Node->getOperand(1);
    if (TLI.isOperationLegalOrCustom(DivRemOpc, VT) ||
        (isDivRemLibcallAvailable(Node, isSigned, TLI) &&
         // If div is legal, it's better to do the normal expansion
         !TLI.isOperationLegalOrCustom(DivOpc, Node->getValueType(0)) &&
         useDivRem(Node, isSigned, false))) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(DivRemOpc, dl, VTs, Tmp2, Tmp3).getValue(1);
    } else if (TLI.isOperationLegalOrCustom(DivOpc, VT)) {
      // X % Y -> X-X/Y*Y
      Tmp1 = DAG.getNode(DivOpc, dl, VT, Tmp2, Tmp3);
      Tmp1 = DAG.getNode(ISD::MUL, dl, VT, Tmp1, Tmp3);
      Tmp1 = DAG.getNode(ISD::SUB, dl, VT, Tmp2, Tmp1);
    } else if (isSigned)
      Tmp1 = ExpandIntLibCall(Node, true,
                              RTLIB::SREM_I8,
                              RTLIB::SREM_I16, RTLIB::SREM_I32,
                              RTLIB::SREM_I64, RTLIB::SREM_I128);
    else
      Tmp1 = ExpandIntLibCall(Node, false,
                              RTLIB::UREM_I8,
                              RTLIB::UREM_I16, RTLIB::UREM_I32,
                              RTLIB::UREM_I64, RTLIB::UREM_I128);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::UDIV:
  case ISD::SDIV: {
    bool isSigned = Node->getOpcode() == ISD::SDIV;
    unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);
    if (TLI.isOperationLegalOrCustom(DivRemOpc, VT) ||
        (isDivRemLibcallAvailable(Node, isSigned, TLI) &&
         useDivRem(Node, isSigned, true)))
      Tmp1 = DAG.getNode(DivRemOpc, dl, VTs, Node->getOperand(0),
                         Node->getOperand(1));
    else if (isSigned)
      Tmp1 = ExpandIntLibCall(Node, true,
                              RTLIB::SDIV_I8,
                              RTLIB::SDIV_I16, RTLIB::SDIV_I32,
                              RTLIB::SDIV_I64, RTLIB::SDIV_I128);
    else
      Tmp1 = ExpandIntLibCall(Node, false,
                              RTLIB::UDIV_I8,
                              RTLIB::UDIV_I16, RTLIB::UDIV_I32,
                              RTLIB::UDIV_I64, RTLIB::UDIV_I128);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::MULHU:
  case ISD::MULHS: {
    unsigned ExpandOpcode = Node->getOpcode() == ISD::MULHU ? ISD::UMUL_LOHI :
                                                              ISD::SMUL_LOHI;
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);
    assert(TLI.isOperationLegalOrCustom(ExpandOpcode, VT) &&
           "If this wasn't legal, it shouldn't have been created!");
    Tmp1 = DAG.getNode(ExpandOpcode, dl, VTs, Node->getOperand(0),
                       Node->getOperand(1));
    Results.push_back(Tmp1.getValue(1));
    break;
  }
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    // Expand into divrem libcall
    ExpandDivRemLibCall(Node, Results);
    break;
  case ISD::MUL: {
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);
    // See if multiply or divide can be lowered using two-result operations.
    // We just need the low half of the multiply; try both the signed
    // and unsigned forms. If the target supports both SMUL_LOHI and
    // UMUL_LOHI, form a preference by checking which forms of plain
    // MULH it supports.
    bool HasSMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::SMUL_LOHI, VT);
    bool HasUMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::UMUL_LOHI, VT);
    bool HasMULHS = TLI.isOperationLegalOrCustom(ISD::MULHS, VT);
    bool HasMULHU = TLI.isOperationLegalOrCustom(ISD::MULHU, VT);
    unsigned OpToUse = 0;
    if (HasSMUL_LOHI && !HasMULHS) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI && !HasMULHU) {
      OpToUse = ISD::UMUL_LOHI;
    } else if (HasSMUL_LOHI) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI) {
      OpToUse = ISD::UMUL_LOHI;
    }
    if (OpToUse) {
      Results.push_back(DAG.getNode(OpToUse, dl, VTs, Node->getOperand(0),
                                    Node->getOperand(1)));
      break;
    }
    Tmp1 = ExpandIntLibCall(Node, false,
                            RTLIB::MUL_I8,
                            RTLIB::MUL_I16, RTLIB::MUL_I32,
                            RTLIB::MUL_I64, RTLIB::MUL_I128);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SADDO:
  case ISD::SSUBO: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue Sum = DAG.getNode(Node->getOpcode() == ISD::SADDO ?
                              ISD::ADD : ISD::SUB, dl, LHS.getValueType(),
                              LHS, RHS);
    Results.push_back(Sum);
    EVT OType = Node->getValueType(1);

    SDValue Zero = DAG.getConstant(0, LHS.getValueType());

    //   LHSSign -> LHS >= 0
    //   RHSSign -> RHS >= 0
    //   SumSign -> Sum >= 0
    //
    //   Add:
    //   Overflow -> (LHSSign == RHSSign) && (LHSSign != SumSign)
    //   Sub:
    //   Overflow -> (LHSSign != RHSSign) && (LHSSign != SumSign)
    //
    SDValue LHSSign = DAG.getSetCC(dl, OType, LHS, Zero, ISD::SETGE);
    SDValue RHSSign = DAG.getSetCC(dl, OType, RHS, Zero, ISD::SETGE);
    SDValue SignsMatch = DAG.getSetCC(dl, OType, LHSSign, RHSSign,
                                      Node->getOpcode() == ISD::SADDO ?
                                      ISD::SETEQ : ISD::SETNE);

    SDValue SumSign = DAG.getSetCC(dl, OType, Sum, Zero, ISD::SETGE);
    SDValue SumSignNE = DAG.getSetCC(dl, OType, LHSSign, SumSign, ISD::SETNE);

    SDValue Cmp = DAG.getNode(ISD::AND, dl, OType, SignsMatch, SumSignNE);
    Results.push_back(Cmp);
    break;
  }
  case ISD::UADDO:
  case ISD::USUBO: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue Sum = DAG.getNode(Node->getOpcode() == ISD::UADDO ?
                              ISD::ADD : ISD::SUB, dl, LHS.getValueType(),
                              LHS, RHS);
    Results.push_back(Sum);
    Results.push_back(DAG.getSetCC(dl, Node->getValueType(1), Sum, LHS,
                                   Node->getOpcode () == ISD::UADDO ?
                                   ISD::SETULT : ISD::SETUGT));
    break;
  }
  case ISD::UMULO:
  case ISD::SMULO: {
    EVT VT = Node->getValueType(0);
    EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits() * 2);
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue BottomHalf;
    SDValue TopHalf;
    static const unsigned Ops[2][3] =
        { { ISD::MULHU, ISD::UMUL_LOHI, ISD::ZERO_EXTEND },
          { ISD::MULHS, ISD::SMUL_LOHI, ISD::SIGN_EXTEND }};
    bool isSigned = Node->getOpcode() == ISD::SMULO;
    if (TLI.isOperationLegalOrCustom(Ops[isSigned][0], VT)) {
      BottomHalf = DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
      TopHalf = DAG.getNode(Ops[isSigned][0], dl, VT, LHS, RHS);
    } else if (TLI.isOperationLegalOrCustom(Ops[isSigned][1], VT)) {
      BottomHalf = DAG.getNode(Ops[isSigned][1], dl, DAG.getVTList(VT, VT), LHS,
                               RHS);
      TopHalf = BottomHalf.getValue(1);
    } else if (TLI.isTypeLegal(EVT::getIntegerVT(*DAG.getContext(),
                                                 VT.getSizeInBits() * 2))) {
      LHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, LHS);
      RHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, RHS);
      Tmp1 = DAG.getNode(ISD::MUL, dl, WideVT, LHS, RHS);
      BottomHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Tmp1,
                               DAG.getIntPtrConstant(0));
      TopHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Tmp1,
                            DAG.getIntPtrConstant(1));
    } else {
      // We can fall back to a libcall with an illegal type for the MUL if we
      // have a libcall big enough.
      // Also, we can fall back to a division in some cases, but that's a big
      // performance hit in the general case.
      RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
      if (WideVT == MVT::i16)
        LC = RTLIB::MUL_I16;
      else if (WideVT == MVT::i32)
        LC = RTLIB::MUL_I32;
      else if (WideVT == MVT::i64)
        LC = RTLIB::MUL_I64;
      else if (WideVT == MVT::i128)
        LC = RTLIB::MUL_I128;
      assert(LC != RTLIB::UNKNOWN_LIBCALL && "Cannot expand this operation!");

      // The high part is obtained by SRA'ing all but one of the bits of low
      // part.
      unsigned LoSize = VT.getSizeInBits();
      SDValue HiLHS = DAG.getNode(ISD::SRA, dl, VT, RHS,
                                DAG.getConstant(LoSize-1, TLI.getPointerTy()));
      SDValue HiRHS = DAG.getNode(ISD::SRA, dl, VT, LHS,
                                DAG.getConstant(LoSize-1, TLI.getPointerTy()));

      // Here we're passing the 2 arguments explicitly as 4 arguments that are
      // pre-lowered to the correct types. This all depends upon WideVT not
      // being a legal type for the architecture and thus has to be split to
      // two arguments.
      SDValue Args[] = { LHS, HiLHS, RHS, HiRHS };
      SDValue Ret = ExpandLibCall(LC, WideVT, Args, 4, isSigned, dl);
      BottomHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Ret,
                               DAG.getIntPtrConstant(0));
      TopHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Ret,
                            DAG.getIntPtrConstant(1));
      // Ret is a node with an illegal type. Because such things are not
      // generally permitted during this phase of legalization, delete the
      // node. The above EXTRACT_ELEMENT nodes should have been folded.
      DAG.DeleteNode(Ret.getNode());
    }

    if (isSigned) {
      Tmp1 = DAG.getConstant(VT.getSizeInBits() - 1,
                             TLI.getShiftAmountTy(BottomHalf.getValueType()));
      Tmp1 = DAG.getNode(ISD::SRA, dl, VT, BottomHalf, Tmp1);
      TopHalf = DAG.getSetCC(dl, getSetCCResultType(VT), TopHalf, Tmp1,
                             ISD::SETNE);
    } else {
      TopHalf = DAG.getSetCC(dl, getSetCCResultType(VT), TopHalf,
                             DAG.getConstant(0, VT), ISD::SETNE);
    }
    Results.push_back(BottomHalf);
    Results.push_back(TopHalf);
    break;
  }
  case ISD::BUILD_PAIR: {
    EVT PairTy = Node->getValueType(0);
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, PairTy, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::ANY_EXTEND, dl, PairTy, Node->getOperand(1));
    Tmp2 = DAG.getNode(ISD::SHL, dl, PairTy, Tmp2,
                       DAG.getConstant(PairTy.getSizeInBits()/2,
                                       TLI.getShiftAmountTy(PairTy)));
    Results.push_back(DAG.getNode(ISD::OR, dl, PairTy, Tmp1, Tmp2));
    break;
  }
  case ISD::SELECT:
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp3 = Node->getOperand(2);
    if (Tmp1.getOpcode() == ISD::SETCC) {
      Tmp1 = DAG.getSelectCC(dl, Tmp1.getOperand(0), Tmp1.getOperand(1),
                             Tmp2, Tmp3,
                             cast<CondCodeSDNode>(Tmp1.getOperand(2))->get());
    } else {
      Tmp1 = DAG.getSelectCC(dl, Tmp1,
                             DAG.getConstant(0, Tmp1.getValueType()),
                             Tmp2, Tmp3, ISD::SETNE);
    }
    Results.push_back(Tmp1);
    break;
  case ISD::BR_JT: {
    SDValue Chain = Node->getOperand(0);
    SDValue Table = Node->getOperand(1);
    SDValue Index = Node->getOperand(2);

    EVT PTy = TLI.getPointerTy();

    const DataLayout &TD = *TLI.getDataLayout();
    unsigned EntrySize =
      DAG.getMachineFunction().getJumpTableInfo()->getEntrySize(TD);

    Index = DAG.getNode(ISD::MUL, dl, PTy,
                        Index, DAG.getConstant(EntrySize, PTy));
    SDValue Addr = DAG.getNode(ISD::ADD, dl, PTy, Index, Table);

    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(), EntrySize * 8);
    SDValue LD = DAG.getExtLoad(ISD::SEXTLOAD, dl, PTy, Chain, Addr,
                                MachinePointerInfo::getJumpTable(), MemVT,
                                false, false, 0);
    Addr = LD;
    if (TM.getRelocationModel() == Reloc::PIC_) {
      // For PIC, the sequence is:
      // BRIND(load(Jumptable + index) + RelocBase)
      // RelocBase can be JumpTable, GOT or some sort of global base.
      Addr = DAG.getNode(ISD::ADD, dl, PTy, Addr,
                          TLI.getPICJumpTableRelocBase(Table, DAG));
    }
    Tmp1 = DAG.getNode(ISD::BRIND, dl, MVT::Other, LD.getValue(1), Addr);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BRCOND:
    // Expand brcond's setcc into its constituent parts and create a BR_CC
    // Node.
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    if (Tmp2.getOpcode() == ISD::SETCC) {
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other,
                         Tmp1, Tmp2.getOperand(2),
                         Tmp2.getOperand(0), Tmp2.getOperand(1),
                         Node->getOperand(2));
    } else {
      // We test only the i1 bit.  Skip the AND if UNDEF.
      Tmp3 = (Tmp2.getOpcode() == ISD::UNDEF) ? Tmp2 :
        DAG.getNode(ISD::AND, dl, Tmp2.getValueType(), Tmp2,
                    DAG.getConstant(1, Tmp2.getValueType()));
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other, Tmp1,
                         DAG.getCondCode(ISD::SETNE), Tmp3,
                         DAG.getConstant(0, Tmp3.getValueType()),
                         Node->getOperand(2));
    }
    Results.push_back(Tmp1);
    break;
  case ISD::SETCC: {
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp3 = Node->getOperand(2);
    LegalizeSetCCCondCode(Node->getValueType(0), Tmp1, Tmp2, Tmp3, dl);

    // If we expanded the SETCC into an AND/OR, return the new node
    if (Tmp2.getNode() == 0) {
      Results.push_back(Tmp1);
      break;
    }

    // Otherwise, SETCC for the given comparison type must be completely
    // illegal; expand it into a SELECT_CC.
    EVT VT = Node->getValueType(0);
    int TrueValue;
    switch (TLI.getBooleanContents(VT.isVector())) {
    case TargetLowering::ZeroOrOneBooleanContent:
    case TargetLowering::UndefinedBooleanContent:
      TrueValue = 1;
      break;
    case TargetLowering::ZeroOrNegativeOneBooleanContent:
      TrueValue = -1;
      break;
    }
    Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, VT, Tmp1, Tmp2,
                       DAG.getConstant(TrueValue, VT), DAG.getConstant(0, VT),
                       Tmp3);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SELECT_CC: {
    Tmp1 = Node->getOperand(0);   // LHS
    Tmp2 = Node->getOperand(1);   // RHS
    Tmp3 = Node->getOperand(2);   // True
    Tmp4 = Node->getOperand(3);   // False
    SDValue CC = Node->getOperand(4);

    LegalizeSetCCCondCode(getSetCCResultType(Tmp1.getValueType()),
                          Tmp1, Tmp2, CC, dl);

    assert(!Tmp2.getNode() && "Can't legalize SELECT_CC with legal condition!");
    Tmp2 = DAG.getConstant(0, Tmp1.getValueType());
    CC = DAG.getCondCode(ISD::SETNE);
    Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, Node->getValueType(0), Tmp1, Tmp2,
                       Tmp3, Tmp4, CC);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BR_CC: {
    Tmp1 = Node->getOperand(0);              // Chain
    Tmp2 = Node->getOperand(2);              // LHS
    Tmp3 = Node->getOperand(3);              // RHS
    Tmp4 = Node->getOperand(1);              // CC

    LegalizeSetCCCondCode(getSetCCResultType(Tmp2.getValueType()),
                          Tmp2, Tmp3, Tmp4, dl);

    assert(!Tmp3.getNode() && "Can't legalize BR_CC with legal condition!");
    Tmp3 = DAG.getConstant(0, Tmp2.getValueType());
    Tmp4 = DAG.getCondCode(ISD::SETNE);
    Tmp1 = DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0), Tmp1, Tmp4, Tmp2,
                       Tmp3, Node->getOperand(4));
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BUILD_VECTOR:
    Results.push_back(ExpandBUILD_VECTOR(Node));
    break;
  case ISD::SRA:
  case ISD::SRL:
  case ISD::SHL: {
    // Scalarize vector SRA/SRL/SHL.
    EVT VT = Node->getValueType(0);
    assert(VT.isVector() && "Unable to legalize non-vector shift");
    assert(TLI.isTypeLegal(VT.getScalarType())&& "Element type must be legal");
    unsigned NumElem = VT.getVectorNumElements();

    SmallVector<SDValue, 8> Scalars;
    for (unsigned Idx = 0; Idx < NumElem; Idx++) {
      SDValue Ex = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
                               VT.getScalarType(),
                               Node->getOperand(0), DAG.getIntPtrConstant(Idx));
      SDValue Sh = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
                               VT.getScalarType(),
                               Node->getOperand(1), DAG.getIntPtrConstant(Idx));
      Scalars.push_back(DAG.getNode(Node->getOpcode(), dl,
                                    VT.getScalarType(), Ex, Sh));
    }
    SDValue Result =
      DAG.getNode(ISD::BUILD_VECTOR, dl, Node->getValueType(0),
                  &Scalars[0], Scalars.size());
    ReplaceNode(SDValue(Node, 0), Result);
    break;
  }
  case ISD::GLOBAL_OFFSET_TABLE:
  case ISD::GlobalAddress:
  case ISD::GlobalTLSAddress:
  case ISD::ExternalSymbol:
  case ISD::ConstantPool:
  case ISD::JumpTable:
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
    // FIXME: Custom lowering for these operations shouldn't return null!
    break;
  }

  // Replace the original node with the legalized result.
  if (!Results.empty())
    ReplaceNode(Node, Results.data());
}

void SelectionDAGLegalize::PromoteNode(SDNode *Node) {
  SmallVector<SDValue, 8> Results;
  MVT OVT = Node->getSimpleValueType(0);
  if (Node->getOpcode() == ISD::UINT_TO_FP ||
      Node->getOpcode() == ISD::SINT_TO_FP ||
      Node->getOpcode() == ISD::SETCC) {
    OVT = Node->getOperand(0).getSimpleValueType();
  }
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), OVT);
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3;
  switch (Node->getOpcode()) {
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTPOP:
    // Zero extend the argument.
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));
    // Perform the larger operation. For CTPOP and CTTZ_ZERO_UNDEF, this is
    // already the correct result.
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    if (Node->getOpcode() == ISD::CTTZ) {
      // FIXME: This should set a bit in the zero extended value instead.
      Tmp2 = DAG.getSetCC(dl, getSetCCResultType(NVT),
                          Tmp1, DAG.getConstant(NVT.getSizeInBits(), NVT),
                          ISD::SETEQ);
      Tmp1 = DAG.getSelect(dl, NVT, Tmp2,
                           DAG.getConstant(OVT.getSizeInBits(), NVT), Tmp1);
    } else if (Node->getOpcode() == ISD::CTLZ ||
               Node->getOpcode() == ISD::CTLZ_ZERO_UNDEF) {
      // Tmp1 = Tmp1 - (sizeinbits(NVT) - sizeinbits(Old VT))
      Tmp1 = DAG.getNode(ISD::SUB, dl, NVT, Tmp1,
                          DAG.getConstant(NVT.getSizeInBits() -
                                          OVT.getSizeInBits(), NVT));
    }
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    break;
  case ISD::BSWAP: {
    unsigned DiffBits = NVT.getSizeInBits() - OVT.getSizeInBits();
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp1 = DAG.getNode(ISD::BSWAP, dl, NVT, Tmp1);
    Tmp1 = DAG.getNode(ISD::SRL, dl, NVT, Tmp1,
                          DAG.getConstant(DiffBits, TLI.getShiftAmountTy(NVT)));
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:
    Tmp1 = PromoteLegalFP_TO_INT(Node->getOperand(0), Node->getValueType(0),
                                 Node->getOpcode() == ISD::FP_TO_SINT, dl);
    Results.push_back(Tmp1);
    break;
  case ISD::UINT_TO_FP:
  case ISD::SINT_TO_FP:
    Tmp1 = PromoteLegalINT_TO_FP(Node->getOperand(0), Node->getValueType(0),
                                 Node->getOpcode() == ISD::SINT_TO_FP, dl);
    Results.push_back(Tmp1);
    break;
  case ISD::VAARG: {
    SDValue Chain = Node->getOperand(0); // Get the chain.
    SDValue Ptr = Node->getOperand(1); // Get the pointer.

    unsigned TruncOp;
    if (OVT.isVector()) {
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger()
        && "VAARG promotion is supported only for vectors or integer types");
      TruncOp = ISD::TRUNCATE;
    }

    // Perform the larger operation, then convert back
    Tmp1 = DAG.getVAArg(NVT, dl, Chain, Ptr, Node->getOperand(2),
             Node->getConstantOperandVal(3));
    Chain = Tmp1.getValue(1);

    Tmp2 = DAG.getNode(TruncOp, dl, OVT, Tmp1);

    // Modified the chain result - switch anything that used the old chain to
    // use the new one.
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Tmp2);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    ReplacedNode(Node);
    break;
  }
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: {
    unsigned ExtOp, TruncOp;
    if (OVT.isVector()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger() && "Cannot promote logic operation");
      ExtOp   = ISD::ANY_EXTEND;
      TruncOp = ISD::TRUNCATE;
    }
    // Promote each of the values to the new type.
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    // Perform the larger operation, then convert back
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);
    Results.push_back(DAG.getNode(TruncOp, dl, OVT, Tmp1));
    break;
  }
  case ISD::SELECT: {
    unsigned ExtOp, TruncOp;
    if (Node->getValueType(0).isVector()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else if (Node->getValueType(0).isInteger()) {
      ExtOp   = ISD::ANY_EXTEND;
      TruncOp = ISD::TRUNCATE;
    } else {
      ExtOp   = ISD::FP_EXTEND;
      TruncOp = ISD::FP_ROUND;
    }
    Tmp1 = Node->getOperand(0);
    // Promote each of the values to the new type.
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    // Perform the larger operation, then round down.
    Tmp1 = DAG.getSelect(dl, NVT, Tmp1, Tmp2, Tmp3);
    if (TruncOp != ISD::FP_ROUND)
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1);
    else
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1,
                         DAG.getIntPtrConstant(0));
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    // Cast the two input vectors.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(1));

    // Convert the shuffle mask to the right # elements.
    Tmp1 = ShuffleWithNarrowerEltType(NVT, OVT, dl, Tmp1, Tmp2, Mask);
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, OVT, Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SETCC: {
    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(2))->get();
      ExtOp = isSignedIntSetCC(CCCode) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    }
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Results.push_back(DAG.getNode(ISD::SETCC, dl, Node->getValueType(0),
                                  Tmp1, Tmp2, Node->getOperand(2)));
    break;
  }
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::FPOW: {
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);
    Results.push_back(DAG.getNode(ISD::FP_ROUND, dl, OVT,
                                  Tmp3, DAG.getIntPtrConstant(0)));
    break;
  }
  case ISD::FLOG2:
  case ISD::FEXP2:
  case ISD::FLOG:
  case ISD::FEXP: {
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Results.push_back(DAG.getNode(ISD::FP_ROUND, dl, OVT,
                                  Tmp2, DAG.getIntPtrConstant(0)));
    break;
  }
  }

  // Replace the original node with the legalized result.
  if (!Results.empty())
    ReplaceNode(Node, Results.data());
}

// SelectionDAG::Legalize - This is the entry point for the file.
//
void SelectionDAG::Legalize() {
  /// run - This is the main entry point to this class.
  ///
  SelectionDAGLegalize(*this).LegalizeDAG();
}
