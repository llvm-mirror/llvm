; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=knl | FileCheck %s

; ModuleID = 'bugpoint-reduced-simplified.bc'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK-LABEL: func
; CHECK: kortestw
; CHECK: kortestw
define void @func() {
bb1:
  br i1 undef, label %L_10, label %L_10

L_10:                                             ; preds = %bb1, %bb1
  br i1 undef, label %L_30, label %bb56

bb56:                                             ; preds = %L_10
  br label %bb33

bb33:                                             ; preds = %bb51, %bb56
  %r111 = load i64* undef, align 8
  br i1 undef, label %bb51, label %bb35

bb35:                                             ; preds = %bb33
  br i1 undef, label %L_19, label %bb37

bb37:                                             ; preds = %bb35
  %r128 = and i64 %r111, 576460752303423488
  %phitmp = icmp eq i64 %r128, 0
  br label %L_19

L_19:                                             ; preds = %bb37, %bb35
  %"$V_S25.0" = phi i1 [ %phitmp, %bb37 ], [ true, %bb35 ]
  br i1 undef, label %bb51, label %bb42

bb42:                                             ; preds = %L_19
  %r136 = select i1 %"$V_S25.0", i32* undef, i32* undef
  br label %bb51

bb51:                                             ; preds = %bb42, %L_19, %bb33
  br i1 false, label %L_30, label %bb33

L_30:                                             ; preds = %bb51, %L_10
  ret void
}
