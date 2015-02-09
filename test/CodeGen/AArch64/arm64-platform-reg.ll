; RUN: llc -mtriple=arm64-apple-ios -o - %s | FileCheck %s --check-prefix=CHECK-RESERVE-X18
; RUN: llc -mtriple=arm64-freebsd-gnu -aarch64-reserve-x18 -o - %s | FileCheck %s --check-prefix=CHECK-RESERVE-X18
; RUN: llc -mtriple=arm64-linux-gnu -o - %s | FileCheck %s

; x18 is reserved as a platform register on Darwin but not on other
; systems. Create loads of register pressure and make sure this is respected.

; Also, fp must always refer to a valid frame record, even if it's not the one
; of the current function, so it shouldn't be used either.

@var = global [30 x i64] zeroinitializer

define void @keep_live() {
  %val = load volatile [30 x i64]* @var
  store volatile [30 x i64] %val, [30 x i64]* @var

; CHECK: ldr x18
; CHECK: str x18

; CHECK-RESERVE-X18-NOT: ldr fp
; CHECK-RESERVE-X18-NOT: ldr x18
; CHECK-RESERVE-X18: Spill
; CHECK-RESERVE-X18-NOT: ldr fp
; CHECK-RESERVE-X18-NOT: ldr x18
; CHECK-RESERVE-X18: ret
  ret void
}
