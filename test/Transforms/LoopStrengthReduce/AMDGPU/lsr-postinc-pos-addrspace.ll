; RUN: llc -march=amdgcn -mcpu=bonaire -print-lsr-output < %s 2>&1 | FileCheck %s

; Test various conditions where OptimizeLoopTermCond doesn't look at a
; memory instruction use and fails to find the address space.

target datalayout = "e-p:32:32-p1:64:64-p2:64:64-p3:32:32-p4:64:64-p5:32:32-p24:64:64-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-v2048:2048-n32:64"

; CHECK-LABEL: @local_cmp_user(
; CHECK: bb11:
; CHECK: %lsr.iv1 = phi i32 [ %lsr.iv.next2, %bb ], [ -2, %entry ]
; CHECK: %lsr.iv = phi i32 [ %lsr.iv.next, %bb ], [ undef, %entry ]

; CHECK: bb:
; CHECK: %lsr.iv.next = add i32 %lsr.iv, -1
; CHECK: %lsr.iv.next2 = add i32 %lsr.iv1, 2
; CHECK: %scevgep = getelementptr i8, i8 addrspace(3)* %t, i32 %lsr.iv.next2
; CHECK: %c1 = icmp ult i8 addrspace(3)* %scevgep, undef
define void @local_cmp_user() nounwind {
entry:
  br label %bb11

bb11:
  %i = phi i32 [ 0, %entry ], [ %i.next, %bb ]
  %ii = shl i32 %i, 1
  %c0 = icmp eq i32 %i, undef
  br i1 %c0, label %bb13, label %bb

bb:
  %t = load i8 addrspace(3)*, i8 addrspace(3)* addrspace(3)* undef
  %p = getelementptr i8, i8 addrspace(3)* %t, i32 %ii
  %c1 = icmp ult i8 addrspace(3)* %p, undef
  %i.next = add i32 %i, 1
  br i1 %c1, label %bb11, label %bb13

bb13:
  unreachable
}

; CHECK-LABEL: @global_cmp_user(
; CHECK: %lsr.iv.next = add i64 %lsr.iv, -1
; CHECK: %lsr.iv.next2 = add i64 %lsr.iv1, 2
; CHECK: %scevgep = getelementptr i8, i8 addrspace(1)* %t, i64 %lsr.iv.next2
define void @global_cmp_user() nounwind {
entry:
  br label %bb11

bb11:
  %i = phi i64 [ 0, %entry ], [ %i.next, %bb ]
  %ii = shl i64 %i, 1
  %c0 = icmp eq i64 %i, undef
  br i1 %c0, label %bb13, label %bb

bb:
  %t = load i8 addrspace(1)*, i8 addrspace(1)* addrspace(1)* undef
  %p = getelementptr i8, i8 addrspace(1)* %t, i64 %ii
  %c1 = icmp ult i8 addrspace(1)* %p, undef
  %i.next = add i64 %i, 1
  br i1 %c1, label %bb11, label %bb13

bb13:
  unreachable
}

; CHECK-LABEL: @global_gep_user(
; CHECK: %p = getelementptr i8, i8 addrspace(1)* %t, i32 %lsr.iv1
; CHECK: %lsr.iv.next = add i32 %lsr.iv, -1
; CHECK: %lsr.iv.next2 = add i32 %lsr.iv1, 2
define void @global_gep_user() nounwind {
entry:
  br label %bb11

bb11:
  %i = phi i32 [ 0, %entry ], [ %i.next, %bb ]
  %ii = shl i32 %i, 1
  %c0 = icmp eq i32 %i, undef
  br i1 %c0, label %bb13, label %bb

bb:
  %t = load i8 addrspace(1)*, i8 addrspace(1)* addrspace(1)* undef
  %p = getelementptr i8, i8 addrspace(1)* %t, i32 %ii
  %c1 = icmp ult i8 addrspace(1)* %p, undef
  %i.next = add i32 %i, 1
  br i1 %c1, label %bb11, label %bb13

bb13:
  unreachable
}

; CHECK-LABEL: @global_sext_scale_user(
; CHECK: %p = getelementptr i8, i8 addrspace(1)* %t, i64 %ii.ext
; CHECK: %lsr.iv.next = add i32 %lsr.iv, -1
; CHECK: %lsr.iv.next2 = add i32 %lsr.iv1, 2
define void @global_sext_scale_user() nounwind {
entry:
  br label %bb11

bb11:
  %i = phi i32 [ 0, %entry ], [ %i.next, %bb ]
  %ii = shl i32 %i, 1
  %ii.ext = sext i32 %ii to i64
  %c0 = icmp eq i32 %i, undef
  br i1 %c0, label %bb13, label %bb

bb:
  %t = load i8 addrspace(1)*, i8 addrspace(1)* addrspace(1)* undef
  %p = getelementptr i8, i8 addrspace(1)* %t, i64 %ii.ext
  %c1 = icmp ult i8 addrspace(1)* %p, undef
  %i.next = add i32 %i, 1
  br i1 %c1, label %bb11, label %bb13

bb13:
  unreachable
}
