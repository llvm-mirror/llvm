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
  let Defs           = [REG_ARGS];

  let mayLoad        = 0;
  let mayStore       = 0;
  let hasSideEffects = 0;
}

// Both Register and Stack based instructions
multiclass RSInst<dag outs_r, dag ins_r, dag outs_s, dag ins_s,
                list<dag> pattern_r, string asmstr_r, string asmstr_s,
                bits<8> inst, int cost> {
  let isCodeGenOnly = 1 in
    def _r : EVMInst<outs_r, ins_r, pattern_r, "false", asmstr_r, inst, cost>;
  let BaseName = NAME in
    def _s : EVMInst<outs_s, ins_s, [], "true", asmstr_s, inst, cost>;
}

def I256IMM : Operand<i256> {
  // TODO: implement Imm256 print
  let PrintMethod = "printImm256Operand";
}

def ADDR : Operand<i256> {
  // TODO: implement address print
  let PrintMethod = "printAddressOperand";
}

// Alpha: number of additional items placed on the stack. (return value)
// Delta: items removed from stack. (arguments)
// i : int256, 0 : none

// delta = 0, alpha = 1
multiclass Inst_0_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs AllRegs:$dst), (ins), (outs), (ins), pattern,
             !strconcat(name, "\t$dst"), name,
             inst, cost>;


// delta = 0, alpha = 0
multiclass Inst_0_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins), (outs), (ins), pattern, name, name, inst, cost>;

// delta = 1, alpha = 0
multiclass Inst_1_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins AllRegs:$src),     // register based
             (outs), (ins),                 // stack based
             pattern,
             !strconcat(name, "\t$src"),    // reg asm
             name,                          // stack asm
             inst, cost>;


// delta = 1, alpha = 1
multiclass Inst_1_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs AllRegs:$dst), (ins AllRegs:$src),     // register based
             (outs), (ins),                             // stack based
             pattern,
             !strconcat(name, "\t$dst = $src"),          // reg asm
             name,                                      // stack asm
             inst, cost>;

multiclass UnaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_1_1<name, [(set AllRegs:$dst, (node AllRegs:$src))], inst, cost>;

// delta = 2, alpha = 0
multiclass Inst_2_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins AllRegs:$src0, AllRegs:$src1),   //register based
             (outs), (ins),                            // stack based
             pattern,
             !strconcat(name, "\t$src0, $src1"),       // reg asm
             name,                                     // stack asm
             inst, cost>;


// delta = 2, alpha = 1
multiclass Inst_2_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs AllRegs:$dst), (ins AllRegs:$src0, AllRegs:$src1),   //register based
             (outs), (ins),                                        // stack based
             pattern,
             !strconcat(name, "\t$dst = $src0, $src1"),               // reg asm
             name,                                                 // stack asm
             inst, cost>;

multiclass BinaryInst<SDNode node, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set AllRegs:$dst, (node AllRegs:$src0, AllRegs:$src1))], inst, cost>;

multiclass ComparisonInst<CondCode cond, string name, bits<8> inst, int cost>
    : Inst_2_1<name, [(set AllRegs:$dst, (setcc AllRegs:$src0, AllRegs:$src1, cond))], inst, cost>;

// delta = 3, alpha = 0
multiclass Inst_3_0<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins AllRegs:$dst, AllRegs:$src1, AllRegs:$src2, AllRegs:$src3),
             (outs), (ins), pattern,
             !strconcat(name, " \t$dst = $src1, $src2, $src3"),
             name, inst, cost>;

// delta = 3, alpha = 1
multiclass Inst_3_1<string name, list<dag> pattern, bits<8> inst, int cost>
    : RSInst<(outs), (ins AllRegs:$src1, AllRegs:$src2, AllRegs:$src3),
             (outs), (ins), pattern,
             !strconcat(name, " \t$src1, $src2, $src3"),
             name, inst, cost>;

// delta = 6, alpha = 1
// TODO
