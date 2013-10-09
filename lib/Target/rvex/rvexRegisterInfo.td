//===-- rvexRegisterInfo.td - rvex Register defs -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the rvex register file
//===----------------------------------------------------------------------===//


// We have banks of 16 registers each.
class rvexReg<string n> : Register<n> {
  field bits<7> Num;
  let Namespace = "rvex";
}

// rvex CPU Registers
class rvexGPRReg<bits<7> num, string n> : rvexReg<n> {
  let Num = num;
}

class rvexBRReg<bits<7> num, string n> : rvexReg<n> {
  let Num = num;
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

let Namespace = "rvex" in {
  // General Purpose Registers
  def R0 : rvexGPRReg< 0, "r0.0">, DwarfRegNum<[0]>;
  def R1 : rvexGPRReg< 1, "r0.1">, DwarfRegNum<[1]>;
  def R2 : rvexGPRReg< 2, "r0.2">, DwarfRegNum<[2]>;
  def R3 : rvexGPRReg< 3, "r0.3">, DwarfRegNum<[3]>;
  def R4 : rvexGPRReg< 4, "r0.4">, DwarfRegNum<[4]>;
  def R5 : rvexGPRReg< 5, "r0.5">, DwarfRegNum<[5]>;
  def R6 : rvexGPRReg< 6, "r0.6">, DwarfRegNum<[6]>;
  def R7 : rvexGPRReg< 7, "r0.7">, DwarfRegNum<[7]>;
  def R8 : rvexGPRReg< 8, "r0.8">, DwarfRegNum<[8]>;
  def R9 : rvexGPRReg< 9, "r0.9">, DwarfRegNum<[9]>;
  def R10 : rvexGPRReg< 10, "r0.10">, DwarfRegNum<[10]>;
  def R11 : rvexGPRReg< 11, "r0.11">, DwarfRegNum<[11]>;
  def R12 : rvexGPRReg< 12, "r0.12">, DwarfRegNum<[12]>;
  def R13 : rvexGPRReg< 13, "r0.13">, DwarfRegNum<[13]>;
  def R14 : rvexGPRReg< 14, "r0.14">, DwarfRegNum<[14]>;
  def R15 : rvexGPRReg< 15, "r0.15">, DwarfRegNum<[15]>;
  def R16 : rvexGPRReg< 16, "r0.16">, DwarfRegNum<[16]>;
  def R17 : rvexGPRReg< 17, "r0.17">, DwarfRegNum<[17]>;
  def R18 : rvexGPRReg< 18, "r0.18">, DwarfRegNum<[18]>;
  def R19 : rvexGPRReg< 19, "r0.19">, DwarfRegNum<[19]>;
  def R20 : rvexGPRReg< 20, "r0.20">, DwarfRegNum<[20]>;
  def R21 : rvexGPRReg< 21, "r0.21">, DwarfRegNum<[21]>;
  def R22 : rvexGPRReg< 22, "r0.22">, DwarfRegNum<[22]>;
  def R23 : rvexGPRReg< 23, "r0.23">, DwarfRegNum<[23]>;
  def R24 : rvexGPRReg< 24, "r0.24">, DwarfRegNum<[24]>;
  def R25 : rvexGPRReg< 25, "r0.25">, DwarfRegNum<[25]>;
  def R26 : rvexGPRReg< 26, "r0.26">, DwarfRegNum<[26]>;
  def R27 : rvexGPRReg< 27, "r0.27">, DwarfRegNum<[27]>;
  def R28 : rvexGPRReg< 28, "r0.28">, DwarfRegNum<[28]>;
  def R29 : rvexGPRReg< 29, "r0.29">, DwarfRegNum<[29]>;
  def R30 : rvexGPRReg< 30, "r0.30">, DwarfRegNum<[30]>;
  def R31 : rvexGPRReg< 31, "r0.31">, DwarfRegNum<[31]>;
  def R32 : rvexGPRReg< 32, "r0.32">, DwarfRegNum<[32]>;
  def R33 : rvexGPRReg< 33, "r0.33">, DwarfRegNum<[33]>;
  def R34 : rvexGPRReg< 34, "r0.34">, DwarfRegNum<[34]>;
  def R35 : rvexGPRReg< 35, "r0.35">, DwarfRegNum<[35]>;
  def R36 : rvexGPRReg< 36, "r0.36">, DwarfRegNum<[36]>;
  def R37 : rvexGPRReg< 37, "r0.37">, DwarfRegNum<[37]>;
  def R38 : rvexGPRReg< 38, "r0.38">, DwarfRegNum<[38]>;
  def R39 : rvexGPRReg< 39, "r0.39">, DwarfRegNum<[39]>;
  def R40 : rvexGPRReg< 40, "r0.40">, DwarfRegNum<[40]>;
  def R41 : rvexGPRReg< 41, "r0.41">, DwarfRegNum<[41]>;
  def R42 : rvexGPRReg< 42, "r0.42">, DwarfRegNum<[42]>;
  def R43 : rvexGPRReg< 43, "r0.43">, DwarfRegNum<[43]>;
  def R44 : rvexGPRReg< 44, "r0.44">, DwarfRegNum<[44]>;
  def R45 : rvexGPRReg< 45, "r0.45">, DwarfRegNum<[45]>;
  def R46 : rvexGPRReg< 46, "r0.46">, DwarfRegNum<[46]>;
  def R47 : rvexGPRReg< 47, "r0.47">, DwarfRegNum<[47]>;
  def R48 : rvexGPRReg< 48, "r0.48">, DwarfRegNum<[48]>;
  def R49 : rvexGPRReg< 49, "r0.49">, DwarfRegNum<[49]>;
  def R50 : rvexGPRReg< 50, "r0.50">, DwarfRegNum<[50]>;
  def R51 : rvexGPRReg< 51, "r0.51">, DwarfRegNum<[51]>;
  def R52 : rvexGPRReg< 52, "r0.52">, DwarfRegNum<[52]>;
  def R53 : rvexGPRReg< 53, "r0.53">, DwarfRegNum<[53]>;
  def R54 : rvexGPRReg< 54, "r0.54">, DwarfRegNum<[54]>;
  def R55 : rvexGPRReg< 55, "r0.55">, DwarfRegNum<[55]>;
  def R56 : rvexGPRReg< 56, "r0.56">, DwarfRegNum<[56]>;
  def R57 : rvexGPRReg< 57, "r0.57">, DwarfRegNum<[57]>;
  def R58 : rvexGPRReg< 58, "r0.58">, DwarfRegNum<[58]>;
  def R59 : rvexGPRReg< 59, "r0.59">, DwarfRegNum<[59]>;
  def R60 : rvexGPRReg< 60, "r0.60">, DwarfRegNum<[60]>;
  def R61 : rvexGPRReg< 61, "r0.61">, DwarfRegNum<[61]>;
  def R62 : rvexGPRReg< 62, "r0.62">, DwarfRegNum<[62]>;
  def R63 : rvexGPRReg< 63, "r0.63">, DwarfRegNum<[63]>;

  def LR   : rvexGPRReg< 64, "l0.0">,   DwarfRegNum<[64]>;
  def PC   : rvexGPRReg< 65, "PC">,   DwarfRegNum<[65]>;

  def B0 : rvexBRReg< 66, "b0.0">, DwarfRegNum<[66]>;
  def B1 : rvexBRReg< 67, "b0.1">, DwarfRegNum<[67]>;
  def B2 : rvexBRReg< 68, "b0.2">, DwarfRegNum<[68]>;
  def B3 : rvexBRReg< 69, "b0.3">, DwarfRegNum<[69]>;
  def B4 : rvexBRReg< 70, "b0.4">, DwarfRegNum<[70]>;
  def B5 : rvexBRReg< 71, "b0.5">, DwarfRegNum<[71]>;
  def B6 : rvexBRReg< 72, "b0.6">, DwarfRegNum<[72]>;
  def B7 : rvexBRReg< 73, "b0.7">, DwarfRegNum<[73]>;



  // Hi/Lo registers
  def HI   : Register<"HI">, DwarfRegNum<[18]>;
  def LO   : Register<"LO">, DwarfRegNum<[19]>;
}

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

def CPURegs : RegisterClass<"rvex", [i32], 32, 
  (add
    (sequence "R%u", 0, 63),

    LR, PC

  )>;

// Hi/Lo Registers
def HILO : RegisterClass<"rvex", [i32], 32, (add HI, LO)>;

def BRRegs   : RegisterClass<"rvex", [i32], 32, (add (sequence "B%u", 0, 7))>;
//def BRRegs   : RegisterClass<"rvex", [i32], 32, (add B0)>;

