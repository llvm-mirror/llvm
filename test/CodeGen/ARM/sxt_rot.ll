; RUN: llc -mtriple=arm-eabi -mattr=+v6 %s -o - | FileCheck %s

define i32 @test0(i8 %A) {
; CHECK: test0
; CHECK: sxtb r0, r0 
  %B = sext i8 %A to i32
  ret i32 %B
}

define signext i8 @test1(i32 %A) {
; CHECK: test1
; CHECK: lsr r0, r0, #8
; CHECK: sxtb r0, r0
  %B = lshr i32 %A, 8
  %C = shl i32 %A, 24
  %D = or i32 %B, %C
  %E = trunc i32 %D to i8
  ret i8 %E
}

define signext i32 @test2(i32 %A, i32 %X) {
; CHECK: test2
; CHECK: sxtab r0, r1, r0
  %B = lshr i32 %A, 8
  %C = shl i32 %A, 24
  %D = or i32 %B, %C
  %E = trunc i32 %D to i8
  %F = sext i8 %E to i32
  %G = add i32 %F, %X
  ret i32 %G
}
