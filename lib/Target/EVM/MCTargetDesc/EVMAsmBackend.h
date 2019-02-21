//===-- EVMAsmBackend.h - EVM Assembler Backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMASMBACKEND_H
#define LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMASMBACKEND_H

#include "MCTargetDesc/EVMFixupKinds.h"
#include "MCTargetDesc/EVMMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
class MCAssembler;
class MCObjectTargetWriter;
class raw_ostream;

class EVMAsmBackend : public MCAsmBackend {

public:
  EVMAsmBackend(const MCSubtargetInfo &STI)
      : MCAsmBackend(support::big) {}
  ~EVMAsmBackend() override {}
};
}

#endif
