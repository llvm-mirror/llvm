; RUN: llc < %s -mtriple=powerpc64-unknown-linux-gnu -mcpu=pwr7 | FileCheck %s
target datalayout = "E-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-f128:128:128-v128:128:128-n32:64"
target triple = "powerpc64-unknown-linux-gnu"

define i32 @test1(i1 %a, i32 %c) nounwind  {
  %x = select i1 %a, i32 %c, i32 0
  ret i32 %x

; CHECK: @test1
; CHECK-NOT: li {{[0-9]+}}, 0
; CHECK: isel 3, 0,
; CHECK: blr
}

