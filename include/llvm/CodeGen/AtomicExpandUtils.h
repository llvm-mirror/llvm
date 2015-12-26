//===-- AtomicExpandUtils.h - Utilities for expanding atomic instructions -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
class Value;
class AtomicRMWInst;


/// Parameters (see the expansion example below):
/// (the builder, %addr, %loaded, %new_val, ordering,
///  /* OUT */ %success, /* OUT */ %new_loaded)
typedef function_ref<void(IRBuilder<> &, Value *, Value *, Value *,
                          AtomicOrdering, Value *&, Value *&)> CreateCmpXchgInstFun;

/// \brief Expand an atomic RMW instruction into a loop utilizing
/// cmpxchg. You'll want to make sure your target machine likes cmpxchg
/// instructions in the first place and that there isn't another, better,
/// transformation available (for example AArch32/AArch64 have linked loads).
///
/// This is useful in passes which can't rewrite the more exotic RMW
/// instructions directly into a platform specific intrinsics (because, say,
/// those intrinsics don't exist). If such a pass is able to expand cmpxchg
/// instructions directly however, then, with this function, it could avoid two
/// extra module passes (avoiding passes by `-atomic-expand` and itself). A
/// specific example would be PNaCl's `RewriteAtomics` pass.
///
/// Given: atomicrmw some_op iN* %addr, iN %incr ordering
///
/// The standard expansion we produce is:
///     [...]
///     %init_loaded = load atomic iN* %addr
///     br label %loop
/// loop:
///     %loaded = phi iN [ %init_loaded, %entry ], [ %new_loaded, %loop ]
///     %new = some_op iN %loaded, %incr
/// ; This is what -atomic-expand will produce using this function on i686 targets:
///     %pair = cmpxchg iN* %addr, iN %loaded, iN %new_val
///     %new_loaded = extractvalue { iN, i1 } %pair, 0
///     %success = extractvalue { iN, i1 } %pair, 1
/// ; End callback produced IR
///     br i1 %success, label %atomicrmw.end, label %loop
/// atomicrmw.end:
///     [...]
///
/// Returns true if the containing function was modified.
bool
expandAtomicRMWToCmpXchg(AtomicRMWInst *AI, CreateCmpXchgInstFun Factory);
}
