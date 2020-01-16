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
def SDT_EVMCallVoid : SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>;
def SDT_EVMCall : SDTypeProfile<1, -1, [SDTCisPtrTy<1>]>;

def SDT_EVMReturn : SDTypeProfile<0, -1, []>;
def SDT_EVMStackArg : SDTypeProfile<1, 1, [SDTCisVT<1, i256>]>;

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
SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>, SDTCisPtrTy<3>]>;

def SDT_EVMSignextend :
SDTypeProfile<1, 2, [SDTCisVT<2, i256>]>;

def SDT_EVMSelectCC     : SDTypeProfile<1, 5, [SDTCisSameAs<1, 2>,
                                               SDTCisSameAs<0, 4>,
                                               SDTCisSameAs<4, 5>]>;

def SDT_EVMArgument : SDTypeProfile<1, 1, [SDTCisVT<1, i256>]>;
def SDT_EVMMove : SDTypeProfile<0, 2, [SDTCisVT<0, i256>, SDTCisSameAs<0, 1>]>;

def SDT_EVMWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

// an wrapper to convert BB into a register
def SDT_EVMBBWrapper : SDTypeProfile<1, 1, [SDTCisPtrTy<0>, SDTCisVT<1, OtherVT>]>;

def SDT_EVMCallSeqStart : SDCallSeqStart<[SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;
def SDT_EVMCallSeqEnd   : SDCallSeqEnd  <[SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;

// custom SDNodes
def EVMSelectcc :
SDNode<"EVMISD::SELECTCC", SDT_EVMSelectCC, [SDNPInGlue]>;

def EVMArgument : // custom lowered node
SDNode<"EVMISD::ARGUMENT", SDT_EVMArgument>;

def EVMMove :
SDNode<"EVMISD::MOVE", SDT_EVMMove>;

def EVMBrcc :
SDNode<"EVMISD::BRCC", SDT_EVMBrcc, [SDNPHasChain, SDNPOutGlue, SDNPInGlue]>;

def EVMWrapper:
SDNode<"EVMISD::WRAPPER", SDT_EVMWrapper>;

def EVMBBWrapper:
SDNode<"EVMISD::BBWRAPPER", SDT_EVMBBWrapper>;

def EVMCall :
SDNode<"EVMISD::CALL", SDT_EVMCall,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def EVMCallVoid :
SDNode<"EVMISD::CALLVOID", SDT_EVMCallVoid,
  [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def EVMRetflag :
SDNode<"EVMISD::RET_FLAG", SDT_EVMReturn, [SDNPHasChain]>;

def EVMSignextend:
SDNode<"EVMISD::SIGNEXTEND", SDT_EVMSignextend, []>;

def EVMByte :
SDNode<"EVMISD::BYTE", SDT_EVMArithBinary, []>;

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

def EVMCallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_EVMCallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def EVMCallseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_EVMCallSeqEnd,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def EVMStackArg : SDNode<"EVMISD::STACKARG", SDT_EVMStackArg, []>;

// custom matching frameindex.
def fiAddr : ComplexPattern<i256, 2, "SelectAddrFI", [frameindex]>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

defm STOP : Inst_0_0<"STOP", [(int_evm_stop)], 0x00, 0>;

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
  RSInst<(outs GPR:$dst), (ins GPR:$src, GPR:$sft),
         [(set GPR:$dst, (EVMSignextend GPR:$src, GPR:$sft))],
         "SIGNEXTEND\t$dst, $src, $sft", "SIGNEXTEND", 0x0b, 5>;

// Comparison instructions ////////////////////////////////////////////////////
let isCompare = 1 in {
defm LT     : ComparisonInst<SETULT, "LT",    0x10, 3>;
defm GT     : ComparisonInst<SETUGT, "GT",    0x11, 3>;
defm SLT    : ComparisonInst<SETLT, "SLT",    0x12, 3>;
defm SGT    : ComparisonInst<SETGT, "SGT",    0x13, 3>;
  let isCommutable = 1 in {
  defm EQ     : ComparisonInst<SETEQ, "EQ",    0x14, 3>;
  }
defm ISZERO : Inst_1_1<"ISZERO",
                       [(set GPR:$dst, (setcc GPR:$src, 0, SETEQ))],
                       0x15, 3>;
}
// Bitwise instructions ///////////////////////////////////////////////////////
let isCommutable = 1 in {
defm AND : BinaryInst<and, "AND", 0x16, 3>;
defm OR  : BinaryInst<or,   "OR", 0x17, 3>;
defm XOR : BinaryInst<xor, "XOR", 0x18, 3>;
// In EVM, the NOT is a bitwise operation.
// LLVM does not generate "NOT" nodes.
defm NOT  :  Inst_1_1<"NOT" , [], 0x19, 3>;
}


// bitwise opcodes -- EIP#145
defm SHL : ShiftInst<shl,  "SHL", 0x1b, 3>;
defm SHR : ShiftInst<srl,  "SHR", 0x1c, 3>;
defm SAR : ShiftInst<sra,  "SAR", 0x1d, 3>;
defm ROL : ShiftInst<rotl, "ROL", 0x1e, 3>;
defm ROR : ShiftInst<rotr, "SAR", 0x1f, 3>;


defm BYTE :  Inst_2_1<"BYTE",
                      [(set GPR:$dst, (int_evm_byte GPR:$src1, GPR:$src2))],
                      0x20, 3>;

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

defm POP        : Inst_1_0<"POP", [], 0x50, 2>;

let mayLoad = 1 in {
defm MLOAD : RSInst<(outs GPR:$dst), (ins GPR:$src),
                    [(set GPR:$dst, (load GPR:$src))],
                   "MLOAD_r\t$dst, $src", "MLOAD", 0x51, 3>;
}

let mayStore = 1 in {
// MSTORE_r offset value
defm MSTORE : Inst_2_0<"MSTORE",
                       [(store GPR:$src2, GPR:$src1)], 0x52, 3>;

defm MSTORE8 : Inst_2_0<"MSTORE8",
                        [(int_evm_mstore8 GPR:$src1, GPR:$src2)],
                        0x53, 3>;
}

let hasSideEffects = 1 in {
defm SLOAD  : Inst_1_1<"SLOAD",
                       [(set GPR:$dst, (int_evm_sload GPR:$src))],
                       0x54, 200>;

defm SSTORE : Inst_2_0<"SSTORE",
                       [(int_evm_sstore GPR:$src1, GPR:$src2)],
                       0x55, 200000>;
}

let isBranch = 1, isTerminator = 1, isIndirectBranch = 1 in {
let isBarrier = 1 in {
defm JUMP : RSInst<(outs), (ins GPR:$dst),
                   [(int_evm_jump GPR:$dst)],
                   "JUMP_r \t$dst", "JUMP", 0x56, 8>;

defm JUMPI : RSInst<(outs), (ins GPR:$src, GPR:$dst),
                    [(int_evm_jumpi GPR:$src, GPR:$dst)],
                    "JUMPI_r \t$src, $dst", "JUMPI", 0x57, 10>;
}
}

defm GETPC    : Inst_0_1<"GETPC",
                         [(set GPR:$dst, (int_evm_getpc))],
                         0x58, 2>;

defm MSIZE    : Inst_0_1<"MSIZE",
                         [(set GPR:$dst, (int_evm_msize))],
                         0x59, 2>;
defm GAS      : Inst_0_1<"GAS",
                         [(set GPR:$dst, (int_evm_gas))],
                         0x5a, 2>;
defm JUMPDEST : Inst_0_0<"JUMPDEST", [(int_evm_jumpdest)], 0x5b, 1>;

let BaseName = "PUSH32" in {
let isCodeGenOnly = 1 in {
def PUSH32_r :
  EVMInst<(outs GPR:$dst), (ins I256Imm:$src), [(set GPR:$dst, imm:$src)],
          "false", "PUSH32_r \t$dst, $src", 0x7f, 3>;
}
def PUSH32 :
  EVMInst<(outs), (ins I256Imm:$src), [], "true", "PUSH32 \t$src", 0x7f, 3>;
}

defm PUSH1 : PUSHInst<0x60, 3>;
defm PUSH2 : PUSHInst<0x61, 3>;
defm PUSH3 : PUSHInst<0x62, 3>;
defm PUSH4 : PUSHInst<0x63, 3>;
defm PUSH5 : PUSHInst<0x64, 3>;
defm PUSH6 : PUSHInst<0x65, 3>;
defm PUSH7 : PUSHInst<0x66, 3>;
defm PUSH8 : PUSHInst<0x67, 3>;
defm PUSH9 : PUSHInst<0x68, 3>;
defm PUSH10: PUSHInst<0x69, 3>;
defm PUSH11: PUSHInst<0x6a, 3>;
defm PUSH12: PUSHInst<0x6b, 3>;
defm PUSH13: PUSHInst<0x6c, 3>;
defm PUSH14: PUSHInst<0x6d, 3>;
defm PUSH15: PUSHInst<0x6e, 3>;
defm PUSH16: PUSHInst<0x6f, 3>;
defm PUSH17: PUSHInst<0x70, 3>;
defm PUSH18: PUSHInst<0x71, 3>;
defm PUSH19: PUSHInst<0x72, 3>;
defm PUSH20: PUSHInst<0x73, 3>;
defm PUSH21: PUSHInst<0x74, 3>;
defm PUSH22: PUSHInst<0x75, 3>;
defm PUSH23: PUSHInst<0x76, 3>;
defm PUSH24: PUSHInst<0x77, 3>;
defm PUSH25: PUSHInst<0x78, 3>;
defm PUSH26: PUSHInst<0x79, 3>;
defm PUSH27: PUSHInst<0x7a, 3>;
defm PUSH28: PUSHInst<0x7b, 3>;
defm PUSH29: PUSHInst<0x7c, 3>;
defm PUSH30: PUSHInst<0x7d, 3>;
defm PUSH31: PUSHInst<0x7e, 3>;

// swap instructions
def SWAP_r: EVMPseudo<(outs), (ins I256Imm:$num), []>;
//only in stack instruction form.
def SWAP1:  EVMInst<(outs), (ins), [], "true", "SWAP1",  0x90, 3>;
def SWAP2:  EVMInst<(outs), (ins), [], "true", "SWAP2",  0x91, 3>;
def SWAP3:  EVMInst<(outs), (ins), [], "true", "SWAP3",  0x92, 3>;
def SWAP4:  EVMInst<(outs), (ins), [], "true", "SWAP4",  0x93, 3>;
def SWAP5:  EVMInst<(outs), (ins), [], "true", "SWAP5",  0x94, 3>;
def SWAP6:  EVMInst<(outs), (ins), [], "true", "SWAP6",  0x95, 3>;
def SWAP7:  EVMInst<(outs), (ins), [], "true", "SWAP7",  0x96, 3>;
def SWAP8:  EVMInst<(outs), (ins), [], "true", "SWAP8",  0x97, 3>;
def SWAP9:  EVMInst<(outs), (ins), [], "true", "SWAP9",  0x98, 3>;
def SWAP10: EVMInst<(outs), (ins), [], "true", "SWAP10", 0x99, 3>;
def SWAP11: EVMInst<(outs), (ins), [], "true", "SWAP11", 0x9a, 3>;
def SWAP12: EVMInst<(outs), (ins), [], "true", "SWAP12", 0x9b, 3>;
def SWAP13: EVMInst<(outs), (ins), [], "true", "SWAP13", 0x9c, 3>;
def SWAP14: EVMInst<(outs), (ins), [], "true", "SWAP14", 0x9d, 3>;
def SWAP15: EVMInst<(outs), (ins), [], "true", "SWAP15", 0x9e, 3>;
def SWAP16: EVMInst<(outs), (ins), [], "true", "SWAP16", 0x9f, 3>;

def DUP_r: EVMPseudo<(outs), (ins I256Imm:$num), []>;
//only in stack instruction form.
def DUP1:  EVMInst<(outs), (ins), [], "true", "DUP1",  0x80, 3>;
def DUP2:  EVMInst<(outs), (ins), [], "true", "DUP2",  0x81, 3>;
def DUP3:  EVMInst<(outs), (ins), [], "true", "DUP3",  0x82, 3>;
def DUP4:  EVMInst<(outs), (ins), [], "true", "DUP4",  0x83, 3>;
def DUP5:  EVMInst<(outs), (ins), [], "true", "DUP5",  0x84, 3>;
def DUP6:  EVMInst<(outs), (ins), [], "true", "DUP6",  0x85, 3>;
def DUP7:  EVMInst<(outs), (ins), [], "true", "DUP7",  0x86, 3>;
def DUP8:  EVMInst<(outs), (ins), [], "true", "DUP8",  0x87, 3>;
def DUP9:  EVMInst<(outs), (ins), [], "true", "DUP9",  0x88, 3>;
def DUP10: EVMInst<(outs), (ins), [], "true", "DUP10", 0x89, 3>;
def DUP11: EVMInst<(outs), (ins), [], "true", "DUP11", 0x8a, 3>;
def DUP12: EVMInst<(outs), (ins), [], "true", "DUP12", 0x8b, 3>;
def DUP13: EVMInst<(outs), (ins), [], "true", "DUP13", 0x8c, 3>;
def DUP14: EVMInst<(outs), (ins), [], "true", "DUP14", 0x8d, 3>;
def DUP15: EVMInst<(outs), (ins), [], "true", "DUP15", 0x8e, 3>;
def DUP16: EVMInst<(outs), (ins), [], "true", "DUP16", 0x8f, 3>;

let isBranch = 1, isBarrier = 1, isTerminator = 1 in {
defm JUMPTO : Inst_1_0<"JUMPTO", [], 0xb0, 8>;
/*
defm JUMPIF : // TODO: argument type is 4 bytes.
    : RSInst<(outs GPR:$dst), (ins GPR:$src1, GPR:$src2),
             pattern,
             "JUMPIF\t$dst, $src1, $src2", "JUMPIF", 0xb1, 10>;
*/
}

let mayStore = 1, hasSideEffects = 1 in {
defm CREATE : Inst_3_1<"CREATE",
                       [(set GPR:$dst,
                        (int_evm_create GPR:$src1, GPR:$src2, GPR:$src3))],
                       0xf0, 32000>;
}

let isCall = 1, hasSideEffects = 1, mayLoad = 1, mayStore = 1 in {
// TODO: We should compute the cost of CALL separately.
defm CALL   : Inst_7_1<"CALL",
                      [(set GPR:$dst,
                       (int_evm_call GPR:$src1, GPR:$src2, GPR:$src3,
                       GPR:$src4, GPR:$src5, GPR:$src6, GPR:$src7))],
                      0xf1, 0>;
defm CALLNODE   : Inst_7_1<"CALLNODE",
                      [(set GPR:$dst,
                       (int_evm_call GPR:$src1, GPR:$src2, GPR:$src3,
                       GPR:$src4, GPR:$src5, GPR:$src6, GPR:$src7))],
                      0xf2, 0>;
}

let isBarrier = 1, hasSideEffects = 1, mayLoad = 1 in {
defm RETURN : RSInst<(outs), (ins GPR:$offset, GPR:$length),
                    [(int_evm_return GPR:$offset, GPR:$length)],
                     "RETURN_r", "RETURN", 0xf3, 0>;
}

let isCall = 1, hasSideEffects = 1, mayStore = 1 in {
defm DELEGATECALL : Inst_6_1<"DELEGATECALL",
                      [(set GPR:$dst,
                       (int_evm_delegatecall GPR:$src1, GPR:$src2,
                        GPR:$src3, GPR:$src4, GPR:$src5, GPR:$src6))],
                    0xf4, 0>;
defm STATICCALL : Inst_6_1<"STATICCALL",
                      [(set GPR:$dst,
                       (int_evm_delegatecall GPR:$src1, GPR:$src2,
                        GPR:$src3, GPR:$src4, GPR:$src5, GPR:$src6))],
                    0xfa, 40>;
}

let hasSideEffects = 1, mayStore = 1 in {
defm CREATE2    : Inst_4_1<"CREATE2",
                           [(set GPR:$dst,
                             (int_evm_create2 GPR:$src1, GPR:$src2,
                              GPR:$src3, GPR:$src4))],
                           0xf5, 32000>;

defm REVERT     : Inst_2_0<"REVERT",
                           [(int_evm_revert GPR:$src1, GPR:$src2)],
                           0xfd, 0>;
}

defm INVALID    : Inst_0_0<"INVALID", [(int_evm_invalid)], 0xfe, 0>;

let mayStore = 1, hasSideEffects = 1 in {
defm SELFDESTRUCT : Inst_1_0<"SELFDESTRUCT",
                             [(int_evm_selfdestruct GPR:$src)],
                             0xff, 5000>;
}
//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//
let usesCustomInserter = 1, isCodeGenOnly = 1 in {
def Selectcc :
EVMPseudo<(outs GPR:$dst),
          (ins GPR:$lhs, GPR:$rhs, i256imm:$cc, GPR:$src, GPR:$src2),
          [(set i256:$dst,
            (EVMSelectcc i256:$lhs, i256:$rhs, imm:$cc, i256:$src, i256:$src2))]>;
}

let Defs = [SP], Uses = [SP] in {
def ADJCALLSTACKDOWN : EVMPseudo<(outs), (ins i256imm:$amt, i256imm:$amt2),
                               [(EVMCallseq_start timm:$amt, timm:$amt2)]>;
def ADJCALLSTACKUP : EVMPseudo<(outs), (ins i256imm:$amt1, i256imm:$amt2),
                            [(EVMCallseq_end timm:$amt1, timm:$amt2)]>;
}

let isCall = 1 in {
  // we split jump to subroutine into 2.
  def pJUMPSUBVOID_r :
    EVMPseudo<(outs), (ins brtarget:$dst, variable_ops), []>;

  def pJUMPSUB_r :
    EVMPseudo<(outs GPR:$rv), (ins brtarget:$dst, variable_ops), []>;

  def pJUMPSUB :
    EVMStackPseudo<(outs), (ins), []>;

  def pJUMPSUBV_r :
    EVMPseudo<(outs), (ins variable_ops), []>;
}

let isTerminator = 1, isBarrier = 1, isBranch = 1 in {
  def pJUMPTO_r  : EVMPseudo<(outs), (ins brtarget:$dst), []>;
  def pJUMPIF_r  : EVMPseudo<(outs), (ins GPR:$src, brtarget:$dst), []>;
  def pJUMPV   : EVMPseudo<(outs), (ins GPR:$src1, brtarget:$dst), []>;

  let isReturn = 1 in {
    def pRETURNSUB_TEMP_r  :
      EVMPseudo<(outs), (ins GPR:$src),
                [(EVMRetflag i256:$src)]>;
    def pRETURNSUBVOID_TEMP_r :
      EVMPseudo<(outs), (ins), [(EVMRetflag)]>;
    def pRETURNSUB:
      EVMPseudo<(outs), (ins), []>;

    // the version with return address embedded
    def pRETURNSUB_r  :
      EVMPseudo<(outs), (ins GPR:$ret, GPR:$src), []>;
    def pRETURNSUBVOID_r :
      EVMPseudo<(outs), (ins GPR:$ret), []>;
  }
}

// In pre-EIP615 we do a little bit differently (due to the difference).
// We make the n'th local to be at location FreeMemoryPointer - (n * 32).
let mayLoad = 1 in {
def pGETLOCAL_r : EVMPseudo<(outs GPR:$dst), (ins i256imm:$idx), []>;
}

let mayStore = 1 in {
def pPUTLOCAL_r : EVMPseudo<(outs), (ins GPR:$src, i256imm:$idx), []>;
}
// This is the placeholder for the incoming arguments.
let hasSideEffects = 1 in {
def pSTACKARG_r : EVMPseudo<(outs GPR:$reg), (ins i256imm:$idx),
                            [(set (i256 GPR:$reg), (EVMStackArg timm:$idx))]>;
def pSTACKARG   : EVMStackPseudo<(outs), (ins i256imm:$idx), []>;
}

// These instructions are used to manually adjust the framepointer in pre-EIP615.
let mayStore = 1 in {
let BaseName = "pADJFPUP" in {
def pADJFPUP_r : EVMPseudo<(outs), (ins i256imm:$size), []>;
def pADJFPUP   : EVMStackPseudo<(outs), (ins i256imm:$idx), []>;
}
let BaseName = "pADJFPDOWN" in {
def pADJFPDOWN_r : EVMPseudo<(outs), (ins i256imm:$size), []>;
def pADJFPDOWN : EVMStackPseudo<(outs), (ins i256imm:$size), []>;
}
}

// Pseudo MOVE instruction
def pMOVE_r : EVMPseudo<(outs GPR:$dst), (ins GPR:$src), []>;

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
          (pJUMPIF_r (SLT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SLE, bb:$BrDst),
          (pJUMPIF_r (ISZERO_r (SGT_r i256:$dst, i256:$src)), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_LT, bb:$BrDst),
          (pJUMPIF_r (LT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_LE, bb:$BrDst),
          (pJUMPIF_r (ISZERO_r (GT_r i256:$dst, i256:$src)), bb:$BrDst)>;

def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SGT, bb:$BrDst),
          (pJUMPIF_r (SGT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_SGE, bb:$BrDst),
          (pJUMPIF_r (ISZERO_r (SLT_r i256:$dst, i256:$src)), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_GT, bb:$BrDst),
          (pJUMPIF_r (GT_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_GE, bb:$BrDst),
          (pJUMPIF_r (ISZERO_r (LT_r i256:$dst, i256:$src)), bb:$BrDst)>;

def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_EQ, bb:$BrDst),
          (pJUMPIF_r (EQ_r i256:$dst, i256:$src), bb:$BrDst)>;
def : Pat<(EVMBrcc i256:$dst, i256:$src, EVM_CC_NE, bb:$BrDst),
          (pJUMPIF_r (ISZERO_r (EQ_r i256:$dst, i256:$src)), bb:$BrDst)>;

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

// Memory accesses
def : Pat<(EVMWrapper tglobaladdr:$in), (PUSH32_r tglobaladdr:$in)>;
def : Pat<(EVMWrapper tblockaddress:$blk), (PUSH32_r tblockaddress:$blk)>;
def : Pat<(brind bb:$dst), (pJUMPTO_r bb:$dst)>;

// Common Intrinsics
def : Pat<(i256 (int_evm_sload GPR:$addr)),
          (i256 (SLOAD_r GPR:$addr))>;
def : Pat<(int_evm_sstore GPR:$offset, GPR:$value),
          (SSTORE_r GPR:$offset, GPR:$value)>;

def : Pat<(int_evm_mload GPR:$addr), (MLOAD_r GPR:$addr)>;
def : Pat<(int_evm_mstore GPR:$offset, GPR:$value),
          (MSTORE_r GPR:$offset, GPR:$value)>;

def : Pat<(i256 (int_evm_shl GPR:$opnd1, GPR:$opnd2)),
          (SHL_r GPR:$opnd1, GPR:$opnd2)>;
def : Pat<(i256 (int_evm_shr GPR:$opnd1, GPR:$opnd2)),
          (SHR_r GPR:$opnd1, GPR:$opnd2)>;
def : Pat<(i256 (int_evm_sar GPR:$opnd1, GPR:$opnd2)),
          (SAR_r GPR:$opnd1, GPR:$opnd2)>;

// External calls
/*
def : Pat<(i256 (EVMCall (EVMWrapper tglobaladdr:$callee))),
          (pJUMPSUB_r tglobaladdr:$callee)>;
def : Pat<(i256 (EVMCall (EVMWrapper texternalsym:$callee))),
          (pJUMPSUB_r texternalsym:$callee)>;

// VOID version of function call.
def : Pat<(EVMCallVoid (EVMWrapper tglobaladdr:$callee)),
          (pJUMPSUBVOID_r tglobaladdr:$callee)>;
def : Pat<(EVMCall (EVMWrapper texternalsym:$callee)),
          (pJUMPSUBVOID_r texternalsym:$callee)>;
          */

//===----------------------------------------------------------------------===//
// Standard extensions
//===----------------------------------------------------------------------===//

