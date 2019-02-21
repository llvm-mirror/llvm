//===-- EVMInstrFormats.td - EVM Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def HasEIP211 : Predicate<"!Subtarget->hasEIP211()">;
def HasEIP615 : Predicate<"!Subtarget->hasEIP615()">;

class StackRel;
def getStackOpcode : InstrMapping {
  let FilterClass = "StackRel";
  let RowFields = ["BaseName"];
  let ColFields = ["StackBased"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

class EVMInstruction<bits<8> inst, string asmstr, string stack, int cost>
    : StackRel, Instruction {
  bit<8> Inst = inst;
  let Namespace = "EVM";
  let AsmString = asmstr;
  let Pattern = [];

  let gas = cost;
}

// Our register-based instructions
class Inst<dag oops, dag iops, list<dag> pattern, string stack,
           string asmstr = "", bits<8> inst = -1, int cost = -1>
    : EVMInstruction<inst, asmstr, stack, cost> {
  dag OutOperandList = oops;
  dag InOperandList  = iops;
  let Pattern        = pattern;
  let Defs           = [REG_ARGS];
}

// Both Register and Stack based instructions
multiclass RSInst<dag outs_r, dag ins_r, dag outs_s, dag ins_s,
                list<dag> pattern_r, string asmstr_r, string asmstr_s,
                bits<8> inst, int cost> {
  let isCodeGenOnly = 1 in
  def _r : Inst<outs_r, ins_r, pattern_r, "false", asmstr_r, inst, cost>;
  let BaseName = NAME in
  def _s : Inst<outs_s, ins_s, [], "true", asmstr_s, inst, cost>;
}

def I256IMM : Operand<GPR256> {
  // TODO: implement Imm256 print
  let PrintMethod = "printImm256Operand";
}

def ADDR : Operand<GPR256> {
  // TODO: implement address print
  let PrintMethod = "printAddressOperand";
}

// Alpha: number of additional items placed on the stack. (return value)
// Delta: items removed from stack. (arguments)
// i : int256, 0 : none

// delta = 0, alpha = 1
class Inst_0_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR256:$dst), (ins), (outs), (ins), pattern,
             !strconcat(name, "\t$dst")),
             inst, cost> {
}


// delta = 0, alpha = 0
class Inst_0_0<string name, bits<8> inst, int cost>
    : RSInst<(outs), (ins), (outs), (ins), [], name, inst, cost> {
}

// delta = 1, alpha = 1
class Inst_1_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR256:$dst), (ins GPR256:$src),         // register based
             (outs), (ins),                             // stack based
             pattern,
             !strconcat(name, "\t$dst, $src")),         // reg asm
             name,                                      // stack asm
             inst, cost> {
}

class ArithmaticUnaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_1_1<name, [(set GPR256:$dst, (node GPR256:$src))], name, inst, cost>;

// delta = 2, alpha = 1
class Inst_2_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR256:$dst), (ins GPR256:$lhs, GPR256:$rhs),         //register based
             (outs), (ins),                                        // stack based
             pattern,
             !strconcat(name, "\t$dst, $lhs, $rhs")),              // reg asm
             name,                                                 // stack asm
             inst, cost> {
}

class BinaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set GPR256:$dst, (node GPR256:$lhs, GPR256:$rhs))], inst, cost>;

class ComparisonInst<CondCode cond, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set GPR256:$dst, (setcc GPR256:$lhs, GPR256:$rhs, cond))], inst, cost>;


// delta = 3, alpha = 1
class Inst_3_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins GPR256:$src1, GPR256:src2, GPR256:src3),
             (outs), (ins), pattern,
             !strconcat(name, " \t$src1, $src2, $src3")),
             name, inst, cost>;


