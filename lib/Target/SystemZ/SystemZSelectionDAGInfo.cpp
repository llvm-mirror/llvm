//===-- SystemZSelectionDAGInfo.cpp - SystemZ SelectionDAG Info -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SystemZSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/SelectionDAG.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-selectiondag-info"

SystemZSelectionDAGInfo::SystemZSelectionDAGInfo(const DataLayout &DL)
    : TargetSelectionDAGInfo(&DL) {}

SystemZSelectionDAGInfo::~SystemZSelectionDAGInfo() {
}

// Decide whether it is best to use a loop or straight-line code for
// a block operation of Size bytes with source address Src and destination
// address Dest.  Sequence is the opcode to use for straight-line code
// (such as MVC) and Loop is the opcode to use for loops (such as MVC_LOOP).
// Return the chain for the completed operation.
static SDValue emitMemMem(SelectionDAG &DAG, SDLoc DL, unsigned Sequence,
                          unsigned Loop, SDValue Chain, SDValue Dst,
                          SDValue Src, uint64_t Size) {
  EVT PtrVT = Src.getValueType();
  // The heuristic we use is to prefer loops for anything that would
  // require 7 or more MVCs.  With these kinds of sizes there isn't
  // much to choose between straight-line code and looping code,
  // since the time will be dominated by the MVCs themselves.
  // However, the loop has 4 or 5 instructions (depending on whether
  // the base addresses can be proved equal), so there doesn't seem
  // much point using a loop for 5 * 256 bytes or fewer.  Anything in
  // the range (5 * 256, 6 * 256) will need another instruction after
  // the loop, so it doesn't seem worth using a loop then either.
  // The next value up, 6 * 256, can be implemented in the same
  // number of straight-line MVCs as 6 * 256 - 1.
  if (Size > 6 * 256)
    return DAG.getNode(Loop, DL, MVT::Other, Chain, Dst, Src,
                       DAG.getConstant(Size, PtrVT),
                       DAG.getConstant(Size / 256, PtrVT));
  return DAG.getNode(Sequence, DL, MVT::Other, Chain, Dst, Src,
                     DAG.getConstant(Size, PtrVT));
}

SDValue SystemZSelectionDAGInfo::
EmitTargetCodeForMemcpy(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Dst, SDValue Src, SDValue Size, unsigned Align,
                        bool IsVolatile, bool AlwaysInline,
                        MachinePointerInfo DstPtrInfo,
                        MachinePointerInfo SrcPtrInfo) const {
  if (IsVolatile)
    return SDValue();

  if (auto *CSize = dyn_cast<ConstantSDNode>(Size))
    return emitMemMem(DAG, DL, SystemZISD::MVC, SystemZISD::MVC_LOOP,
                      Chain, Dst, Src, CSize->getZExtValue());
  return SDValue();
}

// Handle a memset of 1, 2, 4 or 8 bytes with the operands given by
// Chain, Dst, ByteVal and Size.  These cases are expected to use
// MVI, MVHHI, MVHI and MVGHI respectively.
static SDValue memsetStore(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                           SDValue Dst, uint64_t ByteVal, uint64_t Size,
                           unsigned Align,
                           MachinePointerInfo DstPtrInfo) {
  uint64_t StoreVal = ByteVal;
  for (unsigned I = 1; I < Size; ++I)
    StoreVal |= ByteVal << (I * 8);
  return DAG.getStore(Chain, DL,
                      DAG.getConstant(StoreVal, MVT::getIntegerVT(Size * 8)),
                      Dst, DstPtrInfo, false, false, Align);
}

SDValue SystemZSelectionDAGInfo::
EmitTargetCodeForMemset(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Dst, SDValue Byte, SDValue Size,
                        unsigned Align, bool IsVolatile,
                        MachinePointerInfo DstPtrInfo) const {
  EVT PtrVT = Dst.getValueType();

  if (IsVolatile)
    return SDValue();

  if (auto *CSize = dyn_cast<ConstantSDNode>(Size)) {
    uint64_t Bytes = CSize->getZExtValue();
    if (Bytes == 0)
      return SDValue();
    if (auto *CByte = dyn_cast<ConstantSDNode>(Byte)) {
      // Handle cases that can be done using at most two of
      // MVI, MVHI, MVHHI and MVGHI.  The latter two can only be
      // used if ByteVal is all zeros or all ones; in other casees,
      // we can move at most 2 halfwords.
      uint64_t ByteVal = CByte->getZExtValue();
      if (ByteVal == 0 || ByteVal == 255 ?
          Bytes <= 16 && CountPopulation_64(Bytes) <= 2 :
          Bytes <= 4) {
        unsigned Size1 = Bytes == 16 ? 8 : 1 << findLastSet(Bytes);
        unsigned Size2 = Bytes - Size1;
        SDValue Chain1 = memsetStore(DAG, DL, Chain, Dst, ByteVal, Size1,
                                     Align, DstPtrInfo);
        if (Size2 == 0)
          return Chain1;
        Dst = DAG.getNode(ISD::ADD, DL, PtrVT, Dst,
                          DAG.getConstant(Size1, PtrVT));
        DstPtrInfo = DstPtrInfo.getWithOffset(Size1);
        SDValue Chain2 = memsetStore(DAG, DL, Chain, Dst, ByteVal, Size2,
                                     std::min(Align, Size1), DstPtrInfo);
        return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain1, Chain2);
      }
    } else {
      // Handle one and two bytes using STC.
      if (Bytes <= 2) {
        SDValue Chain1 = DAG.getStore(Chain, DL, Byte, Dst, DstPtrInfo,
                                      false, false, Align);
        if (Bytes == 1)
          return Chain1;
        SDValue Dst2 = DAG.getNode(ISD::ADD, DL, PtrVT, Dst,
                                   DAG.getConstant(1, PtrVT));
        SDValue Chain2 = DAG.getStore(Chain, DL, Byte, Dst2,
                                      DstPtrInfo.getWithOffset(1),
                                      false, false, 1);
        return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chain1, Chain2);
      }
    }
    assert(Bytes >= 2 && "Should have dealt with 0- and 1-byte cases already");

    // Handle the special case of a memset of 0, which can use XC.
    auto *CByte = dyn_cast<ConstantSDNode>(Byte);
    if (CByte && CByte->getZExtValue() == 0)
      return emitMemMem(DAG, DL, SystemZISD::XC, SystemZISD::XC_LOOP,
                        Chain, Dst, Dst, Bytes);

    // Copy the byte to the first location and then use MVC to copy
    // it to the rest.
    Chain = DAG.getStore(Chain, DL, Byte, Dst, DstPtrInfo,
                         false, false, Align);
    SDValue DstPlus1 = DAG.getNode(ISD::ADD, DL, PtrVT, Dst,
                                   DAG.getConstant(1, PtrVT));
    return emitMemMem(DAG, DL, SystemZISD::MVC, SystemZISD::MVC_LOOP,
                      Chain, DstPlus1, Dst, Bytes - 1);
  }
  return SDValue();
}

// Use CLC to compare [Src1, Src1 + Size) with [Src2, Src2 + Size),
// deciding whether to use a loop or straight-line code.
static SDValue emitCLC(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                       SDValue Src1, SDValue Src2, uint64_t Size) {
  SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
  EVT PtrVT = Src1.getValueType();
  // A two-CLC sequence is a clear win over a loop, not least because it
  // needs only one branch.  A three-CLC sequence needs the same number
  // of branches as a loop (i.e. 2), but is shorter.  That brings us to
  // lengths greater than 768 bytes.  It seems relatively likely that
  // a difference will be found within the first 768 bytes, so we just
  // optimize for the smallest number of branch instructions, in order
  // to avoid polluting the prediction buffer too much.  A loop only ever
  // needs 2 branches, whereas a straight-line sequence would need 3 or more.
  if (Size > 3 * 256)
    return DAG.getNode(SystemZISD::CLC_LOOP, DL, VTs, Chain, Src1, Src2,
                       DAG.getConstant(Size, PtrVT),
                       DAG.getConstant(Size / 256, PtrVT));
  return DAG.getNode(SystemZISD::CLC, DL, VTs, Chain, Src1, Src2,
                     DAG.getConstant(Size, PtrVT));
}

// Convert the current CC value into an integer that is 0 if CC == 0,
// less than zero if CC == 1 and greater than zero if CC >= 2.
// The sequence starts with IPM, which puts CC into bits 29 and 28
// of an integer and clears bits 30 and 31.
static SDValue addIPMSequence(SDLoc DL, SDValue Glue, SelectionDAG &DAG) {
  SDValue IPM = DAG.getNode(SystemZISD::IPM, DL, MVT::i32, Glue);
  SDValue SRL = DAG.getNode(ISD::SRL, DL, MVT::i32, IPM,
                            DAG.getConstant(SystemZ::IPM_CC, MVT::i32));
  SDValue ROTL = DAG.getNode(ISD::ROTL, DL, MVT::i32, SRL,
                             DAG.getConstant(31, MVT::i32));
  return ROTL;
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForMemcmp(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Src1, SDValue Src2, SDValue Size,
                        MachinePointerInfo Op1PtrInfo,
                        MachinePointerInfo Op2PtrInfo) const {
  if (auto *CSize = dyn_cast<ConstantSDNode>(Size)) {
    uint64_t Bytes = CSize->getZExtValue();
    assert(Bytes > 0 && "Caller should have handled 0-size case");
    Chain = emitCLC(DAG, DL, Chain, Src1, Src2, Bytes);
    SDValue Glue = Chain.getValue(1);
    return std::make_pair(addIPMSequence(DL, Glue, DAG), Chain);
  }
  return std::make_pair(SDValue(), SDValue());
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForMemchr(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Src, SDValue Char, SDValue Length,
                        MachinePointerInfo SrcPtrInfo) const {
  // Use SRST to find the character.  End is its address on success.
  EVT PtrVT = Src.getValueType();
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::Other, MVT::Glue);
  Length = DAG.getZExtOrTrunc(Length, DL, PtrVT);
  Char = DAG.getZExtOrTrunc(Char, DL, MVT::i32);
  Char = DAG.getNode(ISD::AND, DL, MVT::i32, Char,
                     DAG.getConstant(255, MVT::i32));
  SDValue Limit = DAG.getNode(ISD::ADD, DL, PtrVT, Src, Length);
  SDValue End = DAG.getNode(SystemZISD::SEARCH_STRING, DL, VTs, Chain,
                            Limit, Src, Char);
  Chain = End.getValue(1);
  SDValue Glue = End.getValue(2);

  // Now select between End and null, depending on whether the character
  // was found.
  SmallVector<SDValue, 5> Ops;
  Ops.push_back(End);
  Ops.push_back(DAG.getConstant(0, PtrVT));
  Ops.push_back(DAG.getConstant(SystemZ::CCMASK_SRST, MVT::i32));
  Ops.push_back(DAG.getConstant(SystemZ::CCMASK_SRST_FOUND, MVT::i32));
  Ops.push_back(Glue);
  VTs = DAG.getVTList(PtrVT, MVT::Glue);
  End = DAG.getNode(SystemZISD::SELECT_CCMASK, DL, VTs, Ops);
  return std::make_pair(End, Chain);
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForStrcpy(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Dest, SDValue Src,
                        MachinePointerInfo DestPtrInfo,
                        MachinePointerInfo SrcPtrInfo, bool isStpcpy) const {
  SDVTList VTs = DAG.getVTList(Dest.getValueType(), MVT::Other);
  SDValue EndDest = DAG.getNode(SystemZISD::STPCPY, DL, VTs, Chain, Dest, Src,
                                DAG.getConstant(0, MVT::i32));
  return std::make_pair(isStpcpy ? EndDest : Dest, EndDest.getValue(1));
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForStrcmp(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Src1, SDValue Src2,
                        MachinePointerInfo Op1PtrInfo,
                        MachinePointerInfo Op2PtrInfo) const {
  SDVTList VTs = DAG.getVTList(Src1.getValueType(), MVT::Other, MVT::Glue);
  SDValue Unused = DAG.getNode(SystemZISD::STRCMP, DL, VTs, Chain, Src1, Src2,
                               DAG.getConstant(0, MVT::i32));
  Chain = Unused.getValue(1);
  SDValue Glue = Chain.getValue(2);
  return std::make_pair(addIPMSequence(DL, Glue, DAG), Chain);
}

// Search from Src for a null character, stopping once Src reaches Limit.
// Return a pair of values, the first being the number of nonnull characters
// and the second being the out chain.
//
// This can be used for strlen by setting Limit to 0.
static std::pair<SDValue, SDValue> getBoundedStrlen(SelectionDAG &DAG, SDLoc DL,
                                                    SDValue Chain, SDValue Src,
                                                    SDValue Limit) {
  EVT PtrVT = Src.getValueType();
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::Other, MVT::Glue);
  SDValue End = DAG.getNode(SystemZISD::SEARCH_STRING, DL, VTs, Chain,
                            Limit, Src, DAG.getConstant(0, MVT::i32));
  Chain = End.getValue(1);
  SDValue Len = DAG.getNode(ISD::SUB, DL, PtrVT, End, Src);
  return std::make_pair(Len, Chain);
}    

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForStrlen(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                        SDValue Src, MachinePointerInfo SrcPtrInfo) const {
  EVT PtrVT = Src.getValueType();
  return getBoundedStrlen(DAG, DL, Chain, Src, DAG.getConstant(0, PtrVT));
}

std::pair<SDValue, SDValue> SystemZSelectionDAGInfo::
EmitTargetCodeForStrnlen(SelectionDAG &DAG, SDLoc DL, SDValue Chain,
                         SDValue Src, SDValue MaxLength,
                         MachinePointerInfo SrcPtrInfo) const {
  EVT PtrVT = Src.getValueType();
  MaxLength = DAG.getZExtOrTrunc(MaxLength, DL, PtrVT);
  SDValue Limit = DAG.getNode(ISD::ADD, DL, PtrVT, Src, MaxLength);
  return getBoundedStrlen(DAG, DL, Chain, Src, Limit);
}
