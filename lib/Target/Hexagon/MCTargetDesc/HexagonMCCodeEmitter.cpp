//===-- HexagonMCCodeEmitter.cpp - Hexagon Target Descriptions ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCCodeEmitter.h"
#include "MCTargetDesc/HexagonMCInst.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "mccodeemitter"

using namespace llvm;
using namespace Hexagon;

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");

namespace {
/// \brief 10.6 Instruction Packets
/// Possible values for instruction packet parse field.
enum class ParseField { duplex = 0x0, last0 = 0x1, last1 = 0x2, end = 0x3 };
/// \brief Returns the packet bits based on instruction position.
uint32_t getPacketBits(HexagonMCInst const &HMI) {
  unsigned const ParseFieldOffset = 14;
  ParseField Field = HMI.isPacketEnd() ? ParseField::end : ParseField::last0;
  return static_cast <uint32_t> (Field) << ParseFieldOffset;
}
void emitLittleEndian(uint64_t Binary, raw_ostream &OS) {
  OS << static_cast<uint8_t>((Binary >> 0x00) & 0xff);
  OS << static_cast<uint8_t>((Binary >> 0x08) & 0xff);
  OS << static_cast<uint8_t>((Binary >> 0x10) & 0xff);
  OS << static_cast<uint8_t>((Binary >> 0x18) & 0xff);
}
}

HexagonMCCodeEmitter::HexagonMCCodeEmitter(MCInstrInfo const &aMII,
                                           MCSubtargetInfo const &aMST,
                                           MCContext &aMCT)
    : MST(aMST), MCT(aMCT) {}

void HexagonMCCodeEmitter::EncodeInstruction(MCInst const &MI, raw_ostream &OS,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             MCSubtargetInfo const &STI) const {
  HexagonMCInst const &HMB = static_cast<HexagonMCInst const &>(MI);
  uint64_t Binary = getBinaryCodeForInstr(HMB, Fixups, STI) | getPacketBits(HMB);
  assert(HMB.getDesc().getSize() == 4 && "All instructions should be 32bit");
  emitLittleEndian(Binary, OS);
  ++MCNumEmitted;
}

unsigned
HexagonMCCodeEmitter::getMachineOpValue(MCInst const &MI, MCOperand const &MO,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        MCSubtargetInfo const &STI) const {
  if (MO.isReg())
    return MCT.getRegisterInfo()->getEncodingValue(MO.getReg());
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());
  llvm_unreachable("Only Immediates and Registers implemented right now");
}

MCSubtargetInfo const &HexagonMCCodeEmitter::getSubtargetInfo() const {
  return MST;
}

MCCodeEmitter *llvm::createHexagonMCCodeEmitter(MCInstrInfo const &MII,
                                                MCRegisterInfo const &MRI,
                                                MCSubtargetInfo const &MST,
                                                MCContext &MCT) {
  return new HexagonMCCodeEmitter(MII, MST, MCT);
}

#include "HexagonGenMCCodeEmitter.inc"
