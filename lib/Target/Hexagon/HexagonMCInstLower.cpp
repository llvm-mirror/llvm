//===- HexagonMCInstLower.cpp - Convert Hexagon MachineInstr to an MCInst -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Hexagon MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonAsmPrinter.h"
#include "HexagonMachineFunctionInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

namespace llvm {
  void HexagonLowerToMC(const MCInstrInfo &MCII, const MachineInstr *MI,
                        MCInst &MCB, HexagonAsmPrinter &AP);
}

static MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol,
                              HexagonAsmPrinter &Printer) {
  MCContext &MC = Printer.OutContext;
  const MCExpr *ME;

  // Populate the relocation type based on Hexagon target flags
  // set on an operand
  MCSymbolRefExpr::VariantKind RelocationType;
  switch (MO.getTargetFlags()) {
  default:
    RelocationType = MCSymbolRefExpr::VK_None;
    break;
  case HexagonII::MO_PCREL:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_PCREL;
    break;
  case HexagonII::MO_GOT:
    RelocationType = MCSymbolRefExpr::VK_GOT;
    break;
  case HexagonII::MO_LO16:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_LO16;
    break;
  case HexagonII::MO_HI16:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_HI16;
    break;
  case HexagonII::MO_GPREL:
    RelocationType = MCSymbolRefExpr::VK_Hexagon_GPREL;
    break;
  }

  ME = MCSymbolRefExpr::create(Symbol, RelocationType, MC);

  if (!MO.isJTI() && MO.getOffset())
    ME = MCBinaryExpr::createAdd(ME, MCConstantExpr::create(MO.getOffset(), MC),
                                 MC);

  return MCOperand::createExpr(ME);
}

// Create an MCInst from a MachineInstr
void llvm::HexagonLowerToMC(const MCInstrInfo &MCII, const MachineInstr *MI,
                            MCInst &MCB, HexagonAsmPrinter &AP) {
  if (MI->getOpcode() == Hexagon::ENDLOOP0) {
    HexagonMCInstrInfo::setInnerLoop(MCB);
    return;
  }
  if (MI->getOpcode() == Hexagon::ENDLOOP1) {
    HexagonMCInstrInfo::setOuterLoop(MCB);
    return;
  }
  MCInst *MCI = new (AP.OutContext) MCInst;
  MCI->setOpcode(MI->getOpcode());
  assert(MCI->getOpcode() == static_cast<unsigned>(MI->getOpcode()) &&
         "MCI opcode should have been set on construction");
  bool MustExtend = false;

  for (unsigned i = 0, e = MI->getNumOperands(); i < e; i++) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCO;
    if (MO.getTargetFlags() & HexagonII::HMOTF_ConstExtended)
      MustExtend = true;

    switch (MO.getType()) {
    default:
      MI->dump();
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands.
      if (MO.isImplicit()) continue;
      MCO = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_FPImmediate: {
      APFloat Val = MO.getFPImm()->getValueAPF();
      // FP immediates are used only when setting GPRs, so they may be dealt
      // with like regular immediates from this point on.
      MCO = MCOperand::createExpr(
        MCConstantExpr::create(*Val.bitcastToAPInt().getRawData(),
                               AP.OutContext));
      break;
    }
    case MachineOperand::MO_Immediate:
      MCO = MCOperand::createExpr(
        MCConstantExpr::create(MO.getImm(), AP.OutContext));
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MCO = MCOperand::createExpr
              (MCSymbolRefExpr::create(MO.getMBB()->getSymbol(),
               AP.OutContext));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCO = GetSymbolRef(MO, AP.getSymbol(MO.getGlobal()), AP);
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCO = GetSymbolRef(MO, AP.GetExternalSymbolSymbol(MO.getSymbolName()),
                         AP);
      break;
    case MachineOperand::MO_JumpTableIndex:
      MCO = GetSymbolRef(MO, AP.GetJTISymbol(MO.getIndex()), AP);
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCO = GetSymbolRef(MO, AP.GetCPISymbol(MO.getIndex()), AP);
      break;
    case MachineOperand::MO_BlockAddress:
      MCO = GetSymbolRef(MO, AP.GetBlockAddressSymbol(MO.getBlockAddress()),AP);
      break;
    }

    MCI->addOperand(MCO);
  }
  AP.HexagonProcessInstruction(*MCI, *MI);
  HexagonMCInstrInfo::extendIfNeeded(AP.OutContext, MCII, MCB, *MCI,
                                     MustExtend);
  MCB.addOperand(MCOperand::createInst(MCI));
}
