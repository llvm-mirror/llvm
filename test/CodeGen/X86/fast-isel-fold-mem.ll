; RUN: llc < %s                             -mtriple=x86_64-apple-darwin | FileCheck %s
; RUN: llc < %s -fast-isel -fast-isel-abort -mtriple=x86_64-apple-darwin | FileCheck %s

define i64 @fold_load(i64* %a, i64 %b) {
; CHECK-LABEL: fold_load
; CHECK:       addq  (%rdi), %rsi
; CHECK-NEXT:  movq  %rsi, %rax
  %1 = load i64* %a, align 8
  %2 = add i64 %1, %b
  ret i64 %2
}

