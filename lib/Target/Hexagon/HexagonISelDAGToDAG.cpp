//===-- HexagonISelDAGToDAG.cpp - A dag to dag inst selector for Hexagon --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the Hexagon target.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "hexagon-isel"
#include "Hexagon.h"
#include "HexagonISelLowering.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

static
cl::opt<unsigned>
MaxNumOfUsesForConstExtenders("ga-max-num-uses-for-constant-extenders",
  cl::Hidden, cl::init(2),
  cl::desc("Maximum number of uses of a global address such that we still us a"
           "constant extended instruction"));

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

namespace llvm {
  void initializeHexagonDAGToDAGISelPass(PassRegistry&);
}

//===--------------------------------------------------------------------===//
/// HexagonDAGToDAGISel - Hexagon specific code to select Hexagon machine
/// instructions for SelectionDAG operations.
///
namespace {
class HexagonDAGToDAGISel : public SelectionDAGISel {
  /// Subtarget - Keep a pointer to the Hexagon Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const HexagonSubtarget &Subtarget;

  // Keep a reference to HexagonTargetMachine.
  const HexagonTargetMachine& TM;
  DenseMap<const GlobalValue *, unsigned> GlobalAddressUseCountMap;
public:
  explicit HexagonDAGToDAGISel(HexagonTargetMachine &targetmachine,
                               CodeGenOpt::Level OptLevel)
    : SelectionDAGISel(targetmachine, OptLevel),
      Subtarget(targetmachine.getSubtarget<HexagonSubtarget>()),
      TM(targetmachine) {
    initializeHexagonDAGToDAGISelPass(*PassRegistry::getPassRegistry());
  }
  bool hasNumUsesBelowThresGA(SDNode *N) const;

  SDNode *Select(SDNode *N);

  // Complex Pattern Selectors.
  inline bool foldGlobalAddress(SDValue &N, SDValue &R);
  inline bool foldGlobalAddressGP(SDValue &N, SDValue &R);
  bool foldGlobalAddressImpl(SDValue &N, SDValue &R, bool ShouldLookForGP);
  bool SelectADDRri(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRriS11_0(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRriS11_1(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRriS11_2(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectMEMriS11_2(SDValue& Addr, SDValue &Base, SDValue &Offset);
  bool SelectADDRriS11_3(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRrr(SDValue &Addr, SDValue &Base, SDValue &Offset);
  bool SelectADDRriU6_0(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRriU6_1(SDValue& N, SDValue &R1, SDValue &R2);
  bool SelectADDRriU6_2(SDValue& N, SDValue &R1, SDValue &R2);

  virtual const char *getPassName() const {
    return "Hexagon DAG->DAG Pattern Instruction Selection";
  }

  /// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
  /// inline asm expressions.
  virtual bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                            char ConstraintCode,
                                            std::vector<SDValue> &OutOps);
  bool SelectAddr(SDNode *Op, SDValue Addr, SDValue &Base, SDValue &Offset);

  SDNode *SelectLoad(SDNode *N);
  SDNode *SelectBaseOffsetLoad(LoadSDNode *LD, SDLoc dl);
  SDNode *SelectIndexedLoad(LoadSDNode *LD, SDLoc dl);
  SDNode *SelectIndexedLoadZeroExtend64(LoadSDNode *LD, unsigned Opcode,
                                        SDLoc dl);
  SDNode *SelectIndexedLoadSignExtend64(LoadSDNode *LD, unsigned Opcode,
                                        SDLoc dl);
  SDNode *SelectBaseOffsetStore(StoreSDNode *ST, SDLoc dl);
  SDNode *SelectIndexedStore(StoreSDNode *ST, SDLoc dl);
  SDNode *SelectStore(SDNode *N);
  SDNode *SelectSHL(SDNode *N);
  SDNode *SelectSelect(SDNode *N);
  SDNode *SelectTruncate(SDNode *N);
  SDNode *SelectMul(SDNode *N);
  SDNode *SelectZeroExtend(SDNode *N);
  SDNode *SelectIntrinsicWOChain(SDNode *N);
  SDNode *SelectIntrinsicWChain(SDNode *N);
  SDNode *SelectConstant(SDNode *N);
  SDNode *SelectConstantFP(SDNode *N);
  SDNode *SelectAdd(SDNode *N);
  bool isConstExtProfitable(SDNode *N) const;

// XformMskToBitPosU5Imm - Returns the bit position which
// the single bit 32 bit mask represents.
// Used in Clr and Set bit immediate memops.
SDValue XformMskToBitPosU5Imm(uint32_t Imm) {
  int32_t bitPos;
  bitPos = Log2_32(Imm);
  assert(bitPos >= 0 && bitPos < 32 &&
         "Constant out of range for 32 BitPos Memops");
  return CurDAG->getTargetConstant(bitPos, MVT::i32);
}

// XformMskToBitPosU4Imm - Returns the bit position which the single bit 16 bit
// mask represents. Used in Clr and Set bit immediate memops.
SDValue XformMskToBitPosU4Imm(uint16_t Imm) {
  return XformMskToBitPosU5Imm(Imm);
}

// XformMskToBitPosU3Imm - Returns the bit position which the single bit 8 bit
// mask represents. Used in Clr and Set bit immediate memops.
SDValue XformMskToBitPosU3Imm(uint8_t Imm) {
  return XformMskToBitPosU5Imm(Imm);
}

// Return true if there is exactly one bit set in V, i.e., if V is one of the
// following integers: 2^0, 2^1, ..., 2^31.
bool ImmIsSingleBit(uint32_t v) const {
  uint32_t c = CountPopulation_64(v);
  // Only return true if we counted 1 bit.
  return c == 1;
}

// XformM5ToU5Imm - Return a target constant with the specified value, of type
// i32 where the negative literal is transformed into a positive literal for
// use in -= memops.
inline SDValue XformM5ToU5Imm(signed Imm) {
   assert( (Imm >= -31 && Imm <= -1)  && "Constant out of range for Memops");
   return CurDAG->getTargetConstant( - Imm, MVT::i32);
}


// XformU7ToU7M1Imm - Return a target constant decremented by 1, in range
// [1..128], used in cmpb.gtu instructions.
inline SDValue XformU7ToU7M1Imm(signed Imm) {
  assert((Imm >= 1 && Imm <= 128) && "Constant out of range for cmpb op");
  return CurDAG->getTargetConstant(Imm - 1, MVT::i8);
}

// XformS8ToS8M1Imm - Return a target constant decremented by 1.
inline SDValue XformSToSM1Imm(signed Imm) {
  return CurDAG->getTargetConstant(Imm - 1, MVT::i32);
}

// XformU8ToU8M1Imm - Return a target constant decremented by 1.
inline SDValue XformUToUM1Imm(unsigned Imm) {
  assert((Imm >= 1) && "Cannot decrement unsigned int less than 1");
  return CurDAG->getTargetConstant(Imm - 1, MVT::i32);
}

// Include the pieces autogenerated from the target description.
#include "HexagonGenDAGISel.inc"
};
}  // end anonymous namespace


/// createHexagonISelDag - This pass converts a legalized DAG into a
/// Hexagon-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createHexagonISelDag(HexagonTargetMachine &TM,
                                         CodeGenOpt::Level OptLevel) {
  return new HexagonDAGToDAGISel(TM, OptLevel);
}

static void initializePassOnce(PassRegistry &Registry) {
  const char *Name = "Hexagon DAG->DAG Pattern Instruction Selection";
  PassInfo *PI = new PassInfo(Name, "hexagon-isel",
                              &SelectionDAGISel::ID, 0, false, false);
  Registry.registerPass(*PI, true);
}

void llvm::initializeHexagonDAGToDAGISelPass(PassRegistry &Registry) {
  CALL_ONCE_INITIALIZATION(initializePassOnce)
}


static bool IsS11_0_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // immS16 predicate - True if the immediate fits in a 16-bit sign extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isInt<11>(v);
}


static bool IsS11_1_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // immS16 predicate - True if the immediate fits in a 16-bit sign extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedInt<11,1>(v);
}


static bool IsS11_2_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // immS16 predicate - True if the immediate fits in a 16-bit sign extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedInt<11,2>(v);
}


static bool IsS11_3_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // immS16 predicate - True if the immediate fits in a 16-bit sign extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedInt<11,3>(v);
}


static bool IsU6_0_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // u6 predicate - True if the immediate fits in a 6-bit unsigned extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isUInt<6>(v);
}


static bool IsU6_1_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // u6 predicate - True if the immediate fits in a 6-bit unsigned extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedUInt<6,1>(v);
}


static bool IsU6_2_Offset(SDNode * S) {
    ConstantSDNode *N = cast<ConstantSDNode>(S);

  // u6 predicate - True if the immediate fits in a 6-bit unsigned extended
  // field.
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedUInt<6,2>(v);
}


// Intrinsics that return a a predicate.
static unsigned doesIntrinsicReturnPredicate(unsigned ID)
{
  switch (ID) {
    default:
      return 0;
    case Intrinsic::hexagon_C2_cmpeq:
    case Intrinsic::hexagon_C2_cmpgt:
    case Intrinsic::hexagon_C2_cmpgtu:
    case Intrinsic::hexagon_C2_cmpgtup:
    case Intrinsic::hexagon_C2_cmpgtp:
    case Intrinsic::hexagon_C2_cmpeqp:
    case Intrinsic::hexagon_C2_bitsset:
    case Intrinsic::hexagon_C2_bitsclr:
    case Intrinsic::hexagon_C2_cmpeqi:
    case Intrinsic::hexagon_C2_cmpgti:
    case Intrinsic::hexagon_C2_cmpgtui:
    case Intrinsic::hexagon_C2_cmpgei:
    case Intrinsic::hexagon_C2_cmpgeui:
    case Intrinsic::hexagon_C2_cmplt:
    case Intrinsic::hexagon_C2_cmpltu:
    case Intrinsic::hexagon_C2_bitsclri:
    case Intrinsic::hexagon_C2_and:
    case Intrinsic::hexagon_C2_or:
    case Intrinsic::hexagon_C2_xor:
    case Intrinsic::hexagon_C2_andn:
    case Intrinsic::hexagon_C2_not:
    case Intrinsic::hexagon_C2_orn:
    case Intrinsic::hexagon_C2_pxfer_map:
    case Intrinsic::hexagon_C2_any8:
    case Intrinsic::hexagon_C2_all8:
    case Intrinsic::hexagon_A2_vcmpbeq:
    case Intrinsic::hexagon_A2_vcmpbgtu:
    case Intrinsic::hexagon_A2_vcmpheq:
    case Intrinsic::hexagon_A2_vcmphgt:
    case Intrinsic::hexagon_A2_vcmphgtu:
    case Intrinsic::hexagon_A2_vcmpweq:
    case Intrinsic::hexagon_A2_vcmpwgt:
    case Intrinsic::hexagon_A2_vcmpwgtu:
    case Intrinsic::hexagon_C2_tfrrp:
    case Intrinsic::hexagon_S2_tstbit_i:
    case Intrinsic::hexagon_S2_tstbit_r:
      return 1;
  }
}


// Intrinsics that have predicate operands.
static unsigned doesIntrinsicContainPredicate(unsigned ID)
{
  switch (ID) {
    default:
      return 0;
    case Intrinsic::hexagon_C2_tfrpr:
      return Hexagon::TFR_RsPd;
    case Intrinsic::hexagon_C2_and:
      return Hexagon::AND_pp;
    case Intrinsic::hexagon_C2_xor:
      return Hexagon::XOR_pp;
    case Intrinsic::hexagon_C2_or:
      return Hexagon::OR_pp;
    case Intrinsic::hexagon_C2_not:
      return Hexagon::NOT_p;
    case Intrinsic::hexagon_C2_any8:
      return Hexagon::ANY_pp;
    case Intrinsic::hexagon_C2_all8:
      return Hexagon::ALL_pp;
    case Intrinsic::hexagon_C2_vitpack:
      return Hexagon::VITPACK_pp;
    case Intrinsic::hexagon_C2_mask:
      return Hexagon::MASK_p;
    case Intrinsic::hexagon_C2_mux:
      return Hexagon::MUX_rr;

      // Mapping hexagon_C2_muxir to MUX_pri.  This is pretty weird - but
      // that's how it's mapped in q6protos.h.
    case Intrinsic::hexagon_C2_muxir:
      return Hexagon::MUX_ri;

      // Mapping hexagon_C2_muxri to MUX_pir.  This is pretty weird - but
      // that's how it's mapped in q6protos.h.
    case Intrinsic::hexagon_C2_muxri:
      return Hexagon::MUX_ir;

    case Intrinsic::hexagon_C2_muxii:
      return Hexagon::MUX_ii;
    case Intrinsic::hexagon_C2_vmux:
      return Hexagon::VMUX_prr64;
    case Intrinsic::hexagon_S2_valignrb:
      return Hexagon::VALIGN_rrp;
    case Intrinsic::hexagon_S2_vsplicerb:
      return Hexagon::VSPLICE_rrp;
  }
}


static bool OffsetFitsS11(EVT MemType, int64_t Offset) {
  if (MemType == MVT::i64 && isShiftedInt<11,3>(Offset)) {
    return true;
  }
  if (MemType == MVT::i32 && isShiftedInt<11,2>(Offset)) {
    return true;
  }
  if (MemType == MVT::i16 && isShiftedInt<11,1>(Offset)) {
    return true;
  }
  if (MemType == MVT::i8 && isInt<11>(Offset)) {
    return true;
  }
  return false;
}


//
// Try to lower loads of GlobalAdresses into base+offset loads.  Custom
// lowering for GlobalAddress nodes has already turned it into a
// CONST32.
//
SDNode *HexagonDAGToDAGISel::SelectBaseOffsetLoad(LoadSDNode *LD, SDLoc dl) {
  SDValue Chain = LD->getChain();
  SDNode* Const32 = LD->getBasePtr().getNode();
  unsigned Opcode = 0;

  if (Const32->getOpcode() == HexagonISD::CONST32 &&
      ISD::isNormalLoad(LD)) {
    SDValue Base = Const32->getOperand(0);
    EVT LoadedVT = LD->getMemoryVT();
    int64_t Offset = cast<GlobalAddressSDNode>(Base)->getOffset();
    if (Offset != 0 && OffsetFitsS11(LoadedVT, Offset)) {
      MVT PointerTy = getTargetLowering()->getPointerTy();
      const GlobalValue* GV =
        cast<GlobalAddressSDNode>(Base)->getGlobal();
      SDValue TargAddr =
        CurDAG->getTargetGlobalAddress(GV, dl, PointerTy, 0);
      SDNode* NewBase = CurDAG->getMachineNode(Hexagon::CONST32_set,
                                               dl, PointerTy,
                                               TargAddr);
      // Figure out base + offset opcode
      if (LoadedVT == MVT::i64) Opcode = Hexagon::LDrid_indexed;
      else if (LoadedVT == MVT::i32) Opcode = Hexagon::LDriw_indexed;
      else if (LoadedVT == MVT::i16) Opcode = Hexagon::LDrih_indexed;
      else if (LoadedVT == MVT::i8) Opcode = Hexagon::LDrib_indexed;
      else llvm_unreachable("unknown memory type");

      // Build indexed load.
      SDValue TargetConstOff = CurDAG->getTargetConstant(Offset, PointerTy);
      SDNode* Result = CurDAG->getMachineNode(Opcode, dl,
                                              LD->getValueType(0),
                                              MVT::Other,
                                              SDValue(NewBase,0),
                                              TargetConstOff,
                                              Chain);
      MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
      MemOp[0] = LD->getMemOperand();
      cast<MachineSDNode>(Result)->setMemRefs(MemOp, MemOp + 1);
      ReplaceUses(LD, Result);
      return Result;
    }
  }

  return SelectCode(LD);
}


SDNode *HexagonDAGToDAGISel::SelectIndexedLoadSignExtend64(LoadSDNode *LD,
                                                           unsigned Opcode,
                                                           SDLoc dl)
{
  SDValue Chain = LD->getChain();
  EVT LoadedVT = LD->getMemoryVT();
  SDValue Base = LD->getBasePtr();
  SDValue Offset = LD->getOffset();
  SDNode *OffsetNode = Offset.getNode();
  int32_t Val = cast<ConstantSDNode>(OffsetNode)->getSExtValue();
  SDValue N1 = LD->getOperand(1);
  SDValue CPTmpN1_0;
  SDValue CPTmpN1_1;

  if (SelectADDRriS11_2(N1, CPTmpN1_0, CPTmpN1_1) &&
      N1.getNode()->getValueType(0) == MVT::i32) {
    const HexagonInstrInfo *TII =
      static_cast<const HexagonInstrInfo*>(TM.getInstrInfo());
    if (TII->isValidAutoIncImm(LoadedVT, Val)) {
      SDValue TargetConst = CurDAG->getTargetConstant(Val, MVT::i32);
      SDNode *Result_1 = CurDAG->getMachineNode(Opcode, dl, MVT::i32, MVT::i32,
                                                MVT::Other, Base, TargetConst,
                                                Chain);
      SDNode *Result_2 = CurDAG->getMachineNode(Hexagon::SXTW, dl, MVT::i64,
                                                SDValue(Result_1, 0));
      MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
      MemOp[0] = LD->getMemOperand();
      cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);
      const SDValue Froms[] = { SDValue(LD, 0),
                                SDValue(LD, 1),
                                SDValue(LD, 2)
      };
      const SDValue Tos[]   = { SDValue(Result_2, 0),
                                SDValue(Result_1, 1),
                                SDValue(Result_1, 2)
      };
      ReplaceUses(Froms, Tos, 3);
      return Result_2;
    }
    SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
    SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
    SDNode *Result_1 = CurDAG->getMachineNode(Opcode, dl, MVT::i32,
                                              MVT::Other, Base, TargetConst0,
                                              Chain);
    SDNode *Result_2 = CurDAG->getMachineNode(Hexagon::SXTW, dl,
                                                MVT::i64, SDValue(Result_1, 0));
    SDNode* Result_3 = CurDAG->getMachineNode(Hexagon::ADD_ri, dl,
                                              MVT::i32, Base, TargetConstVal,
                                                SDValue(Result_1, 1));
    MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
    MemOp[0] = LD->getMemOperand();
    cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);
    const SDValue Froms[] = { SDValue(LD, 0),
                              SDValue(LD, 1),
                              SDValue(LD, 2)
    };
    const SDValue Tos[]   = { SDValue(Result_2, 0),
                              SDValue(Result_3, 0),
                              SDValue(Result_1, 1)
    };
    ReplaceUses(Froms, Tos, 3);
    return Result_2;
  }
  return SelectCode(LD);
}


SDNode *HexagonDAGToDAGISel::SelectIndexedLoadZeroExtend64(LoadSDNode *LD,
                                                           unsigned Opcode,
                                                           SDLoc dl)
{
  SDValue Chain = LD->getChain();
  EVT LoadedVT = LD->getMemoryVT();
  SDValue Base = LD->getBasePtr();
  SDValue Offset = LD->getOffset();
  SDNode *OffsetNode = Offset.getNode();
  int32_t Val = cast<ConstantSDNode>(OffsetNode)->getSExtValue();
  SDValue N1 = LD->getOperand(1);
  SDValue CPTmpN1_0;
  SDValue CPTmpN1_1;

  if (SelectADDRriS11_2(N1, CPTmpN1_0, CPTmpN1_1) &&
      N1.getNode()->getValueType(0) == MVT::i32) {
    const HexagonInstrInfo *TII =
      static_cast<const HexagonInstrInfo*>(TM.getInstrInfo());
    if (TII->isValidAutoIncImm(LoadedVT, Val)) {
      SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
      SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
      SDNode *Result_1 = CurDAG->getMachineNode(Opcode, dl, MVT::i32,
                                                MVT::i32, MVT::Other, Base,
                                                TargetConstVal, Chain);
      SDNode *Result_2 = CurDAG->getMachineNode(Hexagon::TFRI, dl, MVT::i32,
                                                TargetConst0);
      SDNode *Result_3 = CurDAG->getMachineNode(Hexagon::COMBINE_rr, dl,
                                                MVT::i64, MVT::Other,
                                                SDValue(Result_2,0),
                                                SDValue(Result_1,0));
      MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
      MemOp[0] = LD->getMemOperand();
      cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);
      const SDValue Froms[] = { SDValue(LD, 0),
                                SDValue(LD, 1),
                                SDValue(LD, 2)
      };
      const SDValue Tos[]   = { SDValue(Result_3, 0),
                                SDValue(Result_1, 1),
                                SDValue(Result_1, 2)
      };
      ReplaceUses(Froms, Tos, 3);
      return Result_3;
    }

    // Generate an indirect load.
    SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
    SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
    SDNode *Result_1 = CurDAG->getMachineNode(Opcode, dl, MVT::i32,
                                              MVT::Other,
                                              Base, TargetConst0, Chain);
    SDNode *Result_2 = CurDAG->getMachineNode(Hexagon::TFRI, dl, MVT::i32,
                                              TargetConst0);
    SDNode *Result_3 = CurDAG->getMachineNode(Hexagon::COMBINE_rr, dl,
                                              MVT::i64, MVT::Other,
                                              SDValue(Result_2,0),
                                              SDValue(Result_1,0));
    // Add offset to base.
    SDNode* Result_4 = CurDAG->getMachineNode(Hexagon::ADD_ri, dl, MVT::i32,
                                              Base, TargetConstVal,
                                              SDValue(Result_1, 1));
    MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
    MemOp[0] = LD->getMemOperand();
    cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);
    const SDValue Froms[] = { SDValue(LD, 0),
                              SDValue(LD, 1),
                              SDValue(LD, 2)
    };
    const SDValue Tos[]   = { SDValue(Result_3, 0), // Load value.
                              SDValue(Result_4, 0), // New address.
                              SDValue(Result_1, 1)
    };
    ReplaceUses(Froms, Tos, 3);
    return Result_3;
  }

  return SelectCode(LD);
}


SDNode *HexagonDAGToDAGISel::SelectIndexedLoad(LoadSDNode *LD, SDLoc dl) {
  SDValue Chain = LD->getChain();
  SDValue Base = LD->getBasePtr();
  SDValue Offset = LD->getOffset();
  SDNode *OffsetNode = Offset.getNode();
  // Get the constant value.
  int32_t Val = cast<ConstantSDNode>(OffsetNode)->getSExtValue();
  EVT LoadedVT = LD->getMemoryVT();
  unsigned Opcode = 0;

  // Check for zero ext loads.
  bool zextval = (LD->getExtensionType() == ISD::ZEXTLOAD);

  // Figure out the opcode.
  const HexagonInstrInfo *TII =
    static_cast<const HexagonInstrInfo*>(TM.getInstrInfo());
  if (LoadedVT == MVT::i64) {
    if (TII->isValidAutoIncImm(LoadedVT, Val))
      Opcode = Hexagon::POST_LDrid;
    else
      Opcode = Hexagon::LDrid;
  } else if (LoadedVT == MVT::i32) {
    if (TII->isValidAutoIncImm(LoadedVT, Val))
      Opcode = Hexagon::POST_LDriw;
    else
      Opcode = Hexagon::LDriw;
  } else if (LoadedVT == MVT::i16) {
    if (TII->isValidAutoIncImm(LoadedVT, Val))
      Opcode = zextval ? Hexagon::POST_LDriuh : Hexagon::POST_LDrih;
    else
      Opcode = zextval ? Hexagon::LDriuh : Hexagon::LDrih;
  } else if (LoadedVT == MVT::i8) {
    if (TII->isValidAutoIncImm(LoadedVT, Val))
      Opcode = zextval ? Hexagon::POST_LDriub : Hexagon::POST_LDrib;
    else
      Opcode = zextval ? Hexagon::LDriub : Hexagon::LDrib;
  } else
    llvm_unreachable("unknown memory type");

  // For zero ext i64 loads, we need to add combine instructions.
  if (LD->getValueType(0) == MVT::i64 &&
      LD->getExtensionType() == ISD::ZEXTLOAD) {
    return SelectIndexedLoadZeroExtend64(LD, Opcode, dl);
  }
  if (LD->getValueType(0) == MVT::i64 &&
             LD->getExtensionType() == ISD::SEXTLOAD) {
    // Handle sign ext i64 loads.
    return SelectIndexedLoadSignExtend64(LD, Opcode, dl);
  }
  if (TII->isValidAutoIncImm(LoadedVT, Val)) {
    SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
    SDNode* Result = CurDAG->getMachineNode(Opcode, dl,
                                            LD->getValueType(0),
                                            MVT::i32, MVT::Other, Base,
                                            TargetConstVal, Chain);
    MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
    MemOp[0] = LD->getMemOperand();
    cast<MachineSDNode>(Result)->setMemRefs(MemOp, MemOp + 1);
    const SDValue Froms[] = { SDValue(LD, 0),
                              SDValue(LD, 1),
                              SDValue(LD, 2)
    };
    const SDValue Tos[]   = { SDValue(Result, 0),
                              SDValue(Result, 1),
                              SDValue(Result, 2)
    };
    ReplaceUses(Froms, Tos, 3);
    return Result;
  } else {
    SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
    SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
    SDNode* Result_1 = CurDAG->getMachineNode(Opcode, dl,
                                              LD->getValueType(0),
                                              MVT::Other, Base, TargetConst0,
                                              Chain);
    SDNode* Result_2 = CurDAG->getMachineNode(Hexagon::ADD_ri, dl, MVT::i32,
                                              Base, TargetConstVal,
                                              SDValue(Result_1, 1));
    MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
    MemOp[0] = LD->getMemOperand();
    cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);
    const SDValue Froms[] = { SDValue(LD, 0),
                              SDValue(LD, 1),
                              SDValue(LD, 2)
    };
    const SDValue Tos[]   = { SDValue(Result_1, 0),
                              SDValue(Result_2, 0),
                              SDValue(Result_1, 1)
    };
    ReplaceUses(Froms, Tos, 3);
    return Result_1;
  }
}


SDNode *HexagonDAGToDAGISel::SelectLoad(SDNode *N) {
  SDNode *result;
  SDLoc dl(N);
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::MemIndexedMode AM = LD->getAddressingMode();

  // Handle indexed loads.
  if (AM != ISD::UNINDEXED) {
    result = SelectIndexedLoad(LD, dl);
  } else {
    result = SelectBaseOffsetLoad(LD, dl);
  }

  return result;
}


SDNode *HexagonDAGToDAGISel::SelectIndexedStore(StoreSDNode *ST, SDLoc dl) {
  SDValue Chain = ST->getChain();
  SDValue Base = ST->getBasePtr();
  SDValue Offset = ST->getOffset();
  SDValue Value = ST->getValue();
  SDNode *OffsetNode = Offset.getNode();
  // Get the constant value.
  int32_t Val = cast<ConstantSDNode>(OffsetNode)->getSExtValue();
  EVT StoredVT = ST->getMemoryVT();

  // Offset value must be within representable range
  // and must have correct alignment properties.
  const HexagonInstrInfo *TII =
    static_cast<const HexagonInstrInfo*>(TM.getInstrInfo());
  if (TII->isValidAutoIncImm(StoredVT, Val)) {
    SDValue Ops[] = {Base, CurDAG->getTargetConstant(Val, MVT::i32), Value,
                     Chain};
    unsigned Opcode = 0;

    // Figure out the post inc version of opcode.
    if (StoredVT == MVT::i64) Opcode = Hexagon::POST_STdri;
    else if (StoredVT == MVT::i32) Opcode = Hexagon::POST_STwri;
    else if (StoredVT == MVT::i16) Opcode = Hexagon::POST_SThri;
    else if (StoredVT == MVT::i8) Opcode = Hexagon::POST_STbri;
    else llvm_unreachable("unknown memory type");

    // Build post increment store.
    SDNode* Result = CurDAG->getMachineNode(Opcode, dl, MVT::i32,
                                            MVT::Other, Ops);
    MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
    MemOp[0] = ST->getMemOperand();
    cast<MachineSDNode>(Result)->setMemRefs(MemOp, MemOp + 1);

    ReplaceUses(ST, Result);
    ReplaceUses(SDValue(ST,1), SDValue(Result,1));
    return Result;
  }

  // Note: Order of operands matches the def of instruction:
  // def STrid : STInst<(outs), (ins MEMri:$addr, DoubleRegs:$src1), ...
  // and it differs for POST_ST* for instance.
  SDValue Ops[] = { Base, CurDAG->getTargetConstant(0, MVT::i32), Value,
                    Chain};
  unsigned Opcode = 0;

  // Figure out the opcode.
  if (StoredVT == MVT::i64) Opcode = Hexagon::STrid;
  else if (StoredVT == MVT::i32) Opcode = Hexagon::STriw_indexed;
  else if (StoredVT == MVT::i16) Opcode = Hexagon::STrih;
  else if (StoredVT == MVT::i8) Opcode = Hexagon::STrib;
  else llvm_unreachable("unknown memory type");

  // Build regular store.
  SDValue TargetConstVal = CurDAG->getTargetConstant(Val, MVT::i32);
  SDNode* Result_1 = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
  // Build splitted incriment instruction.
  SDNode* Result_2 = CurDAG->getMachineNode(Hexagon::ADD_ri, dl, MVT::i32,
                                            Base,
                                            TargetConstVal,
                                            SDValue(Result_1, 0));
  MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
  MemOp[0] = ST->getMemOperand();
  cast<MachineSDNode>(Result_1)->setMemRefs(MemOp, MemOp + 1);

  ReplaceUses(SDValue(ST,0), SDValue(Result_2,0));
  ReplaceUses(SDValue(ST,1), SDValue(Result_1,0));
  return Result_2;
}


SDNode *HexagonDAGToDAGISel::SelectBaseOffsetStore(StoreSDNode *ST,
                                                   SDLoc dl) {
  SDValue Chain = ST->getChain();
  SDNode* Const32 = ST->getBasePtr().getNode();
  SDValue Value = ST->getValue();
  unsigned Opcode = 0;

  // Try to lower stores of GlobalAdresses into indexed stores.  Custom
  // lowering for GlobalAddress nodes has already turned it into a
  // CONST32.  Avoid truncating stores for the moment.  Post-inc stores
  // do the same.  Don't think there's a reason for it, so will file a
  // bug to fix.
  if ((Const32->getOpcode() == HexagonISD::CONST32) &&
      !(Value.getValueType() == MVT::i64 && ST->isTruncatingStore())) {
    SDValue Base = Const32->getOperand(0);
    if (Base.getOpcode() == ISD::TargetGlobalAddress) {
      EVT StoredVT = ST->getMemoryVT();
      int64_t Offset = cast<GlobalAddressSDNode>(Base)->getOffset();
      if (Offset != 0 && OffsetFitsS11(StoredVT, Offset)) {
        MVT PointerTy = getTargetLowering()->getPointerTy();
        const GlobalValue* GV =
          cast<GlobalAddressSDNode>(Base)->getGlobal();
        SDValue TargAddr =
          CurDAG->getTargetGlobalAddress(GV, dl, PointerTy, 0);
        SDNode* NewBase = CurDAG->getMachineNode(Hexagon::CONST32_set,
                                                 dl, PointerTy,
                                                 TargAddr);

        // Figure out base + offset opcode
        if (StoredVT == MVT::i64) Opcode = Hexagon::STrid_indexed;
        else if (StoredVT == MVT::i32) Opcode = Hexagon::STriw_indexed;
        else if (StoredVT == MVT::i16) Opcode = Hexagon::STrih_indexed;
        else if (StoredVT == MVT::i8) Opcode = Hexagon::STrib_indexed;
        else llvm_unreachable("unknown memory type");

        SDValue Ops[] = {SDValue(NewBase,0),
                         CurDAG->getTargetConstant(Offset,PointerTy),
                         Value, Chain};
        // build indexed store
        SDNode* Result = CurDAG->getMachineNode(Opcode, dl,
                                                MVT::Other, Ops);
        MachineSDNode::mmo_iterator MemOp = MF->allocateMemRefsArray(1);
        MemOp[0] = ST->getMemOperand();
        cast<MachineSDNode>(Result)->setMemRefs(MemOp, MemOp + 1);
        ReplaceUses(ST, Result);
        return Result;
      }
    }
  }

  return SelectCode(ST);
}


SDNode *HexagonDAGToDAGISel::SelectStore(SDNode *N) {
  SDLoc dl(N);
  StoreSDNode *ST = cast<StoreSDNode>(N);
  ISD::MemIndexedMode AM = ST->getAddressingMode();

  // Handle indexed stores.
  if (AM != ISD::UNINDEXED) {
    return SelectIndexedStore(ST, dl);
  }

  return SelectBaseOffsetStore(ST, dl);
}

SDNode *HexagonDAGToDAGISel::SelectMul(SDNode *N) {
  SDLoc dl(N);

  //
  // %conv.i = sext i32 %tmp1 to i64
  // %conv2.i = sext i32 %add to i64
  // %mul.i = mul nsw i64 %conv2.i, %conv.i
  //
  //   --- match with the following ---
  //
  // %mul.i = mpy (%tmp1, %add)
  //

  if (N->getValueType(0) == MVT::i64) {
    // Shifting a i64 signed multiply.
    SDValue MulOp0 = N->getOperand(0);
    SDValue MulOp1 = N->getOperand(1);

    SDValue OP0;
    SDValue OP1;

    // Handle sign_extend and sextload.
    if (MulOp0.getOpcode() == ISD::SIGN_EXTEND) {
      SDValue Sext0 = MulOp0.getOperand(0);
      if (Sext0.getNode()->getValueType(0) != MVT::i32) {
        return SelectCode(N);
      }

      OP0 = Sext0;
    } else if (MulOp0.getOpcode() == ISD::LOAD) {
      LoadSDNode *LD = cast<LoadSDNode>(MulOp0.getNode());
      if (LD->getMemoryVT() != MVT::i32 ||
          LD->getExtensionType() != ISD::SEXTLOAD ||
          LD->getAddressingMode() != ISD::UNINDEXED) {
        return SelectCode(N);
      }

      SDValue Chain = LD->getChain();
      SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
      OP0 = SDValue (CurDAG->getMachineNode(Hexagon::LDriw, dl, MVT::i32,
                                            MVT::Other,
                                            LD->getBasePtr(), TargetConst0,
                                            Chain), 0);
    } else {
      return SelectCode(N);
    }

    // Same goes for the second operand.
    if (MulOp1.getOpcode() == ISD::SIGN_EXTEND) {
      SDValue Sext1 = MulOp1.getOperand(0);
      if (Sext1.getNode()->getValueType(0) != MVT::i32) {
        return SelectCode(N);
      }

      OP1 = Sext1;
    } else if (MulOp1.getOpcode() == ISD::LOAD) {
      LoadSDNode *LD = cast<LoadSDNode>(MulOp1.getNode());
      if (LD->getMemoryVT() != MVT::i32 ||
          LD->getExtensionType() != ISD::SEXTLOAD ||
          LD->getAddressingMode() != ISD::UNINDEXED) {
        return SelectCode(N);
      }

      SDValue Chain = LD->getChain();
      SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
      OP1 = SDValue (CurDAG->getMachineNode(Hexagon::LDriw, dl, MVT::i32,
                                            MVT::Other,
                                            LD->getBasePtr(), TargetConst0,
                                            Chain), 0);
    } else {
      return SelectCode(N);
    }

    // Generate a mpy instruction.
    SDNode *Result = CurDAG->getMachineNode(Hexagon::MPY64, dl, MVT::i64,
                                            OP0, OP1);
    ReplaceUses(N, Result);
    return Result;
  }

  return SelectCode(N);
}


SDNode *HexagonDAGToDAGISel::SelectSelect(SDNode *N) {
  SDLoc dl(N);
  SDValue N0 = N->getOperand(0);
  if (N0.getOpcode() == ISD::SETCC) {
    SDValue N00 = N0.getOperand(0);
    if (N00.getOpcode() == ISD::SIGN_EXTEND_INREG) {
      SDValue N000 = N00.getOperand(0);
      SDValue N001 = N00.getOperand(1);
      if (cast<VTSDNode>(N001)->getVT() == MVT::i16) {
        SDValue N01 = N0.getOperand(1);
        SDValue N02 = N0.getOperand(2);

        // Pattern: (select:i32 (setcc:i1 (sext_inreg:i32 IntRegs:i32:$src2,
        // i16:Other),IntRegs:i32:$src1, SETLT:Other),IntRegs:i32:$src1,
        // IntRegs:i32:$src2)
        // Emits: (MAXh_rr:i32 IntRegs:i32:$src1, IntRegs:i32:$src2)
        // Pattern complexity = 9  cost = 1  size = 0.
        if (cast<CondCodeSDNode>(N02)->get() == ISD::SETLT) {
          SDValue N1 = N->getOperand(1);
          if (N01 == N1) {
            SDValue N2 = N->getOperand(2);
            if (N000 == N2 &&
                N0.getNode()->getValueType(N0.getResNo()) == MVT::i1 &&
                N00.getNode()->getValueType(N00.getResNo()) == MVT::i32) {
              SDNode *SextNode = CurDAG->getMachineNode(Hexagon::SXTH, dl,
                                                        MVT::i32, N000);
              SDNode *Result = CurDAG->getMachineNode(Hexagon::MAXw_rr, dl,
                                                      MVT::i32,
                                                      SDValue(SextNode, 0),
                                                      N1);
              ReplaceUses(N, Result);
              return Result;
            }
          }
        }

        // Pattern: (select:i32 (setcc:i1 (sext_inreg:i32 IntRegs:i32:$src2,
        // i16:Other), IntRegs:i32:$src1, SETGT:Other), IntRegs:i32:$src1,
        // IntRegs:i32:$src2)
        // Emits: (MINh_rr:i32 IntRegs:i32:$src1, IntRegs:i32:$src2)
        // Pattern complexity = 9  cost = 1  size = 0.
        if (cast<CondCodeSDNode>(N02)->get() == ISD::SETGT) {
          SDValue N1 = N->getOperand(1);
          if (N01 == N1) {
            SDValue N2 = N->getOperand(2);
            if (N000 == N2 &&
                N0.getNode()->getValueType(N0.getResNo()) == MVT::i1 &&
                N00.getNode()->getValueType(N00.getResNo()) == MVT::i32) {
              SDNode *SextNode = CurDAG->getMachineNode(Hexagon::SXTH, dl,
                                                        MVT::i32, N000);
              SDNode *Result = CurDAG->getMachineNode(Hexagon::MINw_rr, dl,
                                                      MVT::i32,
                                                      SDValue(SextNode, 0),
                                                      N1);
              ReplaceUses(N, Result);
              return Result;
            }
          }
        }
      }
    }
  }

  return SelectCode(N);
}


SDNode *HexagonDAGToDAGISel::SelectTruncate(SDNode *N) {
  SDLoc dl(N);
  SDValue Shift = N->getOperand(0);

  //
  // %conv.i = sext i32 %tmp1 to i64
  // %conv2.i = sext i32 %add to i64
  // %mul.i = mul nsw i64 %conv2.i, %conv.i
  // %shr5.i = lshr i64 %mul.i, 32
  // %conv3.i = trunc i64 %shr5.i to i32
  //
  //   --- match with the following ---
  //
  // %conv3.i = mpy (%tmp1, %add)
  //
  // Trunc to i32.
  if (N->getValueType(0) == MVT::i32) {
    // Trunc from i64.
    if (Shift.getNode()->getValueType(0) == MVT::i64) {
      // Trunc child is logical shift right.
      if (Shift.getOpcode() != ISD::SRL) {
        return SelectCode(N);
      }

      SDValue ShiftOp0 = Shift.getOperand(0);
      SDValue ShiftOp1 = Shift.getOperand(1);

      // Shift by const 32
      if (ShiftOp1.getOpcode() != ISD::Constant) {
        return SelectCode(N);
      }

      int32_t ShiftConst =
        cast<ConstantSDNode>(ShiftOp1.getNode())->getSExtValue();
      if (ShiftConst != 32) {
        return SelectCode(N);
      }

      // Shifting a i64 signed multiply
      SDValue Mul = ShiftOp0;
      if (Mul.getOpcode() != ISD::MUL) {
        return SelectCode(N);
      }

      SDValue MulOp0 = Mul.getOperand(0);
      SDValue MulOp1 = Mul.getOperand(1);

      SDValue OP0;
      SDValue OP1;

      // Handle sign_extend and sextload
      if (MulOp0.getOpcode() == ISD::SIGN_EXTEND) {
        SDValue Sext0 = MulOp0.getOperand(0);
        if (Sext0.getNode()->getValueType(0) != MVT::i32) {
          return SelectCode(N);
        }

        OP0 = Sext0;
      } else if (MulOp0.getOpcode() == ISD::LOAD) {
        LoadSDNode *LD = cast<LoadSDNode>(MulOp0.getNode());
        if (LD->getMemoryVT() != MVT::i32 ||
            LD->getExtensionType() != ISD::SEXTLOAD ||
            LD->getAddressingMode() != ISD::UNINDEXED) {
          return SelectCode(N);
        }

        SDValue Chain = LD->getChain();
        SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
        OP0 = SDValue (CurDAG->getMachineNode(Hexagon::LDriw, dl, MVT::i32,
                                              MVT::Other,
                                              LD->getBasePtr(),
                                              TargetConst0, Chain), 0);
      } else {
        return SelectCode(N);
      }

      // Same goes for the second operand.
      if (MulOp1.getOpcode() == ISD::SIGN_EXTEND) {
        SDValue Sext1 = MulOp1.getOperand(0);
        if (Sext1.getNode()->getValueType(0) != MVT::i32)
          return SelectCode(N);

        OP1 = Sext1;
      } else if (MulOp1.getOpcode() == ISD::LOAD) {
        LoadSDNode *LD = cast<LoadSDNode>(MulOp1.getNode());
        if (LD->getMemoryVT() != MVT::i32 ||
            LD->getExtensionType() != ISD::SEXTLOAD ||
            LD->getAddressingMode() != ISD::UNINDEXED) {
          return SelectCode(N);
        }

        SDValue Chain = LD->getChain();
        SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
        OP1 = SDValue (CurDAG->getMachineNode(Hexagon::LDriw, dl, MVT::i32,
                                              MVT::Other,
                                              LD->getBasePtr(),
                                              TargetConst0, Chain), 0);
      } else {
        return SelectCode(N);
      }

      // Generate a mpy instruction.
      SDNode *Result = CurDAG->getMachineNode(Hexagon::MPY, dl, MVT::i32,
                                              OP0, OP1);
      ReplaceUses(N, Result);
      return Result;
    }
  }

  return SelectCode(N);
}


SDNode *HexagonDAGToDAGISel::SelectSHL(SDNode *N) {
  SDLoc dl(N);
  if (N->getValueType(0) == MVT::i32) {
    SDValue Shl_0 = N->getOperand(0);
    SDValue Shl_1 = N->getOperand(1);
    // RHS is const.
    if (Shl_1.getOpcode() == ISD::Constant) {
      if (Shl_0.getOpcode() == ISD::MUL) {
        SDValue Mul_0 = Shl_0.getOperand(0); // Val
        SDValue Mul_1 = Shl_0.getOperand(1); // Const
        // RHS of mul is const.
        if (Mul_1.getOpcode() == ISD::Constant) {
          int32_t ShlConst =
            cast<ConstantSDNode>(Shl_1.getNode())->getSExtValue();
          int32_t MulConst =
            cast<ConstantSDNode>(Mul_1.getNode())->getSExtValue();
          int32_t ValConst = MulConst << ShlConst;
          SDValue Val = CurDAG->getTargetConstant(ValConst,
                                                  MVT::i32);
          if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Val.getNode()))
            if (isInt<9>(CN->getSExtValue())) {
              SDNode* Result =
                CurDAG->getMachineNode(Hexagon::MPYI_ri, dl,
                                       MVT::i32, Mul_0, Val);
              ReplaceUses(N, Result);
              return Result;
            }

        }
      } else if (Shl_0.getOpcode() == ISD::SUB) {
        SDValue Sub_0 = Shl_0.getOperand(0); // Const 0
        SDValue Sub_1 = Shl_0.getOperand(1); // Val
        if (Sub_0.getOpcode() == ISD::Constant) {
          int32_t SubConst =
            cast<ConstantSDNode>(Sub_0.getNode())->getSExtValue();
          if (SubConst == 0) {
            if (Sub_1.getOpcode() == ISD::SHL) {
              SDValue Shl2_0 = Sub_1.getOperand(0); // Val
              SDValue Shl2_1 = Sub_1.getOperand(1); // Const
              if (Shl2_1.getOpcode() == ISD::Constant) {
                int32_t ShlConst =
                  cast<ConstantSDNode>(Shl_1.getNode())->getSExtValue();
                int32_t Shl2Const =
                  cast<ConstantSDNode>(Shl2_1.getNode())->getSExtValue();
                int32_t ValConst = 1 << (ShlConst+Shl2Const);
                SDValue Val = CurDAG->getTargetConstant(-ValConst, MVT::i32);
                if (ConstantSDNode *CN =
                    dyn_cast<ConstantSDNode>(Val.getNode()))
                  if (isInt<9>(CN->getSExtValue())) {
                    SDNode* Result =
                      CurDAG->getMachineNode(Hexagon::MPYI_ri, dl, MVT::i32,
                                             Shl2_0, Val);
                    ReplaceUses(N, Result);
                    return Result;
                  }
              }
            }
          }
        }
      }
    }
  }
  return SelectCode(N);
}


//
// If there is an zero_extend followed an intrinsic in DAG (this means - the
// result of the intrinsic is predicate); convert the zero_extend to
// transfer instruction.
//
// Zero extend -> transfer is lowered here. Otherwise, zero_extend will be
// converted into a MUX as predicate registers defined as 1 bit in the
// compiler. Architecture defines them as 8-bit registers.
// We want to preserve all the lower 8-bits and, not just 1 LSB bit.
//
SDNode *HexagonDAGToDAGISel::SelectZeroExtend(SDNode *N) {
  SDLoc dl(N);
  SDNode *IsIntrinsic = N->getOperand(0).getNode();
  if ((IsIntrinsic->getOpcode() == ISD::INTRINSIC_WO_CHAIN)) {
    unsigned ID =
      cast<ConstantSDNode>(IsIntrinsic->getOperand(0))->getZExtValue();
    if (doesIntrinsicReturnPredicate(ID)) {
      // Now we need to differentiate target data types.
      if (N->getValueType(0) == MVT::i64) {
        // Convert the zero_extend to Rs = Pd followed by COMBINE_rr(0,Rs).
        SDValue TargetConst0 = CurDAG->getTargetConstant(0, MVT::i32);
        SDNode *Result_1 = CurDAG->getMachineNode(Hexagon::TFR_RsPd, dl,
                                                  MVT::i32,
                                                  SDValue(IsIntrinsic, 0));
        SDNode *Result_2 = CurDAG->getMachineNode(Hexagon::TFRI, dl,
                                                  MVT::i32,
                                                  TargetConst0);
        SDNode *Result_3 = CurDAG->getMachineNode(Hexagon::COMBINE_rr, dl,
                                                  MVT::i64, MVT::Other,
                                                  SDValue(Result_2, 0),
                                                  SDValue(Result_1, 0));
        ReplaceUses(N, Result_3);
        return Result_3;
      }
      if (N->getValueType(0) == MVT::i32) {
        // Convert the zero_extend to Rs = Pd
        SDNode* RsPd = CurDAG->getMachineNode(Hexagon::TFR_RsPd, dl,
                                              MVT::i32,
                                              SDValue(IsIntrinsic, 0));
        ReplaceUses(N, RsPd);
        return RsPd;
      }
      llvm_unreachable("Unexpected value type");
    }
  }
  return SelectCode(N);
}


//
// Checking for intrinsics which have predicate registers as operand(s)
// and lowering to the actual intrinsic.
//
SDNode *HexagonDAGToDAGISel::SelectIntrinsicWOChain(SDNode *N) {
  SDLoc dl(N);
  unsigned ID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  unsigned IntrinsicWithPred = doesIntrinsicContainPredicate(ID);

  // We are concerned with only those intrinsics that have predicate registers
  // as at least one of the operands.
  if (IntrinsicWithPred) {
    SmallVector<SDValue, 8> Ops;
    const HexagonInstrInfo *TII =
      static_cast<const HexagonInstrInfo*>(TM.getInstrInfo());
    const MCInstrDesc &MCID = TII->get(IntrinsicWithPred);
    const TargetRegisterInfo *TRI = TM.getRegisterInfo();

    // Iterate over all the operands of the intrinsics.
    // For PredRegs, do the transfer.
    // For Double/Int Regs, just preserve the value
    // For immediates, lower it.
    for (unsigned i = 1; i < N->getNumOperands(); ++i) {
      SDNode *Arg = N->getOperand(i).getNode();
      const TargetRegisterClass *RC = TII->getRegClass(MCID, i, TRI, *MF);

      if (RC == &Hexagon::IntRegsRegClass ||
          RC == &Hexagon::DoubleRegsRegClass) {
        Ops.push_back(SDValue(Arg, 0));
      } else if (RC == &Hexagon::PredRegsRegClass) {
        // Do the transfer.
        SDNode *PdRs = CurDAG->getMachineNode(Hexagon::TFR_PdRs, dl, MVT::i1,
                                              SDValue(Arg, 0));
        Ops.push_back(SDValue(PdRs,0));
      } else if (RC == NULL && (dyn_cast<ConstantSDNode>(Arg) != NULL)) {
        // This is immediate operand. Lower it here making sure that we DO have
        // const SDNode for immediate value.
        int32_t Val = cast<ConstantSDNode>(Arg)->getSExtValue();
        SDValue SDVal = CurDAG->getTargetConstant(Val, MVT::i32);
        Ops.push_back(SDVal);
      } else {
        llvm_unreachable("Unimplemented");
      }
    }
    EVT ReturnValueVT = N->getValueType(0);
    SDNode *Result = CurDAG->getMachineNode(IntrinsicWithPred, dl,
                                            ReturnValueVT, Ops);
    ReplaceUses(N, Result);
    return Result;
  }
  return SelectCode(N);
}

//
// Map floating point constant values.
//
SDNode *HexagonDAGToDAGISel::SelectConstantFP(SDNode *N) {
  SDLoc dl(N);
  ConstantFPSDNode *CN = dyn_cast<ConstantFPSDNode>(N);
  APFloat APF = CN->getValueAPF();
  if (N->getValueType(0) == MVT::f32) {
    return CurDAG->getMachineNode(Hexagon::TFRI_f, dl, MVT::f32,
              CurDAG->getTargetConstantFP(APF.convertToFloat(), MVT::f32));
  }
  else if (N->getValueType(0) == MVT::f64) {
    return CurDAG->getMachineNode(Hexagon::CONST64_Float_Real, dl, MVT::f64,
              CurDAG->getTargetConstantFP(APF.convertToDouble(), MVT::f64));
  }

  return SelectCode(N);
}


//
// Map predicate true (encoded as -1 in LLVM) to a XOR.
//
SDNode *HexagonDAGToDAGISel::SelectConstant(SDNode *N) {
  SDLoc dl(N);
  if (N->getValueType(0) == MVT::i1) {
    SDNode* Result;
    int32_t Val = cast<ConstantSDNode>(N)->getSExtValue();
    if (Val == -1) {
      // Create the IntReg = 1 node.
      SDNode* IntRegTFR =
        CurDAG->getMachineNode(Hexagon::TFRI, dl, MVT::i32,
                               CurDAG->getTargetConstant(0, MVT::i32));

      // Pd = IntReg
      SDNode* Pd = CurDAG->getMachineNode(Hexagon::TFR_PdRs, dl, MVT::i1,
                                          SDValue(IntRegTFR, 0));

      // not(Pd)
      SDNode* NotPd = CurDAG->getMachineNode(Hexagon::NOT_p, dl, MVT::i1,
                                             SDValue(Pd, 0));

      // xor(not(Pd))
      Result = CurDAG->getMachineNode(Hexagon::XOR_pp, dl, MVT::i1,
                                      SDValue(Pd, 0), SDValue(NotPd, 0));

      // We have just built:
      // Rs = Pd
      // Pd = xor(not(Pd), Pd)

      ReplaceUses(N, Result);
      return Result;
    }
  }

  return SelectCode(N);
}


//
// Map add followed by a asr -> asr +=.
//
SDNode *HexagonDAGToDAGISel::SelectAdd(SDNode *N) {
  SDLoc dl(N);
  if (N->getValueType(0) != MVT::i32) {
    return SelectCode(N);
  }
  // Identify nodes of the form: add(asr(...)).
  SDNode* Src1 = N->getOperand(0).getNode();
  if (Src1->getOpcode() != ISD::SRA || !Src1->hasOneUse()
      || Src1->getValueType(0) != MVT::i32) {
    return SelectCode(N);
  }

  // Build Rd = Rd' + asr(Rs, Rt). The machine constraints will ensure that
  // Rd and Rd' are assigned to the same register
  SDNode* Result = CurDAG->getMachineNode(Hexagon::ASR_ADD_rr, dl, MVT::i32,
                                          N->getOperand(1),
                                          Src1->getOperand(0),
                                          Src1->getOperand(1));
  ReplaceUses(N, Result);

  return Result;
}


SDNode *HexagonDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode())
    return NULL;   // Already selected.


  switch (N->getOpcode()) {
  case ISD::Constant:
    return SelectConstant(N);

  case ISD::ConstantFP:
    return SelectConstantFP(N);

  case ISD::ADD:
    return SelectAdd(N);

  case ISD::SHL:
    return SelectSHL(N);

  case ISD::LOAD:
    return SelectLoad(N);

  case ISD::STORE:
    return SelectStore(N);

  case ISD::SELECT:
    return SelectSelect(N);

  case ISD::TRUNCATE:
    return SelectTruncate(N);

  case ISD::MUL:
    return SelectMul(N);

  case ISD::ZERO_EXTEND:
    return SelectZeroExtend(N);

  case ISD::INTRINSIC_WO_CHAIN:
    return SelectIntrinsicWOChain(N);
  }

  return SelectCode(N);
}


//
// Hexagon_TODO: Five functions for ADDRri?! Surely there must be a better way
// to define these instructions.
//
bool HexagonDAGToDAGISel::SelectADDRri(SDValue& Addr, SDValue &Base,
                                       SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return true;
}


bool HexagonDAGToDAGISel::SelectADDRriS11_0(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsS11_0_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsS11_0_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectADDRriS11_1(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsS11_1_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsS11_1_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectADDRriS11_2(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsS11_2_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsS11_2_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectADDRriU6_0(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsU6_0_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsU6_0_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectADDRriU6_1(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsU6_1_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsU6_1_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectADDRriU6_2(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsU6_2_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsU6_2_Offset(Offset.getNode()));
}


bool HexagonDAGToDAGISel::SelectMEMriS11_2(SDValue& Addr, SDValue &Base,
                                           SDValue &Offset) {

  if (Addr.getOpcode() != ISD::ADD) {
    return(SelectADDRriS11_2(Addr, Base, Offset));
  }

  return SelectADDRriS11_2(Addr, Base, Offset);
}


bool HexagonDAGToDAGISel::SelectADDRriS11_3(SDValue& Addr, SDValue &Base,
                                            SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return (IsS11_3_Offset(Offset.getNode()));
  }
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return (IsS11_3_Offset(Offset.getNode()));
}

bool HexagonDAGToDAGISel::SelectADDRrr(SDValue &Addr, SDValue &R1,
                                       SDValue &R2) {
  if (Addr.getOpcode() == ISD::FrameIndex) return false;
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (Addr.getOpcode() == ISD::ADD) {
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1)))
      if (isInt<13>(CN->getSExtValue()))
        return false;  // Let the reg+imm pattern catch this!
    R1 = Addr.getOperand(0);
    R2 = Addr.getOperand(1);
    return true;
  }

  R1 = Addr;

  return true;
}


// Handle generic address case. It is accessed from inlined asm =m constraints,
// which could have any kind of pointer.
bool HexagonDAGToDAGISel::SelectAddr(SDNode *Op, SDValue Addr,
                                          SDValue &Base, SDValue &Offset) {
  if (Addr.getOpcode() == ISD::TargetExternalSymbol ||
      Addr.getOpcode() == ISD::TargetGlobalAddress)
    return false;  // Direct calls.

  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), MVT::i32);
    Offset = CurDAG->getTargetConstant(0, MVT::i32);
    return true;
  }

  if (Addr.getOpcode() == ISD::ADD) {
    Base = Addr.getOperand(0);
    Offset = Addr.getOperand(1);
    return true;
  }

  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, MVT::i32);
  return true;
}


bool HexagonDAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, char ConstraintCode,
                             std::vector<SDValue> &OutOps) {
  SDValue Op0, Op1;

  switch (ConstraintCode) {
  case 'o':   // Offsetable.
  case 'v':   // Not offsetable.
  default: return true;
  case 'm':   // Memory.
    if (!SelectAddr(Op.getNode(), Op, Op0, Op1))
      return true;
    break;
  }

  OutOps.push_back(Op0);
  OutOps.push_back(Op1);
  return false;
}

bool HexagonDAGToDAGISel::isConstExtProfitable(SDNode *N) const {
  unsigned UseCount = 0;
  for (SDNode::use_iterator I = N->use_begin(), E = N->use_end(); I != E; ++I) {
    UseCount++;
  }

  return (UseCount <= 1);

}

//===--------------------------------------------------------------------===//
// Return 'true' if use count of the global address is below threshold.
//===--------------------------------------------------------------------===//
bool HexagonDAGToDAGISel::hasNumUsesBelowThresGA(SDNode *N) const {
  assert(N->getOpcode() == ISD::TargetGlobalAddress &&
         "Expecting a target global address");

  // Always try to fold the address.
  if (TM.getOptLevel() == CodeGenOpt::Aggressive)
    return true;

  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(N);
  DenseMap<const GlobalValue *, unsigned>::const_iterator GI =
    GlobalAddressUseCountMap.find(GA->getGlobal());

  if (GI == GlobalAddressUseCountMap.end())
    return false;

  return GI->second <= MaxNumOfUsesForConstExtenders;
}

//===--------------------------------------------------------------------===//
// Return true if the non GP-relative global address can be folded.
//===--------------------------------------------------------------------===//
inline bool HexagonDAGToDAGISel::foldGlobalAddress(SDValue &N, SDValue &R) {
  return foldGlobalAddressImpl(N, R, false);
}

//===--------------------------------------------------------------------===//
// Return true if the GP-relative global address can be folded.
//===--------------------------------------------------------------------===//
inline bool HexagonDAGToDAGISel::foldGlobalAddressGP(SDValue &N, SDValue &R) {
  return foldGlobalAddressImpl(N, R, true);
}

//===--------------------------------------------------------------------===//
// Fold offset of the global address if number of uses are below threshold.
//===--------------------------------------------------------------------===//
bool HexagonDAGToDAGISel::foldGlobalAddressImpl(SDValue &N, SDValue &R,
                                                bool ShouldLookForGP) {
  if (N.getOpcode() == ISD::ADD) {
    SDValue N0 = N.getOperand(0);
    SDValue N1 = N.getOperand(1);
    if ((ShouldLookForGP && (N0.getOpcode() == HexagonISD::CONST32_GP)) ||
        (!ShouldLookForGP && (N0.getOpcode() == HexagonISD::CONST32))) {
      ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N1);
      GlobalAddressSDNode *GA =
        dyn_cast<GlobalAddressSDNode>(N0.getOperand(0));

      if (Const && GA &&
          (GA->getOpcode() == ISD::TargetGlobalAddress)) {
        if ((N0.getOpcode() == HexagonISD::CONST32) &&
                !hasNumUsesBelowThresGA(GA))
            return false;
        R = CurDAG->getTargetGlobalAddress(GA->getGlobal(),
                                          SDLoc(Const),
                                          N.getValueType(),
                                          GA->getOffset() +
                                          (uint64_t)Const->getSExtValue());
        return true;
      }
    }
  }
  return false;
}
