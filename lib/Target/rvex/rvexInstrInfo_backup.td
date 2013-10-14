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

def SDT_rvexJmpLink      : SDTypeProfile<0, 1, [SDTCisVT<0, iPTR>]>;

def SDT_rvexCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>]>;
def SDT_rvexCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

// Call
def rvexJmpLink : SDNode<"rvexISD::JmpLink",SDT_rvexJmpLink,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;

// Hi and Lo nodes are used to handle global addresses. Used on
// rvexISelLowering to lower stuff like GlobalAddress, ExternalSymbol
// static model. (nothing to do with rvex Registers Hi and Lo)
def rvexGlob    : SDNode<"rvexISD::Glob", SDTIntUnaryOp>;
def rvexGPRel : SDNode<"rvexISD::GPRel", SDTIntUnaryOp>;

// Return
def rvexRet : SDNode<"rvexISD::Ret", SDT_rvexRet, [SDNPHasChain,
                     SDNPOptInGlue, SDNPVariadic]>;

// These are target-independent nodes, but have target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_rvexCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_rvexCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// DivRem(u) nodes
def rvexDivRem    : SDNode<"rvexISD::DivRem", SDT_rvexDivRem,
                           [SDNPOutGlue]>;
def rvexDivRemU   : SDNode<"rvexISD::DivRemU", SDT_rvexDivRem,
                           [SDNPOutGlue]>;

// Type profile for add carry generate instruction
// (outs result, carry), (ins ra, rb, carry_in)
def SDT_rvexAddc         : SDTypeProfile<2, 3, [//SDTCisVT<0, i32>, 
                                              //SDTCisVT<1, i1>,
                                              SDTCisSameAs<0, 2>,
                                              SDTCisSameAs<0, 3>,
                                              SDTCisInt<0>, SDTCisVT<0, i32>,
                                              SDTCisSameAs<1,4>,
                                              SDTCisInt<1>, SDTCisVT<1, i1>]>;
                                              //SDTCisOpSmallerThanOp<1,2>,
                                              //SDTCisOpSmallerThanOp<1,3>,
                                              //SDTCisOpSmallerThanOp<4,0>]>;
def rvexAddc             : SDNode<"rvexISD::Addc", SDT_rvexAddc>;
def rvexAdde             : SDNode<"rvexISD::Adde", SDT_rvexAddc>;

def rvexDivs             : SDNode<"rvexISD::Divs", SDT_rvexAddc>;                              


//===----------------------------------------------------------------------===//
// rvex Operand, Complex Patterns and Transformations Definitions.
//===----------------------------------------------------------------------===//

// Instruction operand types
def brtarget    : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget";
}

def calltarget  : Operand<iPTR> {
  let EncoderMethod = "getJumpTargetOpValue";
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

// Arithmetic and logical instructions with 2 register operands.
class ReturnLogic<bits<8> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FL<op, (outs RC:$ra), (ins RC:$rb, Od:$imm16, RC:$rc),
     !strconcat(instr_asm, "\t$ra = $rb, $imm16, $rc"),
     [], IIAlu, TypeIIAlu> {
  let isReMaterializable = 1;
}

// Arithmetic and logical instructions with 3 register operands.
class ArithLogicR<bits<8> op, string instr_asm, SDNode OpNode,
                  InstrItinClass itin, RegisterClass RC, bit isComm = 0, CType type>:
  FA<op, (outs RC:$ra), (ins RC:$rb, RC:$rc),
     !strconcat(instr_asm, "\t$ra = $rb, $rc"),
     [(set RC:$ra, (OpNode RC:$rb, RC:$rc))], itin, type> {
  let shamt = 0;
  let isCommutable = isComm;  // e.g. add rb rc =  add rc rb
  let isReMaterializable = 1;
}

// Arithmetic and logical instructions with 2 register operands.
class ArithLogicI<bits<8> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FL<op, (outs RC:$ra), (ins RC:$rb, Od:$imm16),
     !strconcat(instr_asm, "\t$ra = $rb, $imm16"),
     [(set RC:$ra, (OpNode RC:$rb, imm_type:$imm16))], IIAlu, TypeIIAlu> {
  let isReMaterializable = 1;
}

// Arithmetic and logical instructions with 3 register operands and 2 returns.
// Used for ADDCG and DIVS
class ArithLogicC<bits<8> op, string instr_asm, SDNode OpNode,
                  InstrItinClass itin, RegisterClass RC, RegisterClass BRRegs, bit isComm = 0, CType type>:
  FA<op, (outs RC:$ra, BRRegs:$co), (ins RC:$rb, RC:$rc, BRRegs:$ci),
     !strconcat(instr_asm, "\t$ra, $co = $rb, $rc, $ci"),
     [/*(set RC:$ra, (OpNode RC:$rb, RC:$rc, RC:$ci))*/], itin, type> {
  let shamt = 0;
  let isCommutable = isComm;  // e.g. add rb rc =  add rc rb
  let isReMaterializable = 1;
}

class CmpInstr<bits<8> op, string instr_asm, 
               InstrItinClass itin, RegisterClass RC, RegisterClass RD, 
               bit isComm = 0, CType type>:
  FA<op, (outs RD:$rc), (ins RC:$ra, RC:$rb),
     !strconcat(instr_asm, "\t$rc = $ra, $rb"), [], itin, type> {
  let rc = 0;
  let shamt = 0;
  let isCommutable = isComm;
}

class CmpInstrImm<bits<8> op, string instr_asm, 
               InstrItinClass itin, RegisterClass RC, RegisterClass RD,
               Operand Od, PatLeaf imm_type, 
               bit isComm = 0, CType type>:
  FL<op, (outs RD:$rc), (ins RC:$ra, Od:$imm16),
     !strconcat(instr_asm, "\t$rc = $ra, $imm16"),
     [], itin, type> {
  let isReMaterializable = 1;
}



// Move immediate imm16 to register ra.
class MoveImm<bits<8> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FL<op, (outs RC:$ra), (ins RC:$rb, Od:$imm),
     !strconcat(instr_asm, "\t$ra = $imm"),
     [(set RC:$ra, (OpNode RC:$rb, imm_type:$imm))], IIAlu, TypeIIAlu> {
  let rb = 0;
  let isReMaterializable = 1;
}

// Shifts
class shift_rotate_imm<bits<8> op, bits<4> isRotate, string instr_asm,
                       SDNode OpNode, PatFrag PF, Operand ImmOpnd,
                       RegisterClass RC>:
  FA<op, (outs RC:$ra), (ins RC:$rb, ImmOpnd:$shamt),
     !strconcat(instr_asm, "\t$ra = $rb, $shamt"),
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

// Conditional Branch
class CBranch<bits<8> op, string instr_asm, RegisterClass RC>:
  FJ<op, (outs), (ins RC:$ra, brtarget:$addr),
             !strconcat(instr_asm, "\t$ra, $addr"),
             [(brcond RC:$ra, bb:$addr)], IIBranch, TypeIIBranch> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 0;
  let neverHasSideEffects = 1;
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

// Jump and Link (Call)
let isCall=1, hasDelaySlot=0 in {
  class JumpLink<bits<8> op, string instr_asm>:
    FJ<op, (outs), (ins CPURegs:$link, calltarget:$target, variable_ops),
       !strconcat(instr_asm, "\t$link = $target"), [],
       IIBranch, TypeIIBranch> {
       }

  class JumpLinkReg<bits<8> op, string instr_asm,
                    RegisterClass RC>:
    FA<op, (outs), (ins RC:$rb, variable_ops),
       !strconcat(instr_asm, "\t$rb = $rb"), [(rvexJmpLink RC:$rb)], IIBranch, TypeIIBranch> {
    let rc = 0;
    let ra = 14;
    let shamt = 0;
  }
}

// Mul, Div
class Mult<bits<8> op, string instr_asm, InstrItinClass itin,
           RegisterClass RC, list<Register> DefRegs>:
  FL<op, (outs RC:$res), (ins RC:$ra, RC:$rb),
     !strconcat(instr_asm, "\t$res = $ra, $rb"), [], itin, TypeIIImul> {
  let imm16 = 0;
  let isCommutable = 1;
  let Defs = DefRegs;
  let neverHasSideEffects = 1;
}

class Mult32<bits<8> op, string instr_asm, InstrItinClass itin>:
  Mult<op, instr_asm, itin, CPURegs, [HI, LO]>;



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
// Pseudo instructions
//===----------------------------------------------------------------------===//

// As stack alignment is always done with addiu, we need a 16-bit immediate
let Defs = [R1], Uses = [R1] in {
def ADJCALLSTACKDOWN : rvexPseudo<(outs), (ins uimm16:$amt),
                                  "!ADJCALLSTACKDOWN $amt",
                                  [(callseq_start timm:$amt)]>;
def ADJCALLSTACKUP   : rvexPseudo<(outs), (ins uimm16:$amt1, uimm16:$amt2),
                                  "!ADJCALLSTACKUP $amt1",
                                  [(callseq_end timm:$amt1, timm:$amt2)]>;
}     

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
//def MOV     
def LDI     : ArithLogicI<0x09, "add ", add, simm16, immSExt16, CPURegs>;
def MOV     : MoveImm<0x08, "mov ", add, simm16, immSExt32, CPURegs>;
def ADDiu   : ArithLogicI<0x09, "add ", add, simm16, immSExt16, CPURegs>;
def XORiu   : ArithLogicR<0x1A, "xor", xor, IIAlu, CPURegs, 1, TypeIIAlu>;

def test_ret   : ReturnLogic<0x09, "return", add, simm16, immSExt16, CPURegs>;

/// Arithmetic Instructions (3-Operand, R-Type)
def ADD     : ArithLogicR<0x13, "add ", add, IIAlu, CPURegs, 1, TypeIIAlu>;
def SUB     : ArithLogicR<0x14, "sub ", sub, IIAlu, CPURegs, 1, TypeIIAlu>;
def AND     : ArithLogicR<0x18, "and ", and, IIAlu, CPURegs, 1, TypeIIAlu>;
def OR      : ArithLogicR<0x19, "or  ", or, IIAlu, CPURegs, 1, TypeIIAlu>;
def XOR     : ArithLogicR<0x1A, "xor ", xor, IIAlu, CPURegs, 1, TypeIIAlu>;

def rvexORC     : SDNode<"rvexISD::Orc", SDTIntBinOp, []>;
def rvexMAX     : SDNode<"rvexISD::Max", SDTIntBinOp, []>;
def rvexMAXU    : SDNode<"rvexISD::Maxu", SDTIntBinOp, []>;
def rvexMIN     : SDNode<"rvexISD::Min", SDTIntBinOp, []>;
def rvexMINU    : SDNode<"rvexISD::Minu", SDTIntBinOp, []>;
def ORC     : ArithLogicR<0x1A, "orc ", rvexORC, IIAlu, CPURegs, 1, TypeIIAlu>;
def MAX     : ArithLogicR<0x1A, "max ", rvexMAX, IIAlu, CPURegs, 1, TypeIIAlu>;
def MAXU    : ArithLogicR<0x1A, "maxu", rvexMAXU, IIAlu, CPURegs, 1, TypeIIAlu>;
def MIN     : ArithLogicR<0x1A, "min ", rvexMIN, IIAlu, CPURegs, 1, TypeIIAlu>;
def MINU    : ArithLogicR<0x1A, "minu", rvexMINU, IIAlu, CPURegs, 1, TypeIIAlu>;

// Arithmetic and logical instructions with 3 register operands and 2 returns.
// Used for ADDCG and DIVS
class SelectRR<bits<8> op, string instr_asm, SDNode OpNode,
                  InstrItinClass itin, RegisterClass RC, RegisterClass BRRegs, bit isComm = 0, CType type>:
  FA<op, (outs RC:$ra), (ins BRRegs:$ci, RC:$rb, RC:$rc),
     !strconcat(instr_asm, "\t$ra = $ci, $rb, $rc"),
     [(set RC:$ra, (OpNode BRRegs:$ci, RC:$rb, RC:$rc))], itin, type> {
  let shamt = 0;
  let isCommutable = isComm;  // e.g. add rb rc =  add rc rb
  let isReMaterializable = 1;
}

def SLCT  : SelectRR<0x1A, "slct", select, IIAlu, CPURegs, BRRegs, 1, TypeIIAlu>;


def rvexDIVS     : ArithLogicC<0x13, "divs ", rvexDivs, IIIdiv, CPURegs, BRRegs, 1, TypeIIIdiv>;

def rvexADDC     : ArithLogicC<0x13, "addcg", rvexAddc, IIAlu, CPURegs, BRRegs, 1, TypeIIAlu>;
def rvexADDE     : ArithLogicC<0x13, "addcg", rvexAddc, IIAlu, CPURegs, BRRegs, 1, TypeIIAlu>;

def CMPEQ   : CmpInstr<0x10, "cmpeq", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPEQi  : CmpInstrImm<0x10, "cmpeq", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPNEQ   : CmpInstr<0x10, "cmpne", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPNEQi  : CmpInstrImm<0x10, "cmpne", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPGT   : CmpInstr<0x10, "cmpgt", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPGTi  : CmpInstrImm<0x10, "cmpgt", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPGTU   : CmpInstr<0x10, "cmpgtu", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPGTUi  : CmpInstrImm<0x10, "cmpgtu", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPLT   : CmpInstr<0x10, "cmplt", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPLTi  : CmpInstrImm<0x10, "cmplt", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPLTU   : CmpInstr<0x10, "cmpltu", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPLTUi  : CmpInstrImm<0x10, "cmpltu", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPGE   : CmpInstr<0x10, "cmpge", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPGEi  : CmpInstrImm<0x10, "cmpge", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPGEU   : CmpInstr<0x10, "cmpgeu", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPGEUi  : CmpInstrImm<0x10, "cmpgeu", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPLE   : CmpInstr<0x10, "cmple", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPLEi  : CmpInstrImm<0x10, "cmple", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;
def CMPLEU   : CmpInstr<0x10, "cmpleu", IIAlu, CPURegs, BRRegs, 0, TypeIIAlu>;
def CMPLEUi  : CmpInstrImm<0x10, "cmpleu", IIAlu, CPURegs, BRRegs,simm16, immSExt32, 0, TypeIIAlu>;



def MULT    : Mult32<0x50, "mpylu", IIImul>;
def MULTu   : Mult32<0x51, "mpyhs", IIImul>;

def MFHI : MoveFromLOHI<0x40, "mfhi", CPURegs, [HI]>;
def MFLO : MoveFromLOHI<0x41, "mflo", CPURegs, [LO]>;
def MTHI : MoveToLOHI<0x42, "mthi", CPURegs, [HI]>;
def MTLO : MoveToLOHI<0x43, "mtlo", CPURegs, [LO]>;

/// Shift Instructions
// work, sra for ashr llvm IR instruction
def SRA     : shift_rotate_imm32<0x1B, 0x00, "shr ", sra>;
def ROL     : shift_rotate_imm32<0x1C, 0x01, "rol ", rotl>;
def ROR     : shift_rotate_imm32<0x1D, 0x01, "ror ", rotr>;
def SHL     : shift_rotate_imm32<0x1E, 0x00, "shl ", shl>;
// work, srl for lshr llvm IR instruction
def SHR     : shift_rotate_imm32<0x1F, 0x00, "shru", srl>;

def BR      : CBranch<0x25, "br  ", BRRegs>;
def BRF     : CBranch<0x25, "brf ", BRRegs>;

def JMP     : UncondBranch<0x26, "goto">;

/// Jump and Branch Instructions
def SWI     : JumpLink<0x2A, "swi">;
def CALL    : JumpLink<0x2B, "call">;



let isReturn=1, isTerminator=1, hasDelaySlot=1, isCodeGenOnly=1,
    isBarrier=1, hasCtrlDep=1, addr=0 in
  def RET   : FJ <0x2C, (outs), (ins CPURegs:$target),
                "return\t$target", [(rvexRet CPURegs:$target)], IIBranch, TypeIIBranch>;
                //"return l0.0", [(rvexRet CPURegs:$target)], IIBranch, TypeIIBranch>;

//def : Pat<(rvexRet CPURegs:$target), (RET CPURegs:$target, 0x8)>;

def JALR    : JumpLinkReg<0x2E, "icall", CPURegs>;

/// No operation
let addr=0 in
  def NOP   : FJ<0, (outs), (ins), "nop", [], IIAlu, TypeIIBranch>;
  
// FrameIndexes are legalized when they are operands from load/store
// instructions. The same not happens for stack address copies, so an
// add op with mem ComplexPattern is used and the stack address copy
// can be matched. It's similar to Sparc LEA_ADDRi
def LEA_ADDiu : EffectiveAddress<"add \t$ra = $addr", CPURegs, mem_ea> {
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
//  Arbitrary patterns that map to one or more instructions
//===----------------------------------------------------------------------===//

// Small immediates
def : Pat<(i32 imm:$in),
          (MOV R0, imm:$in)>;

def : Pat<(rvexJmpLink (i32 tglobaladdr:$dst)),
          (CALL (i32 LR), tglobaladdr:$dst)>;
         

// hi/lo relocs
def : Pat<(rvexHi tglobaladdr:$in), (MOV R0, tglobaladdr:$in)>;
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


def : Pat<(mul CPURegs:$rb, CPURegs:$rc),
              (ADD (MULT CPURegs:$rb, CPURegs:$rc), (MULTu CPURegs:$rb, CPURegs:$rc))>;          

def : Pat<(i32 (extloadi1  addr:$src)), (LBu addr:$src)>;
def : Pat<(i32 (extloadi8  addr:$src)), (LBu addr:$src)>;
def : Pat<(i32 (extloadi16_a addr:$src)), (LHu addr:$src)>;


// brcond patterns
multiclass BrcondPats<RegisterClass RC,
Instruction JEQOp, Instruction JNEOp, 
Instruction CMPEQOp, Instruction CMPEQiOp,
Instruction CMPNEQOp, Instruction CMPNEQiOp,
Instruction CMPGTOp, Instruction CMPGTiOp,
Instruction CMPGTUOp, Instruction CMPGTUiOp,
Instruction CMPLTOp, Instruction CMPLTiOp,
Instruction CMPLTUOp, Instruction CMPLTUiOp,
Instruction CMPGEOp, Instruction CMPGEiOp,
Instruction CMPGEUOp, Instruction CMPGEUiOp,
Instruction CMPLEOp, Instruction CMPLEiOp,
Instruction CMPLEUOp, Instruction CMPLEUiOp,

Register ZEROReg> {

//seteq
def : Pat<(brcond (i32 (seteq RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPEQOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setueq RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPEQOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (seteq RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPEQiOp RC:$lhs, imm:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setueq RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPEQiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

//setne          
def : Pat<(brcond (i32 (setne RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPNEQOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setne RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPNEQiOp RC:$lhs, imm:$rhs), bb:$dst)>;          

//setune                    
def : Pat<(brcond (i32 (setune RC:$lhs, RC:$rhs)), bb:$dst),
          (JNEOp (CMPEQOp RC:$lhs, RC:$rhs), bb:$dst)>;
def : Pat<(brcond (i32 (setune RC:$lhs, imm:$rhs)), bb:$dst),
          (JNEOp (CMPEQiOp RC:$lhs, imm:$rhs), bb:$dst)>;

//setgt
def : Pat<(brcond (i32 (setgt RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPGTOp RC:$lhs, RC:$rhs), bb:$dst)>;  
def : Pat<(brcond (i32 (setgt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPGTiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

//setugt
def : Pat<(brcond (i32 (setugt RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPGTUOp RC:$lhs, RC:$rhs), bb:$dst)>;             
def : Pat<(brcond (i32 (setugt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPGTUiOp RC:$lhs, imm:$rhs), bb:$dst)>; 

//setlt
def : Pat<(brcond (i32 (setlt RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPLTOp RC:$lhs, RC:$rhs), bb:$dst)>;  
def : Pat<(brcond (i32 (setlt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPLTiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

//setult
def : Pat<(brcond (i32 (setult RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPLTUOp RC:$lhs, RC:$rhs), bb:$dst)>;             
def : Pat<(brcond (i32 (setult RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPLTUiOp RC:$lhs, imm:$rhs), bb:$dst)>;           

//setge
def : Pat<(brcond (i32 (setge RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPGEOp RC:$lhs, RC:$rhs), bb:$dst)>;  
def : Pat<(brcond (i32 (setgt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPGEiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

//setuge
def : Pat<(brcond (i32 (setuge RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPGEUOp RC:$lhs, RC:$rhs), bb:$dst)>;             
def : Pat<(brcond (i32 (setugt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPGEUiOp RC:$lhs, imm:$rhs), bb:$dst)>; 

//setle
def : Pat<(brcond (i32 (setle RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPLEOp RC:$lhs, RC:$rhs), bb:$dst)>;  
def : Pat<(brcond (i32 (setlt RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPLEiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

//setule
def : Pat<(brcond (i32 (setule RC:$lhs, RC:$rhs)), bb:$dst),
          (JEQOp (CMPLEUOp RC:$lhs, RC:$rhs), bb:$dst)>;             
def : Pat<(brcond (i32 (setult RC:$lhs, imm:$rhs)), bb:$dst),
          (JEQOp (CMPLEUiOp RC:$lhs, imm:$rhs), bb:$dst)>;  

def : Pat<(brcond RC:$cond, bb:$dst),
          (JNEOp (CMPEQOp RC:$cond, ZEROReg), bb:$dst)>;
}

defm : BrcondPats<CPURegs, BR, BRF, 
CMPEQ, CMPEQi, 
CMPNEQ, CMPNEQi, 

CMPGT, CMPGTi,
CMPGTU, CMPGTUi,
CMPLT, CMPLTi,
CMPLTU, CMPLTUi,

CMPGE, CMPGEi,
CMPGEU, CMPGEUi,
CMPLE, CMPLEi,
CMPLEU, CMPLEUi,

R0>;

// setcc patterns
multiclass SeteqPats<RegisterClass RC, Instruction XOROp> {
  def : Pat<(seteq RC:$lhs, RC:$rhs),
            (XOROp (XOROp RC:$lhs, RC:$rhs), (ADDiu R0, 1))>;
}

defm : SeteqPats<CPURegs, XOR>;
