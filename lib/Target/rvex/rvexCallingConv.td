//===-- rvexCallingConv.td - Calling Conventions for rvex --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for rvex architecture.
//===----------------------------------------------------------------------===//

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A>:
  CCIf<!strconcat("State.getTarget().getSubtarget<rvexSubtarget>().", F), A>;

def RetCC_rvexEABI : CallingConv<[
  // i32 are returned in registers V0, V1, A0, A1
  CCIfType<[i32], CCAssignToReg<[R3, R4, R5, R6, R7, R8, R9, R10]>>
]>;

//===----------------------------------------------------------------------===//
// rvex EABI Calling Convention
//===----------------------------------------------------------------------===//

def CC_rvexEABI : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  // Integer values get stored in stack slots that are 4 bytes in
  // size and 4-byte aligned.
  CCIfType<[i32], CCAssignToStack<4, 4>>
]>;

//===----------------------------------------------------------------------===//
// rvex Calling Convention Dispatch
//===----------------------------------------------------------------------===//

def CC_rvex : CallingConv<[
  CCDelegateTo<CC_rvexEABI>
]>;


def RetCC_rvex : CallingConv<[
  CCDelegateTo<RetCC_rvexEABI>
]>;

def CSR_O32 : CalleeSavedRegs<(add LR, 
									(sequence "R%u", 57, 63))>;

