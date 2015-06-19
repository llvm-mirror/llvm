//===-- rvexISelLowering.cpp - rvex DAG Lowering Implementation -----------===//
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

#define DEBUG_TYPE "rvex-lower"
#include "rvexISelLowering.h"
#include "rvexMachineFunction.h"
#include "rvexTargetMachine.h"
#include "rvexMachineFunction.h"
#include "rvexTargetObjectFile.h"
#include "rvexSubtarget.h"
#include "MCTargetDesc/rvexBaseInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

  static const unsigned IntRegsSize=8;

  static const uint16_t IntRegs[] = {
      rvex::R3, rvex::R4, rvex::R5, rvex::R6,
      rvex::R7, rvex::R8, rvex::R9, rvex::R10
  };

static SDValue GetGlobalReg(SelectionDAG &DAG, EVT Ty) {
  rvexFunctionInfo *FI = DAG.getMachineFunction().getInfo<rvexFunctionInfo>();
  return DAG.getRegister(FI->getGlobalBaseReg(), Ty);
}

static SDValue getTargetNode(SDValue Op, SelectionDAG &DAG, unsigned Flag) {
  EVT Ty = Op.getValueType();

  if (GlobalAddressSDNode *N = dyn_cast<GlobalAddressSDNode>(Op))
    return DAG.getTargetGlobalAddress(N->getGlobal(), SDLoc(Op), Ty, 0,
                                      Flag);
  if (ExternalSymbolSDNode *N = dyn_cast<ExternalSymbolSDNode>(Op))
    return DAG.getTargetExternalSymbol(N->getSymbol(), Ty, Flag);
  if (BlockAddressSDNode *N = dyn_cast<BlockAddressSDNode>(Op))
    return DAG.getTargetBlockAddress(N->getBlockAddress(), Ty, 0, Flag);
  if (JumpTableSDNode *N = dyn_cast<JumpTableSDNode>(Op))
    return DAG.getTargetJumpTable(N->getIndex(), Ty, Flag);
  if (ConstantPoolSDNode *N = dyn_cast<ConstantPoolSDNode>(Op))
    return DAG.getTargetConstantPool(N->getConstVal(), Ty, N->getAlignment(),
                                     N->getOffset(), Flag);

  llvm_unreachable("Unexpected node type.");
  return SDValue();
}

SDValue rvexTargetLowering::getAddrLocal(SDValue Op, SelectionDAG &DAG,
                                         bool Hasrvex64) const {
  SDLoc DL = SDLoc(Op);
  EVT Ty = Op.getValueType();
  unsigned GOTFlag = Hasrvex64 ? rvexII::MO_GOT_PAGE : rvexII::MO_GOT;
  SDValue GOT = DAG.getNode(rvexISD::Wrapper, DL, Ty, GetGlobalReg(DAG, Ty),
                            getTargetNode(Op, DAG, GOTFlag));
  SDValue Load = DAG.getLoad(Ty, DL, DAG.getEntryNode(), GOT,
                             MachinePointerInfo::getGOT(), false, false, false,
                             0);
  unsigned LoFlag = Hasrvex64 ? rvexII::MO_GOT_OFST : rvexII::MO_ABS_LO;
  SDValue Lo = DAG.getNode(rvexISD::Lo, DL, Ty, getTargetNode(Op, DAG, LoFlag));
  return DAG.getNode(ISD::ADD, DL, Ty, Load, Lo);
}

SDValue rvexTargetLowering::getAddrGlobal(SDValue Op, SelectionDAG &DAG,
                                          unsigned Flag) const {
  SDLoc DL = SDLoc(Op);
  EVT Ty = Op.getValueType();
  SDValue Tgt = DAG.getNode(rvexISD::Wrapper, DL, Ty, GetGlobalReg(DAG, Ty),
                            getTargetNode(Op, DAG, Flag));
  return DAG.getLoad(Ty, DL, DAG.getEntryNode(), Tgt,
                     MachinePointerInfo::getGOT(), false, false, false, 0);
}

// getTargetNodeName used for show-isel-dags
const char *rvexTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case rvexISD::JmpLink:           return "rvexISD::JmpLink";
  case rvexISD::GPRel:             return "rvexISD::GPRel";
  case rvexISD::Ret:               return "rvexISD::Ret";
  case rvexISD::Addc:              return "rvexISD::Addc";
  case rvexISD::Adde:              return "rvexISD::Adde";
  case rvexISD::Divs:              return "rvexISD::Divs";
  case rvexISD::Orc:               return "rvexISD::Orc";

  case rvexISD::Max:               return "rvexISD::Max";
  case rvexISD::Maxu:              return "rvexISD::Maxu";
  case rvexISD::Min:               return "rvexISD::Min";
  case rvexISD::Minu:              return "rvexISD::Minu";
  case rvexISD::Slct:              return "RvexISD::Slct";

  case rvexISD::Mpyllu:            return "RvexISD::Mpyllu";
  case rvexISD::Mpylhu:            return "RvexISD::Mpylhu";
  case rvexISD::Mpyhhu:            return "RvexISD::Mpyhhu";
  case rvexISD::Mpyll:             return "RvexISD::Mpyll";
  case rvexISD::Mpylh:             return "RvexISD::Mpylh";
  case rvexISD::Mpyhh:             return "RvexISD::Mpyhh";
  case rvexISD::Mpyl:              return "RvexISD::Mpyl";
  case rvexISD::Mpyh:              return "RvexISD::Mpyh";

  case rvexISD::SXTB:              return "RvexISD::SXTB";
  case rvexISD::SXTH:              return "RvexISD::SXTH";  
  case rvexISD::ZXTB:              return "RvexISD::ZXTB";
  case rvexISD::ZXTH:              return "RvexISD::ZXTH";

      case rvexISD::BR:                 return "RvexISD::BR";
      case rvexISD::BRF:                 return "RvexISD::BRF";


  case rvexISD::DivRem:            return "rvexISD::DivRem";
  case rvexISD::DivRemU:           return "rvexISD::DivRemU";
  case rvexISD::Wrapper:           return "rvexISD::Wrapper";

  default:                         return NULL;
  }
}

rvexTargetLowering::
rvexTargetLowering(rvexTargetMachine &TM)
  : TargetLowering(TM),
    Subtarget(&TM.getSubtarget<rvexSubtarget>()) {

  // Set up the register classes
  addRegisterClass(MVT::i32, &rvex::CPURegsRegClass);
  // addRegisterClass(MVT::i32, &rvex::BRRegsRegClass);

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);  

  // rvex Custom Operations
  setOperationAction(ISD::GlobalAddress,      MVT::i32,   Custom);
  
  //TODO: scheduling modes: None, Source, RegPressure, Hybrid, ILP, VLIW
  bool isVLIWEnabled = TM.getSubtarget<rvexSubtarget>().isVLIWEnabled();
  setSchedulingPreference(isVLIWEnabled ? Sched::VLIW : Sched::Hybrid);

  // Used by legalize types to correctly generate the setcc result.
  // Without this, every float setcc comes with a AND/OR with the result,
  // we don't want this, since the fpcmp result goes to a flag register,
  // which is used implicitly by brcond and select operations.
  AddPromotedToType(ISD::SETCC, MVT::i1, MVT::i32);
  //setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  
  setOperationAction(ISD::SDIV, MVT::i32, Custom);
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIV, MVT::i32, Custom);
  setOperationAction(ISD::UREM, MVT::i32, Expand);

  setOperationAction(ISD::MULHU, MVT::i32, Custom);
  setOperationAction(ISD::MULHS, MVT::i32, Custom);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);  
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  setOperationAction(ISD::SELECT_CC, MVT::i32, Promote);

  setOperationAction(ISD::SELECT_CC, MVT::i1, Promote);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Promote);

  // setOperationAction(ISD::ZERO_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::ANY_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1,    Expand);

  // Custom lowering of ADDE and ADDC
  setOperationAction(ISD::ADDE, MVT::i32, Custom);
  setOperationAction(ISD::ADDC, MVT::i32, Custom);
  setOperationAction(ISD::SUBE, MVT::i32, Custom);
  setOperationAction(ISD::SUBC, MVT::i32, Custom);  

  for (MVT VT : MVT::integer_valuetypes()) {
      setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1,  Promote);
      setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1,  Promote);
      setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1,  Promote);
  }

  setOperationAction(ISD::BR_CC,            MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,            MVT::i1, Expand);
  setOperationAction(ISD::BR_CC,            MVT::i32, Expand);
  setOperationAction(ISD::BR_JT,            MVT::Other, Expand);
  setOperationAction(ISD::BRIND,            MVT::Other, Expand);


  setOperationAction(ISD::AND,              MVT::i1, Promote);
  setOperationAction(ISD::OR,               MVT::i1, Promote);
  setOperationAction(ISD::ADD,              MVT::i1, Promote);
  setOperationAction(ISD::SUB,              MVT::i1, Promote);
  setOperationAction(ISD::XOR,              MVT::i1, Promote);
  setOperationAction(ISD::SHL,              MVT::i1, Promote);
  setOperationAction(ISD::SRA,              MVT::i1, Promote);
  setOperationAction(ISD::SRL,              MVT::i1, Promote);

  setOperationAction(ISD::ROTL,             MVT::i32, Expand);
  setOperationAction(ISD::ROTR,             MVT::i32, Expand);
  setOperationAction(ISD::ROTL,             MVT::i64, Expand);
  setOperationAction(ISD::ROTR,             MVT::i64, Expand);  

  // // Softfloat Floating Point Library Calls
  // // Integer to Float conversions
  // setLibcallName(RTLIB::SINTTOFP_I32_F32, "_r_ilfloat");
  // // setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);

  // setLibcallName(RTLIB::UINTTOFP_I32_F32, "_r_ufloat");
  // // setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);

  // setLibcallName(RTLIB::SINTTOFP_I32_F64, "_d_ilfloat");
  // // setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);

  // setLibcallName(RTLIB::UINTTOFP_I32_F64, "_d_ufloat");
  // // setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);

  // //Software IEC/IEEE single-precision conversion routines.

  // setLibcallName(RTLIB::FPTOSINT_F32_I32, "_r_fix");
  // // setOperationAction(ISD::FP_TO_SINT, MVT::f32, Expand);

  // //FIXME
  // //float32_to_int32_round_to_zero

  // setLibcallName(RTLIB::FPEXT_F32_F64, "_d_r");
  // // setOperationAction(ISD::FP_EXTEND, MVT::f32, Expand);

  // //Software IEC/IEEE single-precision operations.
  // // FIXME are these roundings correct? There is NEARBYINT_F too..
  // setLibcallName(RTLIB::RINT_F32, "float32_round_to_int");
  // // setOperationAction(ISD::FRINT , MVT::f32, Expand);


  // setLibcallName(RTLIB::ADD_F32, "_r_add");
  // // setOperationAction(ISD::FADD, MVT::f32, Expand);

  // setLibcallName(RTLIB::SUB_F32, "_r_sub");
  // // setOperationAction(ISD::FSUB, MVT::f32, Expand);

  // setLibcallName(RTLIB::MUL_F32, "_r_mul");
  // // setOperationAction(ISD::FMUL, MVT::f32, Expand);

  // setLibcallName(RTLIB::DIV_F32, "_r_div");
  // // setOperationAction(ISD::FDIV, MVT::f32, Expand);

  // setLibcallName(RTLIB::ADD_F64, "_d_add");
  // // setOperationAction(ISD::FADD, MVT::f64, Expand);

  // setLibcallName(RTLIB::SUB_F64, "_d_sub");
  // // setOperationAction(ISD::FSUB, MVT::f64, Expand);

  // setLibcallName(RTLIB::MUL_F64, "_d_mul");
  // // setOperationAction(ISD::FMUL, MVT::f64, Expand);

  // setLibcallName(RTLIB::DIV_F64, "_d_div");
  // // setOperationAction(ISD::FDIV, MVT::f64, Expand);  

  // setLibcallName(RTLIB::REM_F32, "float32_rem");
  // // setOperationAction(ISD::SREM, MVT::f32, Expand);
  // //setLibcallName(RTLIB::UREM_F32, "float32_rem");
  // // setOperationAction(ISD::UREM, MVT::f32, Expand);

  // //FIXME softfloat sqrt function?
  // setLibcallName(RTLIB::SQRT_F32, "float32_sqrt");

  // setLibcallName(RTLIB::OEQ_F32, "_r_eq");
  // // setOperationAction(ISD::SETOEQ, MVT::f32, Expand);

  // setLibcallName(RTLIB::OLE_F32, "_r_le");
  // // setOperationAction(ISD::SETOLE, MVT::f32, Expand);

  // setLibcallName(RTLIB::OGE_F32, "_r_ge");
  // // setOperationAction(ISD::SETOGE, MVT::f32, Expand);  

  // setLibcallName(RTLIB::OLT_F32, "_r_lt");
  // // setOperationAction(ISD::SETOLT, MVT::f32, Expand);

  // setLibcallName(RTLIB::OGT_F32, "_r_gt");
  // // setOperationAction(ISD::SETOGT, MVT::f32, Expand);  

  // setLibcallName(RTLIB::OEQ_F64, "_d_eq");
  // // setOperationAction(ISD::SETOEQ, MVT::f64, Expand);

  // setLibcallName(RTLIB::OLE_F64, "_d_le");
  // // setOperationAction(ISD::SETOLE, MVT::f64, Expand);

  // setLibcallName(RTLIB::OGE_F64, "_d_ge");
  // // setOperationAction(ISD::SETOGE, MVT::f64, Expand);

  // setLibcallName(RTLIB::OLT_F64, "_d_lt");
  // // setOperationAction(ISD::SETOLT, MVT::f64, Expand);  

  // setLibcallName(RTLIB::OGT_F64, "_d_gt");
  // // setOperationAction(ISD::SETOGT, MVT::f64, Expand);    

  // //FIXME: Not sure if following rules are coorect:
  // setLibcallName(RTLIB::FPROUND_F64_F32, "_r_d");
  // // setOperationAction(ISD::FP_ROUND, MVT::f64, Expand); 

  // // setOperationAction(ISD::FP_TO_SINT, MVT::f64, Expand);

  // setLibcallName(RTLIB::FPTOSINT_F64_I32, "float64_to_int32");
  // // setOperationAction(ISD::FP_TO_SINT, MVT::f64, Expand);  

  // setLibcallName(RTLIB::UNE_F32, "_r_ne");

  // setLibcallName(RTLIB::UNE_F64, "_d_ne");



//UO_F32
  
  setOperationAction(ISD::VASTART,            MVT::Other, Custom);
  setOperationAction(ISD::VACOPY,            MVT::Other, Expand);
  setOperationAction(ISD::VAEND,             MVT::Other, Expand);  

  setOperationAction(ISD::CTTZ,  MVT::i32, Expand);
  setOperationAction(ISD::CTPOP,  MVT::i32, Expand);
  setOperationAction(ISD::CTLZ,  MVT::i32, Expand);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF  , MVT::i32  , Expand);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF  , MVT::i32  , Expand);


  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);

//- Set .align 2
// It will emit .align 2 later
  setMinFunctionAlignment(2);

// must, computeRegisterProperties - Once all of the register classes are 
//  added, this allows us to compute derived properties we expose.
  computeRegisterProperties();
}

EVT rvexTargetLowering::getSetCCResultType(LLVMContext &Context, EVT VT) const {
  return MVT::i32;
}

SDValue rvexTargetLowering::PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI)
  const {
  unsigned opc = N->getOpcode();

  switch (opc) {
  default: break;

  }

  return SDValue();
}

SDValue rvexTargetLowering::lowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  rvexFunctionInfo *FuncInfo = MF.getInfo<rvexFunctionInfo>();

  SDLoc DL = SDLoc(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 getPointerTy());

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV), false, false, 0);
}


SDValue rvexTargetLowering::
LowerAddCG(SDValue Op, SelectionDAG &DAG) const
{
  unsigned Opc = Op.getOpcode();
  SDLoc dl = SDLoc(Op.getNode());

  DEBUG(errs() << "LowerADDCG!\n");
  SDValue ADDCG;

  // LHS and RHS contain General purpose registers
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue Carry;
  if (Opc == ISD::ADDC)
  {
    // For ADDC the branch register should be zero (Carry in is zero)
    Carry = DAG.getNode(ISD::SETNE, dl, MVT::i32);
    ADDCG = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), LHS, RHS, Carry );
  }
  else
  {
    // For ADDE the branch register is MVT::Glue in Operand(2) and linked to an ADDC instruction
    Carry = Op.getOperand(2);
    ADDCG = DAG.getNode(rvexISD::Adde, dl, DAG.getVTList(MVT::i32, MVT::i32), LHS, RHS, Carry );
  }

  

  return ADDCG;
}


SDValue rvexTargetLowering::
LowerSUBE(SDValue Op, SelectionDAG &DAG) const
{
  unsigned Opc = Op.getOpcode();
  SDLoc dl = SDLoc(Op.getNode());

  DEBUG(errs() << "LowerSUBC!\n");
  SDValue ADDCG;

  // LHS and RHS contain General purpose registers
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue Carry;
  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);
  SDValue ZeroImm = DAG.getTargetConstant(0, MVT::i32);

  RHS = DAG.getNode(rvexISD::Orc, dl, MVT::i32, RHS, Zero);
  // RHS = DAG.getNode(ISD::SUB, dl, MVT::i32, Zero, RHS);

  if (Opc == ISD::SUBC)
  {
    // For ADDC the branch register should be zero (Carry in is zero)
    Carry = DAG.getSetCC(dl, MVT::i32, Zero, ZeroImm, ISD::SETEQ);
    ADDCG = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), LHS, RHS, Carry );
  }
  else
  {
    // For ADDE the branch register is MVT::Glue in Operand(2) and linked to an ADDC instruction
    Carry = Op.getOperand(2);
    ADDCG = DAG.getNode(rvexISD::Adde, dl, DAG.getVTList(MVT::i32, MVT::i32), LHS, RHS, Carry );
  }

  

  return ADDCG;
}



SDValue rvexTargetLowering::
LowerUDIV(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerUDIV!\n");
  SDLoc dl = SDLoc(Op.getNode());

  SDValue DIVS;

  // LHS and RHS contain General purpose registers
  SDValue Div = Op.getOperand(0);
  SDValue Quot = Op.getOperand(1);
  SDValue ADDCarry, DIVCarry;
  SDValue ADDRes, DIVRes;
  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);
  SDValue ZeroImm = DAG.getTargetConstant(0, MVT::i32);

  ADDCarry = DAG.getSetCC(dl, MVT::i32, Zero, ZeroImm, ISD::SETNE);
  DIVCarry = DAG.getSetCC(dl, MVT::i32, Zero, ZeroImm, ISD::SETNE);

  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), Div, Div, ADDCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);
  
  DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), Zero, Quot, ADDCarry ); 
  DIVRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);

  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, DIVCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  DIVCarry = SDValue(DIVS.getNode(), 1);


  int i = 0;

  while(i++ < 16)
  {


    DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, ADDCarry );
    ADDRes = SDValue(DIVS.getNode(), 0);
    ADDCarry = SDValue(DIVS.getNode(), 1);

    DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, Quot, DIVCarry ); 
    DIVRes = SDValue(DIVS.getNode(), 0);
    DIVCarry = SDValue(DIVS.getNode(), 1);     

 

    DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, DIVCarry );
    ADDRes = SDValue(DIVS.getNode(), 0);
    DIVCarry = SDValue(DIVS.getNode(), 1);

    DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, Quot, ADDCarry ); 
    DIVRes = SDValue(DIVS.getNode(), 0);
    ADDCarry = SDValue(DIVS.getNode(), 1);    

  }

  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, ADDCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);

  DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, Quot, DIVCarry ); 
  DIVRes = SDValue(DIVS.getNode(), 0);
  DIVCarry = SDValue(DIVS.getNode(), 1);  

  DIVS = DAG.getNode(rvexISD::Orc, dl, MVT::i32, ADDRes, Zero);



    
  
  return DIVS;

}  

SDValue rvexTargetLowering::
LowerSDIV(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerSDIV!\n");
  SDLoc dl = SDLoc(Op.getNode());

  SDValue DIVS, DIVSNeg;

  // SDValue Div = Op.getOperand(0);
  // SDValue Quot = Op.getOperand(1);

  // DIVS = DAG.getSetCC(dl, MVT::i32, Div, Quot, ISD::SETLT);

  // DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), Div, Quot, DIVS );  

  
  // LHS and RHS contain General purpose registers
  SDValue Div = Op.getOperand(0);
  SDValue Quot = Op.getOperand(1);
  SDValue ADDCarry, DIVCarry;
  SDValue ADDRes, DIVRes, QUOTRes;
  SDValue PosRes;
  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);
  SDValue ZeroImm = DAG.getTargetConstant(0, MVT::i32);

  ADDCarry = DAG.getSetCC(dl, MVT::i32, Zero, ZeroImm, ISD::SETNE);
  DIVCarry = DAG.getSetCC(dl, MVT::i32, Zero, ZeroImm, ISD::SETNE);

  PosRes = DAG.getSetCC(dl, MVT::i32, Zero, Div, ISD::SETLT);


  // Determine if Div is negative or positive
  DIVS = DAG.getNode(ISD::SUB, dl, MVT::i32, Zero, Div); 
  DIVRes = SDValue(DIVS.getNode(), 0);

  // Use positive Div
  DIVS = DAG.getNode(rvexISD::Max, dl, MVT::i32, Div, DIVRes);
  DIVRes = SDValue(DIVS.getNode(), 0);

  // Determine if Quot is negative or positive
  DIVS = DAG.getNode(ISD::SUB, dl, MVT::i32, Zero, Quot); 
  QUOTRes = SDValue(DIVS.getNode(), 0);

  // Use positive Quot
  DIVS = DAG.getNode(rvexISD::Max, dl, MVT::i32, Quot, QUOTRes);
  QUOTRes = SDValue(DIVS.getNode(), 0);

  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, DIVRes, ADDCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);

  // First iteration of Div function which uses the Zero register
  DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), Zero, QUOTRes, ADDCarry ); 
  DIVRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);

  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, DIVCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  DIVCarry = SDValue(DIVS.getNode(), 1);


  int i = 0;
  // Iterate Div instructions
  while(i++ < 16)
  {
    DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, ADDCarry );
    ADDRes = SDValue(DIVS.getNode(), 0);
    ADDCarry = SDValue(DIVS.getNode(), 1);

    DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, QUOTRes, DIVCarry ); 
    DIVRes = SDValue(DIVS.getNode(), 0);
    DIVCarry = SDValue(DIVS.getNode(), 1);     

    DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, DIVCarry );
    ADDRes = SDValue(DIVS.getNode(), 0);
    DIVCarry = SDValue(DIVS.getNode(), 1);

    DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, QUOTRes, ADDCarry ); 
    DIVRes = SDValue(DIVS.getNode(), 0);
    ADDCarry = SDValue(DIVS.getNode(), 1);    
  }

  // Seperate last iteration
  DIVS = DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), ADDRes, ADDRes, ADDCarry );
  ADDRes = SDValue(DIVS.getNode(), 0);
  ADDCarry = SDValue(DIVS.getNode(), 1);

  DIVS = DAG.getNode(rvexISD::Divs, dl, DAG.getVTList(MVT::i32, MVT::i32), DIVRes, QUOTRes, DIVCarry ); 
  DIVRes = SDValue(DIVS.getNode(), 0);
  DIVCarry = SDValue(DIVS.getNode(), 1);  

  // Complement final result
  DIVS = DAG.getNode(rvexISD::Orc, dl, MVT::i32, ADDRes, Zero);

  // SDIV always produces negative results so invert result
  DIVRes = SDValue(DIVS.getNode(), 0);
  DIVSNeg = DAG.getNode(ISD::SUB, dl, MVT::i32, Zero, DIVRes);

  // FIXME Possible optimization:
  // Complement followed by 0-result is equal to adding 1 to first result
  // Saves 1 instruction
  // Replace ORC and SUB by ADD res + 1;

  DIVS = DAG.getNode(rvexISD::Slct, dl, MVT::i32, PosRes, DIVS, DIVSNeg);
  
  return DIVS;

}

SDValue rvexTargetLowering::
LowerMULHS(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerMULHS!\n");
  SDLoc dl = SDLoc(Op.getNode());

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue LHSinv, RHSinv;

  SDValue ShiftImm = DAG.getTargetConstant(16, MVT::i32);
  SDValue MaskImm = DAG.getConstant(0xffff, MVT::i32);
  SDValue Neg1 = DAG.getConstant(0xffffffff, MVT::i32);

  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);

  SDValue tres, t, tneg, w3, k, w2, w1, PosL, PosR;

  LHSinv = DAG.getNode(rvexISD::Mpyl, dl, MVT::i32, LHS, Neg1);
  RHSinv = DAG.getNode(rvexISD::Mpyl, dl, MVT::i32, RHS, Neg1);

  PosL = DAG.getSetCC(dl, MVT::i32, Zero, LHS, ISD::SETLT);
  PosR = DAG.getSetCC(dl, MVT::i32, Zero, RHS, ISD::SETLT);  

  LHS = DAG.getNode(rvexISD::Slct, dl, MVT::i32, PosL, LHS, LHSinv);
  RHS = DAG.getNode(rvexISD::Slct, dl, MVT::i32, PosR, RHS, RHSinv);    

  t = DAG.getNode(rvexISD::Mpyllu, dl, MVT::i32, LHS, RHS);
  w3 = DAG.getNode(ISD::AND, dl, MVT::i32, t, MaskImm);
  k = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpylhu, dl, MVT::i32, LHS, RHS);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, k);
  w2 = DAG.getNode(ISD::AND, dl, MVT::i32, t, MaskImm);
  w1 = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpylhu, dl, MVT::i32, RHS, LHS); 
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, w2); 
  k = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpyhhu, dl, MVT::i32, LHS, RHS);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, w1);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, k);

  tneg = DAG.getNode(rvexISD::Orc, dl, MVT::i32, t, Zero);

  tres = DAG.getNode(rvexISD::Slct, dl, MVT::i32, PosL, t, tneg);
  tneg = DAG.getNode(rvexISD::Orc, dl, MVT::i32, tres, Zero);
  tres = DAG.getNode(rvexISD::Slct, dl, MVT::i32, PosR, tres, tneg);

  return tres;
}  

SDValue rvexTargetLowering::
LowerMULHU(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerMULHU!\n");
  SDLoc dl = SDLoc(Op.getNode());

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue ShiftImm = DAG.getTargetConstant(16, MVT::i32);
  SDValue MaskImm = DAG.getConstant(0xffff, MVT::i32);

  SDValue t, w3, k, w2, w1;

  t = DAG.getNode(rvexISD::Mpyllu, dl, MVT::i32, LHS, RHS);
  w3 = DAG.getNode(ISD::AND, dl, MVT::i32, t, MaskImm);
  k = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpylhu, dl, MVT::i32, LHS, RHS);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, k);
  w2 = DAG.getNode(ISD::AND, dl, MVT::i32, t, MaskImm);
  w1 = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpylhu, dl, MVT::i32, RHS, LHS); 
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, w2); 
  k = DAG.getNode(ISD::SRL, dl, MVT::i32, t, ShiftImm);

  t = DAG.getNode(rvexISD::Mpyhhu, dl, MVT::i32, LHS, RHS);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, w1);
  t = DAG.getNode(ISD::ADD, dl, MVT::i32, t, k); 

  return t;
}  

SDValue rvexTargetLowering::
LowerConstant(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerConstant!\n");
  SDLoc dl = SDLoc(Op.getNode());
  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);
  SDValue ZeroImm = DAG.getTargetConstant(0, MVT::i32);

  return DAG.getSetCC(dl, MVT::i1, Zero, ZeroImm, ISD::SETEQ);

}

SDValue rvexTargetLowering::
LowerZeroExtend(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerZeroExtend!\n");
  SDLoc dl = SDLoc(Op.getNode());

  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);

  return DAG.getNode(rvexISD::Addc, dl, DAG.getVTList(MVT::i32, MVT::i32), Zero, Zero, Op.getOperand(0) );
}

SDValue rvexTargetLowering::
LowerOperation(SDValue Op, SelectionDAG &DAG) const
{
  
  switch (Op.getOpcode())
  {
    default: llvm_unreachable("Don't know how to custom lower this!");
    case ISD::BRCOND:             return LowerBRCOND(Op, DAG);
    case ISD::GlobalAddress:      return LowerGlobalAddress(Op, DAG);
    case ISD::ADDC:               return LowerAddCG(Op, DAG);
    case ISD::ADDE:               return LowerAddCG(Op, DAG);
    case ISD::SUBE:               return LowerSUBE(Op, DAG);
    case ISD::SUBC:               return LowerSUBE(Op, DAG);

    case ISD::UDIV:               return LowerUDIV(Op, DAG);
    case ISD::SDIV:               return LowerSDIV(Op, DAG);

    case ISD::MULHS:              return LowerMULHS(Op, DAG);
    case ISD::MULHU:              return LowerMULHU(Op, DAG);

    case ISD::VASTART:            return lowerVASTART(Op, DAG);

    case ISD::SIGN_EXTEND:
    case ISD::ANY_EXTEND:
    case ISD::ZERO_EXTEND:        return LowerZeroExtend(Op,DAG);

    case ISD::Constant:           return LowerConstant(Op, DAG);
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Lower helper functions
//===----------------------------------------------------------------------===//
// AddLiveIn - This helper function adds the specified physical register to the
// MachineFunction as a live in value.  It also creates a corresponding
// virtual register for it.
static unsigned
AddLiveIn(MachineFunction &MF, unsigned PReg, const TargetRegisterClass *RC)
{
  assert(RC->contains(PReg) && "Not the correct regclass!");
  unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
  MF.getRegInfo().addLiveIn(PReg, VReg);
  return VReg;
}
//===----------------------------------------------------------------------===//
//  Misc Lower Operation implementation
//===----------------------------------------------------------------------===//

SDValue rvexTargetLowering::
LowerBRCOND(SDValue Op, SelectionDAG &DAG) const
{
  return Op;
}

SDValue rvexTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  // FIXME there isn't actually debug info here
  SDLoc dl = SDLoc(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  SDVTList VTs = DAG.getVTList(MVT::i32);

  //rvexTargetObjectFile &TLOF = (rvexTargetObjectFile&)getObjFileLowering();


  SDValue GA = DAG.getTargetGlobalAddress(GV, dl, MVT::i32, 0,
                                          rvexII::MO_NO_FLAG);
  SDValue GPRelNode = DAG.getNode(rvexISD::GPRel, dl, VTs, GA);
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(MVT::i32);
  return DAG.getNode(ISD::ADD, dl, MVT::i32, GOT, GPRelNode);


  


}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// TODO: Implement a generic logic using tblgen that can support this.
// rvex O32 ABI rules:
// ---
// i32 - Passed in A0, A1, A2, A3 and stack
// f32 - Only passed in f32 registers if no int reg has been used yet to hold
//       an argument. Otherwise, passed in A1, A2, A3 and stack.
// f64 - Only passed in two aliased f32 registers if no int reg has been used
//       yet to hold an argument. Otherwise, use A2, A3 and stack. If A1 is
//       not used, it must be shadowed. If only A3 is avaiable, shadow it and
//       go to stack.
//
//  For vararg functions, all arguments are passed in A0, A1, A2, A3 and stack.
//===----------------------------------------------------------------------===//

static bool CC_rvexO32(unsigned ValNo, MVT ValVT,
                       MVT LocVT, CCValAssign::LocInfo LocInfo,
                       ISD::ArgFlagsTy ArgFlags, CCState &State) {

  static const unsigned IntRegsSize=8;

  static const uint16_t IntRegs[] = {
      rvex::R3, rvex::R4, rvex::R5, rvex::R6,
      rvex::R7, rvex::R8, rvex::R9, rvex::R10
  };


  // Do not process byval args here.
  if (ArgFlags.isByVal())
    return true;

  // Promote i8 and i16
  if (LocVT == MVT::i8 || LocVT == MVT::i16) {
    LocVT = MVT::i32;
    if (ArgFlags.isSExt())
      LocInfo = CCValAssign::SExt;
    else if (ArgFlags.isZExt())
      LocInfo = CCValAssign::ZExt;
    else
      LocInfo = CCValAssign::AExt;
  }

  unsigned Reg;


  if (ValVT == MVT::i32) {
    Reg = State.AllocateReg(IntRegs, IntRegsSize);

    LocVT = MVT::i32;
  } else
    llvm_unreachable("Cannot handle this ValVT.");

  if (!Reg) {
    unsigned Offset = State.AllocateStack(ValVT.getSizeInBits() >> 3,
                                          4);
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  } else
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));

  return false;
}

#include "rvexGenCallingConv.inc"

//===----------------------------------------------------------------------===//
//                  Call Calling Convention Implementation
//===----------------------------------------------------------------------===//




SDValue
rvexTargetLowering::passArgOnStack(SDValue StackPtr, unsigned Offset,
                                   SDValue Chain, SDValue Arg, SDLoc DL,
                                   bool IsTailCall, SelectionDAG &DAG) const {
  if (!IsTailCall) {
    SDValue PtrOff = DAG.getNode(ISD::ADD, DL, getPointerTy(), StackPtr,
                                 DAG.getIntPtrConstant(Offset));
    return DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo(), false,
                        false, 0);
  }

  MachineFrameInfo *MFI = DAG.getMachineFunction().getFrameInfo();
  int FI = MFI->CreateFixedObject(Arg.getValueSizeInBits() / 8, Offset, false);
  SDValue FIN = DAG.getFrameIndex(FI, getPointerTy());
  return DAG.getStore(Chain, DL, Arg, FIN, MachinePointerInfo(),
                      /*isVolatile=*/ true, false, 0);
}

void rvexTargetLowering::
getOpndList(SmallVectorImpl<SDValue> &Ops,
            std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
            bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
            CallLoweringInfo &CLI, SDValue Callee, SDValue Chain) const {
  // Insert node "GP copy globalreg" before call to function.
  //


  // Build a sequence of copy-to-reg nodes chained together with token
  // chain and flag operands which copy the outgoing args into registers.
  // The InFlag in necessary since all emitted instructions must be
  // stuck together.
  SDValue InFlag;
  Ops.push_back(Callee);

  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = CLI.DAG.getCopyToReg(Chain, CLI.DL, RegsToPass[i].first,
                                 RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(CLI.DAG.getRegister(RegsToPass[i].first,
                                      RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = CLI.DAG.getMachineFunction().getSubtarget().getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(CLI.CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(CLI.DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);
}

/// LowerCall - functions arguments are copied from virtual regs to
/// (physical regs)/(stack frame), CALLSEQ_START and CALLSEQ_END are emitted.
SDValue
rvexTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                              SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &DL                             = CLI.DL;
  SmallVector<ISD::OutputArg, 32> &Outs = CLI.Outs;
  SmallVector<SDValue, 32> &OutVals     = CLI.OutVals;
  SmallVector<ISD::InputArg, 32> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &IsTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool IsVarArg                         = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();
  bool IsPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(),
                 ArgLocs, *DAG.getContext());
  rvexCC rvexCCInfo(CallConv, true, CCInfo);

  rvexCCInfo.analyzeCallOperands(Outs, IsVarArg,
                                 getTargetMachine().Options.UseSoftFloat,
                                 Callee.getNode(), CLI.Args);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NextStackOffset = CCInfo.getNextStackOffset();

  // Check if it's really possible to do a tail call.
 if (IsTailCall)
   IsTailCall =
     isEligibleForTailCallOptimization(rvexCCInfo, NextStackOffset);


  // Chain is the output chain of the last Load/Store or CopyToReg node.
  // ByValChain is the output chain of the last Memcpy node created for copying
  // byval arguments to the stack.
  unsigned StackAlignment = TFL->getStackAlignment();
  NextStackOffset = RoundUpToAlignment(NextStackOffset, StackAlignment);
  SDValue NextStackOffsetVal = DAG.getIntPtrConstant(NextStackOffset, true);

  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NextStackOffsetVal, DL);

  SDValue StackPtr = DAG.getCopyFromReg(Chain, DL, rvex::R1,
                                        getPointerTy());

  // With EABI is it possible to have 16 args on registers.
  std::deque< std::pair<unsigned, SDValue> > RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  rvexCC::byval_iterator ByValArg = rvexCCInfo.byval_begin();

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    SDValue Arg = OutVals[i];
    CCValAssign &VA = ArgLocs[i];
      
      MVT LocVT = VA.getLocVT();
      ISD::ArgFlagsTy Flags = Outs[i].Flags;

    // ByVal Arg.
    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      assert(ByValArg != rvexCCInfo.byval_end());
      assert(!IsTailCall &&
             "Do not tail-call optimize if there is a byval argument.");
      passByValArg(Chain, DL, RegsToPass, MemOpChains, StackPtr, MFI, DAG, Arg,
                   rvexCCInfo, *ByValArg, Flags, Subtarget->isLittle());
      ++ByValArg;
      continue;
    }

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
//      if (VA.isRegLoc()) {
//        if ((ValVT == MVT::f32 && LocVT == MVT::i32) ||
//            (ValVT == MVT::f64 && LocVT == MVT::i64) ||
//            (ValVT == MVT::i64 && LocVT == MVT::f64))
//          Arg = DAG.getNode(ISD::BITCAST, DL, LocVT, Arg);
//        else if (ValVT == MVT::f64 && LocVT == MVT::i32) {
//          SDValue Lo = DAG.getNode(rvexISD::ExtractElementF64, DL, MVT::i32,
//                                   Arg, DAG.getConstant(0, MVT::i32));
//          SDValue Hi = DAG.getNode(rvexISD::ExtractElementF64, DL, MVT::i32,
//                                   Arg, DAG.getConstant(1, MVT::i32));
//          if (!Subtarget->isLittle())
//            std::swap(Lo, Hi);
//          unsigned LocRegLo = VA.getLocReg();
//          unsigned LocRegHigh = getNextIntArgReg(LocRegLo);
//          RegsToPass.push_back(std::make_pair(LocRegLo, Lo));
//          RegsToPass.push_back(std::make_pair(LocRegHigh, Hi));
//          continue;
//        }
//      }
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, LocVT, Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, LocVT, Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, LocVT, Arg);
      break;
    }

    // Arguments that can be passed on register must be kept at
    // RegsToPass vector
    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    // Register can't get to this point...
    assert(VA.isMemLoc());

    // emit ISD::STORE whichs stores the
    // parameter value to a stack Location
    MemOpChains.push_back(passArgOnStack(StackPtr, VA.getLocMemOffset(),
                                         Chain, Arg, DL, IsTailCall, DAG));
  }

  // Transform all store nodes into one single node because all store
  // nodes are independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                        MemOpChains);

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  bool IsPICCall = (IsPIC); // true if calls are translated to jalr $25
  bool GlobalOrExternal = false, InternalLinkage = false;
  SDValue CalleeLo;

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    if (IsPICCall) {
      InternalLinkage = G->getGlobal()->hasInternalLinkage();

      if (InternalLinkage)
        Callee = getAddrLocal(Callee, DAG, false);

      else
        Callee = getAddrGlobal(Callee, DAG, rvexII::MO_GOT_CALL);
    } else
      Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, getPointerTy(), 0,
                                          rvexII::MO_NO_FLAG);
    GlobalOrExternal = true;
  }
  else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    if (!false && !IsPIC) // !N64 && static
      Callee = DAG.getTargetExternalSymbol(S->getSymbol(), getPointerTy(),
                                            rvexII::MO_NO_FLAG);

    else // N64 || PIC
      Callee = getAddrGlobal(Callee, DAG, rvexII::MO_GOT_CALL);

    GlobalOrExternal = true;
  }

  SmallVector<SDValue, 8> Ops(1, Chain);
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue InFlag;

  // Handle func pointers where LR needs to be loaded with destination before JALR
  if (!GlobalOrExternal)
  {  
    Chain = DAG.getCopyToReg(Chain, DL, rvex::LR, Callee, SDValue(0, 0));
    InFlag = Chain.getValue(1);
    Callee = DAG.getRegister(rvex::LR, getPointerTy());
  }

  getOpndList(Ops, RegsToPass, IsPICCall, GlobalOrExternal, InternalLinkage,
              CLI, Callee, Chain);

  if (IsTailCall)
    return DAG.getNode(rvexISD::JmpLink, DL, MVT::Other, Ops);

  Chain  = DAG.getNode(rvexISD::JmpLink, DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NextStackOffsetVal,
                             DAG.getIntPtrConstant(0, true), InFlag, DL);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, IsVarArg,
                         Ins, DL, DAG, InVals, CLI.Callee.getNode(), CLI.RetTy);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue
rvexTargetLowering::LowerCallResult(SDValue Chain, SDValue InFlag,
                                    CallingConv::ID CallConv, bool IsVarArg,
                                    const SmallVectorImpl<ISD::InputArg> &Ins,
                                    SDLoc DL, SelectionDAG &DAG,
                                    SmallVectorImpl<SDValue> &InVals,
                                    const SDNode *CallNode,
                                    const Type *RetTy) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(),
                 RVLocs, *DAG.getContext());
  rvexCC rvexCCInfo(CallConv, true, CCInfo);

  rvexCCInfo.analyzeCallResult(Ins, getTargetMachine().Options.UseSoftFloat,
                               CallNode, RetTy);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    SDValue Val = DAG.getCopyFromReg(Chain, DL, RVLocs[i].getLocReg(),
                                     RVLocs[i].getLocVT(), InFlag);
    Chain = Val.getValue(1);
    InFlag = Val.getValue(2);

    if (RVLocs[i].getValVT() != RVLocs[i].getLocVT())
      Val = DAG.getNode(ISD::BITCAST, DL, RVLocs[i].getValVT(), Val);

    InVals.push_back(Val);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//             Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//
/// LowerFormalArguments - transform physical registers into virtual registers
/// and generate load operations for arguments places on the stack.
SDValue
rvexTargetLowering::LowerFormalArguments(SDValue Chain,
                                         CallingConv::ID CallConv,
                                         bool IsVarArg,
                                      const SmallVectorImpl<ISD::InputArg> &Ins,
                                         SDLoc DL, SelectionDAG &DAG,
                                         SmallVectorImpl<SDValue> &InVals)
                                          const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  rvexFunctionInfo *rvexFI = MF.getInfo<rvexFunctionInfo>();

  rvexFI->setVarArgsFrameIndex(0);

  // Used with vargs to acumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(),
                 ArgLocs, *DAG.getContext());
  rvexCC rvexCCInfo(CallConv, true, CCInfo);
  Function::const_arg_iterator FuncArg =
    DAG.getMachineFunction().getFunction()->arg_begin();
  bool UseSoftFloat = getTargetMachine().Options.UseSoftFloat;

  rvexCCInfo.analyzeFormalArguments(Ins, UseSoftFloat, FuncArg);
  rvexFI->setFormalArgInfo(CCInfo.getNextStackOffset(),
                           rvexCCInfo.hasByValArg());

  unsigned CurArgIdx = 0;
  rvexCC::byval_iterator ByValArg = rvexCCInfo.byval_begin();

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    std::advance(FuncArg, Ins[i].OrigArgIndex - CurArgIdx);
    CurArgIdx = Ins[i].OrigArgIndex;
    EVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    bool IsRegLoc = VA.isRegLoc();

    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      assert(ByValArg != rvexCCInfo.byval_end());
      copyByValRegs(Chain, DL, OutChains, DAG, Flags, InVals, &*FuncArg,
                    rvexCCInfo, *ByValArg);
      ++ByValArg;
      continue;
    }

    // Arguments stored on registers
    if (IsRegLoc) {
      EVT RegVT = VA.getLocVT();
      unsigned ArgReg = VA.getLocReg();
      const TargetRegisterClass *RC;

      if (RegVT == MVT::i32)
        RC = &rvex::CPURegsRegClass;
      else
        llvm_unreachable("RegVT not supported by FormalArguments Lowering");

      // Transform the arguments stored on
      // physical registers into virtual ones
      unsigned Reg = AddLiveIn(DAG.getMachineFunction(), ArgReg, RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegVT);

      // If this is an 8 or 16-bit value, it has been passed promoted
      // to 32 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      if (VA.getLocInfo() != CCValAssign::Full) {
        unsigned Opcode = 0;
        if (VA.getLocInfo() == CCValAssign::SExt)
          Opcode = ISD::AssertSext;
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          Opcode = ISD::AssertZext;
        if (Opcode)
          ArgValue = DAG.getNode(Opcode, DL, RegVT, ArgValue,
                                 DAG.getValueType(ValVT));
        ArgValue = DAG.getNode(ISD::TRUNCATE, DL, ValVT, ArgValue);
      }

      // // Handle floating point arguments passed in integer registers and
      // // long double arguments passed in floating point registers.
      // if ((RegVT == MVT::i32 && ValVT == MVT::f32) ||
      //     (RegVT == MVT::i64 && ValVT == MVT::f64) ||
      //     (RegVT == MVT::f64 && ValVT == MVT::i64))
      //   ArgValue = DAG.getNode(ISD::BITCAST, DL, ValVT, ArgValue);
      // else if (RegVT == MVT::i32 && ValVT == MVT::f64) {
      //   unsigned Reg2 = addLiveIn(DAG.getMachineFunction(),
      //                             getNextIntArgReg(ArgReg), RC);
      //   SDValue ArgValue2 = DAG.getCopyFromReg(Chain, DL, Reg2, RegVT);
      //   if (!Subtarget->isLittle())
      //     std::swap(ArgValue, ArgValue2);
      //   ArgValue = DAG.getNode(rvexISD::BuildPairF64, DL, MVT::f64,
      //                          ArgValue, ArgValue2);
      // }

      InVals.push_back(ArgValue);
    } else { // VA.isRegLoc()

      // sanity check
      assert(VA.isMemLoc());

      // The stack pointer offset is relative to the caller stack frame.
      int FI = MFI->CreateFixedObject(ValVT.getSizeInBits()/8,
                                      VA.getLocMemOffset(), true);

      // Create load nodes to retrieve arguments from the stack
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy());
      InVals.push_back(DAG.getLoad(ValVT, DL, Chain, FIN,
                                   MachinePointerInfo::getFixedStack(FI),
                                   false, false, false, 0));
    }
  }

  // The rvex ABIs for returning structs by value requires that we copy
  // the sret argument into $v0 for the return. Save the argument into
  // a virtual register so that we can access it from the return points.
  if (DAG.getMachineFunction().getFunction()->hasStructRetAttr()) {
    unsigned Reg = rvexFI->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().
        createVirtualRegister(getRegClassFor( MVT::i32));
      rvexFI->setSRetReturnReg(Reg);
    }
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), DL, Reg, InVals[0]);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Copy, Chain);
  }

  if (IsVarArg)
    writeVarArgRegs(OutChains, rvexCCInfo, Chain, DL, DAG);

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens when on varg functions
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                        OutChains);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//               Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

bool
rvexTargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                   MachineFunction &MF, bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF,
                 RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_rvex);
}

SDValue
rvexTargetLowering::LowerReturn(SDValue Chain,
                                CallingConv::ID CallConv, bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                SDLoc DL, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of
  // the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;
  MachineFunction &MF = DAG.getMachineFunction();

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs,
                 *DAG.getContext());
  rvexCC rvexCCInfo(CallConv, true, CCInfo);

  // Analyze return values.
  rvexCCInfo.analyzeReturn(Outs, getTargetMachine().Options.UseSoftFloat,
                           MF.getFunction()->getReturnType());

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  RetOps.push_back(DAG.getRegister(rvex::R1, MVT::i32));
  RetOps.push_back(DAG.getConstant(0, MVT::i32));
  RetOps.push_back(DAG.getRegister(rvex::LR, MVT::i32));
  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    SDValue Val = OutVals[i];
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    if (RVLocs[i].getValVT() != RVLocs[i].getLocVT())
      Val = DAG.getNode(ISD::BITCAST, DL, RVLocs[i].getLocVT(), Val);

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Flag);

    // Guarantee that all emitted copies are stuck together with flags.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // The rvex ABIs for returning structs by value requires that we copy
  // the sret argument into $v0 for the return. We saved the argument into
  // a virtual register in the entry block, so now we copy the value out
  // and into $v0.
  if (MF.getFunction()->hasStructRetAttr()) {
    rvexFunctionInfo *rvexFI = MF.getInfo<rvexFunctionInfo>();
    unsigned Reg = rvexFI->getSRetReturnReg();

    if (!Reg)
      llvm_unreachable("sret virtual register not created in the entry block");
    SDValue Val = DAG.getCopyFromReg(Chain, DL, Reg, getPointerTy());
    unsigned V0 = rvex::R2;

    Chain = DAG.getCopyToReg(Chain, DL, V0, Val, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(V0, getPointerTy()));
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  // Return on rvex is always a "jr $ra"
  return DAG.getNode(rvexISD::Ret, DL, MVT::Other, RetOps);
}


rvexTargetLowering::rvexCC::rvexCC(CallingConv::ID CC, bool IsO32_,
                                   CCState &Info)
  : CCInfo(Info), CallConv(CC), IsO32(IsO32_) {
  // Pre-allocate reserved argument area.
  CCInfo.AllocateStack(reservedArgArea(), 1);
}

void rvexTargetLowering::rvexCC::
analyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Args,
                    bool IsVarArg, bool IsSoftFloat, const SDNode *CallNode,
                    std::vector<ArgListEntry> &FuncArgs) {
  assert((CallConv != CallingConv::Fast || !IsVarArg) &&
         "CallingConv::Fast shouldn't be used for vararg functions.");

  unsigned NumOpnds = Args.size();
  llvm::CCAssignFn *FixedFn = fixedArgFn(), *VarFn = varArgFn();

  for (unsigned I = 0; I != NumOpnds; ++I) {
    MVT ArgVT = Args[I].VT;
    ISD::ArgFlagsTy ArgFlags = Args[I].Flags;
    bool R;

    if (ArgFlags.isByVal()) {
      handleByValArg(I, ArgVT, ArgVT, CCValAssign::Full, ArgFlags);
      continue;
    }

    if (IsVarArg && !Args[I].IsFixed)
      R = VarFn(I, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo);
    else {
      MVT RegVT = getRegVT(ArgVT, FuncArgs[Args[I].OrigArgIndex].Ty, CallNode,
                           IsSoftFloat);
      R = FixedFn(I, ArgVT, RegVT, CCValAssign::Full, ArgFlags, CCInfo);
    }

    if (R) {
#ifndef NDEBUG
      dbgs() << "Call operand #" << I << " has unhandled type "
             << EVT(ArgVT).getEVTString();
#endif
      llvm_unreachable(0);
    }
  }
}

void rvexTargetLowering::rvexCC::
analyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Args,
                       bool IsSoftFloat, Function::const_arg_iterator FuncArg) {
  unsigned NumArgs = Args.size();
  llvm::CCAssignFn *FixedFn = fixedArgFn();
  unsigned CurArgIdx = 0;

  for (unsigned I = 0; I != NumArgs; ++I) {
    MVT ArgVT = Args[I].VT;
    ISD::ArgFlagsTy ArgFlags = Args[I].Flags;
    std::advance(FuncArg, Args[I].OrigArgIndex - CurArgIdx);
    CurArgIdx = Args[I].OrigArgIndex;

    if (ArgFlags.isByVal()) {
      handleByValArg(I, ArgVT, ArgVT, CCValAssign::Full, ArgFlags);
      continue;
    }

    MVT RegVT = getRegVT(ArgVT, FuncArg->getType(), 0, IsSoftFloat);

    if (!FixedFn(I, ArgVT, RegVT, CCValAssign::Full, ArgFlags, CCInfo))
      continue;

#ifndef NDEBUG
    dbgs() << "Formal Arg #" << I << " has unhandled type "
           << EVT(ArgVT).getEVTString();
#endif
    llvm_unreachable(0);
  }
}

template<typename Ty>
void rvexTargetLowering::rvexCC::
analyzeReturn(const SmallVectorImpl<Ty> &RetVals, bool IsSoftFloat,
              const SDNode *CallNode, const Type *RetTy) const {
  CCAssignFn *Fn;


    Fn = RetCC_rvex;

  for (unsigned I = 0, E = RetVals.size(); I < E; ++I) {
    MVT VT = RetVals[I].VT;
    ISD::ArgFlagsTy Flags = RetVals[I].Flags;
    MVT RegVT = this->getRegVT(VT, RetTy, CallNode, IsSoftFloat);

    if (Fn(I, VT, RegVT, CCValAssign::Full, Flags, this->CCInfo)) {
#ifndef NDEBUG
      dbgs() << "Call result #" << I << " has unhandled type "
             << EVT(VT).getEVTString() << '\n';
#endif
      llvm_unreachable(0);
    }
  }
}

void rvexTargetLowering::rvexCC::
analyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins, bool IsSoftFloat,
                  const SDNode *CallNode, const Type *RetTy) const {
  analyzeReturn(Ins, IsSoftFloat, CallNode, RetTy);
}

void rvexTargetLowering::rvexCC::
analyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs, bool IsSoftFloat,
              const Type *RetTy) const {
  analyzeReturn(Outs, IsSoftFloat, 0, RetTy);
}

void
rvexTargetLowering::rvexCC::handleByValArg(unsigned ValNo, MVT ValVT,
                                           MVT LocVT,
                                           CCValAssign::LocInfo LocInfo,
                                           ISD::ArgFlagsTy ArgFlags) {
  assert(ArgFlags.getByValSize() && "Byval argument's size shouldn't be 0.");

  struct ByValArgInfo ByVal;
  unsigned RegSize = regSize();
  unsigned ByValSize = RoundUpToAlignment(ArgFlags.getByValSize(), RegSize);
  unsigned Align = std::min(std::max(ArgFlags.getByValAlign(), RegSize),
                            RegSize * 2);

  if (useRegsForByval())
    allocateRegs(ByVal, ByValSize, Align);

  // Allocate space on caller's stack.
  ByVal.Address = CCInfo.AllocateStack(ByValSize - RegSize * ByVal.NumRegs,
                                       Align);
  CCInfo.addLoc(CCValAssign::getMem(ValNo, ValVT, ByVal.Address, LocVT,
                                    LocInfo));
  ByValArgs.push_back(ByVal);
}

unsigned rvexTargetLowering::rvexCC::numIntArgRegs() const {
  return array_lengthof(IntRegs);
}

unsigned rvexTargetLowering::rvexCC::reservedArgArea() const {
  return (IsO32 && (CallConv != CallingConv::Fast)) ? 16 : 0;
}

const uint16_t *rvexTargetLowering::rvexCC::intArgRegs() const {
  return IntRegs;
}

llvm::CCAssignFn *rvexTargetLowering::rvexCC::fixedArgFn() const {


  return CC_rvexO32;
}

llvm::CCAssignFn *rvexTargetLowering::rvexCC::varArgFn() const {
  return CC_rvexO32;
}

const uint16_t *rvexTargetLowering::rvexCC::shadowRegs() const {
  return IntRegs;
}

void rvexTargetLowering::rvexCC::allocateRegs(ByValArgInfo &ByVal,
                                              unsigned ByValSize,
                                              unsigned Align) {
  unsigned RegSize = regSize(), NumIntArgRegs = numIntArgRegs();
  const uint16_t *IntArgRegs = intArgRegs(), *ShadowRegs = shadowRegs();
  assert(!(ByValSize % RegSize) && !(Align % RegSize) &&
         "Byval argument's size and alignment should be a multiple of"
         "RegSize.");

  ByVal.FirstIdx = CCInfo.getFirstUnallocated(IntArgRegs, NumIntArgRegs);

  // If Align > RegSize, the first arg register must be even.
  if ((Align > RegSize) && (ByVal.FirstIdx % 2)) {
    CCInfo.AllocateReg(IntArgRegs[ByVal.FirstIdx], ShadowRegs[ByVal.FirstIdx]);
    ++ByVal.FirstIdx;
  }

  // Mark the registers allocated.
  for (unsigned I = ByVal.FirstIdx; ByValSize && (I < NumIntArgRegs);
       ByValSize -= RegSize, ++I, ++ByVal.NumRegs)
    CCInfo.AllocateReg(IntArgRegs[I], ShadowRegs[I]);
}

MVT rvexTargetLowering::rvexCC::getRegVT(MVT VT, const Type *OrigTy,
                                         const SDNode *CallNode,
                                         bool IsSoftFloat) const {
  if (IsSoftFloat || IsO32)
    return VT;



  return VT;
}

void rvexTargetLowering::
copyByValRegs(SDValue Chain, SDLoc DL, std::vector<SDValue> &OutChains,
              SelectionDAG &DAG, const ISD::ArgFlagsTy &Flags,
              SmallVectorImpl<SDValue> &InVals, const Argument *FuncArg,
              const rvexCC &CC, const ByValArgInfo &ByVal) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  unsigned RegAreaSize = ByVal.NumRegs * CC.regSize();
  unsigned FrameObjSize = std::max(Flags.getByValSize(), RegAreaSize);
  int FrameObjOffset;

  if (RegAreaSize)
    FrameObjOffset = (int)CC.reservedArgArea() -
      (int)((CC.numIntArgRegs() - ByVal.FirstIdx) * CC.regSize());
  else
    FrameObjOffset = ByVal.Address;

  // Create frame object.
  EVT PtrTy = getPointerTy();
  int FI = MFI->CreateFixedObject(FrameObjSize, FrameObjOffset, true);
  SDValue FIN = DAG.getFrameIndex(FI, PtrTy);
  InVals.push_back(FIN);

  if (!ByVal.NumRegs)
    return;

  // Copy arg registers.
  MVT RegTy = MVT::getIntegerVT(CC.regSize() * 8);
  const TargetRegisterClass *RC = getRegClassFor(RegTy);

  for (unsigned I = 0; I < ByVal.NumRegs; ++I) {
    unsigned ArgReg = CC.intArgRegs()[ByVal.FirstIdx + I];
    unsigned VReg = AddLiveIn(MF, ArgReg, RC);
    unsigned Offset = I * CC.regSize();
    SDValue StorePtr = DAG.getNode(ISD::ADD, DL, PtrTy, FIN,
                                   DAG.getConstant(Offset, PtrTy));
    SDValue Store = DAG.getStore(Chain, DL, DAG.getRegister(VReg, RegTy),
                                 StorePtr, MachinePointerInfo(FuncArg, Offset),
                                 false, false, 0);
    OutChains.push_back(Store);
  }
}

// Copy byVal arg to registers and stack.
void rvexTargetLowering::
passByValArg(SDValue Chain, SDLoc DL,
             std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
             SmallVector<SDValue, 8> &MemOpChains, SDValue StackPtr,
             MachineFrameInfo *MFI, SelectionDAG &DAG, SDValue Arg,
             const rvexCC &CC, const ByValArgInfo &ByVal,
             const ISD::ArgFlagsTy &Flags, bool isLittle) const {
  unsigned ByValSize = Flags.getByValSize();
  unsigned Offset = 0; // Offset in # of bytes from the beginning of struct.
  unsigned RegSize = CC.regSize();
  unsigned Alignment = std::min(Flags.getByValAlign(), RegSize);
  EVT PtrTy = getPointerTy(), RegTy = MVT::getIntegerVT(RegSize * 8);

  if (ByVal.NumRegs) {
    const uint16_t *ArgRegs = CC.intArgRegs();
    bool LeftoverBytes = (ByVal.NumRegs * RegSize > ByValSize);
    unsigned I = 0;

    // Copy words to registers.
    for (; I < ByVal.NumRegs - LeftoverBytes; ++I, Offset += RegSize) {
      SDValue LoadPtr = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                                    DAG.getConstant(Offset, PtrTy));
      SDValue LoadVal = DAG.getLoad(RegTy, DL, Chain, LoadPtr,
                                    MachinePointerInfo(), false, false, false,
                                    Alignment);
      MemOpChains.push_back(LoadVal.getValue(1));
      unsigned ArgReg = ArgRegs[ByVal.FirstIdx + I];
      RegsToPass.push_back(std::make_pair(ArgReg, LoadVal));
    }

    // Return if the struct has been fully copied.
    if (ByValSize == Offset)
      return;

    // Copy the remainder of the byval argument with sub-word loads and shifts.
    if (LeftoverBytes) {
      assert((ByValSize > Offset) && (ByValSize < Offset + RegSize) &&
             "Size of the remainder should be smaller than RegSize.");
      SDValue Val;

      for (unsigned LoadSize = RegSize / 2, TotalSizeLoaded = 0;
           Offset < ByValSize; LoadSize /= 2) {
        unsigned RemSize = ByValSize - Offset;

        if (RemSize < LoadSize)
          continue;

        // Load subword.
        SDValue LoadPtr = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                                      DAG.getConstant(Offset, PtrTy));
        SDValue LoadVal =
          DAG.getExtLoad(ISD::ZEXTLOAD, DL, RegTy, Chain, LoadPtr,
                         MachinePointerInfo(), MVT::getIntegerVT(LoadSize * 8),
                         false, false, false, Alignment);
        MemOpChains.push_back(LoadVal.getValue(1));

        // Shift the loaded value.
        unsigned Shamt;

        if (isLittle)
          Shamt = TotalSizeLoaded;
        else
          Shamt = (RegSize - (TotalSizeLoaded + LoadSize)) * 8;

        SDValue Shift = DAG.getNode(ISD::SHL, DL, RegTy, LoadVal,
                                    DAG.getConstant(Shamt, MVT::i32));

        if (Val.getNode())
          Val = DAG.getNode(ISD::OR, DL, RegTy, Val, Shift);
        else
          Val = Shift;

        Offset += LoadSize;
        TotalSizeLoaded += LoadSize;
        Alignment = std::min(Alignment, LoadSize);
      }

      unsigned ArgReg = ArgRegs[ByVal.FirstIdx + I];
      RegsToPass.push_back(std::make_pair(ArgReg, Val));
      return;
    }
  }

  // Copy remainder of byval arg to it with memcpy.
  unsigned MemCpySize = ByValSize - Offset;
  SDValue Src = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                            DAG.getConstant(Offset, PtrTy));
  SDValue Dst = DAG.getNode(ISD::ADD, DL, PtrTy, StackPtr,
                            DAG.getIntPtrConstant(ByVal.Address));
  Chain = DAG.getMemcpy(Chain, DL, Dst, Src,
                        DAG.getConstant(MemCpySize, PtrTy), Alignment,
                        /*isVolatile=*/false, /*AlwaysInline=*/false,
                        MachinePointerInfo(), MachinePointerInfo());
  MemOpChains.push_back(Chain);
}

void
rvexTargetLowering::writeVarArgRegs(std::vector<SDValue> &OutChains,
                                    const rvexCC &CC, SDValue Chain,
                                    SDLoc DL, SelectionDAG &DAG) const {
  unsigned NumRegs = CC.numIntArgRegs();
  const uint16_t *ArgRegs = CC.intArgRegs();
  const CCState &CCInfo = CC.getCCInfo();
  unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs, NumRegs);
  unsigned RegSize = CC.regSize();
  MVT RegTy = MVT::getIntegerVT(RegSize * 8);
  const TargetRegisterClass *RC = getRegClassFor(RegTy);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  rvexFunctionInfo *rvexFI = MF.getInfo<rvexFunctionInfo>();

  // Offset of the first variable argument from stack pointer.
  int VaArgOffset;

  if (NumRegs == Idx)
    VaArgOffset = RoundUpToAlignment(CCInfo.getNextStackOffset(), RegSize);
  else
    VaArgOffset =
      (int)CC.reservedArgArea() - (int)(RegSize * (NumRegs - Idx));

  // Record the frame index of the first variable argument
  // which is a value necessary to VASTART.
  int FI = MFI->CreateFixedObject(RegSize, VaArgOffset, true);
  rvexFI->setVarArgsFrameIndex(FI);

  // Copy the integer registers that have not been used for argument passing
  // to the argument register save area. For O32, the save area is allocated
  // in the caller's stack frame, while for N32/64, it is allocated in the
  // callee's stack frame.
  for (unsigned I = Idx; I < NumRegs; ++I, VaArgOffset += RegSize) {
    unsigned Reg = AddLiveIn(MF, ArgRegs[I], RC);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegTy);
    FI = MFI->CreateFixedObject(RegSize, VaArgOffset, true);
    SDValue PtrOff = DAG.getFrameIndex(FI, getPointerTy());
    SDValue Store = DAG.getStore(Chain, DL, ArgValue, PtrOff,
                                 MachinePointerInfo(), false, false, 0);
    cast<StoreSDNode>(Store.getNode())->getMemOperand()->setValue((Value*)nullptr);
    OutChains.push_back(Store);
  }
}

bool rvexTargetLowering::
isEligibleForTailCallOptimization(const rvexCC &rvexCCInfo,
                                  unsigned NextStackOffset) const {


  // Return false if either the callee or caller has a byval argument.
  if (rvexCCInfo.hasByValArg())
    return false;

  // Return true if the callee's argument area is no larger than the
  // caller's.
  //return NextStackOffset <= FI.getIncomingArgSize();
  // XXX: Return false for now as the tail call implementation is broken.
  return false;
}

bool
rvexTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The rvex target isn't yet aware of offsets.
  return false;
}
