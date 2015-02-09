; RUN: llc < %s -march=bpf -verify-machineinstrs -show-mc-encoding | FileCheck %s
; test little endian only for now

; CHECK-LABEL: test_load_add_32
; CHECK: xadd32
; CHECK: encoding: [0xc3
define void @test_load_add_32(i32* %p, i32 zeroext %v) {
entry:
  atomicrmw add i32* %p, i32 %v seq_cst
  ret void
}

; CHECK-LABEL: test_load_add_64
; CHECK: xadd64
; CHECK: encoding: [0xdb
define void @test_load_add_64(i64* %p, i64 zeroext %v) {
entry:
  atomicrmw add i64* %p, i64 %v seq_cst
  ret void
}
