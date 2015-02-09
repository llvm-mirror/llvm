; Test the "m" asm constraint, which is equivalent to "T".
;
; RUN: llc < %s -mtriple=s390x-linux-gnu -no-integrated-as | FileCheck %s

define void @f1(i64 %base) {
; CHECK-LABEL: f1:
; CHECK: blah 0(%r2)
; CHECK: br %r14
  %addr = inttoptr i64 %base to i64 *
  call void asm "blah $0", "=*m" (i64 *%addr)
  ret void
}

; FIXME: at the moment the precise constraint is not passed down to
; target code, so we must conservatively treat "m" as "Q".
