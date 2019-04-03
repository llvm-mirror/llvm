//===-- EVMInstrInfo.td - Target Description for EVM ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the EVM instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "EVMInstrFormats.td"

def getStackOpcode : InstrMapping {
  let FilterClass = "StackRel";
  let RowFields = ["BaseName"];
  let ColFields = ["StackBased"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

//===----------------------------------------------------------------------===//
// EVM specific DAG Nodes.
//===----------------------------------------------------------------------===//

// SDNode properties
def SDT_EVMCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_EVMReturn : SDTypeProfile<0, -1, []>;

def SDT_EVMArithBinary :
SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>]>;

def SDT_EVMArithTernary :
SDTypeProfile<1, 3,
              [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>]>;

def SDT_EVMTwoOpnd :
SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;

def SDT_EVMCondJump :
SDTypeProfile<0, 2, [SDTCisVT<1, OtherVT>]>;

def SDT_EVMBrcc :
SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>, SDTCisVT<3, OtherVT>]>;

def SDT_EVMSignextend :
SDTypeProfile<1, 2, [SDTCisVT<2, i256>]>;

def SDT_EVMSelectCC : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                    SDTCisSameAs<1, 2>, SDTCisVT<3, i256>]>;

def SDT_EVMArgument : SDTypeProfile<1, 1, [SDTCisVT<1, i256>]>;
def SDT_EVMMove : SDTypeProfile<0, 2, [SDTCisVT<0, i256>, SDTCisSameAs<0, 1>]>;

def SDT_EVMCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i256>, SDTCisVT<1, i256>]>;
def SDT_EVMCallSeqEnd   : SDCallSeqEnd  <[SDTCisVT<0, i256>, SDTCisVT<1, i256>]>;

// custom SDNodes
def EVMSelectcc :
SDNode<"EVMISD::SELECTCC", SDT_EVMSelectCC, [SDNPInGlue]>;

def EVMArgument : // custom lowered node
SDNode<"EVMISD::ARGUMENT", SDT_EVMArgument>;

def EVMMove :
SDNode<"EVMISD::MOVE", SDT_EVMMove>;

def EVMBrcc :
SDNode<"EVMISD::BRCC", SDT_EVMBrcc, [SDNPHasChain, SDNPOutGlue, SDNPInGlue]>;

def EVMCall :
SDNode<"EVMISD::CALL", SDT_EVMCall,
       [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;

def EVMReturn :
SDNode<"EVMISD::RETURN", SDT_EVMReturn, [SDNPHasChain]>;

def EVMMod :
SDNode<"EVMISD::MOD", SDT_EVMArithBinary, []>;

def EVMSmod :
SDNode<"EVMISD::SMOD", SDT_EVMArithBinary, []>;

def EVMAddMod :
SDNode<"EVMISD::ADDMOD", SDT_EVMArithTernary, []>;

def EVMExp:
SDNode<"EVMISD::EXP", SDT_EVMArithBinary, []>;

def EVMSignextend:
SDNode<"EVMISD::SIGNEXTEND", SDT_EVMSignextend, []>;

def EVMByte :
SDNode<"EVMISD::BYTE", SDT_EVMArithBinary, []>;

def EVMJump :
SDNode<"EVMISD::JUMP", SDTNone, [SDNPHasChain]>;

def EVMJumpi :
SDNode<"EVMISD::JUMPI", SDT_EVMCondJump,
       [SDNPHasChain, SDNPOutGlue, SDNPInGlue]>;

def EVMJumpif :
SDNode<"EVMISD::JUMPIF", SDT_EVMCondJump,
       [SDNPHasChain, SDNPOutGlue, SDNPInGlue]>;

def EVMSwap :
SDNode<"EVMISD::SWAP", SDT_EVMTwoOpnd, []>;

def EVMSlt:
SDNode<"EVMISD::SLT", SDT_EVMArithBinary, [SDNPHasChain]>;

def EVMSgt:
SDNode<"EVMISD::SGT", SDT_EVMArithBinary, []>;

def EVMLt:
SDNode<"EVMISD::LT", SDT_EVMArithBinary, []>;

def EVMG:
SDNode<"EVMISD::GT", SDT_EVMArithBinary, []>;

def EVMEq:
SDNode<"EVMISD::EQ", SDT_EVMArithBinary, []>;

def EVMIszero:
SDNode<"EVMISD::ISZERO", SDT_EVMArithBinary, []>;

def EVMCallseq_start : SDNode<"EVMISD::CALLSEQ_START", SDT_EVMCallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def EVMCallseq_end : SDNode<"EVMISD::CALLSEQ_END", SDT_EVMCallSeqEnd,
                            [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// custom matching frameindex.
def fiAddr : ComplexPattern<i256, 2, "SelectAddrFI", [frameindex]>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

// Arithmetic instructions ////////////////////////////////////////////////////
let isCommutable = 1 in {
defm ADD  : BinaryInst<add,  "ADD",  0x01, 3>;
defm MUL  : BinaryInst<mul,  "MUL",  0x02, 5>;
}
defm SUB  : BinaryInst<sub,  "SUB",  0x03, 3>;
defm DIV  : BinaryInst<udiv, "DIV",  0x04, 5>;
defm SDIV : BinaryInst<sdiv, "SDIV", 0x05, 5>;

// LLVM does not have those operators
defm MOD    : Inst_2_1<"MOD",
                       [(set GPR:$dst, (urem GPR:$src1, GPR:$src2))],
                       0x06, 5>;
defm SMOD   : Inst_2_1<"SMOD",
                       [(set GPR:$dst, (srem GPR:$src1, GPR:$src2))],
                       0x07, 5>;
defm ADDMOD : Inst_3_1<"ADDMOD",
                       [(set GPR:$dst, (urem (add GPR:$src1, GPR:$src2), GPR:$src3))],
                       0x08, 5>;
defm MULMOD : Inst_3_1<"MULMOD",
                       [(set GPR:$dst, (urem (mul GPR:$src1, GPR:$src2), GPR:$src3))],
                       0x09, 5>;
defm EXP    : Inst_2_1<"EXP",
                       [(set GPR:$dst, (int_evm_exp GPR:$src1, GPR:$src2))],
                       0x0a, 10>;

defm SIGNEXTEND :
  RSInst<(outs GPR:$dst), (ins GPR:$src, I256Imm:$sft),
         [(set GPR:$dst, (EVMSignextend GPR:$src, imm:$sft))],
         "SIGNEXTEND\t$dst, $src, $sft", "SIGNEXTEND", 0xb0, 5>;

// Comparison instructions ////////////////////////////////////////////////////
let isCompare = 1 in {
defm LT     : ComparisonInst<SETULT, "LT",    0x10, 3>;
defm GT     : ComparisonInst<SETUGT, "GT",    0x11, 3>;
defm SLT    : ComparisonInst<SETLT, "SLT",    0x12, 3>;
defm SGT    : ComparisonInst<SETGT, "SGT",    0x13, 3>;
defm EQ     : ComparisonInst<SETEQ, "SEQ",    0x14, 3>;
defm ISZERO : Inst_1_1<"ISZERO",
                       [(set GPR:$dst, (setcc GPR:$src, 0, SETEQ))],
                       0x15, 3>;
}
// Bitwise instructions ///////////////////////////////////////////////////////
let isCommutable = 1 in {
defm AND : BinaryInst<and, "AND", 0x16, 3>;
defm OR  : BinaryInst<or,  "OR",  0x17, 3>;
defm XOR : BinaryInst<xor, "XOR", 0x18, 3>;
}

// LLVM does not generate "NOT" nodes.
let isCompare = 1 in {
defm NOT  :  Inst_1_1<"NOT" , [(set GPR:$dst, (setcc GPR:$src, 0, SETEQ))], 0x19, 3>;
}
defm BYTE :  Inst_2_1<"BYTE", [], 0x20, 3>;

let mayLoad = 1, mayStore = 0 in {
defm SHA3      : Inst_2_1<"SHA3",
                          [(set GPR:$dst, (int_evm_sha3 GPR:$src1, GPR:$src2))],
                          0x21, 30>;
}

defm ADDRESS   : Inst_0_1<"ADDRESS",
                          [(set GPR:$dst, (int_evm_address))],
                          0x30, 2>;
defm BALANCE   : Inst_1_1<"BALANCE",
                          [(set GPR:$dst, (int_evm_balance GPR:$src))],
                          0x31, 400>;
defm ORIGIN    : Inst_0_1<"ORIGIN",
                          [(set GPR:$dst, (int_evm_origin))],
                          0x32, 2>;
defm CALLER    : Inst_0_1<"CALLER",
                          [(set GPR:$dst, (int_evm_caller))],
                          0x33, 2>;
defm CALLVALUE : Inst_0_1<"CALLVALUE",
                          [(set GPR:$dst, (int_evm_callvalue))],
                          0x34, 2>;

let hasSideEffects = 1 in {
defm CALLDATALOAD : Inst_1_1<"CALLDATALOAD",
                             [(set GPR:$dst, (int_evm_calldataload GPR:$src))],
                             0x35, 3>;
}

defm CALLDATASIZE : Inst_0_1<"CALLDATASIZE",
                             [(set GPR:$dst, (int_evm_calldatasize))],
                             0x36, 2>;

let mayStore = 1, hasSideEffects = 1 in {
defm CALLDATACOPY : Inst_3_0<"CALLDATACOPY",
                             [(int_evm_calldatacopy
                               GPR:$src1, GPR:$src2, GPR:$src3)],
                             0x37, 3>;
}

defm CODESIZE     : Inst_0_1<"CODESIZE",
                             [(set GPR:$dst, (int_evm_codesize))],
                             0x38, 2>;

let hasSideEffects = 1 in {

let mayStore = 1 in {
defm CODECOPY     : Inst_3_0<"CODECOPY",
                             [(int_evm_codecopy
                               GPR:$src1, GPR:$src2, GPR:$src3)],
                             0x39, 3>;
}
defm GASPRICE     : Inst_0_1<"GASPRICE",
                             [(set GPR:$dst, (int_evm_gasprice))],
                             0x3a, 2>;
defm EXTCODESIZE  : Inst_1_1<"EXTCODESIZE",
                             [(set GPR:$dst, (int_evm_extcodesize GPR:$src))],
                             0x3b, 700>;
}

let mayStore = 1, hasSideEffects = 1 in {
defm EXTCODECOPY : RSInst<(outs),
                          (ins GPR:$src1, GPR:$src2, GPR:$src3, GPR:$src4),
                          [(int_evm_extcodecopy GPR:$src1, GPR:$src2,
                                                GPR:$src3, GPR:$src4)],
                          "EXTCODECOPY \t$src1, $src2, $src3, $src4",
                          "EXTCODECOPY", 0x3c, 700>;  // TODO: verify
}

//let Predicates = [HasEIP211] in {
let hasSideEffects = 1 in {
defm RETURNDATASIZE : Inst_0_1<"RETURNDATASIZE",
                               [(set GPR:$dst, (int_evm_returndatasize))],
                               0x3d, 2>;

let mayStore = 1 in {
defm RETURNDATACOPY : Inst_3_0<"RETURNDATACOPY",
                               [(int_evm_returndatacopy
                                 GPR:$src1, GPR:$src2, GPR:$src3)],
                               0x3e, 3>;
}
}
//}

defm BLOCKHASH  : Inst_1_1<"BLOCKHASH",
                           [(set GPR:$dst, (int_evm_blockhash GPR:$src))],
                           0x40, 20>;
defm COINBASE   : Inst_0_1<"COINBASE",
                           [(set GPR:$dst, (int_evm_coinbase))],
                           0x41, 2>;
defm TIMESTAMP  : Inst_0_1<"TIMESTAMP",
                           [(set GPR:$dst, (int_evm_timestamp))],
                           0x42, 2>;
defm NUMBER     : Inst_0_1<"NUMBER",
                           [(set GPR:$dst, (int_evm_number))],
                           0x43, 2>;
defm DIFFICULTY : Inst_0_1<"DIFFICULTY",
                           [(set GPR:$dst, (int_evm_difficulty))],
                           0x44, 2>;
defm GASLIMIT   : Inst_0_1<"GASLIMIT",
                           [(set GPR:$dst, (int_evm_gaslimit))],
                           0x45, 2>;



let mayLoad = 1 in {
defm MLOAD : RSInst<(outs GPR:$dst), (ins GPR:$src),
                    [(set GPR:$dst, (load GPR:$src))],
                   "MLOAD_r\t$dst, $src", "MLOAD", 0x51, 3>;
}

let mayStore = 1 in {
defm MSTORE : Inst_2_0<"MSTORE",
                       [(store GPR:$src1, GPR:$src2)], 0x52, 3>;

defm MSTORE8 : Inst_2_0<"MSTORE8", [], 0x53, 3>;
}

let hasSideEffects = 1 in {
defm SLOAD  : Inst_1_1<"SLOAD",
                       [(set GPR:$dst, (int_evm_sload GPR:$src))],
                       0x54, 200>;

defm SSTORE : Inst_2_0<"SSTORE",
                       [(int_evm_sstore GPR:$src1, GPR:$src2)],
                       0x55, 200000>;
}


let isTerminator = 1, isBarrier = 1, isBranch = 1 in {
defm JUMP : RSInst<(outs), (ins brtarget:$dst),
                   [(br bb:$dst)],
                   "JUMP_r\t$dst", "JUMP", 0x56, 8>;

defm JUMPI : RSInst<(outs), (ins GPR:$src, brtarget:$dst),
                   [(EVMJumpi GPR:$src, bb:$dst)],
                   "JUMPI\t$dst", "JUMPI", 0x57, 10>;
}

defm PC       : Inst_0_1<"PC",
                         [(set GPR:$dst, (int_evm_getpc))],
                         0x58, 2>;

defm MSIZE    : Inst_0_1<"MSIZE",
                         [(set GPR:$dst, (int_evm_msize))],
                         0x59, 2>;
defm GAS      : Inst_0_1<"GAS",
                         [(set GPR:$dst, (int_evm_gas))],
                         0x5a, 2>;
defm JUMPDEST : Inst_0_0<"JUMPDEST", [], 0x5b, 1>;

let BaseName = "PUSH32" in {

let isCodeGenOnly = 1 in {
def PUSH32_r :
  EVMInst<(outs GPR:$dst), (ins I256Imm:$src), [(set GPR:$dst, imm:$src)],
          "false", "PUSH32_r \t$src", 0x7f, 3>;
}

def PUSH32 :
  EVMInst<(outs), (ins I256Imm:$src), [], "true", "PUSH32 \t$src", 0x7f, 3>;
}

let isBranch = 1, isBarrier = 1, isTerminator = 1 in {
defm JUMPTO : Inst_1_0<"JUMPTO", [], 0xb0, 8>;
/*
defm JUMPIF : // TODO: argument type is 4 bytes.
    : RSInst<(outs GPR:$dst), (ins GPR:$src1, GPR:$src2),
             pattern,
             "JUMPIF\t$dst, $src1, $src2", "JUMPIF", 0xb1, 10>;
*/
}

//}

let mayStore = 1, hasSideEffects = 1 in {
defm CREATE : Inst_3_1<"CREATE",
                       [(set GPR:$dst,
                        (int_evm_create GPR:$src1, GPR:$src2, GPR:$src3))],
                       0xf0, 32000>;
}

let isCall = 1 in {
// We should compute the cost of CALL separately.
//defm CALL   : Inst_7_1<"CALL", [], 0xf1, 0>;
//defm CALLNODE   : Inst_7_1<"CALLNODE", [], 0xf2, 0>;
}

let isReturn = 1 in {
defm RETURN : RSInst<(outs), (ins GPR:$val),
                     [(EVMReturn GPR:$val)],
                     "RETURN\t$val", "RETURN", 0xf3, 0>;
}

let isCall = 1 in {
//defm DELEGATECALL : Inst_6_1<"DELEGATECALL", [], 0xf4, 0>;
//defm CALLBLACKBOX : Inst_6_1<"CALLBLACKBLOX", [], 0xf5, 0>;
//defm STATICCALL : Inst_7_1<"STATICCALL", [], 0xfa, 40>;
}

let hasSideEffects = 1 in {
let mayStore = 1 in {
defm CREATE2    : Inst_3_1<"CREATE2",
                           [(set GPR:$dst,
                             (int_evm_create2
                              GPR:$src1, GPR:$src2, GPR:$src3))],
                           0xfb, 32000>;
// defm TXEXEGAS // TODO

defm REVERT     : Inst_2_1<"REVERT",
                           [(set GPR:$dst, (int_evm_revert GPR:$src1, GPR:$src2))],
                           0xfd, 0>;
}
}

defm INVALID    : Inst_0_0<"INVALID", [], 0xfe, 0>;

let mayStore = 1, hasSideEffects = 1 in {
defm SELFDESTRUCT : Inst_1_0<"SELFDESTRUCT",
                             [(int_evm_selfdestruct GPR:$src)],
                             0xff, 5000>;
}
//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//
let usesCustomInserter = 1 in {
def PseudoSelectcc :
EVMPseudo<(outs GPR:$dst),
          (ins GPR:$src, GPR:$src2, i256imm:$cc),
          "SelectCC\t$cc | $dst, $src, $src2",
          [(set i256:$dst, (EVMSelectcc i256:$src, i256:$src2, imm:$cc))]>;

}

let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : EVMPseudo<(outs), (ins i256imm:$amt, i256imm:$amt2),
                               "# ADJCALLSTACKDOWN\t$amt, $amt2",
                               [(EVMCallseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : EVMPseudo<(outs), (ins i256imm:$amt1, i256imm:$amt2),
                            "# ADJCALLSTACKUP\t$amt1, $amt2",
                            [(EVMCallseq_end timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

def EVM_CC_EQ  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETEQ);}]>;
def EVM_CC_NE  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETNE);}]>;
def EVM_CC_SGE : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETGE);}]>;
def EVM_CC_SGT : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETGT);}]>;
def EVM_CC_GT  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETUGT);}]>;
def EVM_CC_GE  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETUGE);}]>;
def EVM_CC_SLE : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETLE);}]>;
def EVM_CC_SLT : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETLT);}]>;
def EVM_CC_LT  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETULT);}]>;
def EVM_CC_LE  : PatLeaf<(i256 imm),
                         [{return (N->getZExtValue() == ISD::SETULE);}]>;

// matching BRCC
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SLT, bb:$BrDst),
          (JUMPI_r (SLT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SLE, bb:$BrDst),
          (JUMPI_r (ISZERO_r (SGT_r i256:$dst, i256:$src)), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_LT, bb:$BrDst),
          (JUMPI_r (LT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_LE, bb:$BrDst),
          (JUMPI_r (ISZERO_r (GT_r i256:$dst, i256:$src)), bb:$BrDst)>;

def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SGT, bb:$BrDst),
          (JUMPI_r (SGT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SGE, bb:$BrDst),
          (JUMPI_r (ISZERO_r (SLT_r i256:$dst, i256:$src)), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_GT, bb:$BrDst),
          (JUMPI_r (GT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_GE, bb:$BrDst),
          (JUMPI_r (ISZERO_r (LT_r i256:$dst, i256:$src)), bb:$BrDst)>;

def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_EQ, bb:$BrDst),
          (JUMPI_r (EQ_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_NE, bb:$BrDst),
          (JUMPI_r (ISZERO_r (EQ_r i256:$dst, i256:$src)), bb:$BrDst)>;

// bitwise
def : Pat<(i256 (shl i256:$lhs, i256:$rhs)),
          (i256 (MUL_r i256:$lhs, i256:$rhs))>;
def : Pat<(i256 (srl i256:$lhs, i256:$rhs)),
          (i256 (DIV_r i256:$lhs, i256:$rhs))>;
def : Pat<(i256 (sra i256:$lhs, i256:$rhs)),
          (i256 (SDIV_r i256:$lhs, i256:$rhs))>;

// ext/trunc load/stores
def : Pat<(truncstore i256:$lhs, i256:$rhs),
          (MSTORE_r i256:$lhs, i256:$rhs)>;

// FIXME
def : Pat<(zextload i256:$src), (MLOAD_r i256:$src)>;
def : Pat<(extload i256:$src), (MLOAD_r i256:$src)>;

// copied from other backends.
def frameindex_to_targetframeindex : SDNodeXForm<frameindex, [{
  auto FI = cast<FrameIndexSDNode>(N);
  return CurDAG->getTargetFrameIndex(FI->getIndex(), MVT::i256);
}]>;

//===----------------------------------------------------------------------===//
// Standard extensions
//===----------------------------------------------------------------------===//

