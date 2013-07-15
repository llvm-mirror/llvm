; RUN: llc -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu -O0 | FileCheck %s
; RUN: llc -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu -O0 -disable-fp-elim | FileCheck -check-prefix CHECK-WITHFP %s

; Make sure a reasonably sane prologue and epilogue are
; generated. This test is not robust in the face of an frame-handling
; evolving, but still has value for unrelated changes, I
; believe.
;
; In particular, it will fail when ldp/stp are used for frame setup,
; when FP-elim is implemented, and when addressing from FP is
; implemented.

@var = global i64 0
@local_addr = global i64* null

declare void @foo()

define void @trivial_func() nounwind {
; CHECK: trivial_func: // @trivial_func
; CHECK-NEXT: // BB#0
; CHECK-NEXT: ret

  ret void
}

define void @trivial_fp_func() {
; CHECK-WITHFP-LABEL: trivial_fp_func:

; CHECK-WITHFP: sub sp, sp, #16
; CHECK-WITHFP: stp x29, x30, [sp]
; CHECK-WITHFP-NEXT: mov x29, sp

; Dont't really care, but it would be a Bad Thing if this came after the epilogue.
; CHECK: bl foo
  call void @foo()
  ret void

; CHECK-WITHFP: ldp x29, x30, [sp]
; CHECK-WITHFP: add sp, sp, #16

; CHECK-WITHFP: ret
}

define void @stack_local() {
  %local_var = alloca i64
; CHECK-LABEL: stack_local:
; CHECK: sub sp, sp, #16

  %val = load i64* @var
  store i64 %val, i64* %local_var
; CHECK: str {{x[0-9]+}}, [sp, #{{[0-9]+}}]

  store i64* %local_var, i64** @local_addr
; CHECK: add {{x[0-9]+}}, sp, #{{[0-9]+}}

  ret void
}
