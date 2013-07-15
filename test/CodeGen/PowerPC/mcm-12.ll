; RUN: llc -mcpu=pwr7 -O1 -code-model=medium <%s | FileCheck %s

; Test peephole optimization for medium code model (32-bit TOC offsets)
; for loading a value from the constant pool (TOC-relative).

target datalayout = "E-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-f128:128:128-v128:128:128-n32:64"
target triple = "powerpc64-unknown-linux-gnu"

define double @test_double_const() nounwind {
entry:
  ret double 0x3F4FD4920B498CF0
}

; CHECK: [[VAR:[a-z0-9A-Z_.]+]]:
; CHECK: .quad 4562098671269285104
; CHECK-LABEL: test_double_const:
; CHECK: addis [[REG1:[0-9]+]], 2, [[VAR]]@toc@ha
; CHECK: lfd {{[0-9]+}}, [[VAR]]@toc@l([[REG1]])
