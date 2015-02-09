; RUN: llc -mtriple=arm64-darwin-unknown < %s | FileCheck %s

%T = type { i32, i32, i32, i32 }

; Test if the constant base address gets only materialized once.
define i32 @test1() nounwind {
; CHECK-LABEL:  test1
; CHECK:        movz  w8, #0x40f, lsl #16
; CHECK-NEXT:   movk  w8, #0xc000
; CHECK-NEXT:   ldp w9, w10, [x8, #4]
; CHECK:        ldr w8, [x8, #12]
  %at = inttoptr i64 68141056 to %T*
  %o1 = getelementptr %T* %at, i32 0, i32 1
  %t1 = load i32* %o1
  %o2 = getelementptr %T* %at, i32 0, i32 2
  %t2 = load i32* %o2
  %a1 = add i32 %t1, %t2
  %o3 = getelementptr %T* %at, i32 0, i32 3
  %t3 = load i32* %o3
  %a2 = add i32 %a1, %t3
  ret i32 %a2
}

