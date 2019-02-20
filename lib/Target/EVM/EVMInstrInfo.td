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

def HasEIP211 : Predicate<"!Subtarget->hasEIP211()">;

def HasEIP615 : Predicate<"!Subtarget->hasEIP615()">;


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
def ADD  : ArithmaticBinaryInst<add,  "ADD",  0x01, 3>;
def MUL  : ArithmaticBinaryInst<mul,  "MUL",  0x02, 5>;
def SUB  : ArithmaticBinaryInst<sub,  "SUB",  0x03, 3>;
def DIV  : ArithmaticBinaryInst<udiv, "DIV",  0x04, 5>;
def SDIV : ArithmaticBinaryInst<sdiv, "SDIV", 0x05, 5>;

// LLVM does not have those operators
def MOD    : Inst_2_1<"MOD",    [], 0x06, 5>;
def SMOD   : Inst_2_1<"SMOD",   [], 0x07, 5>;
def ADDMOD : Inst_2_1<"ADDMOD", [], 0x08, 5>;
def MULMOD : Inst_2_1<"MULMOD", [], 0x09, 5>;
def EXP    : Inst_2_1<"EXP",    [], 0x0a, 10>;
def SIGNEXTEND : Inst_2_1<"signextend", 0x0b, [], 5>; // TODO: add DAG

// Comparison instructions ////////////////////////////////////////////////////
def LT  : ComparisonInst<SETULT, "LT", 0x10, 3>;
def GT  : ComparisonInst<SETUGT, "GT", 0x11, 3>;
def SLT : ComparisonInst<SETLT, "SLT", 0x12, 3>;
def SGT : ComparisonInst<SETGT, "SGT", 0x13, 3>;
def EQ  : ComparisonInst<SETEQ, "SEQ", 0x14, 3>;

// Bitwise instructions ///////////////////////////////////////////////////////
def AND : ArithmaticBinaryInst<and, "AND", 0x16, 3>;
def OR  : ArithmaticBinaryInst<or,  "OR,   0x17, 3>;
def XOR : ArithmaticBinaryInst<xor, "XOR", 0x18, 3>;

// LLVM does not generate "NOT" nodes.
def NOT  :  Inst_1_1<"NOT", 0x19, [], 3>;
def BYTE : Inst_2_1<"BYTE", 0x20, [], 3>;

def SHA3      : Inst_2_1<"SHA3",      [], 0x21, 30>;
def ADDRESS   : Inst_0_1<"ADDRESS",   [], 0x30, 2>;
def BALANCE   : Inst_1_1<"BALANCE",   [], 0x31, 400>;
def ORIGIN    : Inst_0_1<"ORIGIN",    [], 0x32, 2>;
def CALLER    : Inst_0_1<"CALLER",    [], 0x33, 2>;
def CALLVALUE : Inst_0_1<"CALLVALUE", [], 0x34, 2>;

def CALLDATALOAD : Inst_1_1<"CALLDATALOAD", [], 0x35, 3>;

def CALLDATASIZE : Inst_0_1<"CALLDATASIZE", [], 0x36, 2>;
def CALLDATACOPY : TernaryInst<"CALLDATACOPY", 0x37, 3>;
def CODESIZE     : StandaloneInst<"CODESIZE", 0x38, 2>;
def CODECOPY     : TernaryInst<"CODECOPY", 0x39, 3>;
def GASPRICE     : Inst_0_1<"GASPRICE",     [], 0x3a, 2>;
def EXTCODESIZE  : Inst_1_1<"EXTCODESIZE",  [], 0x3b, 700>;

def EXTCODECOPY : RSInst<(outs), (ins I256:$src1, I256:src2, I256:src3, I256:src4),
                         (outs), (ins), [],
                         "EXTCODECOPY \t$src1, $src2, $src3, $src4")),
                         "EXTCODECOPY", 0x3c, 700>;  // TODO: verify

let Predicates = [HasEIP211] in {
def RETURNDATASIZE : StandaloneInst<"RETURNDATASIZE", 0x3d, 2>;
def RETURNDATACOPY : TernaryInst<"RETURNDATACOPY", 0x3e, 3>;
}

def BLOCKHASH  : Inst_1_1<"BLOCKHASH",  [], 0x40, 20>;
def COINBASE   : Inst_0_1<"COINBASE",   [], 0x41, 2>;
def TIMESTAMP  : Inst_0_1<"TIMESTAMP",  [], 0x42, 2>;
def NUMBER     : Inst_0_1<"NUMBER",     [], 0x43, 2>;
def DIFFICULTY : Inst_0_1<"DIFFICULTY", [], 0x44, 2>;
def GASLIMIT   : Inst_0_1<"GASLIMIT",   [], 0x45, 2>;


def MLOAD  : Inst_1_1<"MLOAD",  [], 0x51, 3>;
def MSTORE : Inst_2_0<"MSTORE", [], 0x52, 3>;

//def MSTORE8 : ??

def SLOAD  : Inst_1_1<"SLOAD",      [], 0x54, 200>;
def SSTORE : Inst_2_0<"SSTORE",     [], 0x55, 200000>;

def JUMP     : Inst_1_0<"JUMP",     [], 0x56, 8>;
def JUMPI    : Inst_2_0<"JUMPI",    [], 0x57, 10>;
def PC       : Inst_0_1<"PC",       [], 0x58, 2>;
def MSIZE    : Inst_0_1<"MSIZE",    [], 0x59, 2>;
def GAS      : Inst_0_1<"GAS",      [], 0x5a, 2>;
def JUMPDEST : Inst_0_0<"JUMPDEST", [], 0x5b, 1>;

let Predicates = [HasEIP615] in {
def JUMPTO : Inst_1_0<"JUMPTO", [], 0xb0, 8>;
//def JUMPIF : Inst_2_0<"JUMPIF", [], 0xb1, 10>; // TODO: argument type is 4 bytes.

}

def CREATE : Inst_3_1<"CREATE", [], 0xf0, 32000>;
//def CALL   : Inst_7_1<"CALL", [], 0xf1, ??>; // We should compute the cost of CALL separately.
//def CALLNODE   : Inst_7_1<"CALLNODE", [], 0xf2, 0>;
def RETURN : Inst_2_0<"RETURN", [], 0xf3, 0>;
def DELEGATECALL : Inst_6_1<"DELEGATECALL", [], 0xf4, 0>;
// def CALLBLACKBOX : Inst_6_1<"CALLBLACKBLOX", [], 0xf5, 0>;

def STATICCALL : Inst_7_1<"STATICCALL", [], 0xfa, 40>;
def CREATE2    : Inst_3_1<"CREATE2", [], 0xfb, 32000>;
// def TXEXEGAS // TODO
def REVERT     : Inst_2_1<"REVERT", [], 0xfd, 0>;
def INVALID    : Inst_0_0<"INVALID", [], 0xfe, 0>;
def SELFDESTRUCT : Inst_1_0<"SELFDESTRUCT", [], 0xff, 5000>;

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Standard extensions
//===----------------------------------------------------------------------===//

