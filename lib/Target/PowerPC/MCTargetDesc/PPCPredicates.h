//===-- PPCPredicates.h - PPC Branch Predicate Information ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the PowerPC branch predicates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCPREDICATES_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCPREDICATES_H

// GCC #defines PPC on Linux but we use it as our namespace name
#undef PPC

// Generated files will use "namespace PPC". To avoid symbol clash,
// undefine PPC here. PPC may be predefined on some hosts.
#undef PPC

namespace llvm {
namespace PPC {
  /// Predicate - These are "(BI << 5) | BO"  for various predicates.
  enum Predicate {
    PRED_LT       = (0 << 5) | 12,
    PRED_LE       = (1 << 5) |  4,
    PRED_EQ       = (2 << 5) | 12,
    PRED_GE       = (0 << 5) |  4,
    PRED_GT       = (1 << 5) | 12,
    PRED_NE       = (2 << 5) |  4,
    PRED_UN       = (3 << 5) | 12,
    PRED_NU       = (3 << 5) |  4,
    PRED_LT_MINUS = (0 << 5) | 14,
    PRED_LE_MINUS = (1 << 5) |  6,
    PRED_EQ_MINUS = (2 << 5) | 14,
    PRED_GE_MINUS = (0 << 5) |  6,
    PRED_GT_MINUS = (1 << 5) | 14,
    PRED_NE_MINUS = (2 << 5) |  6,
    PRED_UN_MINUS = (3 << 5) | 14,
    PRED_NU_MINUS = (3 << 5) |  6,
    PRED_LT_PLUS  = (0 << 5) | 15,
    PRED_LE_PLUS  = (1 << 5) |  7,
    PRED_EQ_PLUS  = (2 << 5) | 15,
    PRED_GE_PLUS  = (0 << 5) |  7,
    PRED_GT_PLUS  = (1 << 5) | 15,
    PRED_NE_PLUS  = (2 << 5) |  7,
    PRED_UN_PLUS  = (3 << 5) | 15,
    PRED_NU_PLUS  = (3 << 5) |  7,

    // When dealing with individual condition-register bits, we have simple set
    // and unset predicates.
    PRED_BIT_SET =   1024,
    PRED_BIT_UNSET = 1025
  };
  
  /// Invert the specified predicate.  != -> ==, < -> >=.
  Predicate InvertPredicate(Predicate Opcode);

  /// Assume the condition register is set by MI(a,b), return the predicate if
  /// we modify the instructions such that condition register is set by MI(b,a).
  Predicate getSwappedPredicate(Predicate Opcode);
}
}

#endif
