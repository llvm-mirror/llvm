//===-- EVMMCExpr.h - EVM specific MC expression classes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes EVM-specific MCExprs, used for modifiers like
// "%hi" or "%lo" etc.,
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMMCEXPR_H
#define LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMMCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

class StringRef;
class MCOperand;
class EVMMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_EVM_None,
    VK_EVM_LO,
    VK_EVM_HI,
    VK_EVM_PCREL_LO,
    VK_EVM_PCREL_HI,
    VK_EVM_CALL,
    VK_EVM_Invalid
  };

private:
  const MCExpr *Expr;
  const VariantKind Kind;

  int64_t evaluateAsInt64(int64_t Value) const;

  bool evaluatePCRelLo(MCValue &Res, const MCAsmLayout *Layout,
                       const MCFixup *Fixup) const;

  explicit EVMMCExpr(const MCExpr *Expr, VariantKind Kind)
      : Expr(Expr), Kind(Kind) {}

public:
  static const EVMMCExpr *create(const MCExpr *Expr, VariantKind Kind,
                                   MCContext &Ctx);

  VariantKind getKind() const { return Kind; }

  const MCExpr *getSubExpr() const { return Expr; }

  /// Get the MCExpr of the VK_EVM_PCREL_HI Fixup that the
  /// VK_EVM_PCREL_LO points to.
  ///
  /// \returns nullptr if this isn't a VK_EVM_PCREL_LO pointing to a
  /// VK_EVM_PCREL_HI.
  const MCFixup *getPCRelHiFixup() const;

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAsmLayout *Layout,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS EVMMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  bool evaluateAsConstant(int64_t &Res) const;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }

  static bool classof(const EVMMCExpr *) { return true; }

  static VariantKind getVariantKindForName(StringRef name);
  static StringRef getVariantKindName(VariantKind Kind);
};

} // end namespace llvm.

#endif
