; RUN: opt -jump-threading -S < %s | FileCheck %s

declare void @side_effect(i32)

define void @test0(i32 %i, i32 %len) {
; CHECK-LABEL: @test0(
 entry:
  call void @side_effect(i32 0)
  %i.inc = add nuw i32 %i, 1
  %c0 = icmp ult i32 %i.inc, %len
  br i1 %c0, label %left, label %right

 left:
; CHECK: entry:
; CHECK: br i1 %c0, label %left0, label %right

; CHECK: left0:
; CHECK: call void @side_effect
; CHECK-NOT: br i1 %c1
; CHECK: call void @side_effect
  call void @side_effect(i32 0)
  %c1 = icmp ult i32 %i, %len
  br i1 %c1, label %left0, label %right

 left0:
  call void @side_effect(i32 0)
  ret void

 right:
  %t = phi i32 [ 1, %left ], [ 2, %entry ]
  call void @side_effect(i32 %t)
  ret void
}

define void @test1(i32 %i, i32 %len) {
; CHECK-LABEL: @test1(
 entry:
  call void @side_effect(i32 0)
  %i.inc = add nsw i32 %i, 1
  %c0 = icmp slt i32 %i.inc, %len
  br i1 %c0, label %left, label %right

 left:
; CHECK: entry:
; CHECK: br i1 %c0, label %left0, label %right

; CHECK: left0:
; CHECK: call void @side_effect
; CHECK-NOT: br i1 %c1
; CHECK: call void @side_effect
  call void @side_effect(i32 0)
  %c1 = icmp slt i32 %i, %len
  br i1 %c1, label %left0, label %right

 left0:
  call void @side_effect(i32 0)
  ret void

 right:
  %t = phi i32 [ 1, %left ], [ 2, %entry ]
  call void @side_effect(i32 %t)
  ret void
}

define void @test2(i32 %i, i32 %len, i1* %c.ptr) {
; CHECK-LABEL: @test2(

; CHECK: entry:
; CHECK: br i1 %c0, label %cont, label %right
; CHECK: cont:
; CHECK: br i1 %c, label %left0, label %right
; CHECK: left0:
; CHECK: call void @side_effect(i32 0)
; CHECK: call void @side_effect(i32 0)
 entry:
  call void @side_effect(i32 0)
  %i.inc = add nsw i32 %i, 1
  %c0 = icmp slt i32 %i.inc, %len
  br i1 %c0, label %cont, label %right

 cont:
  %c = load i1, i1* %c.ptr
  br i1 %c, label %left, label %right

 left:
  call void @side_effect(i32 0)
  %c1 = icmp slt i32 %i, %len
  br i1 %c1, label %left0, label %right

 left0:
  call void @side_effect(i32 0)
  ret void

 right:
  %t = phi i32 [ 1, %left ], [ 2, %entry ], [ 3, %cont ]
  call void @side_effect(i32 %t)
  ret void
}
