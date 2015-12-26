//===-- X86Disassembler.cpp - Disassembler for x86 and x86_64 -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler.
// It contains code to translate the data produced by the decoder into
//  MCInsts.
// Documentation for the disassembler can be found in X86Disassembler.h.
//
//===----------------------------------------------------------------------===//

#include "X86Disassembler.h"
#include "X86DisassemblerDecoder.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::X86Disassembler;

#define DEBUG_TYPE "x86-disassembler"

#define GET_REGINFO_ENUM
#include "X86GenRegisterInfo.inc"
#define GET_INSTRINFO_ENUM
#include "X86GenInstrInfo.inc"
#define GET_SUBTARGETINFO_ENUM
#include "X86GenSubtargetInfo.inc"

void llvm::X86Disassembler::Debug(const char *file, unsigned line,
                                  const char *s) {
  dbgs() << file << ":" << line << ": " << s;
}

const char *llvm::X86Disassembler::GetInstrName(unsigned Opcode,
                                                const void *mii) {
  const MCInstrInfo *MII = static_cast<const MCInstrInfo *>(mii);
  return MII->getName(Opcode);
}

#define debug(s) DEBUG(Debug(__FILE__, __LINE__, s));

namespace llvm {

// Fill-ins to make the compiler happy.  These constants are never actually
//   assigned; they are just filler to make an automatically-generated switch
//   statement work.
namespace X86 {
  enum {
    BX_SI = 500,
    BX_DI = 501,
    BP_SI = 502,
    BP_DI = 503,
    sib   = 504,
    sib64 = 505
  };
}

extern Target TheX86_32Target, TheX86_64Target;

}

static bool translateInstruction(MCInst &target,
                                InternalInstruction &source,
                                const MCDisassembler *Dis);

X86GenericDisassembler::X86GenericDisassembler(
                                         const MCSubtargetInfo &STI,
                                         MCContext &Ctx,
                                         std::unique_ptr<const MCInstrInfo> MII)
  : MCDisassembler(STI, Ctx), MII(std::move(MII)) {
  const FeatureBitset &FB = STI.getFeatureBits();
  if (FB[X86::Mode16Bit]) {
    fMode = MODE_16BIT;
    return;
  } else if (FB[X86::Mode32Bit]) {
    fMode = MODE_32BIT;
    return;
  } else if (FB[X86::Mode64Bit]) {
    fMode = MODE_64BIT;
    return;
  }

  llvm_unreachable("Invalid CPU mode");
}

namespace {
struct Region {
  ArrayRef<uint8_t> Bytes;
  uint64_t Base;
  Region(ArrayRef<uint8_t> Bytes, uint64_t Base) : Bytes(Bytes), Base(Base) {}
};
} // end anonymous namespace

/// A callback function that wraps the readByte method from Region.
///
/// @param Arg      - The generic callback parameter.  In this case, this should
///                   be a pointer to a Region.
/// @param Byte     - A pointer to the byte to be read.
/// @param Address  - The address to be read.
static int regionReader(const void *Arg, uint8_t *Byte, uint64_t Address) {
  auto *R = static_cast<const Region *>(Arg);
  ArrayRef<uint8_t> Bytes = R->Bytes;
  unsigned Index = Address - R->Base;
  if (Bytes.size() <= Index)
    return -1;
  *Byte = Bytes[Index];
  return 0;
}

/// logger - a callback function that wraps the operator<< method from
///   raw_ostream.
///
/// @param arg      - The generic callback parameter.  This should be a pointe
///                   to a raw_ostream.
/// @param log      - A string to be logged.  logger() adds a newline.
static void logger(void* arg, const char* log) {
  if (!arg)
    return;

  raw_ostream &vStream = *(static_cast<raw_ostream*>(arg));
  vStream << log << "\n";
}

//
// Public interface for the disassembler
//

MCDisassembler::DecodeStatus X86GenericDisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &VStream, raw_ostream &CStream) const {
  CommentStream = &CStream;

  InternalInstruction InternalInstr;

  dlog_t LoggerFn = logger;
  if (&VStream == &nulls())
    LoggerFn = nullptr; // Disable logging completely if it's going to nulls().

  Region R(Bytes, Address);

  int Ret = decodeInstruction(&InternalInstr, regionReader, (const void *)&R,
                              LoggerFn, (void *)&VStream,
                              (const void *)MII.get(), Address, fMode);

  if (Ret) {
    Size = InternalInstr.readerCursor - Address;
    return Fail;
  } else {
    Size = InternalInstr.length;
    return (!translateInstruction(Instr, InternalInstr, this)) ? Success : Fail;
  }
}

//
// Private code that translates from struct InternalInstructions to MCInsts.
//

/// translateRegister - Translates an internal register to the appropriate LLVM
///   register, and appends it as an operand to an MCInst.
///
/// @param mcInst     - The MCInst to append to.
/// @param reg        - The Reg to append.
static void translateRegister(MCInst &mcInst, Reg reg) {
#define ENTRY(x) X86::x,
  uint8_t llvmRegnums[] = {
    ALL_REGS
    0
  };
#undef ENTRY

  uint8_t llvmRegnum = llvmRegnums[reg];
  mcInst.addOperand(MCOperand::createReg(llvmRegnum));
}

/// tryAddingSymbolicOperand - trys to add a symbolic operand in place of the
/// immediate Value in the MCInst.
///
/// @param Value      - The immediate Value, has had any PC adjustment made by
///                     the caller.
/// @param isBranch   - If the instruction is a branch instruction
/// @param Address    - The starting address of the instruction
/// @param Offset     - The byte offset to this immediate in the instruction
/// @param Width      - The byte width of this immediate in the instruction
///
/// If the getOpInfo() function was set when setupForSymbolicDisassembly() was
/// called then that function is called to get any symbolic information for the
/// immediate in the instruction using the Address, Offset and Width.  If that
/// returns non-zero then the symbolic information it returns is used to create
/// an MCExpr and that is added as an operand to the MCInst.  If getOpInfo()
/// returns zero and isBranch is true then a symbol look up for immediate Value
/// is done and if a symbol is found an MCExpr is created with that, else
/// an MCExpr with the immediate Value is created.  This function returns true
/// if it adds an operand to the MCInst and false otherwise.
static bool tryAddingSymbolicOperand(int64_t Value, bool isBranch,
                                     uint64_t Address, uint64_t Offset,
                                     uint64_t Width, MCInst &MI,
                                     const MCDisassembler *Dis) {
  return Dis->tryAddingSymbolicOperand(MI, Value, Address, isBranch,
                                       Offset, Width);
}

/// tryAddingPcLoadReferenceComment - trys to add a comment as to what is being
/// referenced by a load instruction with the base register that is the rip.
/// These can often be addresses in a literal pool.  The Address of the
/// instruction and its immediate Value are used to determine the address
/// being referenced in the literal pool entry.  The SymbolLookUp call back will
/// return a pointer to a literal 'C' string if the referenced address is an
/// address into a section with 'C' string literals.
static void tryAddingPcLoadReferenceComment(uint64_t Address, uint64_t Value,
                                            const void *Decoder) {
  const MCDisassembler *Dis = static_cast<const MCDisassembler*>(Decoder);
  Dis->tryAddingPcLoadReferenceComment(Value, Address);
}

static const uint8_t segmentRegnums[SEG_OVERRIDE_max] = {
  0,        // SEG_OVERRIDE_NONE
  X86::CS,
  X86::SS,
  X86::DS,
  X86::ES,
  X86::FS,
  X86::GS
};

/// translateSrcIndex   - Appends a source index operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction.
static bool translateSrcIndex(MCInst &mcInst, InternalInstruction &insn) {
  unsigned baseRegNo;

  if (insn.mode == MODE_64BIT)
    baseRegNo = insn.prefixPresent[0x67] ? X86::ESI : X86::RSI;
  else if (insn.mode == MODE_32BIT)
    baseRegNo = insn.prefixPresent[0x67] ? X86::SI : X86::ESI;
  else {
    assert(insn.mode == MODE_16BIT);
    baseRegNo = insn.prefixPresent[0x67] ? X86::ESI : X86::SI;
  }
  MCOperand baseReg = MCOperand::createReg(baseRegNo);
  mcInst.addOperand(baseReg);

  MCOperand segmentReg;
  segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);
  mcInst.addOperand(segmentReg);
  return false;
}

/// translateDstIndex   - Appends a destination index operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction.

static bool translateDstIndex(MCInst &mcInst, InternalInstruction &insn) {
  unsigned baseRegNo;

  if (insn.mode == MODE_64BIT)
    baseRegNo = insn.prefixPresent[0x67] ? X86::EDI : X86::RDI;
  else if (insn.mode == MODE_32BIT)
    baseRegNo = insn.prefixPresent[0x67] ? X86::DI : X86::EDI;
  else {
    assert(insn.mode == MODE_16BIT);
    baseRegNo = insn.prefixPresent[0x67] ? X86::EDI : X86::DI;
  }
  MCOperand baseReg = MCOperand::createReg(baseRegNo);
  mcInst.addOperand(baseReg);
  return false;
}

/// translateImmediate  - Appends an immediate operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param immediate    - The immediate value to append.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The internal instruction.
static void translateImmediate(MCInst &mcInst, uint64_t immediate,
                               const OperandSpecifier &operand,
                               InternalInstruction &insn,
                               const MCDisassembler *Dis) {
  // Sign-extend the immediate if necessary.

  OperandType type = (OperandType)operand.type;

  bool isBranch = false;
  uint64_t pcrel = 0;
  if (type == TYPE_RELv) {
    isBranch = true;
    pcrel = insn.startLocation +
            insn.immediateOffset + insn.immediateSize;
    switch (insn.displacementSize) {
    default:
      break;
    case 1:
      if(immediate & 0x80)
        immediate |= ~(0xffull);
      break;
    case 2:
      if(immediate & 0x8000)
        immediate |= ~(0xffffull);
      break;
    case 4:
      if(immediate & 0x80000000)
        immediate |= ~(0xffffffffull);
      break;
    case 8:
      break;
    }
  }
  // By default sign-extend all X86 immediates based on their encoding.
  else if (type == TYPE_IMM8 || type == TYPE_IMM16 || type == TYPE_IMM32 ||
           type == TYPE_IMM64 || type == TYPE_IMMv) {
    switch (operand.encoding) {
    default:
      break;
    case ENCODING_IB:
      if(immediate & 0x80)
        immediate |= ~(0xffull);
      break;
    case ENCODING_IW:
      if(immediate & 0x8000)
        immediate |= ~(0xffffull);
      break;
    case ENCODING_ID:
      if(immediate & 0x80000000)
        immediate |= ~(0xffffffffull);
      break;
    case ENCODING_IO:
      break;
    }
  } else if (type == TYPE_IMM3) {
    // Check for immediates that printSSECC can't handle.
    if (immediate >= 8) {
      unsigned NewOpc;
      switch (mcInst.getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case X86::CMPPDrmi:  NewOpc = X86::CMPPDrmi_alt;  break;
      case X86::CMPPDrri:  NewOpc = X86::CMPPDrri_alt;  break;
      case X86::CMPPSrmi:  NewOpc = X86::CMPPSrmi_alt;  break;
      case X86::CMPPSrri:  NewOpc = X86::CMPPSrri_alt;  break;
      case X86::CMPSDrm:   NewOpc = X86::CMPSDrm_alt;   break;
      case X86::CMPSDrr:   NewOpc = X86::CMPSDrr_alt;   break;
      case X86::CMPSSrm:   NewOpc = X86::CMPSSrm_alt;   break;
      case X86::CMPSSrr:   NewOpc = X86::CMPSSrr_alt;   break;
      case X86::VPCOMBri:  NewOpc = X86::VPCOMBri_alt;  break;
      case X86::VPCOMBmi:  NewOpc = X86::VPCOMBmi_alt;  break;
      case X86::VPCOMWri:  NewOpc = X86::VPCOMWri_alt;  break;
      case X86::VPCOMWmi:  NewOpc = X86::VPCOMWmi_alt;  break;
      case X86::VPCOMDri:  NewOpc = X86::VPCOMDri_alt;  break;
      case X86::VPCOMDmi:  NewOpc = X86::VPCOMDmi_alt;  break;
      case X86::VPCOMQri:  NewOpc = X86::VPCOMQri_alt;  break;
      case X86::VPCOMQmi:  NewOpc = X86::VPCOMQmi_alt;  break;
      case X86::VPCOMUBri: NewOpc = X86::VPCOMUBri_alt; break;
      case X86::VPCOMUBmi: NewOpc = X86::VPCOMUBmi_alt; break;
      case X86::VPCOMUWri: NewOpc = X86::VPCOMUWri_alt; break;
      case X86::VPCOMUWmi: NewOpc = X86::VPCOMUWmi_alt; break;
      case X86::VPCOMUDri: NewOpc = X86::VPCOMUDri_alt; break;
      case X86::VPCOMUDmi: NewOpc = X86::VPCOMUDmi_alt; break;
      case X86::VPCOMUQri: NewOpc = X86::VPCOMUQri_alt; break;
      case X86::VPCOMUQmi: NewOpc = X86::VPCOMUQmi_alt; break;
      }
      // Switch opcode to the one that doesn't get special printing.
      mcInst.setOpcode(NewOpc);
    }
  } else if (type == TYPE_IMM5) {
    // Check for immediates that printAVXCC can't handle.
    if (immediate >= 32) {
      unsigned NewOpc;
      switch (mcInst.getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case X86::VCMPPDrmi:   NewOpc = X86::VCMPPDrmi_alt;   break;
      case X86::VCMPPDrri:   NewOpc = X86::VCMPPDrri_alt;   break;
      case X86::VCMPPSrmi:   NewOpc = X86::VCMPPSrmi_alt;   break;
      case X86::VCMPPSrri:   NewOpc = X86::VCMPPSrri_alt;   break;
      case X86::VCMPSDrm:    NewOpc = X86::VCMPSDrm_alt;    break;
      case X86::VCMPSDrr:    NewOpc = X86::VCMPSDrr_alt;    break;
      case X86::VCMPSSrm:    NewOpc = X86::VCMPSSrm_alt;    break;
      case X86::VCMPSSrr:    NewOpc = X86::VCMPSSrr_alt;    break;
      case X86::VCMPPDYrmi:  NewOpc = X86::VCMPPDYrmi_alt;  break;
      case X86::VCMPPDYrri:  NewOpc = X86::VCMPPDYrri_alt;  break;
      case X86::VCMPPSYrmi:  NewOpc = X86::VCMPPSYrmi_alt;  break;
      case X86::VCMPPSYrri:  NewOpc = X86::VCMPPSYrri_alt;  break;
      case X86::VCMPPDZrmi:  NewOpc = X86::VCMPPDZrmi_alt;  break;
      case X86::VCMPPDZrri:  NewOpc = X86::VCMPPDZrri_alt;  break;
      case X86::VCMPPDZrrib: NewOpc = X86::VCMPPDZrrib_alt; break;
      case X86::VCMPPSZrmi:  NewOpc = X86::VCMPPSZrmi_alt;  break;
      case X86::VCMPPSZrri:  NewOpc = X86::VCMPPSZrri_alt;  break;
      case X86::VCMPPSZrrib: NewOpc = X86::VCMPPSZrrib_alt; break;
      case X86::VCMPSDZrm:   NewOpc = X86::VCMPSDZrmi_alt;  break;
      case X86::VCMPSDZrr:   NewOpc = X86::VCMPSDZrri_alt;  break;
      case X86::VCMPSSZrm:   NewOpc = X86::VCMPSSZrmi_alt;  break;
      case X86::VCMPSSZrr:   NewOpc = X86::VCMPSSZrri_alt;  break;
      }
      // Switch opcode to the one that doesn't get special printing.
      mcInst.setOpcode(NewOpc);
    }
  } else if (type == TYPE_AVX512ICC) {
    if (immediate >= 8 || ((immediate & 0x3) == 3)) {
      unsigned NewOpc;
      switch (mcInst.getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case X86::VPCMPBZ128rmi:    NewOpc = X86::VPCMPBZ128rmi_alt;    break;
      case X86::VPCMPBZ128rmik:   NewOpc = X86::VPCMPBZ128rmik_alt;   break;
      case X86::VPCMPBZ128rri:    NewOpc = X86::VPCMPBZ128rri_alt;    break;
      case X86::VPCMPBZ128rrik:   NewOpc = X86::VPCMPBZ128rrik_alt;   break;
      case X86::VPCMPBZ256rmi:    NewOpc = X86::VPCMPBZ256rmi_alt;    break;
      case X86::VPCMPBZ256rmik:   NewOpc = X86::VPCMPBZ256rmik_alt;   break;
      case X86::VPCMPBZ256rri:    NewOpc = X86::VPCMPBZ256rri_alt;    break;
      case X86::VPCMPBZ256rrik:   NewOpc = X86::VPCMPBZ256rrik_alt;   break;
      case X86::VPCMPBZrmi:       NewOpc = X86::VPCMPBZrmi_alt;       break;
      case X86::VPCMPBZrmik:      NewOpc = X86::VPCMPBZrmik_alt;      break;
      case X86::VPCMPBZrri:       NewOpc = X86::VPCMPBZrri_alt;       break;
      case X86::VPCMPBZrrik:      NewOpc = X86::VPCMPBZrrik_alt;      break;
      case X86::VPCMPDZ128rmi:    NewOpc = X86::VPCMPDZ128rmi_alt;    break;
      case X86::VPCMPDZ128rmib:   NewOpc = X86::VPCMPDZ128rmib_alt;   break;
      case X86::VPCMPDZ128rmibk:  NewOpc = X86::VPCMPDZ128rmibk_alt;  break;
      case X86::VPCMPDZ128rmik:   NewOpc = X86::VPCMPDZ128rmik_alt;   break;
      case X86::VPCMPDZ128rri:    NewOpc = X86::VPCMPDZ128rri_alt;    break;
      case X86::VPCMPDZ128rrik:   NewOpc = X86::VPCMPDZ128rrik_alt;   break;
      case X86::VPCMPDZ256rmi:    NewOpc = X86::VPCMPDZ256rmi_alt;    break;
      case X86::VPCMPDZ256rmib:   NewOpc = X86::VPCMPDZ256rmib_alt;   break;
      case X86::VPCMPDZ256rmibk:  NewOpc = X86::VPCMPDZ256rmibk_alt;  break;
      case X86::VPCMPDZ256rmik:   NewOpc = X86::VPCMPDZ256rmik_alt;   break;
      case X86::VPCMPDZ256rri:    NewOpc = X86::VPCMPDZ256rri_alt;    break;
      case X86::VPCMPDZ256rrik:   NewOpc = X86::VPCMPDZ256rrik_alt;   break;
      case X86::VPCMPDZrmi:       NewOpc = X86::VPCMPDZrmi_alt;       break;
      case X86::VPCMPDZrmib:      NewOpc = X86::VPCMPDZrmib_alt;      break;
      case X86::VPCMPDZrmibk:     NewOpc = X86::VPCMPDZrmibk_alt;     break;
      case X86::VPCMPDZrmik:      NewOpc = X86::VPCMPDZrmik_alt;      break;
      case X86::VPCMPDZrri:       NewOpc = X86::VPCMPDZrri_alt;       break;
      case X86::VPCMPDZrrik:      NewOpc = X86::VPCMPDZrrik_alt;      break;
      case X86::VPCMPQZ128rmi:    NewOpc = X86::VPCMPQZ128rmi_alt;    break;
      case X86::VPCMPQZ128rmib:   NewOpc = X86::VPCMPQZ128rmib_alt;   break;
      case X86::VPCMPQZ128rmibk:  NewOpc = X86::VPCMPQZ128rmibk_alt;  break;
      case X86::VPCMPQZ128rmik:   NewOpc = X86::VPCMPQZ128rmik_alt;   break;
      case X86::VPCMPQZ128rri:    NewOpc = X86::VPCMPQZ128rri_alt;    break;
      case X86::VPCMPQZ128rrik:   NewOpc = X86::VPCMPQZ128rrik_alt;   break;
      case X86::VPCMPQZ256rmi:    NewOpc = X86::VPCMPQZ256rmi_alt;    break;
      case X86::VPCMPQZ256rmib:   NewOpc = X86::VPCMPQZ256rmib_alt;   break;
      case X86::VPCMPQZ256rmibk:  NewOpc = X86::VPCMPQZ256rmibk_alt;  break;
      case X86::VPCMPQZ256rmik:   NewOpc = X86::VPCMPQZ256rmik_alt;   break;
      case X86::VPCMPQZ256rri:    NewOpc = X86::VPCMPQZ256rri_alt;    break;
      case X86::VPCMPQZ256rrik:   NewOpc = X86::VPCMPQZ256rrik_alt;   break;
      case X86::VPCMPQZrmi:       NewOpc = X86::VPCMPQZrmi_alt;       break;
      case X86::VPCMPQZrmib:      NewOpc = X86::VPCMPQZrmib_alt;      break;
      case X86::VPCMPQZrmibk:     NewOpc = X86::VPCMPQZrmibk_alt;     break;
      case X86::VPCMPQZrmik:      NewOpc = X86::VPCMPQZrmik_alt;      break;
      case X86::VPCMPQZrri:       NewOpc = X86::VPCMPQZrri_alt;       break;
      case X86::VPCMPQZrrik:      NewOpc = X86::VPCMPQZrrik_alt;      break;
      case X86::VPCMPUBZ128rmi:   NewOpc = X86::VPCMPUBZ128rmi_alt;   break;
      case X86::VPCMPUBZ128rmik:  NewOpc = X86::VPCMPUBZ128rmik_alt;  break;
      case X86::VPCMPUBZ128rri:   NewOpc = X86::VPCMPUBZ128rri_alt;   break;
      case X86::VPCMPUBZ128rrik:  NewOpc = X86::VPCMPUBZ128rrik_alt;  break;
      case X86::VPCMPUBZ256rmi:   NewOpc = X86::VPCMPUBZ256rmi_alt;   break;
      case X86::VPCMPUBZ256rmik:  NewOpc = X86::VPCMPUBZ256rmik_alt;  break;
      case X86::VPCMPUBZ256rri:   NewOpc = X86::VPCMPUBZ256rri_alt;   break;
      case X86::VPCMPUBZ256rrik:  NewOpc = X86::VPCMPUBZ256rrik_alt;  break;
      case X86::VPCMPUBZrmi:      NewOpc = X86::VPCMPUBZrmi_alt;      break;
      case X86::VPCMPUBZrmik:     NewOpc = X86::VPCMPUBZrmik_alt;     break;
      case X86::VPCMPUBZrri:      NewOpc = X86::VPCMPUBZrri_alt;      break;
      case X86::VPCMPUBZrrik:     NewOpc = X86::VPCMPUBZrrik_alt;     break;
      case X86::VPCMPUDZ128rmi:   NewOpc = X86::VPCMPUDZ128rmi_alt;   break;
      case X86::VPCMPUDZ128rmib:  NewOpc = X86::VPCMPUDZ128rmib_alt;  break;
      case X86::VPCMPUDZ128rmibk: NewOpc = X86::VPCMPUDZ128rmibk_alt; break;
      case X86::VPCMPUDZ128rmik:  NewOpc = X86::VPCMPUDZ128rmik_alt;  break;
      case X86::VPCMPUDZ128rri:   NewOpc = X86::VPCMPUDZ128rri_alt;   break;
      case X86::VPCMPUDZ128rrik:  NewOpc = X86::VPCMPUDZ128rrik_alt;  break;
      case X86::VPCMPUDZ256rmi:   NewOpc = X86::VPCMPUDZ256rmi_alt;   break;
      case X86::VPCMPUDZ256rmib:  NewOpc = X86::VPCMPUDZ256rmib_alt;  break;
      case X86::VPCMPUDZ256rmibk: NewOpc = X86::VPCMPUDZ256rmibk_alt; break;
      case X86::VPCMPUDZ256rmik:  NewOpc = X86::VPCMPUDZ256rmik_alt;  break;
      case X86::VPCMPUDZ256rri:   NewOpc = X86::VPCMPUDZ256rri_alt;   break;
      case X86::VPCMPUDZ256rrik:  NewOpc = X86::VPCMPUDZ256rrik_alt;  break;
      case X86::VPCMPUDZrmi:      NewOpc = X86::VPCMPUDZrmi_alt;      break;
      case X86::VPCMPUDZrmib:     NewOpc = X86::VPCMPUDZrmib_alt;     break;
      case X86::VPCMPUDZrmibk:    NewOpc = X86::VPCMPUDZrmibk_alt;    break;
      case X86::VPCMPUDZrmik:     NewOpc = X86::VPCMPUDZrmik_alt;     break;
      case X86::VPCMPUDZrri:      NewOpc = X86::VPCMPUDZrri_alt;      break;
      case X86::VPCMPUDZrrik:     NewOpc = X86::VPCMPUDZrrik_alt;     break;
      case X86::VPCMPUQZ128rmi:   NewOpc = X86::VPCMPUQZ128rmi_alt;   break;
      case X86::VPCMPUQZ128rmib:  NewOpc = X86::VPCMPUQZ128rmib_alt;  break;
      case X86::VPCMPUQZ128rmibk: NewOpc = X86::VPCMPUQZ128rmibk_alt; break;
      case X86::VPCMPUQZ128rmik:  NewOpc = X86::VPCMPUQZ128rmik_alt;  break;
      case X86::VPCMPUQZ128rri:   NewOpc = X86::VPCMPUQZ128rri_alt;   break;
      case X86::VPCMPUQZ128rrik:  NewOpc = X86::VPCMPUQZ128rrik_alt;  break;
      case X86::VPCMPUQZ256rmi:   NewOpc = X86::VPCMPUQZ256rmi_alt;   break;
      case X86::VPCMPUQZ256rmib:  NewOpc = X86::VPCMPUQZ256rmib_alt;  break;
      case X86::VPCMPUQZ256rmibk: NewOpc = X86::VPCMPUQZ256rmibk_alt; break;
      case X86::VPCMPUQZ256rmik:  NewOpc = X86::VPCMPUQZ256rmik_alt;  break;
      case X86::VPCMPUQZ256rri:   NewOpc = X86::VPCMPUQZ256rri_alt;   break;
      case X86::VPCMPUQZ256rrik:  NewOpc = X86::VPCMPUQZ256rrik_alt;  break;
      case X86::VPCMPUQZrmi:      NewOpc = X86::VPCMPUQZrmi_alt;      break;
      case X86::VPCMPUQZrmib:     NewOpc = X86::VPCMPUQZrmib_alt;     break;
      case X86::VPCMPUQZrmibk:    NewOpc = X86::VPCMPUQZrmibk_alt;    break;
      case X86::VPCMPUQZrmik:     NewOpc = X86::VPCMPUQZrmik_alt;     break;
      case X86::VPCMPUQZrri:      NewOpc = X86::VPCMPUQZrri_alt;      break;
      case X86::VPCMPUQZrrik:     NewOpc = X86::VPCMPUQZrrik_alt;     break;
      case X86::VPCMPUWZ128rmi:   NewOpc = X86::VPCMPUWZ128rmi_alt;   break;
      case X86::VPCMPUWZ128rmik:  NewOpc = X86::VPCMPUWZ128rmik_alt;  break;
      case X86::VPCMPUWZ128rri:   NewOpc = X86::VPCMPUWZ128rri_alt;   break;
      case X86::VPCMPUWZ128rrik:  NewOpc = X86::VPCMPUWZ128rrik_alt;  break;
      case X86::VPCMPUWZ256rmi:   NewOpc = X86::VPCMPUWZ256rmi_alt;   break;
      case X86::VPCMPUWZ256rmik:  NewOpc = X86::VPCMPUWZ256rmik_alt;  break;
      case X86::VPCMPUWZ256rri:   NewOpc = X86::VPCMPUWZ256rri_alt;   break;
      case X86::VPCMPUWZ256rrik:  NewOpc = X86::VPCMPUWZ256rrik_alt;  break;
      case X86::VPCMPUWZrmi:      NewOpc = X86::VPCMPUWZrmi_alt;      break;
      case X86::VPCMPUWZrmik:     NewOpc = X86::VPCMPUWZrmik_alt;     break;
      case X86::VPCMPUWZrri:      NewOpc = X86::VPCMPUWZrri_alt;      break;
      case X86::VPCMPUWZrrik:     NewOpc = X86::VPCMPUWZrrik_alt;     break;
      case X86::VPCMPWZ128rmi:    NewOpc = X86::VPCMPWZ128rmi_alt;    break;
      case X86::VPCMPWZ128rmik:   NewOpc = X86::VPCMPWZ128rmik_alt;   break;
      case X86::VPCMPWZ128rri:    NewOpc = X86::VPCMPWZ128rri_alt;    break;
      case X86::VPCMPWZ128rrik:   NewOpc = X86::VPCMPWZ128rrik_alt;   break;
      case X86::VPCMPWZ256rmi:    NewOpc = X86::VPCMPWZ256rmi_alt;    break;
      case X86::VPCMPWZ256rmik:   NewOpc = X86::VPCMPWZ256rmik_alt;   break;
      case X86::VPCMPWZ256rri:    NewOpc = X86::VPCMPWZ256rri_alt;    break;
      case X86::VPCMPWZ256rrik:   NewOpc = X86::VPCMPWZ256rrik_alt;   break;
      case X86::VPCMPWZrmi:       NewOpc = X86::VPCMPWZrmi_alt;       break;
      case X86::VPCMPWZrmik:      NewOpc = X86::VPCMPWZrmik_alt;      break;
      case X86::VPCMPWZrri:       NewOpc = X86::VPCMPWZrri_alt;       break;
      case X86::VPCMPWZrrik:      NewOpc = X86::VPCMPWZrrik_alt;      break;
      }
      // Switch opcode to the one that doesn't get special printing.
      mcInst.setOpcode(NewOpc);
    }
  }

  switch (type) {
  case TYPE_XMM32:
  case TYPE_XMM64:
  case TYPE_XMM128:
    mcInst.addOperand(MCOperand::createReg(X86::XMM0 + (immediate >> 4)));
    return;
  case TYPE_XMM256:
    mcInst.addOperand(MCOperand::createReg(X86::YMM0 + (immediate >> 4)));
    return;
  case TYPE_XMM512:
    mcInst.addOperand(MCOperand::createReg(X86::ZMM0 + (immediate >> 4)));
    return;
  case TYPE_BNDR:
    mcInst.addOperand(MCOperand::createReg(X86::BND0 + (immediate >> 4)));
  case TYPE_REL8:
    isBranch = true;
    pcrel = insn.startLocation + insn.immediateOffset + insn.immediateSize;
    if (immediate & 0x80)
      immediate |= ~(0xffull);
    break;
  case TYPE_REL16:
    isBranch = true;
    pcrel = insn.startLocation + insn.immediateOffset + insn.immediateSize;
    if (immediate & 0x8000)
      immediate |= ~(0xffffull);
    break;
  case TYPE_REL32:
  case TYPE_REL64:
    isBranch = true;
    pcrel = insn.startLocation + insn.immediateOffset + insn.immediateSize;
    if(immediate & 0x80000000)
      immediate |= ~(0xffffffffull);
    break;
  default:
    // operand is 64 bits wide.  Do nothing.
    break;
  }

  if(!tryAddingSymbolicOperand(immediate + pcrel, isBranch, insn.startLocation,
                               insn.immediateOffset, insn.immediateSize,
                               mcInst, Dis))
    mcInst.addOperand(MCOperand::createImm(immediate));

  if (type == TYPE_MOFFS8 || type == TYPE_MOFFS16 ||
      type == TYPE_MOFFS32 || type == TYPE_MOFFS64) {
    MCOperand segmentReg;
    segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);
    mcInst.addOperand(segmentReg);
  }
}

/// translateRMRegister - Translates a register stored in the R/M field of the
///   ModR/M byte to its LLVM equivalent and appends it to an MCInst.
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction to extract the R/M field
///                       from.
/// @return             - 0 on success; -1 otherwise
static bool translateRMRegister(MCInst &mcInst,
                                InternalInstruction &insn) {
  if (insn.eaBase == EA_BASE_sib || insn.eaBase == EA_BASE_sib64) {
    debug("A R/M register operand may not have a SIB byte");
    return true;
  }

  switch (insn.eaBase) {
  default:
    debug("Unexpected EA base register");
    return true;
  case EA_BASE_NONE:
    debug("EA_BASE_NONE for ModR/M base");
    return true;
#define ENTRY(x) case EA_BASE_##x:
  ALL_EA_BASES
#undef ENTRY
    debug("A R/M register operand may not have a base; "
          "the operand must be a register.");
    return true;
#define ENTRY(x)                                                      \
  case EA_REG_##x:                                                    \
    mcInst.addOperand(MCOperand::createReg(X86::x)); break;
  ALL_REGS
#undef ENTRY
  }

  return false;
}

/// translateRMMemory - Translates a memory operand stored in the Mod and R/M
///   fields of an internal instruction (and possibly its SIB byte) to a memory
///   operand in LLVM's format, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The instruction to extract Mod, R/M, and SIB fields
///                       from.
/// @return             - 0 on success; nonzero otherwise
static bool translateRMMemory(MCInst &mcInst, InternalInstruction &insn,
                              const MCDisassembler *Dis) {
  // Addresses in an MCInst are represented as five operands:
  //   1. basereg       (register)  The R/M base, or (if there is a SIB) the
  //                                SIB base
  //   2. scaleamount   (immediate) 1, or (if there is a SIB) the specified
  //                                scale amount
  //   3. indexreg      (register)  x86_registerNONE, or (if there is a SIB)
  //                                the index (which is multiplied by the
  //                                scale amount)
  //   4. displacement  (immediate) 0, or the displacement if there is one
  //   5. segmentreg    (register)  x86_registerNONE for now, but could be set
  //                                if we have segment overrides

  MCOperand baseReg;
  MCOperand scaleAmount;
  MCOperand indexReg;
  MCOperand displacement;
  MCOperand segmentReg;
  uint64_t pcrel = 0;

  if (insn.eaBase == EA_BASE_sib || insn.eaBase == EA_BASE_sib64) {
    if (insn.sibBase != SIB_BASE_NONE) {
      switch (insn.sibBase) {
      default:
        debug("Unexpected sibBase");
        return true;
#define ENTRY(x)                                          \
      case SIB_BASE_##x:                                  \
        baseReg = MCOperand::createReg(X86::x); break;
      ALL_SIB_BASES
#undef ENTRY
      }
    } else {
      baseReg = MCOperand::createReg(0);
    }

    // Check whether we are handling VSIB addressing mode for GATHER.
    // If sibIndex was set to SIB_INDEX_NONE, index offset is 4 and
    // we should use SIB_INDEX_XMM4|YMM4 for VSIB.
    // I don't see a way to get the correct IndexReg in readSIB:
    //   We can tell whether it is VSIB or SIB after instruction ID is decoded,
    //   but instruction ID may not be decoded yet when calling readSIB.
    uint32_t Opcode = mcInst.getOpcode();
    bool IndexIs128 = (Opcode == X86::VGATHERDPDrm ||
                       Opcode == X86::VGATHERDPDYrm ||
                       Opcode == X86::VGATHERQPDrm ||
                       Opcode == X86::VGATHERDPSrm ||
                       Opcode == X86::VGATHERQPSrm ||
                       Opcode == X86::VPGATHERDQrm ||
                       Opcode == X86::VPGATHERDQYrm ||
                       Opcode == X86::VPGATHERQQrm ||
                       Opcode == X86::VPGATHERDDrm ||
                       Opcode == X86::VPGATHERQDrm);
    bool IndexIs256 = (Opcode == X86::VGATHERQPDYrm ||
                       Opcode == X86::VGATHERDPSYrm ||
                       Opcode == X86::VGATHERQPSYrm ||
                       Opcode == X86::VGATHERDPDZrm ||
                       Opcode == X86::VPGATHERDQZrm ||
                       Opcode == X86::VPGATHERQQYrm ||
                       Opcode == X86::VPGATHERDDYrm ||
                       Opcode == X86::VPGATHERQDYrm);
    bool IndexIs512 = (Opcode == X86::VGATHERQPDZrm ||
                       Opcode == X86::VGATHERDPSZrm ||
                       Opcode == X86::VGATHERQPSZrm ||
                       Opcode == X86::VPGATHERQQZrm ||
                       Opcode == X86::VPGATHERDDZrm ||
                       Opcode == X86::VPGATHERQDZrm);
    if (IndexIs128 || IndexIs256 || IndexIs512) {
      unsigned IndexOffset = insn.sibIndex -
                         (insn.addressSize == 8 ? SIB_INDEX_RAX:SIB_INDEX_EAX);
      SIBIndex IndexBase = IndexIs512 ? SIB_INDEX_ZMM0 :
                           IndexIs256 ? SIB_INDEX_YMM0 : SIB_INDEX_XMM0;
      insn.sibIndex = (SIBIndex)(IndexBase +
                           (insn.sibIndex == SIB_INDEX_NONE ? 4 : IndexOffset));
    }

    if (insn.sibIndex != SIB_INDEX_NONE) {
      switch (insn.sibIndex) {
      default:
        debug("Unexpected sibIndex");
        return true;
#define ENTRY(x)                                          \
      case SIB_INDEX_##x:                                 \
        indexReg = MCOperand::createReg(X86::x); break;
      EA_BASES_32BIT
      EA_BASES_64BIT
      REGS_XMM
      REGS_YMM
      REGS_ZMM
#undef ENTRY
      }
    } else {
      indexReg = MCOperand::createReg(0);
    }

    scaleAmount = MCOperand::createImm(insn.sibScale);
  } else {
    switch (insn.eaBase) {
    case EA_BASE_NONE:
      if (insn.eaDisplacement == EA_DISP_NONE) {
        debug("EA_BASE_NONE and EA_DISP_NONE for ModR/M base");
        return true;
      }
      if (insn.mode == MODE_64BIT){
        pcrel = insn.startLocation +
                insn.displacementOffset + insn.displacementSize;
        tryAddingPcLoadReferenceComment(insn.startLocation +
                                        insn.displacementOffset,
                                        insn.displacement + pcrel, Dis);
        baseReg = MCOperand::createReg(X86::RIP); // Section 2.2.1.6
      }
      else
        baseReg = MCOperand::createReg(0);

      indexReg = MCOperand::createReg(0);
      break;
    case EA_BASE_BX_SI:
      baseReg = MCOperand::createReg(X86::BX);
      indexReg = MCOperand::createReg(X86::SI);
      break;
    case EA_BASE_BX_DI:
      baseReg = MCOperand::createReg(X86::BX);
      indexReg = MCOperand::createReg(X86::DI);
      break;
    case EA_BASE_BP_SI:
      baseReg = MCOperand::createReg(X86::BP);
      indexReg = MCOperand::createReg(X86::SI);
      break;
    case EA_BASE_BP_DI:
      baseReg = MCOperand::createReg(X86::BP);
      indexReg = MCOperand::createReg(X86::DI);
      break;
    default:
      indexReg = MCOperand::createReg(0);
      switch (insn.eaBase) {
      default:
        debug("Unexpected eaBase");
        return true;
        // Here, we will use the fill-ins defined above.  However,
        //   BX_SI, BX_DI, BP_SI, and BP_DI are all handled above and
        //   sib and sib64 were handled in the top-level if, so they're only
        //   placeholders to keep the compiler happy.
#define ENTRY(x)                                        \
      case EA_BASE_##x:                                 \
        baseReg = MCOperand::createReg(X86::x); break;
      ALL_EA_BASES
#undef ENTRY
#define ENTRY(x) case EA_REG_##x:
      ALL_REGS
#undef ENTRY
        debug("A R/M memory operand may not be a register; "
              "the base field must be a base.");
        return true;
      }
    }

    scaleAmount = MCOperand::createImm(1);
  }

  displacement = MCOperand::createImm(insn.displacement);

  segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);

  mcInst.addOperand(baseReg);
  mcInst.addOperand(scaleAmount);
  mcInst.addOperand(indexReg);
  if(!tryAddingSymbolicOperand(insn.displacement + pcrel, false,
                               insn.startLocation, insn.displacementOffset,
                               insn.displacementSize, mcInst, Dis))
    mcInst.addOperand(displacement);
  mcInst.addOperand(segmentReg);
  return false;
}

/// translateRM - Translates an operand stored in the R/M (and possibly SIB)
///   byte of an instruction to LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The instruction to extract Mod, R/M, and SIB fields
///                       from.
/// @return             - 0 on success; nonzero otherwise
static bool translateRM(MCInst &mcInst, const OperandSpecifier &operand,
                        InternalInstruction &insn, const MCDisassembler *Dis) {
  switch (operand.type) {
  default:
    debug("Unexpected type for a R/M operand");
    return true;
  case TYPE_R8:
  case TYPE_R16:
  case TYPE_R32:
  case TYPE_R64:
  case TYPE_Rv:
  case TYPE_MM64:
  case TYPE_XMM:
  case TYPE_XMM32:
  case TYPE_XMM64:
  case TYPE_XMM128:
  case TYPE_XMM256:
  case TYPE_XMM512:
  case TYPE_VK1:
  case TYPE_VK2:
  case TYPE_VK4:
  case TYPE_VK8:
  case TYPE_VK16:
  case TYPE_VK32:
  case TYPE_VK64:
  case TYPE_DEBUGREG:
  case TYPE_CONTROLREG:
  case TYPE_BNDR:
    return translateRMRegister(mcInst, insn);
  case TYPE_M:
  case TYPE_M8:
  case TYPE_M16:
  case TYPE_M32:
  case TYPE_M64:
  case TYPE_M128:
  case TYPE_M256:
  case TYPE_M512:
  case TYPE_Mv:
  case TYPE_M32FP:
  case TYPE_M64FP:
  case TYPE_M80FP:
  case TYPE_M1616:
  case TYPE_M1632:
  case TYPE_M1664:
  case TYPE_LEA:
    return translateRMMemory(mcInst, insn, Dis);
  }
}

/// translateFPRegister - Translates a stack position on the FPU stack to its
///   LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param stackPos     - The stack position to translate.
static void translateFPRegister(MCInst &mcInst,
                                uint8_t stackPos) {
  mcInst.addOperand(MCOperand::createReg(X86::ST0 + stackPos));
}

/// translateMaskRegister - Translates a 3-bit mask register number to
///   LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param maskRegNum   - Number of mask register from 0 to 7.
/// @return             - false on success; true otherwise.
static bool translateMaskRegister(MCInst &mcInst,
                                uint8_t maskRegNum) {
  if (maskRegNum >= 8) {
    debug("Invalid mask register number");
    return true;
  }

  mcInst.addOperand(MCOperand::createReg(X86::K0 + maskRegNum));
  return false;
}

/// translateOperand - Translates an operand stored in an internal instruction
///   to LLVM's format and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The internal instruction.
/// @return             - false on success; true otherwise.
static bool translateOperand(MCInst &mcInst, const OperandSpecifier &operand,
                             InternalInstruction &insn,
                             const MCDisassembler *Dis) {
  switch (operand.encoding) {
  default:
    debug("Unhandled operand encoding during translation");
    return true;
  case ENCODING_REG:
    translateRegister(mcInst, insn.reg);
    return false;
  case ENCODING_WRITEMASK:
    return translateMaskRegister(mcInst, insn.writemask);
  CASE_ENCODING_RM:
    return translateRM(mcInst, operand, insn, Dis);
  case ENCODING_CB:
  case ENCODING_CW:
  case ENCODING_CD:
  case ENCODING_CP:
  case ENCODING_CO:
  case ENCODING_CT:
    debug("Translation of code offsets isn't supported.");
    return true;
  case ENCODING_IB:
  case ENCODING_IW:
  case ENCODING_ID:
  case ENCODING_IO:
  case ENCODING_Iv:
  case ENCODING_Ia:
    translateImmediate(mcInst,
                       insn.immediates[insn.numImmediatesTranslated++],
                       operand,
                       insn,
                       Dis);
    return false;
  case ENCODING_SI:
    return translateSrcIndex(mcInst, insn);
  case ENCODING_DI:
    return translateDstIndex(mcInst, insn);
  case ENCODING_RB:
  case ENCODING_RW:
  case ENCODING_RD:
  case ENCODING_RO:
  case ENCODING_Rv:
    translateRegister(mcInst, insn.opcodeRegister);
    return false;
  case ENCODING_FP:
    translateFPRegister(mcInst, insn.modRM & 7);
    return false;
  case ENCODING_VVVV:
    translateRegister(mcInst, insn.vvvv);
    return false;
  case ENCODING_DUP:
    return translateOperand(mcInst, insn.operands[operand.type - TYPE_DUP0],
                            insn, Dis);
  }
}

/// translateInstruction - Translates an internal instruction and all its
///   operands to an MCInst.
///
/// @param mcInst       - The MCInst to populate with the instruction's data.
/// @param insn         - The internal instruction.
/// @return             - false on success; true otherwise.
static bool translateInstruction(MCInst &mcInst,
                                InternalInstruction &insn,
                                const MCDisassembler *Dis) {
  if (!insn.spec) {
    debug("Instruction has no specification");
    return true;
  }

  mcInst.clear();
  mcInst.setOpcode(insn.instructionID);
  // If when reading the prefix bytes we determined the overlapping 0xf2 or 0xf3
  // prefix bytes should be disassembled as xrelease and xacquire then set the
  // opcode to those instead of the rep and repne opcodes.
  if (insn.xAcquireRelease) {
    if(mcInst.getOpcode() == X86::REP_PREFIX)
      mcInst.setOpcode(X86::XRELEASE_PREFIX);
    else if(mcInst.getOpcode() == X86::REPNE_PREFIX)
      mcInst.setOpcode(X86::XACQUIRE_PREFIX);
  }

  insn.numImmediatesTranslated = 0;

  for (const auto &Op : insn.operands) {
    if (Op.encoding != ENCODING_NONE) {
      if (translateOperand(mcInst, Op, insn, Dis)) {
        return true;
      }
    }
  }

  return false;
}

static MCDisassembler *createX86Disassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  std::unique_ptr<const MCInstrInfo> MII(T.createMCInstrInfo());
  return new X86Disassembler::X86GenericDisassembler(STI, Ctx, std::move(MII));
}

extern "C" void LLVMInitializeX86Disassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(TheX86_32Target,
                                         createX86Disassembler);
  TargetRegistry::RegisterMCDisassembler(TheX86_64Target,
                                         createX86Disassembler);
}
