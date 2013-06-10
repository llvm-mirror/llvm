//===- rvexInstrInfo.td - Target Description for rvex Target -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the rvex implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "rvexInstrFormats.td"

//===----------------------------------------------------------------------===//
// rvex profiles and nodes
//===----------------------------------------------------------------------===//

def SDT_rvexRet          : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_rvexDivRem       : SDTypeProfile<0, 2,
                                         [SDTCisInt<0>,
                                          SDTCisSameAs<0, 1>]>;

// Hi and Lo nodes are used to handle global addresses. Used on
// rvexISelLowering to lower stuff like GlobalAddress, ExternalSymbol
// static model. (nothing to do with rvex Registers Hi and Lo)
def rvexHi    : SDNode<"rvexISD::Hi", SDTIntUnaryOp>;
def rvexLo    : SDNode<"rvexISD::Lo", SDTIntUnaryOp>;
def rvexGPRel : SDNode<"rvexISD::GPRel", SDTIntUnaryOp>;

// Return
def rvexRet : SDNode<"rvexISD::Ret", SDT_rvexRet, [SDNPHasChain,
                     SDNPOptInGlue]>;

// DivRem(u) nodes
def rvexDivRem    : SDNode<"rvexISD::DivRem", SDT_rvexDivRem,
                           [SDNPOutGlue]>;
def rvexDivRemU   : SDNode<"rvexISD::DivRemU", SDT_rvexDivRem,
                           [SDNPOutGlue]>;

//===----------------------------------------------------------------------===//
// rvex Instruction Predicate Definitions.
//===----------------------------------------------------------------------===//
def RelocPIC    :     Predicate<"TM.getRelocationModel() == Reloc::PIC_">,
                      AssemblerPredicate<"Featurervex32">;

//===----------------------------------------------------------------------===//
// rvex Operand, Complex Patterns and Transformations Definitions.
//===----------------------------------------------------------------------===//

// Instruction operand types
def brtarget    : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget";
}


// Signed Operand
def simm16      : Operand<i32> {
  let DecoderMethod= "DecodeSimm16";
}

def shamt       : Operand<i32>;

// Unsigned Operand
def uimm16      : Operand<i32> {
  let PrintMethod = "printUnsignedImm";
}

// Address operand
def mem : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops CPURegs, simm16);
  let EncoderMethod = "getMemEncoding";
}

def mem_ea : Operand<i32> {
  let PrintMethod = "printMemOperandEA";
  let MIOperandInfo = (ops CPURegs, simm16);
  let EncoderMethod = "getMemEncoding";
}

// Transformation Function - get the lower 16 bits.
def LO16 : SDNodeXForm<imm, [{
  return getImm(N, N->getZExtValue() & 0xFFFF);
}]>;

// Transformation Function - get the higher 16 bits.
def HI16 : SDNodeXForm<imm, [{
  return getImm(N, (N->getZExtValue() >> 16) & 0xFFFF);
}]>;

// Node immediate fits as 16-bit sign extended on target immediate.
// e.g. addi, andi
def immSExt32  : PatLeaf<(imm), [{ return isInt<32>(N->getSExtValue()); }]>;
def immSExt16  : PatLeaf<(imm), [{ return isInt<16>(N->getSExtValue()); }]>;

// Node immediate fits as 16-bit zero extended on target immediate.
// The LO16 param means that only the lower 16 bits of the node
// immediate are caught.
// e.g. addiu, sltiu
def immZExt16  : PatLeaf<(imm), [{
  if (N->getValueType(0) == MVT::i32)
    return (uint32_t)N->getZExtValue() == (unsigned short)N->getZExtValue();
  else
    return (uint64_t)N->getZExtValue() == (unsigned short)N->getZExtValue();
}]>;

// shamt field must fit in 5 bits.
def immZExt5 : ImmLeaf<i32, [{return Imm == (Imm & 0x1f);}]>;

// rvex Address Mode! SDNode frameindex could possibily be a match
// since load and store instructions from stack used it.
def addr : ComplexPattern<iPTR, 2, "SelectAddr", [frameindex], [SDNPWantParent]>;

//===----------------------------------------------------------------------===//
// Pattern fragment for load/store
//===----------------------------------------------------------------------===//

class AlignedLoad<PatFrag Node> :
  PatFrag<(ops node:$ptr), (Node node:$ptr), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  return LD->getMemoryVT().getSizeInBits()/8 <= LD->getAlignment();
}]>;

class AlignedStore<PatFrag Node> :
  PatFrag<(ops node:$val, node:$ptr), (Node node:$val, node:$ptr), [{
  StoreSDNode *SD = cast<StoreSDNode>(N);
  return SD->getMemoryVT().getSizeInBits()/8 <= SD->getAlignment();
}]>;

// Load/Store PatFrags.
def sextloadi16_a   : AlignedLoad<sextloadi16>;
def zextloadi16_a   : AlignedLoad<zextloadi16>;
def extloadi16_a    : AlignedLoad<extloadi16>;
def load_a          : AlignedLoad<load>;
def truncstorei16_a : AlignedStore<truncstorei16>;
def store_a         : AlignedStore<store>;

//===----------------------------------------------------------------------===//
// Instructions specific format
//===----------------------------------------------------------------------===//

// Arithmetic and logical instructions with 3 register operands.
class ArithLogicR<bits<8> op, string instr_asm, SDNode OpNode,
                  InstrItinClass itin, RegisterClass RC, bit isComm = 0, CType type>:
  FA<op, (outs RC:$ra), (ins RC:$rb, RC:$rc),
     !strconcat(instr_asm, "\t\t$ra = $rb, $rc"),
     [(set RC:$ra, (OpNode RC:$rb, RC:$rc))], itin, type> {
  let shamt = 0;
  let isCommutable = isComm;	// e.g. add rb rc =  add rc rb
  let isReMaterializable = 1;
}

// Arithmetic and logical instructions with 2 register operands.
class ArithLogicI<bits<8> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FL<op, (outs RC:$ra), (ins RC:$rb, Od:$imm16),
     !strconcat(instr_asm, "\t\t$ra = $rb, $imm16"),
     [(set RC:$ra, (OpNode RC:$rb, imm_type:$imm16))], IIAlu, TypeIIAlu> {
  let isReMaterializable = 1;
}

// Move immediate imm16 to register ra.
class MoveImm<bits<8> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FL<op, (outs RC:$ra), (ins RC:$rb, Od:$imm),
     !strconcat(instr_asm, "\t\t$ra = $imm"),
     [(set RC:$ra, (OpNode RC:$rb, imm_type:$imm))], IIAlu, TypeIIAlu> {
  let rb = 0;
  let isReMaterializable = 1;
}

// Shifts
class shift_rotate_imm<bits<8> op, bits<4> isRotate, string instr_asm,
                       SDNode OpNode, PatFrag PF, Operand ImmOpnd,
                       RegisterClass RC>:
  FA<op, (outs RC:$ra), (ins RC:$rb, ImmOpnd:$shamt),
     !strconcat(instr_asm, "\t\t$ra = $rb, $shamt"),
     [(set RC:$ra, (OpNode RC:$rb, PF:$shamt))], IIAlu, TypeIIAlu> {
  let rc = isRotate;
  let shamt = shamt;
}

// 32-bit shift instructions.
class shift_rotate_imm32<bits<8> func, bits<4> isRotate, string instr_asm,
                         SDNode OpNode>:
  shift_rotate_imm<func, isRotate, instr_asm, OpNode, immZExt5, shamt, CPURegs>;

// Load Upper Imediate
class LoadUpper<bits<8> op, string instr_asm, RegisterClass RC, Operand Imm>:
  FL<op, (outs RC:$ra), (ins Imm:$imm16),
     !strconcat(instr_asm, "\t\t$ra = $imm16"), [], IIAlu, TypeIIAlu> {
  let rb = 0;
  let neverHasSideEffects = 1;
  let isReMaterializable = 1;
}

class FMem<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
          InstrItinClass itin, CType type>: FL<op, outs, ins, asmstr, pattern, itin, type> {
  bits<20> addr;
  let Inst{19-16} = addr{19-16};
  let Inst{15-0}  = addr{15-0};
  let DecoderMethod = "DecodeMem";
}

// Memory Load/Store
let canFoldAsLoad = 1 in
class LoadM<bits<8> op, string instr_asm, PatFrag OpNode, RegisterClass RC,
            Operand MemOpnd, bit Pseudo>:
  FMem<op, (outs RC:$ra), (ins MemOpnd:$addr),
     !strconcat(instr_asm, "\t$ra = $addr"),
     [(set RC:$ra, (OpNode addr:$addr))], IILoad, TypeIILoad> {
  let isPseudo = Pseudo;
}

class StoreM<bits<8> op, string instr_asm, PatFrag OpNode, RegisterClass RC,
             Operand MemOpnd, bit Pseudo>:
  FMem<op, (outs), (ins RC:$ra, MemOpnd:$addr),
     !strconcat(instr_asm, "\t$addr = $ra"),
     [(OpNode RC:$ra, addr:$addr)], IIStore, TypeIIStore> {
  let isPseudo = Pseudo;
}

// 32-bit load.
multiclass LoadM32<bits<8> op, string instr_asm, PatFrag OpNode,
                   bit Pseudo = 0> {
  def #NAME# : LoadM<op, instr_asm, OpNode, CPURegs, mem, Pseudo>;
}

// 32-bit store.
multiclass StoreM32<bits<8> op, string instr_asm, PatFrag OpNode,
                    bit Pseudo = 0> {
  def #NAME# : StoreM<op, instr_asm, OpNode, CPURegs, mem, Pseudo>;
}

// Unconditional branch, such as JMP
class UncondBranch<bits<8> op, string instr_asm>:
  FJ<op, (outs), (ins brtarget:$addr),
             !strconcat(instr_asm, "\t$addr"), [(br bb:$addr)], IIBranch, TypeIIBranch> {
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let hasDelaySlot = 0;
  let DecoderMethod = "DecodeJumpRelativeTarget";
}

// Mul, Div
class Mult<bits<8> op, string instr_asm, InstrItinClass itin,
           RegisterClass RC, list<Register> DefRegs>:
  FL<op, (outs), (ins RC:$ra, RC:$rb),
     !strconcat(instr_asm, "\t$ra, $rb"), [], itin, TypeIIImul> {
  let imm16 = 0;
  let isCommutable = 1;
  let Defs = DefRegs;
  let neverHasSideEffects = 1;
}

class Mult32<bits<8> op, string instr_asm, InstrItinClass itin>:
  Mult<op, instr_asm, itin, CPURegs, [HI, LO]>;

class Div<SDNode opNode, bits<8> op, string instr_asm, InstrItinClass itin,
          RegisterClass RC, list<Register> DefRegs>:
  FL<op, (outs), (ins RC:$ra, RC:$rb),
     !strconcat(instr_asm, "\t$ra, $rb"),
     [(opNode RC:$ra, RC:$rb)], itin, TypeIIIdiv> {
  let imm16 = 0;
  let Defs = DefRegs;
}

class Div32<SDNode opNode, bits<8> op, string instr_asm, InstrItinClass itin>:
  Div<opNode, op, instr_asm, itin, CPURegs, [HI, LO]>;

// Move from Hi/Lo
class MoveFromLOHI<bits<8> op, string instr_asm, RegisterClass RC,
                   list<Register> UseRegs>:
  FL<op, (outs RC:$ra), (ins),
     !strconcat(instr_asm, "\t$ra"), [], IIHiLo, TypeIIHiLo> {
  let rb = 0;
  let imm16 = 0;
  let Uses = UseRegs;
  let neverHasSideEffects = 1;
}

class MoveToLOHI<bits<8> op, string instr_asm, RegisterClass RC,
                 list<Register> DefRegs>:
  FL<op, (outs), (ins RC:$ra),
     !strconcat(instr_asm, "\t$ra"), [], IIHiLo, TypeIIHiLo> {
  let rb = 0;
  let imm16 = 0;
  let Defs = DefRegs;
  let neverHasSideEffects = 1;
}

class EffectiveAddress<string instr_asm, RegisterClass RC, Operand Mem> :
  FMem<0x09, (outs RC:$ra), (ins Mem:$addr),
     instr_asm, [(set RC:$ra, addr:$addr)], IIAlu, TypeIIAlu>;

//===----------------------------------------------------------------------===//
// Instruction definition
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// rvexI Instructions
//===----------------------------------------------------------------------===//

/// Load and Store Instructions
///  aligned
defm LD     : LoadM32<0x00,  "ldw ",  load_a>;
defm ST     : StoreM32<0x01, "stw ",  store_a>;
defm LB     : LoadM32<0x03, "ldb ",  sextloadi8>;
defm LBu    : LoadM32<0x04, "ldbu", zextloadi8>;
defm SB     : StoreM32<0x05, "stb ", truncstorei8>;
defm LH     : LoadM32<0x06, "ldh ",  sextloadi16_a>;
defm LHu    : LoadM32<0x07, "ldhu", zextloadi16_a>;
defm SH     : StoreM32<0x08, "sth ", truncstorei16_a>;

/// Arithmetic Instructions (ALU Immediate)
def LDI     : MoveImm<0x08, "mov", add, simm16, immSExt32, CPURegs>;
// add defined in include/llvm/Target/TargetSelectionDAG.td, line 315 (def add).
def ADDiu   : ArithLogicI<0x09, "add", add, simm16, immSExt16, CPURegs>;
def XORiu   : ArithLogicR<0x1A, "xor", xor, IIAlu, CPURegs, 1, TypeIIAlu>;

/// Arithmetic Instructions (3-Operand, R-Type)
def ADD     : ArithLogicR<0x13, "add", add, IIAlu, CPURegs, 1, TypeIIAlu>;
def SUB     : ArithLogicR<0x14, "sub", sub, IIAlu, CPURegs, 1, TypeIIAlu>;
def MUL     : ArithLogicR<0x15, "mul", mul, IIImul, CPURegs, 1, TypeIIImul>;
def SDIV    : Div32<rvexDivRem, 0x16, "div", IIIdiv>;
def UDIV    : Div32<rvexDivRemU, 0x17, "divu", IIIdiv>;
def AND     : ArithLogicR<0x18, "and", and, IIAlu, CPURegs, 1, TypeIIAlu>;
def OR      : ArithLogicR<0x19, "or", or, IIAlu, CPURegs, 1, TypeIIAlu>;
def XOR     : ArithLogicR<0x1A, "xor", xor, IIAlu, CPURegs, 1, TypeIIAlu>;

def MULT    : Mult32<0x50, "mult", IIImul>;
def MULTu   : Mult32<0x51, "multu", IIImul>;

def MFHI : MoveFromLOHI<0x40, "mfhi", CPURegs, [HI]>;
def MFLO : MoveFromLOHI<0x41, "mflo", CPURegs, [LO]>;
def MTHI : MoveToLOHI<0x42, "mthi", CPURegs, [HI]>;
def MTLO : MoveToLOHI<0x43, "mtlo", CPURegs, [LO]>;

/// Shift Instructions
// work, sra for ashr llvm IR instruction
def SRA     : shift_rotate_imm32<0x1B, 0x00, "sra", sra>;
def ROL     : shift_rotate_imm32<0x1C, 0x01, "rol", rotl>;
def ROR     : shift_rotate_imm32<0x1D, 0x01, "ror", rotr>;
def SHL     : shift_rotate_imm32<0x1E, 0x00, "shl", shl>;
// work, srl for lshr llvm IR instruction
def SHR     : shift_rotate_imm32<0x1F, 0x00, "shr", srl>;

def JMP     : UncondBranch<0x26, "jmp">;

let isReturn=1, isTerminator=1, hasDelaySlot=1, isCodeGenOnly=1,
    isBarrier=1, hasCtrlDep=1 in
  def RET : FJ <0x2C, (outs), (ins CPURegs:$target),
                "ret\t$target", [(rvexRet CPURegs:$target)], IIBranch, TypeIIBranch>;

/// No operation
let addr=0 in
  def NOP   : FJ<0, (outs), (ins), "nop", [], IIAlu, TypeIIBranch>;
  
// FrameIndexes are legalized when they are operands from load/store
// instructions. The same not happens for stack address copies, so an
// add op with mem ComplexPattern is used and the stack address copy
// can be matched. It's similar to Sparc LEA_ADDRi
def LEA_ADDiu : EffectiveAddress<"add\t\t$ra = $addr", CPURegs, mem_ea> {
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
//  Arbitrary patterns that map to one or more instructions
//===----------------------------------------------------------------------===//

// Small immediates
def : Pat<(i32 imm:$in),
          (LDI R0, imm:$in)>;

// hi/lo relocs
def : Pat<(rvexHi tglobaladdr:$in), (SHL (ADDiu R0, tglobaladdr:$in), 16)>;
// Expect rvex add LUi support, like Mips
//def : Pat<(rvexHi tglobaladdr:$in), (LUi tglobaladdr:$in)>;
def : Pat<(rvexLo tglobaladdr:$in), (ADDiu R0, tglobaladdr:$in)>;

def : Pat<(add CPURegs:$hi, (rvexLo tglobaladdr:$lo)),
          (ADDiu CPURegs:$hi, tglobaladdr:$lo)>;

// gp_rel relocs
def : Pat<(add CPURegs:$gp, (rvexGPRel tglobaladdr:$in)),
          (ADDiu CPURegs:$gp, tglobaladdr:$in)>;

def : Pat<(not CPURegs:$in),
          (XOR CPURegs:$in, (ADDiu R0, 1))>;

def : Pat<(i32 (extloadi1  addr:$src)), (LBu addr:$src)>;
def : Pat<(i32 (extloadi8  addr:$src)), (LBu addr:$src)>;
def : Pat<(i32 (extloadi16_a addr:$src)), (LHu addr:$src)>;

// brcond patterns
multiclass BrcondPats<RegisterClass RC, Instruction JEQOp, Instruction JNEOp, 
Instruction JLTOp, Instruction JGTOp, Instruction JLEOp, Instruction JGEOp, 
Instruction CMPOp, Register ZEROReg> {
def : Pat<(brcond (i32 (seteq RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setueq RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setne RC:$lhs, RC:$rhs)), bb:$dst),
          (JNEOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setune RC:$lhs, RC:$rhs)), bb:$dst),
          (JNEOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setlt RC:$lhs, RC:$rhs)), bb:$dst),
          (JLTOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setult RC:$lhs, RC:$rhs)), bb:$dst),
          (JLTOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setgt RC:$lhs, RC:$rhs)), bb:$dst),
          (JGTOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setugt RC:$lhs, RC:$rhs)), bb:$dst),
          (JGTOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setle RC:$lhs, RC:$rhs)), bb:$dst),
          (JLEOp (CMPOp RC:$rhs, RC:$lhs), bb:$dst)>;
def : Pat<(brcond (i32 (setule RC:$lhs, RC:$rhs)), bb:$dst),
          (JLEOp (CMPOp RC:$rhs, RC:$lhs), bb:$dst)>;
def : Pat<(brcond (i32 (setge RC:$lhs, RC:$rhs)), bb:$dst),
          (JGEOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setuge RC:$lhs, RC:$rhs)), bb:$dst),
          (JGEOp (CMPOp RC:$lhs, RC:$rhs), bb:$dst)>;

def : Pat<(brcond RC:$cond, bb:$dst),
          (JNEOp (CMPOp RC:$cond, ZEROReg), bb:$dst)>;
}

//defm : BrcondPats<CPURegs, JEQ, JNE, JLT, JGT, JLE, JGE, CMP, ZERO>;

// setcc patterns
multiclass SeteqPats<RegisterClass RC, Instruction XOROp> {
  def : Pat<(seteq RC:$lhs, RC:$rhs),
            (XOROp (XOROp RC:$lhs, RC:$rhs), (ADDiu R0, 1))>;
}

defm : SeteqPats<CPURegs, XOR>;
