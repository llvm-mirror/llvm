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

//===----------------------------------------------------------------------===//
// EVM specific DAG Nodes.
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//
def SDT_EVMCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def SDT_EVMArithBinary :
SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>]>;

def SDT_EVMArithTernary :
SDTypeProfile<1, 3,
              [SDTCisInt<0>, SDTCisSameAs<0, 1>,
               SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>]>;

def SDT_EVMTwoOpnd :
SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;

def EVMCall :
SDNode<"EVMISD::CALL", SDT_EVMCall,
       [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;

def EVMReturn :
SDNode<"EVMISD::RETURN", SDTNone, [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def EVMMod :
SDNode<"EVMISD::MOD", SDT_EVMArithBinary, []>;

def EVMSmod :
SDNode<"EVMISD::SMOD", SDT_EVMArithBinary, []>;

def EVMAddMod :
SDNode<"EVMISD::ADDMOD", SDT_EVMArithTernary, []>;

def EVMExp:
SDNode<"EVMISD::EXP", SDT_EVMArithBinary, []>;

def EVMSignextend :
SDNode<"EVMISD::SIGNEXTEND", SDT_EVMArithBinary, []>;

def EVMByte :
SDNode<"EVMISD::BYTE", SDT_EVMArithBinary, []>;

def EVMJump :
SDNode<"EVMISD::JUMP", SDTNone, []>;

def EVMJumpi :
SDNode<"EVMISD::JUMPI", SDTNone, []>;

def EVMSwap :
SDNode<"EVMISD::SWAP", SDT_EVMTwoOpnd, []>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

// Arithmetic instructions ////////////////////////////////////////////////////
defm ADD  : BinaryInst<add,  "ADD",  0x01, 3>;
defm MUL  : BinaryInst<mul,  "MUL",  0x02, 5>;
defm SUB  : BinaryInst<sub,  "SUB",  0x03, 3>;
defm DIV  : BinaryInst<udiv, "DIV",  0x04, 5>;
defm SDIV : BinaryInst<sdiv, "SDIV", 0x05, 5>;

// LLVM does not have those operators
defm MOD    : Inst_2_1<"MOD",    [], 0x06, 5>;
defm SMOD   : Inst_2_1<"SMOD",   [], 0x07, 5>;
defm ADDMOD : Inst_2_1<"ADDMOD", [], 0x08, 5>;
defm MULMOD : Inst_2_1<"MULMOD", [], 0x09, 5>;
defm EXP    : Inst_2_1<"EXP",
                       [(set AllRegs:$dst, (int_evm_exp AllRegs:$src1, AllRegs:$src2))],
                       0x0a, 10>;
defm SIGNEXTEND : Inst_2_1<"SIGNEXTEND", [], 0x0b, 5>; // TODO: add DAG

// Comparison instructions ////////////////////////////////////////////////////
let isCompare = 1 in {
defm LT  : ComparisonInst<SETULT, "LT", 0x10, 3>;
defm GT  : ComparisonInst<SETUGT, "GT", 0x11, 3>;
defm SLT : ComparisonInst<SETLT, "SLT", 0x12, 3>;
defm SGT : ComparisonInst<SETGT, "SGT", 0x13, 3>;
defm EQ  : ComparisonInst<SETEQ, "SEQ", 0x14, 3>;
}
// Bitwise instructions ///////////////////////////////////////////////////////
defm AND : BinaryInst<and, "AND", 0x16, 3>;
defm OR  : BinaryInst<or,  "OR",  0x17, 3>;
defm XOR : BinaryInst<xor, "XOR", 0x18, 3>;

// LLVM does not generate "NOT" nodes.
let isCompare = 1 in {
defm NOT  :  Inst_1_1<"NOT" , [(set AllRegs:$dst, (setcc AllRegs:$src, 0, SETEQ))], 0x19, 3>;
}
defm BYTE :  Inst_2_1<"BYTE", [], 0x20, 3>;

let mayLoad = 1, mayStore = 0 in {
defm SHA3      : Inst_2_1<"SHA3",
                          [(set AllRegs:$dst, (int_evm_sha3 AllRegs:$src1, AllRegs:$src2))],
                          0x21, 30>;
}

defm ADDRESS   : Inst_0_1<"ADDRESS",
                          [(set AllRegs:$dst, (int_evm_address))],
                          0x30, 2>;
defm BALANCE   : Inst_1_1<"BALANCE",
                          [(set AllRegs:$dst, (int_evm_balance AllRegs:$src))],
                          0x31, 400>;
defm ORIGIN    : Inst_0_1<"ORIGIN",
                          [(set AllRegs:$dst, (int_evm_origin))],
                          0x32, 2>;
defm CALLER    : Inst_0_1<"CALLER",
                          [(set AllRegs:$dst, (int_evm_caller))],
                          0x33, 2>;
defm CALLVALUE : Inst_0_1<"CALLVALUE",
                          [(set AllRegs:$dst, (int_evm_callvalue))],
                          0x34, 2>;

let hasSideEffects = 1 in {
defm CALLDATALOAD : Inst_1_1<"CALLDATALOAD",
                             [(set AllRegs:$dst, (int_evm_calldataload AllRegs:$src))],
                             0x35, 3>;
}

defm CALLDATASIZE : Inst_0_1<"CALLDATASIZE",
                             [(set AllRegs:$dst, (int_evm_calldatasize))],
                             0x36, 2>;

let mayStore = 1, hasSideEffects = 1 in {
defm CALLDATACOPY : Inst_3_0<"CALLDATACOPY",
                             [(int_evm_calldatacopy
                               AllRegs:$src1, AllRegs:$src2, AllRegs:$src3)],
                             0x37, 3>;
}

defm CODESIZE     : Inst_0_1<"CODESIZE",
                             [(set AllRegs:$dst, (int_evm_codesize))],
                             0x38, 2>;

let hasSideEffects = 1 in {

let mayStore = 1 in {
defm CODECOPY     : Inst_3_0<"CODECOPY",
                             [(int_evm_codecopy
                               AllRegs:$src1, AllRegs:$src2, AllRegs:$src3)],
                             0x39, 3>;
}
defm GASPRICE     : Inst_0_1<"GASPRICE",
                             [(set AllRegs:$dst, (int_evm_gasprice))],
                             0x3a, 2>;
defm EXTCODESIZE  : Inst_1_1<"EXTCODESIZE",
                             [(set AllRegs:$dst, (int_evm_extcodesize AllRegs:$src))],
                             0x3b, 700>;
}

let mayStore = 1, hasSideEffects = 1 in {
defm EXTCODECOPY : RSInst<(outs),
                          (ins AllRegs:$src1, AllRegs:$src2, AllRegs:$src3, AllRegs:$src4),
                          (outs), (ins),
                          [(int_evm_extcodecopy AllRegs:$src1, AllRegs:$src2,
                                                AllRegs:$src3, AllRegs:$src4)],
                          "EXTCODECOPY \t$src1, $src2, $src3, $src4",
                          "EXTCODECOPY", 0x3c, 700>;  // TODO: verify
}

//let Predicates = [HasEIP211] in {
let hasSideEffects = 1 in {
defm RETURNDATASIZE : Inst_0_1<"RETURNDATASIZE",
                               [(set AllRegs:$dst, (int_evm_returndatasize))],
                               0x3d, 2>;

let mayStore = 1 in {
defm RETURNDATACOPY : Inst_3_0<"RETURNDATACOPY",
                               [(int_evm_returndatacopy
                                 AllRegs:$src1, AllRegs:$src2, AllRegs:$src3)],
                               0x3e, 3>;
}
}
//}

defm BLOCKHASH  : Inst_1_1<"BLOCKHASH",
                           [(set AllRegs:$dst, (int_evm_blockhash AllRegs:$src))],
                           0x40, 20>;
defm COINBASE   : Inst_0_1<"COINBASE",
                           [(set AllRegs:$dst, (int_evm_coinbase))],
                           0x41, 2>;
defm TIMESTAMP  : Inst_0_1<"TIMESTAMP",
                           [(set AllRegs:$dst, (int_evm_timestamp))],
                           0x42, 2>;
defm NUMBER     : Inst_0_1<"NUMBER",
                           [(set AllRegs:$dst, (int_evm_number))],
                           0x43, 2>;
defm DIFFICULTY : Inst_0_1<"DIFFICULTY",
                           [(set AllRegs:$dst, (int_evm_difficulty))],
                           0x44, 2>;
defm GASLIMIT   : Inst_0_1<"GASLIMIT",
                           [(set AllRegs:$dst, (int_evm_gaslimit))],
                           0x45, 2>;


let hasSideEffects = 1 in {
let mayLoad = 1 in {
defm MLOAD  : Inst_1_1<"MLOAD",  [], 0x51, 3>;
}

let mayStore = 1 in {
defm MSTORE : Inst_2_0<"MSTORE", [], 0x52, 3>;
//defm MSTORE8 : ??
}

defm SLOAD  : Inst_1_1<"SLOAD",
                       [(set AllRegs:$dst, (int_evm_sload AllRegs:$src))],
                       0x54, 200>;

defm SSTORE : Inst_2_0<"SSTORE",
                       [(int_evm_sstore AllRegs:$src1, AllRegs:$src2)],
                       0x55, 200000>;
}

let isBranch = 1 in {
defm JUMP     : Inst_1_0<"JUMP",     [], 0x56, 8>;
defm JUMPI    : Inst_2_0<"JUMPI",    [], 0x57, 10>;
}

defm PC       : Inst_0_1<"PC",
                         [(set AllRegs:$dst, (int_evm_getpc))],
                         0x58, 2>;

defm MSIZE    : Inst_0_1<"MSIZE",
                         [(set AllRegs:$dst, (int_evm_msize))],
                         0x59, 2>;
defm GAS      : Inst_0_1<"GAS",
                         [(set AllRegs:$dst, (int_evm_gas))],
                         0x5a, 2>;
defm JUMPDEST : Inst_0_0<"JUMPDEST", [], 0x5b, 1>;

//let Predicates = [HasEIP615] in {

let isBranch = 1 in {
defm JUMPTO : Inst_1_0<"JUMPTO", [], 0xb0, 8>;
//defm JUMPIF : Inst_2_0<"JUMPIF", [], 0xb1, 10>; // TODO: argument type is 4 bytes.
}

//}

let mayStore = 1, hasSideEffects = 1 in {
defm CREATE : Inst_3_1<"CREATE",
                       [(set AllRegs:$dst, (int_evm_create
                                            AllRegs:$src1, AllRegs:$src2, AllRegs:$src3))],
                       0xf0, 32000>;
}

let isCall = 1 in {
// We should compute the cost of CALL separately.
//defm CALL   : Inst_7_1<"CALL", [], 0xf1, 0>;
//defm CALLNODE   : Inst_7_1<"CALLNODE", [], 0xf2, 0>;
}

let isReturn = 1 in {
defm RETURN : Inst_2_0<"RETURN", [], 0xf3, 0>;
}

let isCall = 1 in {
//defm DELEGATECALL : Inst_6_1<"DELEGATECALL", [], 0xf4, 0>;
//defm CALLBLACKBOX : Inst_6_1<"CALLBLACKBLOX", [], 0xf5, 0>;
//defm STATICCALL : Inst_7_1<"STATICCALL", [], 0xfa, 40>;
}

let hasSideEffects = 1 in {
let mayStore = 1 in {
defm CREATE2    : Inst_3_1<"CREATE2",
                           [(set AllRegs:$dst,
                             (int_evm_create2
                              AllRegs:$src1, AllRegs:$src2, AllRegs:$src3))],
                           0xfb, 32000>;
// defm TXEXEGAS // TODO

defm REVERT     : Inst_2_1<"REVERT",
                           [(set AllRegs:$dst, (int_evm_revert AllRegs:$src1, AllRegs:$src2))],
                           0xfd, 0>;
}
}

defm INVALID    : Inst_0_0<"INVALID", [], 0xfe, 0>;

let mayStore = 1, hasSideEffects = 1 in {
defm SELFDESTRUCT : Inst_1_0<"SELFDESTRUCT",
                             [(int_evm_selfdestruct AllRegs:$src)],
                             0xff, 5000>;
}

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//





//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Standard extensions
//===----------------------------------------------------------------------===//

