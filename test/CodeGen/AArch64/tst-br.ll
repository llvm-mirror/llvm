; RUN: llc -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu | FileCheck %s

; We've got the usual issues with LLVM reordering blocks here. The
; tests are correct for the current order, but who knows when that
; will change. Beware!
@var32 = global i32 0
@var64 = global i64 0

define i32 @test_tbz() {
; CHECK-LABEL: test_tbz:

  %val = load i32* @var32
  %val64 = load i64* @var64

  %tbit0 = and i32 %val, 32768
  %tst0 = icmp ne i32 %tbit0, 0
  br i1 %tst0, label %test1, label %end1
; CHECK: tbz {{w[0-9]+}}, #15, [[LBL_end1:.LBB0_[0-9]+]]

test1:
  %tbit1 = and i32 %val, 4096
  %tst1 = icmp ne i32 %tbit1, 0
  br i1 %tst1, label %test2, label %end1
; CHECK: tbz {{w[0-9]+}}, #12, [[LBL_end1]]

test2:
  %tbit2 = and i64 %val64, 32768
  %tst2 = icmp ne i64 %tbit2, 0
  br i1 %tst2, label %test3, label %end1
; CHECK: tbz {{x[0-9]+}}, #15, [[LBL_end1]]

test3:
  %tbit3 = and i64 %val64, 4096
  %tst3 = icmp ne i64 %tbit3, 0
  br i1 %tst3, label %end2, label %end1
; CHECK: tbz {{x[0-9]+}}, #12, [[LBL_end1]]

end2:
; CHECK: movz x0, #1
; CHECK-NEXT: ret
  ret i32 1

end1:
; CHECK: [[LBL_end1]]:
; CHECK-NEXT: mov x0, xzr
; CHECK-NEXT: ret
  ret i32 0
}
