//===-- rvexInstrFormats.td - rvex Instruction Formats -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe rvex instructions format
//
//  CPU INSTRUCTION FORMATS
//
//  opcode  - operation code.
//  ra      - dst reg, only used on 3 regs instr.
//  rb      - src reg.
//  rc      - src reg (on a 3 reg instr).
//  cx      - immediate
//
//===----------------------------------------------------------------------===//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<4> val> {
  bits<4> Value = val;
}

def Pseudo    : Format<0>;
def FrmR      : Format<1>;
def FrmI      : Format<2>;
def FrmJ      : Format<3>;
def FrmOther  : Format<4>; // Instruction w/ a custom format


class CType<bits<4> val> {
  bits<4> Value = val;
}
def TypeIIAlu              : CType<0>;
def TypeIILoadStore        : CType<1>;
def TypeIIBranch           : CType<2>;
def TypeIIHiLo             : CType<3>;
def TypeIIImul             : CType<4>;
def TypeIIAluImm           : CType<5>;
def TypeIILR               : CType<6>;
def TypeIIPseudo           : CType<7>;

// Generic rvex Format
class rvexInst<dag outs, dag ins, string asmstr, list<dag> pattern,
               InstrItinClass itin, Format f, CType type>: Instruction
{
  field bits<32> Inst;
  Format Form = f;

  let Namespace = "rvex";

  let Size = 4;

  bits<8> Opcode = 0;

  // Top 8 bits are the 'opcode' field
  let Inst{31-24} = Opcode;

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

  //
  // Attributes specific to rvex instructions...
  //



  // TSFlags layout should be kept in sync with rvexInstrInfo.h.
  CType rvexType = type;
  let TSFlags{0-3}   = rvexType.Value;
  
  bits<1> isrvexSolo = 0;
  let TSFlags{4} = isrvexSolo;

  bits<1> isrvexLong = 0;
  let TSFlags{5} = isrvexLong;

  let DecoderNamespace = "rvex";

  field bits<32> SoftFail = 0;
}

// rvex Pseudo Instructions Format
class rvexPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>:
      rvexInst<outs, ins, asmstr, pattern, IIPseudo, Pseudo, TypeIIPseudo> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

//===----------------------------------------------------------------------===//
// Format A instruction class in rvex : <|opcode|ra|rb|rc|cx|>
//===----------------------------------------------------------------------===//

class FA<bits<8> op, dag outs, dag ins, string asmstr,
         list<dag> pattern, InstrItinClass itin, CType type>:
      rvexInst<outs, ins, asmstr, pattern, itin, FrmR, type>
{
  bits<4>  ra;
  bits<4>  rb;
  bits<4>  rc;
  bits<12> shamt;

  let Opcode = op;

  let Inst{23-20} = ra;
  let Inst{19-16} = rb;
  let Inst{15-12} = rc;
  let Inst{11-0}  = shamt;
}

//===----------------------------------------------------------------------===//
// Format I instruction class in rvex : <|opcode|ra|rb|cx|>
//===----------------------------------------------------------------------===//

class FL<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin, CType type>: rvexInst<outs, ins, asmstr, pattern, itin, FrmI, type>
{
  bits<4>  ra;
  bits<4>  rb;
  bits<8> imm8;

  let Opcode = op;

  let Inst{23-20} = ra;
  let Inst{19-16} = rb;
  let Inst{7-0}  = imm8;
}

//===----------------------------------------------------------------------===//
// Format J instruction class in rvex : <|opcode|address|>
//===----------------------------------------------------------------------===//

class FJ<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin, CType type>: rvexInst<outs, ins, asmstr, pattern, itin, FrmJ, type>
{
  bits<24> addr;

  let Opcode = op;

  let Inst{23-0} = addr;
}


