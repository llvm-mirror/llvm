; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=thumbv8.1m.main-arm-none-eabi -mattr=+mve -verify-machineinstrs %s -o - | FileCheck %s

define arm_aapcs_vfpcc <4 x i32> @sext_v4i1_v4i32(<4 x i32> %src) {
; CHECK-LABEL: sext_v4i1_v4i32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s32 gt, q0, zr
; CHECK-NEXT:    vmov.i32 q0, #0x0
; CHECK-NEXT:    vmov.i8 q1, #0xff
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <4 x i32> %src, zeroinitializer
  %0 = sext <4 x i1> %c to <4 x i32>
  ret <4 x i32> %0
}

define arm_aapcs_vfpcc <8 x i16> @sext_v8i1_v8i16(<8 x i16> %src) {
; CHECK-LABEL: sext_v8i1_v8i16:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s16 gt, q0, zr
; CHECK-NEXT:    vmov.i16 q0, #0x0
; CHECK-NEXT:    vmov.i8 q1, #0xff
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <8 x i16> %src, zeroinitializer
  %0 = sext <8 x i1> %c to <8 x i16>
  ret <8 x i16> %0
}

define arm_aapcs_vfpcc <16 x i8> @sext_v16i1_v16i8(<16 x i8> %src) {
; CHECK-LABEL: sext_v16i1_v16i8:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s8 gt, q0, zr
; CHECK-NEXT:    vmov.i8 q0, #0x0
; CHECK-NEXT:    vmov.i8 q1, #0xff
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <16 x i8> %src, zeroinitializer
  %0 = sext <16 x i1> %c to <16 x i8>
  ret <16 x i8> %0
}

define arm_aapcs_vfpcc <2 x i64> @sext_v2i1_v2i64(<2 x i64> %src) {
; CHECK-LABEL: sext_v2i1_v2i64:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmov r1, s0
; CHECK-NEXT:    movs r2, #0
; CHECK-NEXT:    vmov r0, s1
; CHECK-NEXT:    rsbs r1, r1, #0
; CHECK-NEXT:    sbcs.w r0, r2, r0
; CHECK-NEXT:    vmov r1, s2
; CHECK-NEXT:    mov.w r0, #0
; CHECK-NEXT:    it lt
; CHECK-NEXT:    movlt r0, #1
; CHECK-NEXT:    cmp r0, #0
; CHECK-NEXT:    it ne
; CHECK-NEXT:    movne.w r0, #-1
; CHECK-NEXT:    vmov.32 q1[0], r0
; CHECK-NEXT:    vmov.32 q1[1], r0
; CHECK-NEXT:    vmov r0, s3
; CHECK-NEXT:    rsbs r1, r1, #0
; CHECK-NEXT:    sbcs.w r0, r2, r0
; CHECK-NEXT:    it lt
; CHECK-NEXT:    movlt r2, #1
; CHECK-NEXT:    cmp r2, #0
; CHECK-NEXT:    it ne
; CHECK-NEXT:    movne.w r2, #-1
; CHECK-NEXT:    vmov.32 q1[2], r2
; CHECK-NEXT:    vmov.32 q1[3], r2
; CHECK-NEXT:    vmov q0, q1
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <2 x i64> %src, zeroinitializer
  %0 = sext <2 x i1> %c to <2 x i64>
  ret <2 x i64> %0
}


define arm_aapcs_vfpcc <4 x i32> @zext_v4i1_v4i32(<4 x i32> %src) {
; CHECK-LABEL: zext_v4i1_v4i32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s32 gt, q0, zr
; CHECK-NEXT:    vmov.i32 q0, #0x0
; CHECK-NEXT:    vmov.i32 q1, #0x1
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <4 x i32> %src, zeroinitializer
  %0 = zext <4 x i1> %c to <4 x i32>
  ret <4 x i32> %0
}

define arm_aapcs_vfpcc <8 x i16> @zext_v8i1_v8i16(<8 x i16> %src) {
; CHECK-LABEL: zext_v8i1_v8i16:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s16 gt, q0, zr
; CHECK-NEXT:    vmov.i16 q0, #0x0
; CHECK-NEXT:    vmov.i16 q1, #0x1
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <8 x i16> %src, zeroinitializer
  %0 = zext <8 x i1> %c to <8 x i16>
  ret <8 x i16> %0
}

define arm_aapcs_vfpcc <16 x i8> @zext_v16i1_v16i8(<16 x i8> %src) {
; CHECK-LABEL: zext_v16i1_v16i8:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vcmp.s8 gt, q0, zr
; CHECK-NEXT:    vmov.i8 q0, #0x0
; CHECK-NEXT:    vmov.i8 q1, #0x1
; CHECK-NEXT:    vpsel q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %c = icmp sgt <16 x i8> %src, zeroinitializer
  %0 = zext <16 x i1> %c to <16 x i8>
  ret <16 x i8> %0
}

define arm_aapcs_vfpcc <2 x i64> @zext_v2i1_v2i64(<2 x i64> %src) {
; CHECK-LABEL: zext_v2i1_v2i64:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmov r1, s0
; CHECK-NEXT:    movs r2, #0
; CHECK-NEXT:    vmov r0, s1
; CHECK-NEXT:    rsbs r1, r1, #0
; CHECK-NEXT:    sbcs.w r0, r2, r0
; CHECK-NEXT:    vmov r1, s2
; CHECK-NEXT:    mov.w r0, #0
; CHECK-NEXT:    it lt
; CHECK-NEXT:    movlt r0, #1
; CHECK-NEXT:    cmp r0, #0
; CHECK-NEXT:    it ne
; CHECK-NEXT:    movne.w r0, #-1
; CHECK-NEXT:    vmov.32 q1[0], r0
; CHECK-NEXT:    vmov r0, s3
; CHECK-NEXT:    rsbs r1, r1, #0
; CHECK-NEXT:    sbcs.w r0, r2, r0
; CHECK-NEXT:    it lt
; CHECK-NEXT:    movlt r2, #1
; CHECK-NEXT:    adr r0, .LCPI7_0
; CHECK-NEXT:    cmp r2, #0
; CHECK-NEXT:    vldrw.u32 q0, [r0]
; CHECK-NEXT:    it ne
; CHECK-NEXT:    movne.w r2, #-1
; CHECK-NEXT:    vmov.32 q1[2], r2
; CHECK-NEXT:    vand q0, q1, q0
; CHECK-NEXT:    bx lr
; CHECK-NEXT:    .p2align 4
; CHECK-NEXT:  @ %bb.1:
; CHECK-NEXT:  .LCPI7_0:
; CHECK-NEXT:    .long 1 @ 0x1
; CHECK-NEXT:    .long 0 @ 0x0
; CHECK-NEXT:    .long 1 @ 0x1
; CHECK-NEXT:    .long 0 @ 0x0
entry:
  %c = icmp sgt <2 x i64> %src, zeroinitializer
  %0 = zext <2 x i1> %c to <2 x i64>
  ret <2 x i64> %0
}