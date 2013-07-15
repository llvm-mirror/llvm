//===-- AArch64BaseInfo.cpp - AArch64 Base encoding information------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides basic encoding and assembly information for AArch64.
//
//===----------------------------------------------------------------------===//
#include "AArch64BaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Regex.h"

using namespace llvm;

StringRef NamedImmMapper::toString(uint32_t Value, bool &Valid) const {
  for (unsigned i = 0; i < NumPairs; ++i) {
    if (Pairs[i].Value == Value) {
      Valid = true;
      return Pairs[i].Name;
    }
  }

  Valid = false;
  return StringRef();
}

uint32_t NamedImmMapper::fromString(StringRef Name, bool &Valid) const {
  std::string LowerCaseName = Name.lower();
  for (unsigned i = 0; i < NumPairs; ++i) {
    if (Pairs[i].Name == LowerCaseName) {
      Valid = true;
      return Pairs[i].Value;
    }
  }

  Valid = false;
  return -1;
}

bool NamedImmMapper::validImm(uint32_t Value) const {
  return Value < TooBigImm;
}

const NamedImmMapper::Mapping A64AT::ATMapper::ATPairs[] = {
  {"s1e1r", S1E1R},
  {"s1e2r", S1E2R},
  {"s1e3r", S1E3R},
  {"s1e1w", S1E1W},
  {"s1e2w", S1E2W},
  {"s1e3w", S1E3W},
  {"s1e0r", S1E0R},
  {"s1e0w", S1E0W},
  {"s12e1r", S12E1R},
  {"s12e1w", S12E1W},
  {"s12e0r", S12E0R},
  {"s12e0w", S12E0W},
};

A64AT::ATMapper::ATMapper()
  : NamedImmMapper(ATPairs, 0) {}

const NamedImmMapper::Mapping A64DB::DBarrierMapper::DBarrierPairs[] = {
  {"oshld", OSHLD},
  {"oshst", OSHST},
  {"osh", OSH},
  {"nshld", NSHLD},
  {"nshst", NSHST},
  {"nsh", NSH},
  {"ishld", ISHLD},
  {"ishst", ISHST},
  {"ish", ISH},
  {"ld", LD},
  {"st", ST},
  {"sy", SY}
};

A64DB::DBarrierMapper::DBarrierMapper()
  : NamedImmMapper(DBarrierPairs, 16u) {}

const NamedImmMapper::Mapping A64DC::DCMapper::DCPairs[] = {
  {"zva", ZVA},
  {"ivac", IVAC},
  {"isw", ISW},
  {"cvac", CVAC},
  {"csw", CSW},
  {"cvau", CVAU},
  {"civac", CIVAC},
  {"cisw", CISW}
};

A64DC::DCMapper::DCMapper()
  : NamedImmMapper(DCPairs, 0) {}

const NamedImmMapper::Mapping A64IC::ICMapper::ICPairs[] = {
  {"ialluis",  IALLUIS},
  {"iallu", IALLU},
  {"ivau", IVAU}
};

A64IC::ICMapper::ICMapper()
  : NamedImmMapper(ICPairs, 0) {}

const NamedImmMapper::Mapping A64ISB::ISBMapper::ISBPairs[] = {
  {"sy",  SY},
};

A64ISB::ISBMapper::ISBMapper()
  : NamedImmMapper(ISBPairs, 16) {}

const NamedImmMapper::Mapping A64PRFM::PRFMMapper::PRFMPairs[] = {
  {"pldl1keep", PLDL1KEEP},
  {"pldl1strm", PLDL1STRM},
  {"pldl2keep", PLDL2KEEP},
  {"pldl2strm", PLDL2STRM},
  {"pldl3keep", PLDL3KEEP},
  {"pldl3strm", PLDL3STRM},
  {"plil1keep", PLIL1KEEP},
  {"plil1strm", PLIL1STRM},
  {"plil2keep", PLIL2KEEP},
  {"plil2strm", PLIL2STRM},
  {"plil3keep", PLIL3KEEP},
  {"plil3strm", PLIL3STRM},
  {"pstl1keep", PSTL1KEEP},
  {"pstl1strm", PSTL1STRM},
  {"pstl2keep", PSTL2KEEP},
  {"pstl2strm", PSTL2STRM},
  {"pstl3keep", PSTL3KEEP},
  {"pstl3strm", PSTL3STRM}
};

A64PRFM::PRFMMapper::PRFMMapper()
  : NamedImmMapper(PRFMPairs, 32) {}

const NamedImmMapper::Mapping A64PState::PStateMapper::PStatePairs[] = {
  {"spsel", SPSel},
  {"daifset", DAIFSet},
  {"daifclr", DAIFClr}
};

A64PState::PStateMapper::PStateMapper()
  : NamedImmMapper(PStatePairs, 0) {}

const NamedImmMapper::Mapping A64SysReg::MRSMapper::MRSPairs[] = {
  {"mdccsr_el0", MDCCSR_EL0},
  {"dbgdtrrx_el0", DBGDTRRX_EL0},
  {"mdrar_el1", MDRAR_EL1},
  {"oslsr_el1", OSLSR_EL1},
  {"dbgauthstatus_el1", DBGAUTHSTATUS_EL1},
  {"pmceid0_el0", PMCEID0_EL0},
  {"pmceid1_el0", PMCEID1_EL0},
  {"midr_el1", MIDR_EL1},
  {"ccsidr_el1", CCSIDR_EL1},
  {"clidr_el1", CLIDR_EL1},
  {"ctr_el0", CTR_EL0},
  {"mpidr_el1", MPIDR_EL1},
  {"revidr_el1", REVIDR_EL1},
  {"aidr_el1", AIDR_EL1},
  {"dczid_el0", DCZID_EL0},
  {"id_pfr0_el1", ID_PFR0_EL1},
  {"id_pfr1_el1", ID_PFR1_EL1},
  {"id_dfr0_el1", ID_DFR0_EL1},
  {"id_afr0_el1", ID_AFR0_EL1},
  {"id_mmfr0_el1", ID_MMFR0_EL1},
  {"id_mmfr1_el1", ID_MMFR1_EL1},
  {"id_mmfr2_el1", ID_MMFR2_EL1},
  {"id_mmfr3_el1", ID_MMFR3_EL1},
  {"id_isar0_el1", ID_ISAR0_EL1},
  {"id_isar1_el1", ID_ISAR1_EL1},
  {"id_isar2_el1", ID_ISAR2_EL1},
  {"id_isar3_el1", ID_ISAR3_EL1},
  {"id_isar4_el1", ID_ISAR4_EL1},
  {"id_isar5_el1", ID_ISAR5_EL1},
  {"id_aa64pfr0_el1", ID_AA64PFR0_EL1},
  {"id_aa64pfr1_el1", ID_AA64PFR1_EL1},
  {"id_aa64dfr0_el1", ID_AA64DFR0_EL1},
  {"id_aa64dfr1_el1", ID_AA64DFR1_EL1},
  {"id_aa64afr0_el1", ID_AA64AFR0_EL1},
  {"id_aa64afr1_el1", ID_AA64AFR1_EL1},
  {"id_aa64isar0_el1", ID_AA64ISAR0_EL1},
  {"id_aa64isar1_el1", ID_AA64ISAR1_EL1},
  {"id_aa64mmfr0_el1", ID_AA64MMFR0_EL1},
  {"id_aa64mmfr1_el1", ID_AA64MMFR1_EL1},
  {"mvfr0_el1", MVFR0_EL1},
  {"mvfr1_el1", MVFR1_EL1},
  {"mvfr2_el1", MVFR2_EL1},
  {"rvbar_el1", RVBAR_EL1},
  {"rvbar_el2", RVBAR_EL2},
  {"rvbar_el3", RVBAR_EL3},
  {"isr_el1", ISR_EL1},
  {"cntpct_el0", CNTPCT_EL0},
  {"cntvct_el0", CNTVCT_EL0},

  // Trace registers
  {"trcstatr", TRCSTATR},
  {"trcidr8", TRCIDR8},
  {"trcidr9", TRCIDR9},
  {"trcidr10", TRCIDR10},
  {"trcidr11", TRCIDR11},
  {"trcidr12", TRCIDR12},
  {"trcidr13", TRCIDR13},
  {"trcidr0", TRCIDR0},
  {"trcidr1", TRCIDR1},
  {"trcidr2", TRCIDR2},
  {"trcidr3", TRCIDR3},
  {"trcidr4", TRCIDR4},
  {"trcidr5", TRCIDR5},
  {"trcidr6", TRCIDR6},
  {"trcidr7", TRCIDR7},
  {"trcoslsr", TRCOSLSR},
  {"trcpdsr", TRCPDSR},
  {"trcdevaff0", TRCDEVAFF0},
  {"trcdevaff1", TRCDEVAFF1},
  {"trclsr", TRCLSR},
  {"trcauthstatus", TRCAUTHSTATUS},
  {"trcdevarch", TRCDEVARCH},
  {"trcdevid", TRCDEVID},
  {"trcdevtype", TRCDEVTYPE},
  {"trcpidr4", TRCPIDR4},
  {"trcpidr5", TRCPIDR5},
  {"trcpidr6", TRCPIDR6},
  {"trcpidr7", TRCPIDR7},
  {"trcpidr0", TRCPIDR0},
  {"trcpidr1", TRCPIDR1},
  {"trcpidr2", TRCPIDR2},
  {"trcpidr3", TRCPIDR3},
  {"trccidr0", TRCCIDR0},
  {"trccidr1", TRCCIDR1},
  {"trccidr2", TRCCIDR2},
  {"trccidr3", TRCCIDR3},

  // GICv3 registers
  {"icc_iar1_el1", ICC_IAR1_EL1},
  {"icc_iar0_el1", ICC_IAR0_EL1},
  {"icc_hppir1_el1", ICC_HPPIR1_EL1},
  {"icc_hppir0_el1", ICC_HPPIR0_EL1},
  {"icc_rpr_el1", ICC_RPR_EL1},
  {"ich_vtr_el2", ICH_VTR_EL2},
  {"ich_eisr_el2", ICH_EISR_EL2},
  {"ich_elsr_el2", ICH_ELSR_EL2}
};

A64SysReg::MRSMapper::MRSMapper() {
    InstPairs = &MRSPairs[0];
    NumInstPairs = llvm::array_lengthof(MRSPairs);
}

const NamedImmMapper::Mapping A64SysReg::MSRMapper::MSRPairs[] = {
  {"dbgdtrtx_el0", DBGDTRTX_EL0},
  {"oslar_el1", OSLAR_EL1},
  {"pmswinc_el0", PMSWINC_EL0},

  // Trace registers
  {"trcoslar", TRCOSLAR},
  {"trclar", TRCLAR},

  // GICv3 registers
  {"icc_eoir1_el1", ICC_EOIR1_EL1},
  {"icc_eoir0_el1", ICC_EOIR0_EL1},
  {"icc_dir_el1", ICC_DIR_EL1},
  {"icc_sgi1r_el1", ICC_SGI1R_EL1},
  {"icc_asgi1r_el1", ICC_ASGI1R_EL1},
  {"icc_sgi0r_el1", ICC_SGI0R_EL1}
};

A64SysReg::MSRMapper::MSRMapper() {
    InstPairs = &MSRPairs[0];
    NumInstPairs = llvm::array_lengthof(MSRPairs);
}


const NamedImmMapper::Mapping A64SysReg::SysRegMapper::SysRegPairs[] = {
  {"osdtrrx_el1", OSDTRRX_EL1},
  {"osdtrtx_el1",  OSDTRTX_EL1},
  {"teecr32_el1", TEECR32_EL1},
  {"mdccint_el1", MDCCINT_EL1},
  {"mdscr_el1", MDSCR_EL1},
  {"dbgdtr_el0", DBGDTR_EL0},
  {"oseccr_el1", OSECCR_EL1},
  {"dbgvcr32_el2", DBGVCR32_EL2},
  {"dbgbvr0_el1", DBGBVR0_EL1},
  {"dbgbvr1_el1", DBGBVR1_EL1},
  {"dbgbvr2_el1", DBGBVR2_EL1},
  {"dbgbvr3_el1", DBGBVR3_EL1},
  {"dbgbvr4_el1", DBGBVR4_EL1},
  {"dbgbvr5_el1", DBGBVR5_EL1},
  {"dbgbvr6_el1", DBGBVR6_EL1},
  {"dbgbvr7_el1", DBGBVR7_EL1},
  {"dbgbvr8_el1", DBGBVR8_EL1},
  {"dbgbvr9_el1", DBGBVR9_EL1},
  {"dbgbvr10_el1", DBGBVR10_EL1},
  {"dbgbvr11_el1", DBGBVR11_EL1},
  {"dbgbvr12_el1", DBGBVR12_EL1},
  {"dbgbvr13_el1", DBGBVR13_EL1},
  {"dbgbvr14_el1", DBGBVR14_EL1},
  {"dbgbvr15_el1", DBGBVR15_EL1},
  {"dbgbcr0_el1", DBGBCR0_EL1},
  {"dbgbcr1_el1", DBGBCR1_EL1},
  {"dbgbcr2_el1", DBGBCR2_EL1},
  {"dbgbcr3_el1", DBGBCR3_EL1},
  {"dbgbcr4_el1", DBGBCR4_EL1},
  {"dbgbcr5_el1", DBGBCR5_EL1},
  {"dbgbcr6_el1", DBGBCR6_EL1},
  {"dbgbcr7_el1", DBGBCR7_EL1},
  {"dbgbcr8_el1", DBGBCR8_EL1},
  {"dbgbcr9_el1", DBGBCR9_EL1},
  {"dbgbcr10_el1", DBGBCR10_EL1},
  {"dbgbcr11_el1", DBGBCR11_EL1},
  {"dbgbcr12_el1", DBGBCR12_EL1},
  {"dbgbcr13_el1", DBGBCR13_EL1},
  {"dbgbcr14_el1", DBGBCR14_EL1},
  {"dbgbcr15_el1", DBGBCR15_EL1},
  {"dbgwvr0_el1", DBGWVR0_EL1},
  {"dbgwvr1_el1", DBGWVR1_EL1},
  {"dbgwvr2_el1", DBGWVR2_EL1},
  {"dbgwvr3_el1", DBGWVR3_EL1},
  {"dbgwvr4_el1", DBGWVR4_EL1},
  {"dbgwvr5_el1", DBGWVR5_EL1},
  {"dbgwvr6_el1", DBGWVR6_EL1},
  {"dbgwvr7_el1", DBGWVR7_EL1},
  {"dbgwvr8_el1", DBGWVR8_EL1},
  {"dbgwvr9_el1", DBGWVR9_EL1},
  {"dbgwvr10_el1", DBGWVR10_EL1},
  {"dbgwvr11_el1", DBGWVR11_EL1},
  {"dbgwvr12_el1", DBGWVR12_EL1},
  {"dbgwvr13_el1", DBGWVR13_EL1},
  {"dbgwvr14_el1", DBGWVR14_EL1},
  {"dbgwvr15_el1", DBGWVR15_EL1},
  {"dbgwcr0_el1", DBGWCR0_EL1},
  {"dbgwcr1_el1", DBGWCR1_EL1},
  {"dbgwcr2_el1", DBGWCR2_EL1},
  {"dbgwcr3_el1", DBGWCR3_EL1},
  {"dbgwcr4_el1", DBGWCR4_EL1},
  {"dbgwcr5_el1", DBGWCR5_EL1},
  {"dbgwcr6_el1", DBGWCR6_EL1},
  {"dbgwcr7_el1", DBGWCR7_EL1},
  {"dbgwcr8_el1", DBGWCR8_EL1},
  {"dbgwcr9_el1", DBGWCR9_EL1},
  {"dbgwcr10_el1", DBGWCR10_EL1},
  {"dbgwcr11_el1", DBGWCR11_EL1},
  {"dbgwcr12_el1", DBGWCR12_EL1},
  {"dbgwcr13_el1", DBGWCR13_EL1},
  {"dbgwcr14_el1", DBGWCR14_EL1},
  {"dbgwcr15_el1", DBGWCR15_EL1},
  {"teehbr32_el1", TEEHBR32_EL1},
  {"osdlr_el1", OSDLR_EL1},
  {"dbgprcr_el1", DBGPRCR_EL1},
  {"dbgclaimset_el1", DBGCLAIMSET_EL1},
  {"dbgclaimclr_el1", DBGCLAIMCLR_EL1},
  {"csselr_el1", CSSELR_EL1},
  {"vpidr_el2", VPIDR_EL2},
  {"vmpidr_el2", VMPIDR_EL2},
  {"sctlr_el1", SCTLR_EL1},
  {"sctlr_el2", SCTLR_EL2},
  {"sctlr_el3", SCTLR_EL3},
  {"actlr_el1", ACTLR_EL1},
  {"actlr_el2", ACTLR_EL2},
  {"actlr_el3", ACTLR_EL3},
  {"cpacr_el1", CPACR_EL1},
  {"hcr_el2", HCR_EL2},
  {"scr_el3", SCR_EL3},
  {"mdcr_el2", MDCR_EL2},
  {"sder32_el3", SDER32_EL3},
  {"cptr_el2", CPTR_EL2},
  {"cptr_el3", CPTR_EL3},
  {"hstr_el2", HSTR_EL2},
  {"hacr_el2", HACR_EL2},
  {"mdcr_el3", MDCR_EL3},
  {"ttbr0_el1", TTBR0_EL1},
  {"ttbr0_el2", TTBR0_EL2},
  {"ttbr0_el3", TTBR0_EL3},
  {"ttbr1_el1", TTBR1_EL1},
  {"tcr_el1", TCR_EL1},
  {"tcr_el2", TCR_EL2},
  {"tcr_el3", TCR_EL3},
  {"vttbr_el2", VTTBR_EL2},
  {"vtcr_el2", VTCR_EL2},
  {"dacr32_el2", DACR32_EL2},
  {"spsr_el1", SPSR_EL1},
  {"spsr_el2", SPSR_EL2},
  {"spsr_el3", SPSR_EL3},
  {"elr_el1", ELR_EL1},
  {"elr_el2", ELR_EL2},
  {"elr_el3", ELR_EL3},
  {"sp_el0", SP_EL0},
  {"sp_el1", SP_EL1},
  {"sp_el2", SP_EL2},
  {"spsel", SPSel},
  {"nzcv", NZCV},
  {"daif", DAIF},
  {"currentel", CurrentEL},
  {"spsr_irq", SPSR_irq},
  {"spsr_abt", SPSR_abt},
  {"spsr_und", SPSR_und},
  {"spsr_fiq", SPSR_fiq},
  {"fpcr", FPCR},
  {"fpsr", FPSR},
  {"dspsr_el0", DSPSR_EL0},
  {"dlr_el0", DLR_EL0},
  {"ifsr32_el2", IFSR32_EL2},
  {"afsr0_el1", AFSR0_EL1},
  {"afsr0_el2", AFSR0_EL2},
  {"afsr0_el3", AFSR0_EL3},
  {"afsr1_el1", AFSR1_EL1},
  {"afsr1_el2", AFSR1_EL2},
  {"afsr1_el3", AFSR1_EL3},
  {"esr_el1", ESR_EL1},
  {"esr_el2", ESR_EL2},
  {"esr_el3", ESR_EL3},
  {"fpexc32_el2", FPEXC32_EL2},
  {"far_el1", FAR_EL1},
  {"far_el2", FAR_EL2},
  {"far_el3", FAR_EL3},
  {"hpfar_el2", HPFAR_EL2},
  {"par_el1", PAR_EL1},
  {"pmcr_el0", PMCR_EL0},
  {"pmcntenset_el0", PMCNTENSET_EL0},
  {"pmcntenclr_el0", PMCNTENCLR_EL0},
  {"pmovsclr_el0", PMOVSCLR_EL0},
  {"pmselr_el0", PMSELR_EL0},
  {"pmccntr_el0", PMCCNTR_EL0},
  {"pmxevtyper_el0", PMXEVTYPER_EL0},
  {"pmxevcntr_el0", PMXEVCNTR_EL0},
  {"pmuserenr_el0", PMUSERENR_EL0},
  {"pmintenset_el1", PMINTENSET_EL1},
  {"pmintenclr_el1", PMINTENCLR_EL1},
  {"pmovsset_el0", PMOVSSET_EL0},
  {"mair_el1", MAIR_EL1},
  {"mair_el2", MAIR_EL2},
  {"mair_el3", MAIR_EL3},
  {"amair_el1", AMAIR_EL1},
  {"amair_el2", AMAIR_EL2},
  {"amair_el3", AMAIR_EL3},
  {"vbar_el1", VBAR_EL1},
  {"vbar_el2", VBAR_EL2},
  {"vbar_el3", VBAR_EL3},
  {"rmr_el1", RMR_EL1},
  {"rmr_el2", RMR_EL2},
  {"rmr_el3", RMR_EL3},
  {"contextidr_el1", CONTEXTIDR_EL1},
  {"tpidr_el0", TPIDR_EL0},
  {"tpidr_el2", TPIDR_EL2},
  {"tpidr_el3", TPIDR_EL3},
  {"tpidrro_el0", TPIDRRO_EL0},
  {"tpidr_el1", TPIDR_EL1},
  {"cntfrq_el0", CNTFRQ_EL0},
  {"cntvoff_el2", CNTVOFF_EL2},
  {"cntkctl_el1", CNTKCTL_EL1},
  {"cnthctl_el2", CNTHCTL_EL2},
  {"cntp_tval_el0", CNTP_TVAL_EL0},
  {"cnthp_tval_el2", CNTHP_TVAL_EL2},
  {"cntps_tval_el1", CNTPS_TVAL_EL1},
  {"cntp_ctl_el0", CNTP_CTL_EL0},
  {"cnthp_ctl_el2", CNTHP_CTL_EL2},
  {"cntps_ctl_el1", CNTPS_CTL_EL1},
  {"cntp_cval_el0", CNTP_CVAL_EL0},
  {"cnthp_cval_el2", CNTHP_CVAL_EL2},
  {"cntps_cval_el1", CNTPS_CVAL_EL1},
  {"cntv_tval_el0", CNTV_TVAL_EL0},
  {"cntv_ctl_el0", CNTV_CTL_EL0},
  {"cntv_cval_el0", CNTV_CVAL_EL0},
  {"pmevcntr0_el0", PMEVCNTR0_EL0},
  {"pmevcntr1_el0", PMEVCNTR1_EL0},
  {"pmevcntr2_el0", PMEVCNTR2_EL0},
  {"pmevcntr3_el0", PMEVCNTR3_EL0},
  {"pmevcntr4_el0", PMEVCNTR4_EL0},
  {"pmevcntr5_el0", PMEVCNTR5_EL0},
  {"pmevcntr6_el0", PMEVCNTR6_EL0},
  {"pmevcntr7_el0", PMEVCNTR7_EL0},
  {"pmevcntr8_el0", PMEVCNTR8_EL0},
  {"pmevcntr9_el0", PMEVCNTR9_EL0},
  {"pmevcntr10_el0", PMEVCNTR10_EL0},
  {"pmevcntr11_el0", PMEVCNTR11_EL0},
  {"pmevcntr12_el0", PMEVCNTR12_EL0},
  {"pmevcntr13_el0", PMEVCNTR13_EL0},
  {"pmevcntr14_el0", PMEVCNTR14_EL0},
  {"pmevcntr15_el0", PMEVCNTR15_EL0},
  {"pmevcntr16_el0", PMEVCNTR16_EL0},
  {"pmevcntr17_el0", PMEVCNTR17_EL0},
  {"pmevcntr18_el0", PMEVCNTR18_EL0},
  {"pmevcntr19_el0", PMEVCNTR19_EL0},
  {"pmevcntr20_el0", PMEVCNTR20_EL0},
  {"pmevcntr21_el0", PMEVCNTR21_EL0},
  {"pmevcntr22_el0", PMEVCNTR22_EL0},
  {"pmevcntr23_el0", PMEVCNTR23_EL0},
  {"pmevcntr24_el0", PMEVCNTR24_EL0},
  {"pmevcntr25_el0", PMEVCNTR25_EL0},
  {"pmevcntr26_el0", PMEVCNTR26_EL0},
  {"pmevcntr27_el0", PMEVCNTR27_EL0},
  {"pmevcntr28_el0", PMEVCNTR28_EL0},
  {"pmevcntr29_el0", PMEVCNTR29_EL0},
  {"pmevcntr30_el0", PMEVCNTR30_EL0},
  {"pmccfiltr_el0", PMCCFILTR_EL0},
  {"pmevtyper0_el0", PMEVTYPER0_EL0},
  {"pmevtyper1_el0", PMEVTYPER1_EL0},
  {"pmevtyper2_el0", PMEVTYPER2_EL0},
  {"pmevtyper3_el0", PMEVTYPER3_EL0},
  {"pmevtyper4_el0", PMEVTYPER4_EL0},
  {"pmevtyper5_el0", PMEVTYPER5_EL0},
  {"pmevtyper6_el0", PMEVTYPER6_EL0},
  {"pmevtyper7_el0", PMEVTYPER7_EL0},
  {"pmevtyper8_el0", PMEVTYPER8_EL0},
  {"pmevtyper9_el0", PMEVTYPER9_EL0},
  {"pmevtyper10_el0", PMEVTYPER10_EL0},
  {"pmevtyper11_el0", PMEVTYPER11_EL0},
  {"pmevtyper12_el0", PMEVTYPER12_EL0},
  {"pmevtyper13_el0", PMEVTYPER13_EL0},
  {"pmevtyper14_el0", PMEVTYPER14_EL0},
  {"pmevtyper15_el0", PMEVTYPER15_EL0},
  {"pmevtyper16_el0", PMEVTYPER16_EL0},
  {"pmevtyper17_el0", PMEVTYPER17_EL0},
  {"pmevtyper18_el0", PMEVTYPER18_EL0},
  {"pmevtyper19_el0", PMEVTYPER19_EL0},
  {"pmevtyper20_el0", PMEVTYPER20_EL0},
  {"pmevtyper21_el0", PMEVTYPER21_EL0},
  {"pmevtyper22_el0", PMEVTYPER22_EL0},
  {"pmevtyper23_el0", PMEVTYPER23_EL0},
  {"pmevtyper24_el0", PMEVTYPER24_EL0},
  {"pmevtyper25_el0", PMEVTYPER25_EL0},
  {"pmevtyper26_el0", PMEVTYPER26_EL0},
  {"pmevtyper27_el0", PMEVTYPER27_EL0},
  {"pmevtyper28_el0", PMEVTYPER28_EL0},
  {"pmevtyper29_el0", PMEVTYPER29_EL0},
  {"pmevtyper30_el0", PMEVTYPER30_EL0},

  // Trace registers
  {"trcprgctlr", TRCPRGCTLR},
  {"trcprocselr", TRCPROCSELR},
  {"trcconfigr", TRCCONFIGR},
  {"trcauxctlr", TRCAUXCTLR},
  {"trceventctl0r", TRCEVENTCTL0R},
  {"trceventctl1r", TRCEVENTCTL1R},
  {"trcstallctlr", TRCSTALLCTLR},
  {"trctsctlr", TRCTSCTLR},
  {"trcsyncpr", TRCSYNCPR},
  {"trcccctlr", TRCCCCTLR},
  {"trcbbctlr", TRCBBCTLR},
  {"trctraceidr", TRCTRACEIDR},
  {"trcqctlr", TRCQCTLR},
  {"trcvictlr", TRCVICTLR},
  {"trcviiectlr", TRCVIIECTLR},
  {"trcvissctlr", TRCVISSCTLR},
  {"trcvipcssctlr", TRCVIPCSSCTLR},
  {"trcvdctlr", TRCVDCTLR},
  {"trcvdsacctlr", TRCVDSACCTLR},
  {"trcvdarcctlr", TRCVDARCCTLR},
  {"trcseqevr0", TRCSEQEVR0},
  {"trcseqevr1", TRCSEQEVR1},
  {"trcseqevr2", TRCSEQEVR2},
  {"trcseqrstevr", TRCSEQRSTEVR},
  {"trcseqstr", TRCSEQSTR},
  {"trcextinselr", TRCEXTINSELR},
  {"trccntrldvr0", TRCCNTRLDVR0},
  {"trccntrldvr1", TRCCNTRLDVR1},
  {"trccntrldvr2", TRCCNTRLDVR2},
  {"trccntrldvr3", TRCCNTRLDVR3},
  {"trccntctlr0", TRCCNTCTLR0},
  {"trccntctlr1", TRCCNTCTLR1},
  {"trccntctlr2", TRCCNTCTLR2},
  {"trccntctlr3", TRCCNTCTLR3},
  {"trccntvr0", TRCCNTVR0},
  {"trccntvr1", TRCCNTVR1},
  {"trccntvr2", TRCCNTVR2},
  {"trccntvr3", TRCCNTVR3},
  {"trcimspec0", TRCIMSPEC0},
  {"trcimspec1", TRCIMSPEC1},
  {"trcimspec2", TRCIMSPEC2},
  {"trcimspec3", TRCIMSPEC3},
  {"trcimspec4", TRCIMSPEC4},
  {"trcimspec5", TRCIMSPEC5},
  {"trcimspec6", TRCIMSPEC6},
  {"trcimspec7", TRCIMSPEC7},
  {"trcrsctlr2", TRCRSCTLR2},
  {"trcrsctlr3", TRCRSCTLR3},
  {"trcrsctlr4", TRCRSCTLR4},
  {"trcrsctlr5", TRCRSCTLR5},
  {"trcrsctlr6", TRCRSCTLR6},
  {"trcrsctlr7", TRCRSCTLR7},
  {"trcrsctlr8", TRCRSCTLR8},
  {"trcrsctlr9", TRCRSCTLR9},
  {"trcrsctlr10", TRCRSCTLR10},
  {"trcrsctlr11", TRCRSCTLR11},
  {"trcrsctlr12", TRCRSCTLR12},
  {"trcrsctlr13", TRCRSCTLR13},
  {"trcrsctlr14", TRCRSCTLR14},
  {"trcrsctlr15", TRCRSCTLR15},
  {"trcrsctlr16", TRCRSCTLR16},
  {"trcrsctlr17", TRCRSCTLR17},
  {"trcrsctlr18", TRCRSCTLR18},
  {"trcrsctlr19", TRCRSCTLR19},
  {"trcrsctlr20", TRCRSCTLR20},
  {"trcrsctlr21", TRCRSCTLR21},
  {"trcrsctlr22", TRCRSCTLR22},
  {"trcrsctlr23", TRCRSCTLR23},
  {"trcrsctlr24", TRCRSCTLR24},
  {"trcrsctlr25", TRCRSCTLR25},
  {"trcrsctlr26", TRCRSCTLR26},
  {"trcrsctlr27", TRCRSCTLR27},
  {"trcrsctlr28", TRCRSCTLR28},
  {"trcrsctlr29", TRCRSCTLR29},
  {"trcrsctlr30", TRCRSCTLR30},
  {"trcrsctlr31", TRCRSCTLR31},
  {"trcssccr0", TRCSSCCR0},
  {"trcssccr1", TRCSSCCR1},
  {"trcssccr2", TRCSSCCR2},
  {"trcssccr3", TRCSSCCR3},
  {"trcssccr4", TRCSSCCR4},
  {"trcssccr5", TRCSSCCR5},
  {"trcssccr6", TRCSSCCR6},
  {"trcssccr7", TRCSSCCR7},
  {"trcsscsr0", TRCSSCSR0},
  {"trcsscsr1", TRCSSCSR1},
  {"trcsscsr2", TRCSSCSR2},
  {"trcsscsr3", TRCSSCSR3},
  {"trcsscsr4", TRCSSCSR4},
  {"trcsscsr5", TRCSSCSR5},
  {"trcsscsr6", TRCSSCSR6},
  {"trcsscsr7", TRCSSCSR7},
  {"trcsspcicr0", TRCSSPCICR0},
  {"trcsspcicr1", TRCSSPCICR1},
  {"trcsspcicr2", TRCSSPCICR2},
  {"trcsspcicr3", TRCSSPCICR3},
  {"trcsspcicr4", TRCSSPCICR4},
  {"trcsspcicr5", TRCSSPCICR5},
  {"trcsspcicr6", TRCSSPCICR6},
  {"trcsspcicr7", TRCSSPCICR7},
  {"trcpdcr", TRCPDCR},
  {"trcacvr0", TRCACVR0},
  {"trcacvr1", TRCACVR1},
  {"trcacvr2", TRCACVR2},
  {"trcacvr3", TRCACVR3},
  {"trcacvr4", TRCACVR4},
  {"trcacvr5", TRCACVR5},
  {"trcacvr6", TRCACVR6},
  {"trcacvr7", TRCACVR7},
  {"trcacvr8", TRCACVR8},
  {"trcacvr9", TRCACVR9},
  {"trcacvr10", TRCACVR10},
  {"trcacvr11", TRCACVR11},
  {"trcacvr12", TRCACVR12},
  {"trcacvr13", TRCACVR13},
  {"trcacvr14", TRCACVR14},
  {"trcacvr15", TRCACVR15},
  {"trcacatr0", TRCACATR0},
  {"trcacatr1", TRCACATR1},
  {"trcacatr2", TRCACATR2},
  {"trcacatr3", TRCACATR3},
  {"trcacatr4", TRCACATR4},
  {"trcacatr5", TRCACATR5},
  {"trcacatr6", TRCACATR6},
  {"trcacatr7", TRCACATR7},
  {"trcacatr8", TRCACATR8},
  {"trcacatr9", TRCACATR9},
  {"trcacatr10", TRCACATR10},
  {"trcacatr11", TRCACATR11},
  {"trcacatr12", TRCACATR12},
  {"trcacatr13", TRCACATR13},
  {"trcacatr14", TRCACATR14},
  {"trcacatr15", TRCACATR15},
  {"trcdvcvr0", TRCDVCVR0},
  {"trcdvcvr1", TRCDVCVR1},
  {"trcdvcvr2", TRCDVCVR2},
  {"trcdvcvr3", TRCDVCVR3},
  {"trcdvcvr4", TRCDVCVR4},
  {"trcdvcvr5", TRCDVCVR5},
  {"trcdvcvr6", TRCDVCVR6},
  {"trcdvcvr7", TRCDVCVR7},
  {"trcdvcmr0", TRCDVCMR0},
  {"trcdvcmr1", TRCDVCMR1},
  {"trcdvcmr2", TRCDVCMR2},
  {"trcdvcmr3", TRCDVCMR3},
  {"trcdvcmr4", TRCDVCMR4},
  {"trcdvcmr5", TRCDVCMR5},
  {"trcdvcmr6", TRCDVCMR6},
  {"trcdvcmr7", TRCDVCMR7},
  {"trccidcvr0", TRCCIDCVR0},
  {"trccidcvr1", TRCCIDCVR1},
  {"trccidcvr2", TRCCIDCVR2},
  {"trccidcvr3", TRCCIDCVR3},
  {"trccidcvr4", TRCCIDCVR4},
  {"trccidcvr5", TRCCIDCVR5},
  {"trccidcvr6", TRCCIDCVR6},
  {"trccidcvr7", TRCCIDCVR7},
  {"trcvmidcvr0", TRCVMIDCVR0},
  {"trcvmidcvr1", TRCVMIDCVR1},
  {"trcvmidcvr2", TRCVMIDCVR2},
  {"trcvmidcvr3", TRCVMIDCVR3},
  {"trcvmidcvr4", TRCVMIDCVR4},
  {"trcvmidcvr5", TRCVMIDCVR5},
  {"trcvmidcvr6", TRCVMIDCVR6},
  {"trcvmidcvr7", TRCVMIDCVR7},
  {"trccidcctlr0", TRCCIDCCTLR0},
  {"trccidcctlr1", TRCCIDCCTLR1},
  {"trcvmidcctlr0", TRCVMIDCCTLR0},
  {"trcvmidcctlr1", TRCVMIDCCTLR1},
  {"trcitctrl", TRCITCTRL},
  {"trcclaimset", TRCCLAIMSET},
  {"trcclaimclr", TRCCLAIMCLR},

  // GICv3 registers
  {"icc_bpr1_el1", ICC_BPR1_EL1},
  {"icc_bpr0_el1", ICC_BPR0_EL1},
  {"icc_pmr_el1", ICC_PMR_EL1},
  {"icc_ctlr_el1", ICC_CTLR_EL1},
  {"icc_ctlr_el3", ICC_CTLR_EL3},
  {"icc_sre_el1", ICC_SRE_EL1},
  {"icc_sre_el2", ICC_SRE_EL2},
  {"icc_sre_el3", ICC_SRE_EL3},
  {"icc_igrpen0_el1", ICC_IGRPEN0_EL1},
  {"icc_igrpen1_el1", ICC_IGRPEN1_EL1},
  {"icc_igrpen1_el3", ICC_IGRPEN1_EL3},
  {"icc_seien_el1", ICC_SEIEN_EL1},
  {"icc_ap0r0_el1", ICC_AP0R0_EL1},
  {"icc_ap0r1_el1", ICC_AP0R1_EL1},
  {"icc_ap0r2_el1", ICC_AP0R2_EL1},
  {"icc_ap0r3_el1", ICC_AP0R3_EL1},
  {"icc_ap1r0_el1", ICC_AP1R0_EL1},
  {"icc_ap1r1_el1", ICC_AP1R1_EL1},
  {"icc_ap1r2_el1", ICC_AP1R2_EL1},
  {"icc_ap1r3_el1", ICC_AP1R3_EL1},
  {"ich_ap0r0_el2", ICH_AP0R0_EL2},
  {"ich_ap0r1_el2", ICH_AP0R1_EL2},
  {"ich_ap0r2_el2", ICH_AP0R2_EL2},
  {"ich_ap0r3_el2", ICH_AP0R3_EL2},
  {"ich_ap1r0_el2", ICH_AP1R0_EL2},
  {"ich_ap1r1_el2", ICH_AP1R1_EL2},
  {"ich_ap1r2_el2", ICH_AP1R2_EL2},
  {"ich_ap1r3_el2", ICH_AP1R3_EL2},
  {"ich_hcr_el2", ICH_HCR_EL2},
  {"ich_misr_el2", ICH_MISR_EL2},
  {"ich_vmcr_el2", ICH_VMCR_EL2},
  {"ich_vseir_el2", ICH_VSEIR_EL2},
  {"ich_lr0_el2", ICH_LR0_EL2},
  {"ich_lr1_el2", ICH_LR1_EL2},
  {"ich_lr2_el2", ICH_LR2_EL2},
  {"ich_lr3_el2", ICH_LR3_EL2},
  {"ich_lr4_el2", ICH_LR4_EL2},
  {"ich_lr5_el2", ICH_LR5_EL2},
  {"ich_lr6_el2", ICH_LR6_EL2},
  {"ich_lr7_el2", ICH_LR7_EL2},
  {"ich_lr8_el2", ICH_LR8_EL2},
  {"ich_lr9_el2", ICH_LR9_EL2},
  {"ich_lr10_el2", ICH_LR10_EL2},
  {"ich_lr11_el2", ICH_LR11_EL2},
  {"ich_lr12_el2", ICH_LR12_EL2},
  {"ich_lr13_el2", ICH_LR13_EL2},
  {"ich_lr14_el2", ICH_LR14_EL2},
  {"ich_lr15_el2", ICH_LR15_EL2}
};

uint32_t
A64SysReg::SysRegMapper::fromString(StringRef Name, bool &Valid) const {
  // First search the registers shared by all
  std::string NameLower = Name.lower();
  for (unsigned i = 0; i < array_lengthof(SysRegPairs); ++i) {
    if (SysRegPairs[i].Name == NameLower) {
      Valid = true;
      return SysRegPairs[i].Value;
    }
  }

  // Now try the instruction-specific registers (either read-only or
  // write-only).
  for (unsigned i = 0; i < NumInstPairs; ++i) {
    if (InstPairs[i].Name == NameLower) {
      Valid = true;
      return InstPairs[i].Value;
    }
  }

  // Try to parse an S<op0>_<op1>_<Cn>_<Cm>_<op2> register name, where the bits
  // are: 11 xxx 1x11 xxxx xxx
  Regex GenericRegPattern("^s3_([0-7])_c(1[15])_c([0-9]|1[0-5])_([0-7])$");

  SmallVector<StringRef, 4> Ops;
  if (!GenericRegPattern.match(NameLower, &Ops)) {
    Valid = false;
    return -1;
  }

  uint32_t Op0 = 3, Op1 = 0, CRn = 0, CRm = 0, Op2 = 0;
  uint32_t Bits;
  Ops[1].getAsInteger(10, Op1);
  Ops[2].getAsInteger(10, CRn);
  Ops[3].getAsInteger(10, CRm);
  Ops[4].getAsInteger(10, Op2);
  Bits = (Op0 << 14) | (Op1 << 11) | (CRn << 7) | (CRm << 3) | Op2;

  Valid = true;
  return Bits;
}

std::string
A64SysReg::SysRegMapper::toString(uint32_t Bits, bool &Valid) const {
  for (unsigned i = 0; i < array_lengthof(SysRegPairs); ++i) {
    if (SysRegPairs[i].Value == Bits) {
      Valid = true;
      return SysRegPairs[i].Name;
    }
  }

  for (unsigned i = 0; i < NumInstPairs; ++i) {
    if (InstPairs[i].Value == Bits) {
      Valid = true;
      return InstPairs[i].Name;
    }
  }

  uint32_t Op0 = (Bits >> 14) & 0x3;
  uint32_t Op1 = (Bits >> 11) & 0x7;
  uint32_t CRn = (Bits >> 7) & 0xf;
  uint32_t CRm = (Bits >> 3) & 0xf;
  uint32_t Op2 = Bits & 0x7;

  // Only combinations matching: 11 xxx 1x11 xxxx xxx are valid for a generic
  // name.
  if (Op0 != 3 || (CRn != 11 && CRn != 15)) {
      Valid = false;
      return "";
  }

  assert(Op0 == 3 && (CRn == 11 || CRn == 15) && "Invalid generic sysreg");

  Valid = true;
  return "s3_" + utostr(Op1) + "_c" + utostr(CRn)
               + "_c" + utostr(CRm) + "_" + utostr(Op2);
}

const NamedImmMapper::Mapping A64TLBI::TLBIMapper::TLBIPairs[] = {
  {"ipas2e1is", IPAS2E1IS},
  {"ipas2le1is", IPAS2LE1IS},
  {"vmalle1is", VMALLE1IS},
  {"alle2is", ALLE2IS},
  {"alle3is", ALLE3IS},
  {"vae1is", VAE1IS},
  {"vae2is", VAE2IS},
  {"vae3is", VAE3IS},
  {"aside1is", ASIDE1IS},
  {"vaae1is", VAAE1IS},
  {"alle1is", ALLE1IS},
  {"vale1is", VALE1IS},
  {"vale2is", VALE2IS},
  {"vale3is", VALE3IS},
  {"vmalls12e1is", VMALLS12E1IS},
  {"vaale1is", VAALE1IS},
  {"ipas2e1", IPAS2E1},
  {"ipas2le1", IPAS2LE1},
  {"vmalle1", VMALLE1},
  {"alle2", ALLE2},
  {"alle3", ALLE3},
  {"vae1", VAE1},
  {"vae2", VAE2},
  {"vae3", VAE3},
  {"aside1", ASIDE1},
  {"vaae1", VAAE1},
  {"alle1", ALLE1},
  {"vale1", VALE1},
  {"vale2", VALE2},
  {"vale3", VALE3},
  {"vmalls12e1", VMALLS12E1},
  {"vaale1", VAALE1}
};

A64TLBI::TLBIMapper::TLBIMapper()
  : NamedImmMapper(TLBIPairs, 0) {}

bool A64Imms::isFPImm(const APFloat &Val, uint32_t &Imm8Bits) {
  const fltSemantics &Sem = Val.getSemantics();
  unsigned FracBits = APFloat::semanticsPrecision(Sem) - 1;

  uint32_t ExpMask;
  switch (FracBits) {
  case 10: // IEEE half-precision
    ExpMask = 0x1f;
    break;
  case 23: // IEEE single-precision
    ExpMask = 0xff;
    break;
  case 52: // IEEE double-precision
    ExpMask = 0x7ff;
    break;
  case 112: // IEEE quad-precision
    // No immediates are valid for double precision.
    return false;
  default:
    llvm_unreachable("Only half, single and double precision supported");
  }

  uint32_t ExpStart = FracBits;
  uint64_t FracMask = (1ULL << FracBits) - 1;

  uint32_t Sign = Val.isNegative();

  uint64_t Bits= Val.bitcastToAPInt().getLimitedValue();
  uint64_t Fraction = Bits & FracMask;
  int32_t Exponent = ((Bits >> ExpStart) & ExpMask);
  Exponent -= ExpMask >> 1;

  // S[d] = imm8<7>:NOT(imm8<6>):Replicate(imm8<6>, 5):imm8<5:0>:Zeros(19)
  // D[d] = imm8<7>:NOT(imm8<6>):Replicate(imm8<6>, 8):imm8<5:0>:Zeros(48)
  // This translates to: only 4 bits of fraction; -3 <= exp <= 4.
  uint64_t A64FracStart = FracBits - 4;
  uint64_t A64FracMask = 0xf;

  // Are there too many fraction bits?
  if (Fraction & ~(A64FracMask << A64FracStart))
    return false;

  if (Exponent < -3 || Exponent > 4)
    return false;

  uint32_t PackedFraction = (Fraction >> A64FracStart) & A64FracMask;
  uint32_t PackedExp = (Exponent + 7) & 0x7;

  Imm8Bits = (Sign << 7) | (PackedExp << 4) | PackedFraction;
  return true;
}

// Encoding of the immediate for logical (immediate) instructions:
//
// | N | imms   | immr   | size | R            | S            |
// |---+--------+--------+------+--------------+--------------|
// | 1 | ssssss | rrrrrr |   64 | UInt(rrrrrr) | UInt(ssssss) |
// | 0 | 0sssss | xrrrrr |   32 | UInt(rrrrr)  | UInt(sssss)  |
// | 0 | 10ssss | xxrrrr |   16 | UInt(rrrr)   | UInt(ssss)   |
// | 0 | 110sss | xxxrrr |    8 | UInt(rrr)    | UInt(sss)    |
// | 0 | 1110ss | xxxxrr |    4 | UInt(rr)     | UInt(ss)     |
// | 0 | 11110s | xxxxxr |    2 | UInt(r)      | UInt(s)      |
// | 0 | 11111x | -      |      | UNALLOCATED  |              |
//
// Columns 'R', 'S' and 'size' specify a "bitmask immediate" of size bits in
// which the lower S+1 bits are ones and the remaining bits are zero, then
// rotated right by R bits, which is then replicated across the datapath.
//
// + Values of 'N', 'imms' and 'immr' which do not match the above table are
//   RESERVED.
// + If all 's' bits in the imms field are set then the instruction is
//   RESERVED.
// + The 'x' bits in the 'immr' field are IGNORED.

bool A64Imms::isLogicalImm(unsigned RegWidth, uint64_t Imm, uint32_t &Bits) {
  int RepeatWidth;
  int Rotation = 0;
  int Num1s = 0;

  // Because there are S+1 ones in the replicated mask, an immediate of all
  // zeros is not allowed. Filtering it here is probably more efficient.
  if (Imm == 0) return false;

  for (RepeatWidth = RegWidth; RepeatWidth > 1; RepeatWidth /= 2) {
    uint64_t RepeatMask = RepeatWidth == 64 ? -1 : (1ULL << RepeatWidth) - 1;
    uint64_t ReplicatedMask = Imm & RepeatMask;

    if (ReplicatedMask == 0) continue;

    // First we have to make sure the mask is actually repeated in each slot for
    // this width-specifier.
    bool IsReplicatedMask = true;
    for (unsigned i = RepeatWidth; i < RegWidth; i += RepeatWidth) {
      if (((Imm >> i) & RepeatMask) != ReplicatedMask) {
        IsReplicatedMask = false;
        break;
      }
    }
    if (!IsReplicatedMask) continue;

    // Now we have to work out the amount of rotation needed. The first part of
    // this calculation is actually independent of RepeatWidth, but the complex
    // case will depend on it.
    Rotation = countTrailingZeros(Imm);
    if (Rotation == 0) {
      // There were no leading zeros, which means it's either in place or there
      // are 1s at each end (e.g. 0x8003 needs rotating).
      Rotation = RegWidth == 64 ? CountLeadingOnes_64(Imm)
                                : CountLeadingOnes_32(Imm);
      Rotation = RepeatWidth - Rotation;
    }

    uint64_t ReplicatedOnes = ReplicatedMask;
    if (Rotation != 0 && Rotation != 64)
      ReplicatedOnes = (ReplicatedMask >> Rotation)
        | ((ReplicatedMask << (RepeatWidth - Rotation)) & RepeatMask);

    // Of course, they may not actually be ones, so we have to check that:
    if (!isMask_64(ReplicatedOnes))
      continue;

    Num1s = CountTrailingOnes_64(ReplicatedOnes);

    // We know we've got an almost valid encoding (certainly, if this is invalid
    // no other parameters would work).
    break;
  }

  // The encodings which would produce all 1s are RESERVED.
  if (RepeatWidth == 1 || Num1s == RepeatWidth) return false;

  uint32_t N = RepeatWidth == 64;
  uint32_t ImmR = RepeatWidth - Rotation;
  uint32_t ImmS = Num1s - 1;

  switch (RepeatWidth) {
  default: break; // No action required for other valid rotations.
  case 16: ImmS |= 0x20; break; // 10ssss
  case 8: ImmS |= 0x30; break;  // 110sss
  case 4: ImmS |= 0x38; break;  // 1110ss
  case 2: ImmS |= 0x3c; break;  // 11110s
  }

  Bits = ImmS | (ImmR << 6) | (N << 12);

  return true;
}


bool A64Imms::isLogicalImmBits(unsigned RegWidth, uint32_t Bits,
                               uint64_t &Imm) {
  uint32_t N = Bits >> 12;
  uint32_t ImmR = (Bits >> 6) & 0x3f;
  uint32_t ImmS = Bits & 0x3f;

  // N=1 encodes a 64-bit replication and is invalid for the 32-bit
  // instructions.
  if (RegWidth == 32 && N != 0) return false;

  int Width = 0;
  if (N == 1)
    Width = 64;
  else if ((ImmS & 0x20) == 0)
    Width = 32;
  else if ((ImmS & 0x10) == 0)
    Width = 16;
  else if ((ImmS & 0x08) == 0)
    Width = 8;
  else if ((ImmS & 0x04) == 0)
    Width = 4;
  else if ((ImmS & 0x02) == 0)
    Width = 2;
  else {
    // ImmS  is 0b11111x: UNALLOCATED
    return false;
  }

  int Num1s = (ImmS & (Width - 1)) + 1;

  // All encodings which would map to -1 (signed) are RESERVED.
  if (Num1s == Width) return false;

  int Rotation = (ImmR & (Width - 1));
  uint64_t Mask = (1ULL << Num1s) - 1;
  uint64_t WidthMask = Width == 64 ? -1 : (1ULL << Width) - 1;
  if (Rotation != 0 && Rotation != 64)
    Mask = (Mask >> Rotation)
      | ((Mask << (Width - Rotation)) & WidthMask);

  Imm = Mask;
  for (unsigned i = 1; i < RegWidth / Width; ++i) {
    Mask <<= Width;
    Imm |= Mask;
  }

  return true;
}

bool A64Imms::isMOVZImm(int RegWidth, uint64_t Value, int &UImm16, int &Shift) {
  // If high bits are set then a 32-bit MOVZ can't possibly work.
  if (RegWidth == 32 && (Value & ~0xffffffffULL))
    return false;

  for (int i = 0; i < RegWidth; i += 16) {
    // If the value is 0 when we mask out all the bits that could be set with
    // the current LSL value then it's representable.
    if ((Value & ~(0xffffULL << i)) == 0) {
      Shift = i / 16;
      UImm16 = (Value >> i) & 0xffff;
      return true;
    }
  }
  return false;
}

bool A64Imms::isMOVNImm(int RegWidth, uint64_t Value, int &UImm16, int &Shift) {
  // MOVN is defined to set its register to NOT(LSL(imm16, shift)).

  // We have to be a little careful about a 32-bit register: 0xffff_1234 *is*
  // representable, but ~0xffff_1234 == 0xffff_ffff_0000_edcb which is not
  // a valid input for isMOVZImm.
  if (RegWidth == 32 && (Value & ~0xffffffffULL))
    return false;

  uint64_t MOVZEquivalent = RegWidth == 32 ? ~Value & 0xffffffff : ~Value;

  return isMOVZImm(RegWidth, MOVZEquivalent, UImm16, Shift);
}

bool A64Imms::isOnlyMOVNImm(int RegWidth, uint64_t Value,
                            int &UImm16, int &Shift) {
  if (isMOVZImm(RegWidth, Value, UImm16, Shift))
    return false;

  return isMOVNImm(RegWidth, Value, UImm16, Shift);
}
