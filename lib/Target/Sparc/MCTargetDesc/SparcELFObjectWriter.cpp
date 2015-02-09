//===-- SparcELFObjectWriter.cpp - Sparc ELF Writer -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcFixupKinds.h"
#include "MCTargetDesc/SparcMCExpr.h"
#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
  class SparcELFObjectWriter : public MCELFObjectTargetWriter {
  public:
    SparcELFObjectWriter(bool Is64Bit, uint8_t OSABI)
      : MCELFObjectTargetWriter(Is64Bit, OSABI,
                                Is64Bit ?  ELF::EM_SPARCV9 : ELF::EM_SPARC,
                                /*HasRelocationAddend*/ true) {}

    virtual ~SparcELFObjectWriter() {}
  protected:
    unsigned GetRelocType(const MCValue &Target, const MCFixup &Fixup,
                          bool IsPCRel) const override;
  };
}

unsigned SparcELFObjectWriter::GetRelocType(const MCValue &Target,
                                            const MCFixup &Fixup,
                                            bool IsPCRel) const {

  if (const SparcMCExpr *SExpr = dyn_cast<SparcMCExpr>(Fixup.getValue())) {
    if (SExpr->getKind() == SparcMCExpr::VK_Sparc_R_DISP32)
      return ELF::R_SPARC_DISP32;
  }

  if (IsPCRel) {
    switch((unsigned)Fixup.getKind()) {
    default:
      llvm_unreachable("Unimplemented fixup -> relocation");
    case FK_Data_1:                  return ELF::R_SPARC_DISP8;
    case FK_Data_2:                  return ELF::R_SPARC_DISP16;
    case FK_Data_4:                  return ELF::R_SPARC_DISP32;
    case FK_Data_8:                  return ELF::R_SPARC_DISP64;
    case Sparc::fixup_sparc_call30:  return ELF::R_SPARC_WDISP30;
    case Sparc::fixup_sparc_br22:    return ELF::R_SPARC_WDISP22;
    case Sparc::fixup_sparc_br19:    return ELF::R_SPARC_WDISP19;
    case Sparc::fixup_sparc_pc22:    return ELF::R_SPARC_PC22;
    case Sparc::fixup_sparc_pc10:    return ELF::R_SPARC_PC10;
    case Sparc::fixup_sparc_wplt30:  return ELF::R_SPARC_WPLT30;
    }
  }

  switch((unsigned)Fixup.getKind()) {
  default:
    llvm_unreachable("Unimplemented fixup -> relocation");
  case FK_Data_1:                return ELF::R_SPARC_8;
  case FK_Data_2:                return ((Fixup.getOffset() % 2)
                                         ? ELF::R_SPARC_UA16
                                         : ELF::R_SPARC_16);
  case FK_Data_4:                return ((Fixup.getOffset() % 4)
                                         ? ELF::R_SPARC_UA32
                                         : ELF::R_SPARC_32);
  case FK_Data_8:                return ((Fixup.getOffset() % 8)
                                         ? ELF::R_SPARC_UA64
                                         : ELF::R_SPARC_64);
  case Sparc::fixup_sparc_hi22:  return ELF::R_SPARC_HI22;
  case Sparc::fixup_sparc_lo10:  return ELF::R_SPARC_LO10;
  case Sparc::fixup_sparc_h44:   return ELF::R_SPARC_H44;
  case Sparc::fixup_sparc_m44:   return ELF::R_SPARC_M44;
  case Sparc::fixup_sparc_l44:   return ELF::R_SPARC_L44;
  case Sparc::fixup_sparc_hh:    return ELF::R_SPARC_HH22;
  case Sparc::fixup_sparc_hm:    return ELF::R_SPARC_HM10;
  case Sparc::fixup_sparc_got22: return ELF::R_SPARC_GOT22;
  case Sparc::fixup_sparc_got10: return ELF::R_SPARC_GOT10;
  case Sparc::fixup_sparc_tls_gd_hi22:   return ELF::R_SPARC_TLS_GD_HI22;
  case Sparc::fixup_sparc_tls_gd_lo10:   return ELF::R_SPARC_TLS_GD_LO10;
  case Sparc::fixup_sparc_tls_gd_add:    return ELF::R_SPARC_TLS_GD_ADD;
  case Sparc::fixup_sparc_tls_gd_call:   return ELF::R_SPARC_TLS_GD_CALL;
  case Sparc::fixup_sparc_tls_ldm_hi22:  return ELF::R_SPARC_TLS_LDM_HI22;
  case Sparc::fixup_sparc_tls_ldm_lo10:  return ELF::R_SPARC_TLS_LDM_LO10;
  case Sparc::fixup_sparc_tls_ldm_add:   return ELF::R_SPARC_TLS_LDM_ADD;
  case Sparc::fixup_sparc_tls_ldm_call:  return ELF::R_SPARC_TLS_LDM_CALL;
  case Sparc::fixup_sparc_tls_ldo_hix22: return ELF::R_SPARC_TLS_LDO_HIX22;
  case Sparc::fixup_sparc_tls_ldo_lox10: return ELF::R_SPARC_TLS_LDO_LOX10;
  case Sparc::fixup_sparc_tls_ldo_add:   return ELF::R_SPARC_TLS_LDO_ADD;
  case Sparc::fixup_sparc_tls_ie_hi22:   return ELF::R_SPARC_TLS_IE_HI22;
  case Sparc::fixup_sparc_tls_ie_lo10:   return ELF::R_SPARC_TLS_IE_LO10;
  case Sparc::fixup_sparc_tls_ie_ld:     return ELF::R_SPARC_TLS_IE_LD;
  case Sparc::fixup_sparc_tls_ie_ldx:    return ELF::R_SPARC_TLS_IE_LDX;
  case Sparc::fixup_sparc_tls_ie_add:    return ELF::R_SPARC_TLS_IE_ADD;
  case Sparc::fixup_sparc_tls_le_hix22:  return ELF::R_SPARC_TLS_LE_HIX22;
  case Sparc::fixup_sparc_tls_le_lox10:  return ELF::R_SPARC_TLS_LE_LOX10;
  }

  return ELF::R_SPARC_NONE;
}

MCObjectWriter *llvm::createSparcELFObjectWriter(raw_ostream &OS,
                                                 bool Is64Bit,
                                                 uint8_t OSABI) {
  MCELFObjectTargetWriter *MOTW = new SparcELFObjectWriter(Is64Bit, OSABI);
  return createELFObjectWriter(MOTW, OS,  /*IsLittleEndian=*/false);
}
