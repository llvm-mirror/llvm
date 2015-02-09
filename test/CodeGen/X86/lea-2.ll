; RUN: llc < %s -mtriple=i686-linux -x86-asm-syntax=intel | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-linux -x86-asm-syntax=intel | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-linux-gnux32 -x86-asm-syntax=intel | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-nacl -x86-asm-syntax=intel | FileCheck %s

define i32 @test1(i32 %A, i32 %B) {
  %tmp1 = shl i32 %A, 2
  %tmp3 = add i32 %B, -5
  %tmp4 = add i32 %tmp3, %tmp1
; The above computation of %tmp4 should match a single lea, without using
; actual add instructions.
; CHECK-NOT: add
; CHECK: lea {{[a-z]+}}, [{{[a-z]+}} + 4*{{[a-z]+}} - 5]

  ret i32 %tmp4
}


