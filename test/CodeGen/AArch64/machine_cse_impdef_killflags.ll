; RUN: llc < %s -mtriple=aarch64-apple-ios -fast-isel -verify-machineinstrs | FileCheck %s

; Check that the kill flag is cleared between CSE'd instructions on their
; imp-def'd registers.
; The verifier would complain otherwise.
define i64 @csed-impdef-killflag(i64 %a) {
; CHECK-LABEL: csed-impdef-killflag
; CHECK-DAG:  mov    [[REG0:w[0-9]+]], wzr
; CHECK-DAG:  orr    [[REG1:w[0-9]+]], wzr, #0x1
; CHECK-DAG:  orr    [[REG2:x[0-9]+]], xzr, #0x2
; CHECK-DAG:  orr    [[REG3:x[0-9]+]], xzr, #0x3
; CHECK:      cmp    x0, #0
; CHECK-DAG:  csel   w[[SELECT_WREG_1:[0-9]+]], [[REG0]], [[REG1]], ne
; CHECK-DAG:  csel   [[SELECT_XREG_2:x[0-9]+]], [[REG2]], [[REG3]], ne
; CHECK:      ubfx   [[SELECT_XREG_1:x[0-9]+]], x[[SELECT_WREG_1]], #0, #32
; CHECK-NEXT: add    x0, [[SELECT_XREG_2]], [[SELECT_XREG_1]]
; CHECK-NEXT: ret

  %1 = icmp ne i64 %a, 0
  %2 = select i1 %1, i32 0, i32 1
  %3 = icmp ne i64 %a, 0
  %4 = select i1 %3, i64 2, i64 3
  %5 = zext i32 %2 to i64
  %6 = add i64 %4, %5
  ret i64 %6
}
