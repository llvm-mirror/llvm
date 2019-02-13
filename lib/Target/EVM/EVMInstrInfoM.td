//===-- EVMInstrInfoM.td - EVM 'M' instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the EVM instructions from the standard 'M', Integer
// Multiplication and Division instruction set extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// EVM specific DAG Nodes.
//===----------------------------------------------------------------------===//

def evm_divw  : SDNode<"EVMISD::DIVW",  SDTIntBinOp>;
def evm_divuw : SDNode<"EVMISD::DIVUW", SDTIntBinOp>;
def evm_remuw : SDNode<"EVMISD::REMUW", SDTIntBinOp>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtM] in {
def MUL     : ALU_rr<0b0000001, 0b000, "mul">;
def MULH    : ALU_rr<0b0000001, 0b001, "mulh">;
def MULHSU  : ALU_rr<0b0000001, 0b010, "mulhsu">;
def MULHU   : ALU_rr<0b0000001, 0b011, "mulhu">;
def DIV     : ALU_rr<0b0000001, 0b100, "div">;
def DIVU    : ALU_rr<0b0000001, 0b101, "divu">;
def REM     : ALU_rr<0b0000001, 0b110, "rem">;
def REMU    : ALU_rr<0b0000001, 0b111, "remu">;
} // Predicates = [HasStdExtM]

let Predicates = [HasStdExtM, IsRV64] in {
def MULW    : ALUW_rr<0b0000001, 0b000, "mulw">;
def DIVW    : ALUW_rr<0b0000001, 0b100, "divw">;
def DIVUW   : ALUW_rr<0b0000001, 0b101, "divuw">;
def REMW    : ALUW_rr<0b0000001, 0b110, "remw">;
def REMUW   : ALUW_rr<0b0000001, 0b111, "remuw">;
} // Predicates = [HasStdExtM, IsRV64]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtM] in {
def : PatGprGpr<mul, MUL>;
def : PatGprGpr<mulhs, MULH>;
def : PatGprGpr<mulhu, MULHU>;
// No ISDOpcode for mulhsu
def : PatGprGpr<sdiv, DIV>;
def : PatGprGpr<udiv, DIVU>;
def : PatGprGpr<srem, REM>;
def : PatGprGpr<urem, REMU>;
} // Predicates = [HasStdExtM]

let Predicates = [HasStdExtM, IsRV64] in {
def : Pat<(sext_inreg (mul GPR:$rs1, GPR:$rs2), i32),
          (MULW GPR:$rs1, GPR:$rs2)>;

def : PatGprGpr<evm_divw, DIVW>;
def : PatGprGpr<evm_divuw, DIVUW>;
def : PatGprGpr<evm_remuw, REMUW>;

// Handle the specific cases where using DIVU/REMU would be correct and result
// in fewer instructions than emitting DIVUW/REMUW then zero-extending the
// result.
def : Pat<(zexti32 (evm_divuw (zexti32 GPR:$rs1), (zexti32 GPR:$rs2))),
          (DIVU GPR:$rs1, GPR:$rs2)>;
def : Pat<(zexti32 (evm_remuw (zexti32 GPR:$rs1), (zexti32 GPR:$rs2))),
          (REMU GPR:$rs1, GPR:$rs2)>;

// Although the sexti32 operands may not have originated from an i32 srem,
// this pattern is safe as it is impossible for two sign extended inputs to
// produce a result where res[63:32]=0 and res[31]=1.
def : Pat<(srem (sexti32 GPR:$rs1), (sexti32 GPR:$rs2)),
          (REMW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (srem (sexti32 GPR:$rs1),
                            (sexti32 GPR:$rs2)), i32),
          (REMW GPR:$rs1, GPR:$rs2)>;
} // Predicates = [HasStdExtM, IsRV64]
