//===-- SIInstrInfo.h - SI Instruction Info Interface -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Interface definition for SIInstrInfo.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_LIB_TARGET_R600_SIINSTRINFO_H
#define LLVM_LIB_TARGET_R600_SIINSTRINFO_H

#include "AMDGPUInstrInfo.h"
#include "SIDefines.h"
#include "SIRegisterInfo.h"

namespace llvm {

class SIInstrInfo : public AMDGPUInstrInfo {
private:
  const SIRegisterInfo RI;

  unsigned buildExtractSubReg(MachineBasicBlock::iterator MI,
                              MachineRegisterInfo &MRI,
                              MachineOperand &SuperReg,
                              const TargetRegisterClass *SuperRC,
                              unsigned SubIdx,
                              const TargetRegisterClass *SubRC) const;
  MachineOperand buildExtractSubRegOrImm(MachineBasicBlock::iterator MI,
                                         MachineRegisterInfo &MRI,
                                         MachineOperand &SuperReg,
                                         const TargetRegisterClass *SuperRC,
                                         unsigned SubIdx,
                                         const TargetRegisterClass *SubRC) const;

  unsigned split64BitImm(SmallVectorImpl<MachineInstr *> &Worklist,
                         MachineBasicBlock::iterator MI,
                         MachineRegisterInfo &MRI,
                         const TargetRegisterClass *RC,
                         const MachineOperand &Op) const;

  void swapOperands(MachineBasicBlock::iterator Inst) const;

  void splitScalar64BitUnaryOp(SmallVectorImpl<MachineInstr *> &Worklist,
                               MachineInstr *Inst, unsigned Opcode) const;

  void splitScalar64BitBinaryOp(SmallVectorImpl<MachineInstr *> &Worklist,
                                MachineInstr *Inst, unsigned Opcode) const;

  void splitScalar64BitBCNT(SmallVectorImpl<MachineInstr *> &Worklist,
                            MachineInstr *Inst) const;
  void splitScalar64BitBFE(SmallVectorImpl<MachineInstr *> &Worklist,
                           MachineInstr *Inst) const;

  bool checkInstOffsetsDoNotOverlap(MachineInstr *MIa,
                                    MachineInstr *MIb) const;

  unsigned findUsedSGPR(const MachineInstr *MI, int OpIndices[3]) const;

public:
  explicit SIInstrInfo(const AMDGPUSubtarget &st);

  const SIRegisterInfo &getRegisterInfo() const override {
    return RI;
  }

  bool isReallyTriviallyReMaterializable(const MachineInstr *MI,
                                         AliasAnalysis *AA) const override;

  bool areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                               int64_t &Offset1,
                               int64_t &Offset2) const override;

  bool getMemOpBaseRegImmOfs(MachineInstr *LdSt, unsigned &BaseReg,
                             unsigned &Offset,
                             const TargetRegisterInfo *TRI) const final;

  bool shouldClusterLoads(MachineInstr *FirstLdSt,
                          MachineInstr *SecondLdSt,
                          unsigned NumLoads) const final;

  void copyPhysReg(MachineBasicBlock &MBB,
                   MachineBasicBlock::iterator MI, DebugLoc DL,
                   unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

  unsigned calculateLDSSpillAddress(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MI,
                                    RegScavenger *RS,
                                    unsigned TmpReg,
                                    unsigned Offset,
                                    unsigned Size) const;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  bool expandPostRAPseudo(MachineBasicBlock::iterator MI) const override;

  // \brief Returns an opcode that can be used to move a value to a \p DstRC
  // register.  If there is no hardware instruction that can store to \p
  // DstRC, then AMDGPU::COPY is returned.
  unsigned getMovOpcode(const TargetRegisterClass *DstRC) const;
  int commuteOpcode(const MachineInstr &MI) const;

  MachineInstr *commuteInstruction(MachineInstr *MI,
                                   bool NewMI = false) const override;
  bool findCommutedOpIndices(MachineInstr *MI,
                             unsigned &SrcOpIdx1,
                             unsigned &SrcOpIdx2) const override;

  bool areMemAccessesTriviallyDisjoint(
    MachineInstr *MIa, MachineInstr *MIb,
    AliasAnalysis *AA = nullptr) const override;

  MachineInstr *buildMovInstr(MachineBasicBlock *MBB,
                              MachineBasicBlock::iterator I,
                              unsigned DstReg, unsigned SrcReg) const override;
  bool isMov(unsigned Opcode) const override;

  bool FoldImmediate(MachineInstr *UseMI, MachineInstr *DefMI,
                     unsigned Reg, MachineRegisterInfo *MRI) const final;

  unsigned getMachineCSELookAheadLimit() const override { return 500; }

  MachineInstr *convertToThreeAddress(MachineFunction::iterator &MBB,
                                      MachineBasicBlock::iterator &MI,
                                      LiveVariables *LV) const override;

  bool isSALU(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SALU;
  }

  bool isVALU(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VALU;
  }

  bool isSOP1(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOP1;
  }

  bool isSOP2(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOP2;
  }

  bool isSOPC(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPC;
  }

  bool isSOPK(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPK;
  }

  bool isSOPP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPP;
  }

  bool isVOP1(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP1;
  }

  bool isVOP2(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP2;
  }

  bool isVOP3(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP3;
  }

  bool isVOPC(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOPC;
  }

  bool isMUBUF(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MUBUF;
  }

  bool isMTBUF(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MTBUF;
  }

  bool isSMRD(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SMRD;
  }

  bool isDS(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::DS;
  }

  bool isMIMG(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MIMG;
  }

  bool isFLAT(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FLAT;
  }

  bool isWQM(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::WQM;
  }

  bool isVGPRSpill(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VGPRSpill;
  }

  bool isInlineConstant(const APInt &Imm) const;
  bool isInlineConstant(const MachineOperand &MO, unsigned OpSize) const;
  bool isLiteralConstant(const MachineOperand &MO, unsigned OpSize) const;

  bool isImmOperandLegal(const MachineInstr *MI, unsigned OpNo,
                         const MachineOperand &MO) const;

  /// \brief Return true if this 64-bit VALU instruction has a 32-bit encoding.
  /// This function will return false if you pass it a 32-bit instruction.
  bool hasVALU32BitEncoding(unsigned Opcode) const;

  /// \brief Returns true if this operand uses the constant bus.
  bool usesConstantBus(const MachineRegisterInfo &MRI,
                       const MachineOperand &MO,
                       unsigned OpSize) const;

  /// \brief Return true if this instruction has any modifiers.
  ///  e.g. src[012]_mod, omod, clamp.
  bool hasModifiers(unsigned Opcode) const;

  bool hasModifiersSet(const MachineInstr &MI,
                       unsigned OpName) const;

  bool verifyInstruction(const MachineInstr *MI,
                         StringRef &ErrInfo) const override;

  static unsigned getVALUOp(const MachineInstr &MI);

  bool isSALUOpSupportedOnVALU(const MachineInstr &MI) const;

  /// \brief Return the correct register class for \p OpNo.  For target-specific
  /// instructions, this will return the register class that has been defined
  /// in tablegen.  For generic instructions, like REG_SEQUENCE it will return
  /// the register class of its machine operand.
  /// to infer the correct register class base on the other operands.
  const TargetRegisterClass *getOpRegClass(const MachineInstr &MI,
                                           unsigned OpNo) const;

  /// \brief Return the size in bytes of the operand OpNo on the given
  // instruction opcode.
  unsigned getOpSize(uint16_t Opcode, unsigned OpNo) const {
    const MCOperandInfo &OpInfo = get(Opcode).OpInfo[OpNo];

    if (OpInfo.RegClass == -1) {
      // If this is an immediate operand, this must be a 32-bit literal.
      assert(OpInfo.OperandType == MCOI::OPERAND_IMMEDIATE);
      return 4;
    }

    return RI.getRegClass(OpInfo.RegClass)->getSize();
  }

  /// \brief This form should usually be preferred since it handles operands
  /// with unknown register classes.
  unsigned getOpSize(const MachineInstr &MI, unsigned OpNo) const {
    return getOpRegClass(MI, OpNo)->getSize();
  }

  /// \returns true if it is legal for the operand at index \p OpNo
  /// to read a VGPR.
  bool canReadVGPR(const MachineInstr &MI, unsigned OpNo) const;

  /// \brief Legalize the \p OpIndex operand of this instruction by inserting
  /// a MOV.  For example:
  /// ADD_I32_e32 VGPR0, 15
  /// to
  /// MOV VGPR1, 15
  /// ADD_I32_e32 VGPR0, VGPR1
  ///
  /// If the operand being legalized is a register, then a COPY will be used
  /// instead of MOV.
  void legalizeOpWithMove(MachineInstr *MI, unsigned OpIdx) const;

  /// \brief Check if \p MO is a legal operand if it was the \p OpIdx Operand
  /// for \p MI.
  bool isOperandLegal(const MachineInstr *MI, unsigned OpIdx,
                      const MachineOperand *MO = nullptr) const;

  /// \brief Legalize all operands in this instruction.  This function may
  /// create new instruction and insert them before \p MI.
  void legalizeOperands(MachineInstr *MI) const;

  /// \brief Split an SMRD instruction into two smaller loads of half the
  //  size storing the results in \p Lo and \p Hi.
  void splitSMRD(MachineInstr *MI, const TargetRegisterClass *HalfRC,
                 unsigned HalfImmOp, unsigned HalfSGPROp,
                 MachineInstr *&Lo, MachineInstr *&Hi) const;

  void moveSMRDToVALU(MachineInstr *MI, MachineRegisterInfo &MRI) const;

  /// \brief Replace this instruction's opcode with the equivalent VALU
  /// opcode.  This function will also move the users of \p MI to the
  /// VALU if necessary.
  void moveToVALU(MachineInstr &MI) const;

  unsigned calculateIndirectAddress(unsigned RegIndex,
                                    unsigned Channel) const override;

  const TargetRegisterClass *getIndirectAddrRegClass() const override;

  MachineInstrBuilder buildIndirectWrite(MachineBasicBlock *MBB,
                                         MachineBasicBlock::iterator I,
                                         unsigned ValueReg,
                                         unsigned Address,
                                         unsigned OffsetReg) const override;

  MachineInstrBuilder buildIndirectRead(MachineBasicBlock *MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned ValueReg,
                                        unsigned Address,
                                        unsigned OffsetReg) const override;
  void reserveIndirectRegisters(BitVector &Reserved,
                                const MachineFunction &MF) const;

  void LoadM0(MachineInstr *MoveRel, MachineBasicBlock::iterator I,
              unsigned SavReg, unsigned IndexReg) const;

  void insertNOPs(MachineBasicBlock::iterator MI, int Count) const;

  /// \brief Returns the operand named \p Op.  If \p MI does not have an
  /// operand named \c Op, this function returns nullptr.
  MachineOperand *getNamedOperand(MachineInstr &MI, unsigned OperandName) const;

  const MachineOperand *getNamedOperand(const MachineInstr &MI,
                                        unsigned OpName) const {
    return getNamedOperand(const_cast<MachineInstr &>(MI), OpName);
  }

  uint64_t getDefaultRsrcDataFormat() const;

};

namespace AMDGPU {

  int getVOPe64(uint16_t Opcode);
  int getVOPe32(uint16_t Opcode);
  int getCommuteRev(uint16_t Opcode);
  int getCommuteOrig(uint16_t Opcode);
  int getAddr64Inst(uint16_t Opcode);
  int getAtomicRetOp(uint16_t Opcode);
  int getAtomicNoRetOp(uint16_t Opcode);

  const uint64_t RSRC_DATA_FORMAT = 0xf00000000000LL;
  const uint64_t RSRC_TID_ENABLE = 1LL << 55;

} // End namespace AMDGPU

namespace SI {
namespace KernelInputOffsets {

/// Offsets in bytes from the start of the input buffer
enum Offsets {
  NGROUPS_X = 0,
  NGROUPS_Y = 4,
  NGROUPS_Z = 8,
  GLOBAL_SIZE_X = 12,
  GLOBAL_SIZE_Y = 16,
  GLOBAL_SIZE_Z = 20,
  LOCAL_SIZE_X = 24,
  LOCAL_SIZE_Y = 28,
  LOCAL_SIZE_Z = 32
};

} // End namespace KernelInputOffsets

// Metadata offset in imm_const_buffer_1
enum M2SMetadataOffsets {
  GLOBAL_SIZE_X                        = 0,
  GLOBAL_SIZE_Y                        = 4,
  GLOBAL_SIZE_Z                        = 8,
  WORK_DIM                             = 12,
  LOCAL_SIZE_X                         = 16,
  LOCAL_SIZE_Y                         = 20,
  LOCAL_SIZE_Z                         = 24,
  RESERVED_0                           = 28,
  NGROUPS_X                            = 32,
  NGROUPS_Y                            = 36,
  NGROUPS_Z                            = 40,
  RESERVED_1                           = 44,
  OFFSET_TO_PRIVATE_MEM_RING           = 48,
  PRIVATE_MEM_ALLOCATED_PER_THREAD     = 52,
  RESERVED_2                           = 56,
  RESERVED_3                           = 60,
  OFFSET_TO_LOCAL_MEM_RING             = 64,
  LOCAL_MEM_ALLOCATED_PER_GROUP        = 68,
  RESERVED_4                           = 72,
  PTR_MATH_LIB                         = 76,
  IEEE_32_FLOAT_ZERO                   = 80,
  IEEE_32_FLOAT_HALF                   = 84,
  IEEE_32_FLOAT_ONE                    = 88,
  IEEE_32_FLOAT_TWO                    = 92,
  GLOBAL_OFFSET_X_THREAD_SPWAN         = 96,
  GLOBAL_OFFSET_Y_THREAD_SPAWN         = 100,
  GLOBAL_OFFSET_Z_THREAD_SPAWN         = 104,
  GLOBAL_OFFSET_FLAT_THREAD_SPAWN      = 108,
  GROUP_OFFSET_X_THREAD_SPAWN          = 112,
  GROUP_OFFSET_Y_THREAD_SPAWN          = 116,
  GROUP_OFFSET_Z_THREAD_SPAWN          = 120,
  GROUP_OFFSET_FLAT_THREAD_SPAWN       = 124,
  OFFSET_DATA_SEG_GLOBAL_MEM           = 128,
  OFFSET_PRINT_SUPPORT                 = 132,
  SIZE_PRINT_BUFFER                    = 136,
  RESERVED_5                           = 140,
  GLOBAL_OFFSET_X                      = 144,
  GLOBAL_OFFSET_Y                      = 148,
  GLOBAL_OFFSET_Z                      = 152,
  RESERVED_6                           = 156,
  REGION_WORK_SIZE_X                   = 160,
  REGION_WORK_SIZE_Y                   = 164,
  REGION_WORK_SIZE_Z                   = 168,
  RESERVED_7                           = 172,
  GLOBAL_SIZE_X_DIV_REGION_WORK_SIZE_X = 176,
  GLOBAL_SIZE_Y_DIV_REGION_WORK_SIZE_Y = 180,
  GLOBAL_SIZE_Z_DIV_REGION_WORK_SIZE_Z = 184,
  RESERVED_8                           = 188,
  REGION_WORK_SIZE_X_DIV_LOCAL_SIZE_X  = 192,
  REGION_WORK_SIZE_Y_DIV_LOCAL_SIZE_Y  = 196,
  REGION_WORK_SIZE_Z_DIV_LOCAL_SIZE_Z  = 200,
  OFFSET_SW_GWS                        = 204
};

} // End namespace SI

} // End namespace llvm

#endif
