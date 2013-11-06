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

static SDValue GetGlobalReg(SelectionDAG &DAG, EVT Ty) {
  rvexFunctionInfo *FI = DAG.getMachineFunction().getInfo<rvexFunctionInfo>();
  return DAG.getRegister(FI->getGlobalBaseReg(), Ty);
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

      case rvexISD::MTB:                 return "RvexISD::MTB";
      case rvexISD::MFB:                 return "RvexISD::MFB";

  case rvexISD::DivRem:            return "rvexISD::DivRem";
  case rvexISD::DivRemU:           return "rvexISD::DivRemU";
  case rvexISD::Wrapper:           return "rvexISD::Wrapper";

  default:                         return NULL;
  }
}

rvexTargetLowering::
rvexTargetLowering(rvexTargetMachine &TM)
  : TargetLowering(TM, new rvexTargetObjectFile()),
    Subtarget(&TM.getSubtarget<rvexSubtarget>()) {

  // Set up the register classes
  addRegisterClass(MVT::i32, &rvex::CPURegsRegClass);
  addRegisterClass(MVT::i1, &rvex::BRRegsRegClass);

  // rvex Custom Operations
  setOperationAction(ISD::GlobalAddress,      MVT::i32,   Custom);
  
  //TODO: scheduling modes: None, Source, RegPressure, Hybrid, ILP, VLIW
  bool isVLIWEnabled = static_cast<rvexTargetMachine*>(&TM)->getSubtargetImpl()->isVLIWEnabled();
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
  //setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);  
  //setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  setOperationAction(ISD::SELECT_CC, MVT::i32, Promote);

  setOperationAction(ISD::ZERO_EXTEND, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::i32, Expand);

  // Custom lowering of ADDE and ADDC
  setOperationAction(ISD::ADDE, MVT::i32, Custom);
  setOperationAction(ISD::ADDC, MVT::i32, Custom);

  setLoadExtAction(ISD::EXTLOAD,  MVT::i1,  Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i1,  Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i1,  Promote);

  setOperationAction(ISD::BR_CC, MVT::Other,   Expand);
  setOperationAction(ISD::BR_CC, MVT::i1,   Expand);
  setOperationAction(ISD::BR_CC, MVT::i32,   Expand);
  setOperationAction(ISD::BR_JT,            MVT::Other, Expand);
  setOperationAction(ISD::BRIND,            MVT::Other, Expand);

  setOperationAction(ISD::AND,                MVT::i1, Promote);
  setOperationAction(ISD::OR,                 MVT::i1, Promote);
  setOperationAction(ISD::ADD,                MVT::i1, Promote);
  setOperationAction(ISD::SUB,                MVT::i1, Promote);
  setOperationAction(ISD::XOR,                MVT::i1, Promote);
  setOperationAction(ISD::SHL,                MVT::i1, Promote);
  setOperationAction(ISD::SRA,                MVT::i1, Promote);
  setOperationAction(ISD::SRL,                MVT::i1, Promote);

  setOperationAction(ISD::ROTL,             MVT::i32, Expand);
  setOperationAction(ISD::ROTR,             MVT::i32, Expand);
  setOperationAction(ISD::ROTL,             MVT::i64, Expand);
  setOperationAction(ISD::ROTR,             MVT::i64, Expand);  

  // Softfloat Floating Point Library Calls
  // Integer to Float conversions
  setLibcallName(RTLIB::SINTTOFP_I32_F32, "int32_to_float32");
  //setLibcallName(RTLIB::SINTTOFP_I32_F32, "_r_ilfloat");
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);

  //setLibcallName(RTLIB::UINTTOFP_I32_F32, "_r_ufloat");
  //setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);

  setLibcallName(RTLIB::SINTTOFP_I32_F64, "int32_to_float64");
  //setLibcallName(RTLIB::SINTTOFP_I32_F64, "_d_ilfloat");
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);

  //setLibcallName(RTLIB::UINTTOFP_I32_F64, "_d_ufloat");
  //setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);

  //Software IEC/IEEE single-precision conversion routines.
  setLibcallName(RTLIB::FPTOSINT_F32_I32, "float32_to_int32");
  setOperationAction(ISD::FP_TO_SINT, MVT::f32, Expand);

  //FIXME
  //float32_to_int32_round_to_zero

  setLibcallName(RTLIB::FPEXT_F32_F64, "float32_to_float64");
  //setLibcallName(RTLIB::FPEXT_F32_F64, "_d_r");
  setOperationAction(ISD::FP_EXTEND, MVT::f32, Expand);

  //Software IEC/IEEE single-precision operations.
  // FIXME are these roundings correct? There is NEARBYINT_F too..
  setLibcallName(RTLIB::RINT_F32, "float32_round_to_int");
  setOperationAction(ISD::FRINT , MVT::f32, Expand);

  setLibcallName(RTLIB::ADD_F32, "float32_add");
  //setLibcallName(RTLIB::ADD_F32, "_r_add");
  setOperationAction(ISD::FADD, MVT::f32, Expand);

  setLibcallName(RTLIB::SUB_F32, "float32_sub");
  //setLibcallName(RTLIB::SUB_F32, "_r_sub");
  setOperationAction(ISD::FSUB, MVT::f32, Expand);

  setLibcallName(RTLIB::MUL_F32, "float32_mul");
  //setLibcallName(RTLIB::MUL_F32, "_r_mul");
  setOperationAction(ISD::FMUL, MVT::f32, Expand);

  setLibcallName(RTLIB::DIV_F32, "float32_div");
  //setLibcallName(RTLIB::DIV_F32, "_r_div");
  setOperationAction(ISD::FDIV, MVT::f32, Expand);

  setLibcallName(RTLIB::REM_F32, "float32_rem");
  setOperationAction(ISD::SREM, MVT::f32, Expand);
  //setLibcallName(RTLIB::UREM_F32, "float32_rem");
  setOperationAction(ISD::UREM, MVT::f32, Expand);

  setLibcallName(RTLIB::SQRT_F32, "float32_sqrt");

  setLibcallName(RTLIB::OEQ_F32, "float32_eq");
  //setLibcallName(RTLIB::OEQ_F32, "_r_eq");
  setOperationAction(ISD::SETOEQ, MVT::f32, Expand);

  setLibcallName(RTLIB::OLE_F32, "float32_le");
  //setLibcallName(RTLIB::OLE_F32, "_r_le");
  setOperationAction(ISD::SETOLE, MVT::f32, Expand);

  setLibcallName(RTLIB::OLT_F32, "float32_lt");
  //setLibcallName(RTLIB::OLT_F32, "_r_lt");
  setOperationAction(ISD::SETOLT, MVT::f32, Expand);

  




//- Set .align 2
// It will emit .align 2 later
  setMinFunctionAlignment(2);

// must, computeRegisterProperties - Once all of the register classes are 
//  added, this allows us to compute derived properties we expose.
  computeRegisterProperties();
}

EVT rvexTargetLowering::getSetCCResultType(EVT VT) const {
  return MVT::i1;
}

SDValue rvexTargetLowering::PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI)
  const {
  unsigned opc = N->getOpcode();

  switch (opc) {
  default: break;

  }

  return SDValue();
}

SDValue rvexTargetLowering::
LowerAddCG(SDValue Op, SelectionDAG &DAG) const
{
  unsigned Opc = Op.getOpcode();
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
LowerUDIV(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerUDIV!\n");
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();
  SDValue Zero = DAG.getRegister(rvex::R0, MVT::i32);
  SDValue ZeroImm = DAG.getTargetConstant(0, MVT::i32);

  return DAG.getSetCC(dl, MVT::i1, Zero, ZeroImm, ISD::SETEQ);

}

SDValue rvexTargetLowering::
LowerZeroExtend(SDValue Op, SelectionDAG &DAG) const
{
  DEBUG(errs() << "LowerZeroExtend!\n");
  SDNode* N = Op.getNode();
  DebugLoc dl = N->getDebugLoc();

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
    case ISD::UDIV:               return LowerUDIV(Op, DAG);
    case ISD::SDIV:               return LowerSDIV(Op, DAG);

    case ISD::MULHS:              return LowerMULHS(Op, DAG);
    case ISD::MULHU:              return LowerMULHU(Op, DAG);

    case ISD::ZERO_EXTEND:         return LowerZeroExtend(Op,DAG);

    case ISD::Constant:           return LowerConstant(Op, DAG);
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Lower helper functions
//===----------------------------------------------------------------------===//

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
  DebugLoc dl = Op.getDebugLoc();
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  SDVTList VTs = DAG.getVTList(MVT::i32);

  //rvexTargetObjectFile &TLOF = (rvexTargetObjectFile&)getObjFileLowering();


  SDValue GA = DAG.getTargetGlobalAddress(GV, dl, MVT::i32, 0,
                                          rvexII::MO_NO_FLAG);
  SDValue GPRelNode = DAG.getNode(rvexISD::GPRel, dl, VTs, &GA, 1);
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(MVT::i32);
  return DAG.getNode(ISD::ADD, dl, MVT::i32, GOT, GPRelNode);


  


}

#include "rvexGenCallingConv.inc"

SDValue
rvexTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                              SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  DebugLoc &dl                          = CLI.DL;
  SmallVector<ISD::OutputArg, 32> &Outs = CLI.Outs;
  SmallVector<SDValue, 32> &OutVals     = CLI.OutVals;
  SmallVector<ISD::InputArg, 32> &Ins   = CLI.Ins;
  SDValue InChain                       = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;

  // MBlaze does not yet support tail call optimization
  isTailCall = false;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFL = MF.getTarget().getFrameLowering();
  bool IsPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;
  rvexFunctionInfo *rvexFI = MF.getInfo<rvexFunctionInfo>();

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 getTargetMachine(), ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeCallOperands(Outs, CC_rvex);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NextStackOffset = CCInfo.getNextStackOffset();

  // If this is the first call, create a stack frame object that points to
  // a location to which .cprestore saves $gp.
  if (IsPIC && rvexFI->globalBaseRegFixed() && !rvexFI->getGPFI())
    rvexFI->setGPFI(MFI->CreateFixedObject(4, 0, true));
  // Get the frame index of the stack frame object that points to the location
  // of dynamically allocated area on the stack.
  int DynAllocFI = rvexFI->getDynAllocFI();
  unsigned MaxCallFrameSize = rvexFI->getMaxCallFrameSize();

  if (MaxCallFrameSize < NextStackOffset) {
    rvexFI->setMaxCallFrameSize(NextStackOffset);

    // Set the offsets relative to $sp of the $gp restore slot and dynamically
    // allocated stack space. These offsets must be aligned to a boundary
    // determined by the stack alignment of the ABI.
    unsigned StackAlignment = TFL->getStackAlignment();
    NextStackOffset = (NextStackOffset + StackAlignment - 1) /
                      StackAlignment * StackAlignment;

    MFI->setObjectOffset(DynAllocFI, NextStackOffset);
  }
  // Chain is the output chain of the last Load/Store or CopyToReg node.
  // ByValChain is the output chain of the last Memcpy node created for copying
  // byval arguments to the stack.
  SDValue Chain, CallSeqStart, ByValChain;
  SDValue NextStackOffsetVal = DAG.getIntPtrConstant(NextStackOffset, true);
  Chain = CallSeqStart = DAG.getCALLSEQ_START(InChain, NextStackOffsetVal);
  ByValChain = InChain;

  // With EABI is it possible to have 16 args on registers.
  SmallVector<std::pair<unsigned, SDValue>, 16> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  int FirstFI = -MFI->getNumFixedObjects() - 1, LastFI = 0;

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    SDValue Arg = OutVals[i];
    CCValAssign &VA = ArgLocs[i];
    MVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    // ByVal Arg.
    if (Flags.isByVal()) {
      assert("!!!Error!!!, Flags.isByVal()==true");
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      continue;
    }

    // Register can't get to this point...
    assert(VA.isMemLoc());

    // Create the frame index object for this incoming parameter
    LastFI = MFI->CreateFixedObject(ValVT.getSizeInBits()/8,
                                    VA.getLocMemOffset(), true);
    SDValue PtrOff = DAG.getFrameIndex(LastFI, getPointerTy());

    // emit ISD::STORE whichs stores the
    // parameter value to a stack Location
    MemOpChains.push_back(DAG.getStore(Chain, dl, Arg, PtrOff,
                                       MachinePointerInfo(), false, false, 0));
  }

  // Extend range of indices of frame objects for outgoing arguments that were
  // created during this function call. Skip this step if no such objects were
  // created.
  if (LastFI)
    rvexFI->extendOutArgFIRange(FirstFI, LastFI);

  // If a memcpy has been created to copy a byval arg to a stack, replace the
  // chain input of CallSeqStart with ByValChain.
  if (InChain != ByValChain)
    DAG.UpdateNodeOperands(CallSeqStart.getNode(), ByValChain,
                           NextStackOffsetVal);

  // Transform all store nodes into one single node because all store
  // nodes are independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                        &MemOpChains[0], MemOpChains.size());

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  unsigned char OpFlag;
  bool IsPICCall = IsPIC; // true if calls are translated to jalr $25
  bool GlobalOrExternal = false;
  SDValue CalleeLo;

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    OpFlag = IsPICCall ? rvexII::MO_GOT_CALL : rvexII::MO_NO_FLAG;
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl,
                                          getPointerTy(), 0, OpFlag);
    GlobalOrExternal = true;
  }
  else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    if (!IsPIC) // static
      OpFlag = rvexII::MO_NO_FLAG;
    else // O32 & PIC
      OpFlag = rvexII::MO_GOT_CALL;
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), getPointerTy(),
                                         OpFlag);
    GlobalOrExternal = true;
  }

  SDValue InFlag;

  // Create nodes that load address of callee and copy it to T9
  if (IsPICCall) {
    if (GlobalOrExternal) {
      // Load callee address
      Callee = DAG.getNode(rvexISD::Wrapper, dl, getPointerTy(),
                           GetGlobalReg(DAG, getPointerTy()), Callee);
      SDValue LoadValue = DAG.getLoad(getPointerTy(), dl, DAG.getEntryNode(),
                                      Callee, MachinePointerInfo::getGOT(),
                                      false, false, false, 0);

      // Use GOT+LO if callee has internal linkage.
      if (CalleeLo.getNode()) {
        SDValue Lo = DAG.getNode(rvexISD::Lo, dl, getPointerTy(), CalleeLo);
        Callee = DAG.getNode(ISD::ADD, dl, getPointerTy(), LoadValue, Lo);
      } else
        Callee = LoadValue;
    }
  }

  // T9 should contain the address of the callee function if
  // -reloction-model=pic or it is an indirect call.
  if (IsPICCall || !GlobalOrExternal) {
    // copy to T9
    unsigned T9Reg = rvex::LR;
    Chain = DAG.getCopyToReg(Chain, dl, T9Reg, Callee, SDValue(0, 0));
    InFlag = Chain.getValue(1);
    Callee = DAG.getRegister(T9Reg, getPointerTy());
  }

  // rvexJmpLink = #chain, #target_address, #opt_in_flags...
  //             = Chain, Callee, Reg#1, Reg#2, ...
  //
  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = getTargetMachine().getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain  = DAG.getNode(rvexISD::JmpLink, dl, NodeTys, &Ops[0], Ops.size());
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain,
                             DAG.getIntPtrConstant(NextStackOffset, true),
                             DAG.getIntPtrConstant(0, true), InFlag);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, isVarArg,
                         Ins, dl, DAG, InVals);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue
rvexTargetLowering::LowerCallResult(SDValue Chain, SDValue InFlag,
                                    CallingConv::ID CallConv, bool isVarArg,
                                    const SmallVectorImpl<ISD::InputArg> &Ins,
                                    DebugLoc dl, SelectionDAG &DAG,
                                    SmallVectorImpl<SDValue> &InVals) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
     getTargetMachine(), RVLocs, *DAG.getContext());

  CCInfo.AnalyzeCallResult(Ins, RetCC_rvex);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                               RVLocs[i].getValVT(), InFlag).getValue(1);
    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

/// LowerFormalArguments - transform physical registers into virtual registers
/// and generate load operations for arguments places on the stack.
SDValue
rvexTargetLowering::LowerFormalArguments(SDValue Chain,
                                         CallingConv::ID CallConv,
                                         bool isVarArg,
                                      const SmallVectorImpl<ISD::InputArg> &Ins,
                                         DebugLoc dl, SelectionDAG &DAG,
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
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 getTargetMachine(), ArgLocs, *DAG.getContext());
                         
  CCInfo.AnalyzeFormalArguments(Ins, CC_rvex);

  Function::const_arg_iterator FuncArg =
    DAG.getMachineFunction().getFunction()->arg_begin();
  int LastFI = 0;// rvexFI->LastInArgFI is 0 at the entry of this function.

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i, ++FuncArg) {
    CCValAssign &VA = ArgLocs[i];
    EVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Ins[i].Flags;

    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end."); 
      continue;
    }
    // sanity check
    assert(VA.isMemLoc());

    // The stack pointer offset is relative to the caller stack frame.
    LastFI = MFI->CreateFixedObject(ValVT.getSizeInBits()/8,
                                    VA.getLocMemOffset(), true);

    // Create load nodes to retrieve arguments from the stack
    SDValue FIN = DAG.getFrameIndex(LastFI, getPointerTy());
    InVals.push_back(DAG.getLoad(ValVT, dl, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(LastFI),
                                 false, false, false, 0));
  }
  rvexFI->setLastInArgFI(LastFI);
  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens when on varg functions
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                        &OutChains[0], OutChains.size());
  }
  return Chain;
}

//===----------------------------------------------------------------------===//
//               Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

SDValue
rvexTargetLowering::LowerReturn(SDValue Chain,
                                CallingConv::ID CallConv, bool isVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                DebugLoc dl, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of
  // the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 getTargetMachine(), RVLocs, *DAG.getContext());

  // Analize return values.
  CCInfo.AnalyzeReturn(Outs, RetCC_rvex);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  RetOps.push_back(DAG.getRegister(rvex::LR, MVT::i32));

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                             OutVals[i], Flag);

    // guarantee that all emitted copies are
    // stuck together, avoiding something bad
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(rvexISD::Ret, dl, MVT::Other, &RetOps[0], RetOps.size());
}

bool
rvexTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The rvex target isn't yet aware of offsets.
  return false;
}