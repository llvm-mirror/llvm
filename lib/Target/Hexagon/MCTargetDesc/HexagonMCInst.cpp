//===- HexagonMCInst.cpp - Hexagon sub-class of MCInst --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class extends MCInst to allow some Hexagon VLIW annotations.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInst.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"

using namespace llvm;

std::unique_ptr <MCInstrInfo const> HexagonMCInst::MCII;

HexagonMCInst::HexagonMCInst() : MCInst() {}
HexagonMCInst::HexagonMCInst(MCInstrDesc const &mcid) : MCInst() {}

void HexagonMCInst::AppendImplicitOperands(MCInst &MCI) {
  MCI.addOperand(MCOperand::CreateImm(0));
  MCI.addOperand(MCOperand::CreateInst(nullptr));
}

std::bitset<16> HexagonMCInst::GetImplicitBits(MCInst const &MCI) {
  SanityCheckImplicitOperands(MCI);
  std::bitset<16> Bits(MCI.getOperand(MCI.getNumOperands() - 2).getImm());
  return Bits;
}

void HexagonMCInst::SetImplicitBits(MCInst &MCI, std::bitset<16> Bits) {
  SanityCheckImplicitOperands(MCI);
  MCI.getOperand(MCI.getNumOperands() - 2).setImm(Bits.to_ulong());
}

void HexagonMCInst::setPacketBegin(bool f) {
  std::bitset<16> Bits(GetImplicitBits(*this));
  Bits.set(packetBeginIndex, f);
  SetImplicitBits(*this, Bits);
}

bool HexagonMCInst::isPacketBegin() const {
  std::bitset<16> Bits(GetImplicitBits(*this));
  return Bits.test(packetBeginIndex);
}

void HexagonMCInst::setPacketEnd(bool f) {
  std::bitset<16> Bits(GetImplicitBits(*this));
  Bits.set(packetEndIndex, f);
  SetImplicitBits(*this, Bits);
}

bool HexagonMCInst::isPacketEnd() const {
  std::bitset<16> Bits(GetImplicitBits(*this));
  return Bits.test(packetEndIndex);
}

void HexagonMCInst::resetPacket() {
  setPacketBegin(false);
  setPacketEnd(false);
}

MCInstrDesc const &HexagonMCInst::getDesc() const {
  return (MCII->get(getOpcode()));
}

// Return the Hexagon ISA class for the insn.
unsigned HexagonMCInst::getType() const {
  const uint64_t F = getDesc().TSFlags;

  return ((F >> HexagonII::TypePos) & HexagonII::TypeMask);
}

// Return whether the insn is an actual insn.
bool HexagonMCInst::isCanon() const {
  return (!getDesc().isPseudo() && !isPrefix() &&
          getType() != HexagonII::TypeENDLOOP);
}

// Return whether the insn is a prefix.
bool HexagonMCInst::isPrefix() const {
  return (getType() == HexagonII::TypePREFIX);
}

// Return whether the insn is solo, i.e., cannot be in a packet.
bool HexagonMCInst::isSolo() const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::SoloPos) & HexagonII::SoloMask);
}

// Return whether the insn is a new-value consumer.
bool HexagonMCInst::isNewValue() const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::NewValuePos) & HexagonII::NewValueMask);
}

// Return whether the instruction is a legal new-value producer.
bool HexagonMCInst::hasNewValue() const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::hasNewValuePos) & HexagonII::hasNewValueMask);
}

// Return the operand that consumes or produces a new value.
const MCOperand &HexagonMCInst::getNewValue() const {
  const uint64_t F = getDesc().TSFlags;
  const unsigned O =
      (F >> HexagonII::NewValueOpPos) & HexagonII::NewValueOpMask;
  const MCOperand &MCO = getOperand(O);

  assert((isNewValue() || hasNewValue()) && MCO.isReg());
  return (MCO);
}

// Return whether the instruction needs to be constant extended.
// 1) Always return true if the instruction has 'isExtended' flag set.
//
// isExtendable:
// 2) For immediate extended operands, return true only if the value is
//    out-of-range.
// 3) For global address, always return true.

bool HexagonMCInst::isConstExtended(void) const {
  if (isExtended())
    return true;

  if (!isExtendable())
    return false;

  short ExtOpNum = getCExtOpNum();
  int MinValue = getMinValue();
  int MaxValue = getMaxValue();
  const MCOperand &MO = getOperand(ExtOpNum);

  // We could be using an instruction with an extendable immediate and shoehorn
  // a global address into it. If it is a global address it will be constant
  // extended. We do this for COMBINE.
  // We currently only handle isGlobal() because it is the only kind of
  // object we are going to end up with here for now.
  // In the future we probably should add isSymbol(), etc.
  if (MO.isExpr())
    return true;

  // If the extendable operand is not 'Immediate' type, the instruction should
  // have 'isExtended' flag set.
  assert(MO.isImm() && "Extendable operand must be Immediate type");

  int ImmValue = MO.getImm();
  return (ImmValue < MinValue || ImmValue > MaxValue);
}

// Return whether the instruction must be always extended.
bool HexagonMCInst::isExtended(void) const {
  const uint64_t F = getDesc().TSFlags;
  return (F >> HexagonII::ExtendedPos) & HexagonII::ExtendedMask;
}

// Return true if the instruction may be extended based on the operand value.
bool HexagonMCInst::isExtendable(void) const {
  const uint64_t F = getDesc().TSFlags;
  return (F >> HexagonII::ExtendablePos) & HexagonII::ExtendableMask;
}

// Return number of bits in the constant extended operand.
unsigned HexagonMCInst::getBitCount(void) const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::ExtentBitsPos) & HexagonII::ExtentBitsMask);
}

// Return constant extended operand number.
unsigned short HexagonMCInst::getCExtOpNum(void) const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::ExtendableOpPos) & HexagonII::ExtendableOpMask);
}

// Return whether the operand can be constant extended.
bool HexagonMCInst::isOperandExtended(const unsigned short OperandNum) const {
  const uint64_t F = getDesc().TSFlags;
  return ((F >> HexagonII::ExtendableOpPos) & HexagonII::ExtendableOpMask) ==
         OperandNum;
}

// Return the min value that a constant extendable operand can have
// without being extended.
int HexagonMCInst::getMinValue(void) const {
  const uint64_t F = getDesc().TSFlags;
  unsigned isSigned =
      (F >> HexagonII::ExtentSignedPos) & HexagonII::ExtentSignedMask;
  unsigned bits = (F >> HexagonII::ExtentBitsPos) & HexagonII::ExtentBitsMask;

  if (isSigned) // if value is signed
    return -1U << (bits - 1);
  else
    return 0;
}

// Return the max value that a constant extendable operand can have
// without being extended.
int HexagonMCInst::getMaxValue(void) const {
  const uint64_t F = getDesc().TSFlags;
  unsigned isSigned =
      (F >> HexagonII::ExtentSignedPos) & HexagonII::ExtentSignedMask;
  unsigned bits = (F >> HexagonII::ExtentBitsPos) & HexagonII::ExtentBitsMask;

  if (isSigned) // if value is signed
    return ~(-1U << (bits - 1));
  else
    return ~(-1U << bits);
}
