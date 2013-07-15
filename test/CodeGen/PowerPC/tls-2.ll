target datalayout = "E-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v128:128:128-n32:64"
target triple = "powerpc64-unknown-freebsd10.0"
; RUN: llc -O1 < %s -march=ppc64 | FileCheck %s

@a = thread_local global i32 0, align 4

;CHECK-LABEL:          localexec:
define i32 @localexec() nounwind {
entry:
;CHECK:          addis [[REG1:[0-9]+]], 13, a@tprel@ha
;CHECK-NEXT:     li [[REG2:[0-9]+]], 42
;CHECK-NEXT:     stw [[REG2]], a@tprel@l([[REG1]])
  store i32 42, i32* @a, align 4
  ret i32 0
}
