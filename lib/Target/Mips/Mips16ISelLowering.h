//===-- Mips16ISelLowering.h - Mips16 DAG Lowering Interface ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsTargetLowering specialized for mips16.
//
//===----------------------------------------------------------------------===//

#ifndef Mips16ISELLOWERING_H
#define Mips16ISELLOWERING_H

#include "MipsISelLowering.h"

namespace llvm {
  class Mips16TargetLowering : public MipsTargetLowering  {
  public:
    explicit Mips16TargetLowering(MipsTargetMachine &TM);

    virtual bool allowsUnalignedMemoryAccesses(EVT VT, bool *Fast) const;

    virtual MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr *MI, MachineBasicBlock *MBB) const;

  private:
    virtual bool
    isEligibleForTailCallOptimization(const MipsCC &MipsCCInfo,
                                      unsigned NextStackOffset,
                                      const MipsFunctionInfo& FI) const;

    void setMips16HardFloatLibCalls();

    unsigned int
      getMips16HelperFunctionStubNumber(ArgListTy &Args) const;

    const char *getMips16HelperFunction
      (Type* RetTy, ArgListTy &Args, bool &needHelper) const;

    virtual void
    getOpndList(SmallVectorImpl<SDValue> &Ops,
                std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
                bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
                CallLoweringInfo &CLI, SDValue Callee, SDValue Chain) const;

    MachineBasicBlock *emitSel16(unsigned Opc, MachineInstr *MI,
                                 MachineBasicBlock *BB) const;

    MachineBasicBlock *emitSeliT16(unsigned Opc1, unsigned Opc2,
                                   MachineInstr *MI,
                                   MachineBasicBlock *BB) const;

    MachineBasicBlock *emitSelT16(unsigned Opc1, unsigned Opc2,
                                  MachineInstr *MI,
                                  MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_T8I816_ins(unsigned BtOpc, unsigned CmpOpc,
                                           MachineInstr *MI,
                                           MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_T8I8I16_ins(
      unsigned BtOpc, unsigned CmpiOpc, unsigned CmpiXOpc, bool ImmSigned,
      MachineInstr *MI,  MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_CCRX16_ins(
      unsigned SltOpc,
      MachineInstr *MI,  MachineBasicBlock *BB) const;

    MachineBasicBlock *emitFEXT_CCRXI16_ins(
      unsigned SltiOpc, unsigned SltiXOpc,
      MachineInstr *MI,  MachineBasicBlock *BB )const;
  };
}

#endif // Mips16ISELLOWERING_H
