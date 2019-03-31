//===-- EVMInstrFormats.td - EVM Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//def HasEIP211 : Predicate<"!Subtarget->hasEIP211()">;
//def HasEIP615 : Predicate<"!Subtarget->hasEIP615()">;

class StackRel;
def getStackOpcode : InstrMapping {
  let FilterClass = "StackRel";
  let RowFields = ["BaseName"];
  let ColFields = ["StackBased"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

// Our register-based instructions
class EVMInst<dag oops, dag iops, list<dag> pattern, string stack,
           string asmstr = "", bits<8> inst, int cost = -1>
    : StackRel, Instruction {

  field bits<8> Inst = inst;
  string StackBased = stack;
  string BaseName    = NAME;
  dag OutOperandList = oops;
  dag InOperandList  = iops;

  let Namespace      = "EVM";
  let AsmString      = asmstr;
  let Pattern        = pattern;

  let mayLoad        = 0;
  let mayStore       = 0;
  let hasSideEffects = 0;
}

// Both Register and Stack based instructions
multiclass RSInst<dag outs_r, dag ins_r,
                list<dag> pattern_r, string asmstr_r, string asmstr_s,
                bits<8> inst, int cost> {
  let isCodeGenOnly = 1 in
    def _r : EVMInst<outs_r, ins_r, pattern_r, "false", asmstr_r, inst, cost>;
  let BaseName = NAME in
    def "" : EVMInst<(outs), (ins), [], "true", asmstr_s, inst, cost>;
}


class EVMPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : EVMInst<outs, ins, pattern, "false", asmstr, 0>
{
  let isCodeGenOnly = 1;
}


def I256Imm : Operand<i256> {
  // TODO: implement Imm256 print
  //let PrintMethod = "printImm256Operand";
}

// The memory size of EVM is 1024 elements with 256 bit each.
def AddrImm :
PatLeaf<(i256 imm), [{ return isUInt<10>(N->getZExtValue()); }]>;

def EVMaddr : Operand<i256> {
  let PrintMethod = "printAddrOperand";
}

def brtarget : Operand<OtherVT> {
  // TODO: implement target print
  //let PrintMethod = "printBrTargetOperand";
}

// Alpha: number of additional items placed on the stack. (return value)
// Delta: items removed from stack. (arguments)
// i : int256, 0 : none

// delta = 0, alpha = 1
multiclass Inst_0_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR:$dst), (ins), pattern,
             !strconcat(name, "\t$dst"), name,
             inst, cost>;


// delta = 0, alpha = 0
multiclass Inst_0_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins), pattern, name, name, inst, cost>;

// delta = 1, alpha = 0
multiclass Inst_1_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins GPR:$src),     // register based
             pattern,
             !strconcat(name, "\t$src"),    // reg asm
             name,                          // stack asm
             inst, cost>;

// delta = 1, alpha = 1
multiclass Inst_1_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR:$dst), (ins GPR:$src),     // register based
             pattern,
             !strconcat(name, "\t$dst = $src"),          // reg asm
             name,                                      // stack asm
             inst, cost>;

multiclass UnaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_1_1<name, [(set GPR:$dst, (node GPR:$src))], inst, cost>;

// delta = 2, alpha = 0
multiclass Inst_2_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins GPR:$src1, GPR:$src2),   //register based
             pattern,
             !strconcat(name, "\t$src1, $src2"),       // reg asm
             name,                                     // stack asm
             inst, cost>;


// delta = 2, alpha = 1
multiclass Inst_2_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR:$dst), (ins GPR:$src1, GPR:$src2),   //register based
             pattern,
             !strconcat(name, "\t$dst = $src1, $src2"),               // reg asm
             name,                                                 // stack asm
             inst, cost>;

multiclass BinaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set GPR:$dst, (node GPR:$src1, GPR:$src2))], inst, cost>;

multiclass ComparisonInst<CondCode cond, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set GPR:$dst, (setcc GPR:$src1, GPR:$src2, cond))], inst, cost>;

// delta = 3, alpha = 0
multiclass Inst_3_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins GPR:$src1, GPR:$src2, GPR:$src3),
             pattern,
             !strconcat(name, " \t$src1, $src2, $src3"),
             name, inst, cost>;

// delta = 3, alpha = 1
multiclass Inst_3_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs GPR:$dst), (ins GPR:$src1, GPR:$src2, GPR:$src3),
             pattern,
             !strconcat(name, " \t$dst = $src1, $src2, $src3"),
             name, inst, cost>;

// delta = 6, alpha = 1
// TODO
