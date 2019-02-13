//===-- EVMFixupKinds.h - EVM Specific Fixup Entries --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMFIXUPKINDS_H
#define LLVM_LIB_TARGET_EVM_MCTARGETDESC_EVMFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

#undef EVM

namespace llvm {
namespace EVM {
enum Fixups {
  // fixup_evm_hi20 - 20-bit fixup corresponding to hi(foo) for
  // instructions like lui
  fixup_evm_hi20 = FirstTargetFixupKind,
  // fixup_evm_lo12_i - 12-bit fixup corresponding to lo(foo) for
  // instructions like addi
  fixup_evm_lo12_i,
  // fixup_evm_lo12_s - 12-bit fixup corresponding to lo(foo) for
  // the S-type store instructions
  fixup_evm_lo12_s,
  // fixup_evm_pcrel_hi20 - 20-bit fixup corresponding to pcrel_hi(foo) for
  // instructions like auipc
  fixup_evm_pcrel_hi20,
  // fixup_evm_pcrel_lo12_i - 12-bit fixup corresponding to pcrel_lo(foo) for
  // instructions like addi
  fixup_evm_pcrel_lo12_i,
  // fixup_evm_pcrel_lo12_s - 12-bit fixup corresponding to pcrel_lo(foo) for
  // the S-type store instructions
  fixup_evm_pcrel_lo12_s,
  // fixup_evm_jal - 20-bit fixup for symbol references in the jal
  // instruction
  fixup_evm_jal,
  // fixup_evm_branch - 12-bit fixup for symbol references in the branch
  // instructions
  fixup_evm_branch,
  // fixup_evm_rvc_jump - 11-bit fixup for symbol references in the
  // compressed jump instruction
  fixup_evm_rvc_jump,
  // fixup_evm_rvc_branch - 8-bit fixup for symbol references in the
  // compressed branch instruction
  fixup_evm_rvc_branch,
  // fixup_evm_call - A fixup representing a call attached to the auipc
  // instruction in a pair composed of adjacent auipc+jalr instructions.
  fixup_evm_call,
  // fixup_evm_relax - Used to generate an R_EVM_RELAX relocation type,
  // which indicates the linker may relax the instruction pair.
  fixup_evm_relax,
  // fixup_evm_align - Used to generate an R_EVM_ALIGN relocation type,
  // which indicates the linker should fixup the alignment after linker
  // relaxation.
  fixup_evm_align,

  // fixup_evm_invalid - used as a sentinel and a marker, must be last fixup
  fixup_evm_invalid,
  NumTargetFixupKinds = fixup_evm_invalid - FirstTargetFixupKind
};
} // end namespace EVM
} // end namespace llvm

#endif
