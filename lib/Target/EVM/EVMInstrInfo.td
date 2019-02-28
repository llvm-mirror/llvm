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
defm EXP    : Inst_2_1<"EXP",    [], 0x0a, 10>;
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

defm SHA3      : Inst_2_1<"SHA3",      [], 0x21, 30>;
defm ADDRESS   : Inst_0_1<"ADDRESS",   [], 0x30, 2>;
defm BALANCE   : Inst_1_1<"BALANCE",   [], 0x31, 400>;
defm ORIGIN    : Inst_0_1<"ORIGIN",    [], 0x32, 2>;
defm CALLER    : Inst_0_1<"CALLER",    [], 0x33, 2>;
defm CALLVALUE : Inst_0_1<"CALLVALUE", [], 0x34, 2>;

defm CALLDATALOAD : Inst_1_1<"CALLDATALOAD", [], 0x35, 3>;

defm CALLDATASIZE : Inst_0_1<"CALLDATASIZE", [], 0x36, 2>;
defm CALLDATACOPY : Inst_3_0<"CALLDATACOPY", [], 0x37, 3>;
defm CODESIZE     : Inst_0_0<"CODESIZE",     [], 0x38, 2>;
defm CODECOPY     : Inst_3_0<"CODECOPY",     [], 0x39, 3>;
defm GASPRICE     : Inst_0_1<"GASPRICE",     [], 0x3a, 2>;
defm EXTCODESIZE  : Inst_1_1<"EXTCODESIZE",  [], 0x3b, 700>;

defm EXTCODECOPY : RSInst<(outs), (ins AllRegs:$src1, AllRegs:$src2, AllRegs:$src3, AllRegs:$src4),
                          (outs), (ins), [],
                          "EXTCODECOPY \t$src1, $src2, $src3, $src4",
                          "EXTCODECOPY", 0x3c, 700>;  // TODO: verify

//let Predicates = [HasEIP211] in {
defm RETURNDATASIZE : Inst_0_0<"RETURNDATASIZE", [], 0x3d, 2>;
defm RETURNDATACOPY : Inst_3_0<"RETURNDATACOPY", [], 0x3e, 3>;
//}

defm BLOCKHASH  : Inst_1_1<"BLOCKHASH",  [], 0x40, 20>;
defm COINBASE   : Inst_0_1<"COINBASE",   [], 0x41, 2>;
defm TIMESTAMP  : Inst_0_1<"TIMESTAMP",  [], 0x42, 2>;
defm NUMBER     : Inst_0_1<"NUMBER",     [], 0x43, 2>;
defm DIFFICULTY : Inst_0_1<"DIFFICULTY", [], 0x44, 2>;
defm GASLIMIT   : Inst_0_1<"GASLIMIT",   [], 0x45, 2>;


let hasSideEffects = 1 in {
let mayLoad = 1 in {
defm MLOAD  : Inst_1_1<"MLOAD",  [], 0x51, 3>;
}

let mayStore = 1 in {
defm MSTORE : Inst_2_0<"MSTORE", [], 0x52, 3>;
//defm MSTORE8 : ??
}

let mayLoad = 1 in {
defm SLOAD  : Inst_1_1<"SLOAD",      [], 0x54, 200>;
}

let mayStore = 1 in {
defm SSTORE : Inst_2_0<"SSTORE",     [], 0x55, 200000>;
}
}

let isBranch = 1 in {
defm JUMP     : Inst_1_0<"JUMP",     [], 0x56, 8>;
defm JUMPI    : Inst_2_0<"JUMPI",    [], 0x57, 10>;
}

defm PC       : Inst_0_1<"PC",       [], 0x58, 2>;

defm MSIZE    : Inst_0_1<"MSIZE",    [], 0x59, 2>;
defm GAS      : Inst_0_1<"GAS",      [], 0x5a, 2>;
defm JUMPDEST : Inst_0_0<"JUMPDEST", [], 0x5b, 1>;

//let Predicates = [HasEIP615] in {

let isBranch = 1 in {
defm JUMPTO : Inst_1_0<"JUMPTO", [], 0xb0, 8>;
//defm JUMPIF : Inst_2_0<"JUMPIF", [], 0xb1, 10>; // TODO: argument type is 4 bytes.
}

//}

defm CREATE : Inst_3_1<"CREATE", [], 0xf0, 32000>;

let isCall = 1 in {
//defm CALL   : Inst_7_1<"CALL", [], 0xf1, ??>; // We should compute the cost of CALL separately.
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

defm CREATE2    : Inst_3_1<"CREATE2", [], 0xfb, 32000>;
// defm TXEXEGAS // TODO
defm REVERT     : Inst_2_1<"REVERT", [], 0xfd, 0>;
defm INVALID    : Inst_0_0<"INVALID", [], 0xfe, 0>;

defm SELFDESTRUCT : Inst_1_0<"SELFDESTRUCT", [], 0xff, 5000>;

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

