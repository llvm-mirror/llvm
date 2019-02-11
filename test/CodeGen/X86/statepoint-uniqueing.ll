; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -verify-machineinstrs < %s | FileCheck %s

; Checks for a crash we had when two gc.relocate calls would
; relocating identical values

target datalayout = "e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

declare void @use(...)
declare void @f()
declare token @llvm.experimental.gc.statepoint.p0f_isVoidf(i64, i32, void ()*, i32, i32, ...)
declare i32 addrspace(1)* @llvm.experimental.gc.relocate.p1i32(token, i32, i32) #3
declare i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token, i32, i32) #3

define void @test_gcrelocate_uniqueing(i32 addrspace(1)* %ptr) gc "statepoint-example" {
; CHECK-LABEL: test_gcrelocate_uniqueing:
; CHECK:       # %bb.0:
; CHECK-NEXT:    subq $24, %rsp
; CHECK-NEXT:    .cfi_def_cfa_offset 32
; CHECK-NEXT:    movq %rdi, {{[0-9]+}}(%rsp)
; CHECK-NEXT:    callq f
; CHECK-NEXT:  .Ltmp0:
; CHECK-NEXT:    movq {{[0-9]+}}(%rsp), %rdi
; CHECK-NEXT:    movq %rdi, %rsi
; CHECK-NEXT:    xorl %eax, %eax
; CHECK-NEXT:    callq use
; CHECK-NEXT:    addq $24, %rsp
; CHECK-NEXT:    .cfi_def_cfa_offset 8
; CHECK-NEXT:    retq
  %tok = tail call token (i64, i32, void ()*, i32, i32, ...)
      @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @f, i32 0, i32 0, i32 0, i32 2, i32 addrspace(1)* %ptr, i32 undef, i32 addrspace(1)* %ptr, i32 addrspace(1)* %ptr)
  %a = call i32 addrspace(1)* @llvm.experimental.gc.relocate.p1i32(token %tok, i32 9, i32 9)
  %b = call i32 addrspace(1)* @llvm.experimental.gc.relocate.p1i32(token %tok, i32 10, i32 10)
  call void (...) @use(i32 addrspace(1)* %a, i32 addrspace(1)* %b)
  ret void
}

define void @test_gcptr_uniqueing(i32 addrspace(1)* %ptr) gc "statepoint-example" {
; CHECK-LABEL: test_gcptr_uniqueing:
; CHECK:       # %bb.0:
; CHECK-NEXT:    subq $24, %rsp
; CHECK-NEXT:    .cfi_def_cfa_offset 32
; CHECK-NEXT:    movq %rdi, {{[0-9]+}}(%rsp)
; CHECK-NEXT:    callq f
; CHECK-NEXT:  .Ltmp1:
; CHECK-NEXT:    movq {{[0-9]+}}(%rsp), %rdi
; CHECK-NEXT:    movq %rdi, %rsi
; CHECK-NEXT:    xorl %eax, %eax
; CHECK-NEXT:    callq use
; CHECK-NEXT:    addq $24, %rsp
; CHECK-NEXT:    .cfi_def_cfa_offset 8
; CHECK-NEXT:    retq
  %ptr2 = bitcast i32 addrspace(1)* %ptr to i8 addrspace(1)*
  %tok = tail call token (i64, i32, void ()*, i32, i32, ...)
      @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @f, i32 0, i32 0, i32 0, i32 2, i32 addrspace(1)* %ptr, i32 undef, i32 addrspace(1)* %ptr, i8 addrspace(1)* %ptr2)
  %a = call i32 addrspace(1)* @llvm.experimental.gc.relocate.p1i32(token %tok, i32 9, i32 9)
  %b = call i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %tok, i32 10, i32 10)
  call void (...) @use(i32 addrspace(1)* %a, i8 addrspace(1)* %b)
  ret void
}
