//===-- EVMCallingConv.td - Calling Conventions EVM --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the EVM architecture.
//
//===----------------------------------------------------------------------===//

def CC_EVM : CallingConv<[
  // Promote smaller type args to i256
  CCIfType<[ i8, i16, i32, i64, i128 ], CCPromoteToType<i256>>,

  // Could be assigned to the stack in 8-byte aligned units, but unsupported
  CCAssignToStack<32, 8>
]>;

def RetCC_EVM : CallingConv<[
  // Promote smaller type args to i256
  CCIfType<[ i8, i16, i32, i64, i128 ], CCPromoteToType<i256>>,
  CCAssignToStack<32, 8>
]>;


