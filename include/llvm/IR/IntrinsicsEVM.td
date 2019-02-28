//===- IntrinsicsEVM.td - Defines EVM intrinsics -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the EVM-specific intrinsics.
//
//===----------------------------------------------------------------------===//

// Specialized loads from packet
let TargetPrefix = "evm" in {  // All intrinsics start with "llvm.evm."
  def int_evm_exp : GCCBuiltin<"__builtin_evm_exp">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty, llvm_i256_ty], [IntrNoMem]>;
  def int_evm_sha3 : GCCBuiltin<"__builtin_evm_sha3">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty, llvm_i256_ty], [IntrReadMem]>;
  def int_evm_address : GCCBuiltin<"__builtin_evm_address">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_balance : GCCBuiltin<"__builtin_evm_balance">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty], [IntrNoMem]>;
  def int_evm_origin : GCCBuiltin<"__builtin_evm_origin">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_caller : GCCBuiltin<"__builtin_evm_caller">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_callvalue : GCCBuiltin<"__builtin_evm_callvalue">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_calldataload : GCCBuiltin<"__builtin_evm_calldataload">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty], [IntrNoMem]>;
  def int_evm_calldatasize : GCCBuiltin<"__builtin_evm_calldatasize">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_calldatacopy : GCCBuiltin<"__builtin_evm_calldatacopy">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrWriteMem]>;
  def int_evm_codesize : GCCBuiltin<"__builtin_evm_codesize">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_codecopy : GCCBuiltin<"__builtin_evm_codecopy">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrWriteMem]>;
  def int_evm_gasprice : GCCBuiltin<"__builtin_evm_gasprice">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_extcodesize : GCCBuiltin<"__builtin_evm_extcodesize">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty], [IntrNoMem]>;
  def int_evm_extcodecopy : GCCBuiltin<"__builtin_evm_extcodecopy">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrWriteMem]>;
  def int_evm_returndatasize : GCCBuiltin<"__builtin_evm_returndatasize">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_returndatacopy : GCCBuiltin<"__builtin_evm_returndatacopy">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrWriteMem]>;

  // Those are getter intrinsics
  def int_evm_blockhash : GCCBuiltin<"__builtin_evm_blockhash">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty], [IntrNoMem]>;
  def int_evm_coinbase : GCCBuiltin<"__builtin_evm_coinbase">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_timestamp : GCCBuiltin<"__builtin_evm_timestamp">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_number : GCCBuiltin<"__builtin_evm_number">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_difficulty : GCCBuiltin<"__builtin_evm_difficulty">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_gaslimit : GCCBuiltin<"__builtin_evm_gaslimit">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;


  def int_evm_sload : GCCBuiltin<"__builtin_evm_sload">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty], [IntrNoMem]>;
  def int_evm_sstore : GCCBuiltin<"__builtin_evm_sstore">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;


  def int_evm_getpc : GCCBuiltin<"__builtin_evm_getpc">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_msize : GCCBuiltin<"__builtin_evm_msize">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;
  def int_evm_gas : GCCBuiltin<"__builtin_evm_gas">,
              Intrinsic<[llvm_i256_ty], [], [IntrNoMem]>;

  def int_evm_log0 : GCCBuiltin<"__builtin_evm_log0">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_evm_log1 : GCCBuiltin<"__builtin_evm_log1">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_evm_log2 : GCCBuiltin<"__builtin_evm_log2">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty,
                             llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_evm_log3 : GCCBuiltin<"__builtin_evm_log3">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty,
                             llvm_i256_ty, llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_evm_log4 : GCCBuiltin<"__builtin_evm_log4">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty,
                             llvm_i256_ty, llvm_i256_ty, llvm_i256_ty], [IntrNoMem, IntrHasSideEffects]>;

  def int_evm_create : GCCBuiltin<"__builtin_evm_create">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty],
                        [IntrWriteMem, IntrHasSideEffects]>;
  def int_evm_create2 : GCCBuiltin<"__builtin_evm_create2">,
              Intrinsic<[llvm_i256_ty], [llvm_i256_ty, llvm_i256_ty, llvm_i256_ty],
                        [IntrWriteMem, IntrHasSideEffects]>;

  def int_evm_revert : GCCBuiltin<"__builtin_evm_revert">,
              Intrinsic<[], [llvm_i256_ty, llvm_i256_ty], [IntrWriteMem, IntrHasSideEffects]>;

  def int_evm_selfdestruct : GCCBuiltin<"__builtin_evm_selfdestruct">,
              Intrinsic<[], [llvm_i256_ty], [IntrWriteMem, IntrHasSideEffects]>;
}
