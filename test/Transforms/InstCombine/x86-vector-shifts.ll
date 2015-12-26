; RUN: opt < %s -instcombine -S | FileCheck %s
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

;
; ASHR - Immediate
;

define <8 x i16> @sse2_psrai_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrai_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %v, i32 0)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrai_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrai_w_15
; CHECK-NEXT: %1 = ashr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %v, i32 15)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrai_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrai_w_64
; CHECK-NEXT: %1 = ashr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %v, i32 64)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_psrai_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrai_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %v, i32 0)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrai_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrai_d_15
; CHECK-NEXT: %1 = ashr <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %v, i32 15)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrai_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrai_d_64
; CHECK-NEXT: %1 = ashr <4 x i32> %v, <i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %v, i32 64)
  ret <4 x i32> %1
}

define <16 x i16> @avx2_psrai_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrai_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %v, i32 0)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrai_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrai_w_15
; CHECK-NEXT: %1 = ashr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %v, i32 15)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrai_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrai_w_64
; CHECK-NEXT: %1 = ashr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %v, i32 64)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_psrai_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrai_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %v, i32 0)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrai_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrai_d_15
; CHECK-NEXT: %1 = ashr <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %v, i32 15)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrai_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrai_d_64
; CHECK-NEXT: %1 = ashr <8 x i32> %v, <i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %v, i32 64)
  ret <8 x i32> %1
}

;
; LSHR - Immediate
;

define <8 x i16> @sse2_psrli_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrli_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16> %v, i32 0)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrli_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrli_w_15
; CHECK-NEXT: %1 = lshr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16> %v, i32 15)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrli_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrli_w_64
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16> %v, i32 64)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_psrli_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrli_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32> %v, i32 0)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrli_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrli_d_15
; CHECK-NEXT: %1 = lshr <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32> %v, i32 15)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrli_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrli_d_64
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32> %v, i32 64)
  ret <4 x i32> %1
}

define <2 x i64> @sse2_psrli_q_0(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrli_q_0
; CHECK-NEXT: ret <2 x i64> %v
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64> %v, i32 0)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psrli_q_15(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrli_q_15
; CHECK-NEXT: %1 = lshr <2 x i64> %v, <i64 15, i64 15>
; CHECK-NEXT: ret <2 x i64> %1
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64> %v, i32 15)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psrli_q_64(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrli_q_64
; CHECK-NEXT: ret <2 x i64> zeroinitializer
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64> %v, i32 64)
  ret <2 x i64> %1
}

define <16 x i16> @avx2_psrli_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrli_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16> %v, i32 0)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrli_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrli_w_15
; CHECK-NEXT: %1 = lshr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16> %v, i32 15)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrli_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrli_w_64
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16> %v, i32 64)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_psrli_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrli_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32> %v, i32 0)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrli_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrli_d_15
; CHECK-NEXT: %1 = lshr <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32> %v, i32 15)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrli_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrli_d_64
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32> %v, i32 64)
  ret <8 x i32> %1
}

define <4 x i64> @avx2_psrli_q_0(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrli_q_0
; CHECK-NEXT: ret <4 x i64> %v
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64> %v, i32 0)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psrli_q_15(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrli_q_15
; CHECK-NEXT: %1 = lshr <4 x i64> %v, <i64 15, i64 15, i64 15, i64 15>
; CHECK-NEXT: ret <4 x i64> %1
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64> %v, i32 15)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psrli_q_64(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrli_q_64
; CHECK-NEXT: ret <4 x i64> zeroinitializer
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64> %v, i32 64)
  ret <4 x i64> %1
}

;
; SHL - Immediate
;

define <8 x i16> @sse2_pslli_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_pslli_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16> %v, i32 0)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_pslli_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_pslli_w_15
; CHECK-NEXT: %1 = shl <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16> %v, i32 15)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_pslli_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_pslli_w_64
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16> %v, i32 64)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_pslli_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_pslli_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32> %v, i32 0)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_pslli_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_pslli_d_15
; CHECK-NEXT: %1 = shl <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32> %v, i32 15)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_pslli_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_pslli_d_64
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32> %v, i32 64)
  ret <4 x i32> %1
}

define <2 x i64> @sse2_pslli_q_0(<2 x i64> %v) {
; CHECK-LABEL: @sse2_pslli_q_0
; CHECK-NEXT: ret <2 x i64> %v
  %1 = tail call <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64> %v, i32 0)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_pslli_q_15(<2 x i64> %v) {
; CHECK-LABEL: @sse2_pslli_q_15
; CHECK-NEXT: %1 = shl <2 x i64> %v, <i64 15, i64 15>
; CHECK-NEXT: ret <2 x i64> %1
  %1 = tail call <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64> %v, i32 15)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_pslli_q_64(<2 x i64> %v) {
; CHECK-LABEL: @sse2_pslli_q_64
; CHECK-NEXT: ret <2 x i64> zeroinitializer
  %1 = tail call <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64> %v, i32 64)
  ret <2 x i64> %1
}

define <16 x i16> @avx2_pslli_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_pslli_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16> %v, i32 0)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_pslli_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_pslli_w_15
; CHECK-NEXT: %1 = shl <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16> %v, i32 15)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_pslli_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_pslli_w_64
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16> %v, i32 64)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_pslli_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_pslli_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32> %v, i32 0)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_pslli_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_pslli_d_15
; CHECK-NEXT: %1 = shl <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32> %v, i32 15)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_pslli_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_pslli_d_64
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32> %v, i32 64)
  ret <8 x i32> %1
}

define <4 x i64> @avx2_pslli_q_0(<4 x i64> %v) {
; CHECK-LABEL: @avx2_pslli_q_0
; CHECK-NEXT: ret <4 x i64> %v
  %1 = tail call <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64> %v, i32 0)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_pslli_q_15(<4 x i64> %v) {
; CHECK-LABEL: @avx2_pslli_q_15
; CHECK-NEXT: %1 = shl <4 x i64> %v, <i64 15, i64 15, i64 15, i64 15>
; CHECK-NEXT: ret <4 x i64> %1
  %1 = tail call <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64> %v, i32 15)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_pslli_q_64(<4 x i64> %v) {
; CHECK-LABEL: @avx2_pslli_q_64
; CHECK-NEXT: ret <4 x i64> zeroinitializer
  %1 = tail call <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64> %v, i32 64)
  ret <4 x i64> %1
}

;
; ASHR - Constant Vector
;

define <8 x i16> @sse2_psra_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psra_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> zeroinitializer)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psra_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psra_w_15
; CHECK-NEXT: %1 = ashr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psra_w_15_splat(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psra_w_15_splat
; CHECK-NEXT: %1 = ashr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psra_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psra_w_64
; CHECK-NEXT: %1 = ashr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_psra_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psra_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> zeroinitializer)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psra_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psra_d_15
; CHECK-NEXT: %1 = ashr <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psra_d_15_splat(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psra_d_15_splat
; CHECK-NEXT: %1 = ashr <4 x i32> %v, <i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psra_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psra_d_64
; CHECK-NEXT: %1 = ashr <4 x i32> %v, <i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <16 x i16> @avx2_psra_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psra_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> zeroinitializer)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psra_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psra_w_15
; CHECK-NEXT: %1 = ashr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psra_w_15_splat(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psra_w_15_splat
; CHECK-NEXT: %1 = ashr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psra_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psra_w_64
; CHECK-NEXT: %1 = ashr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_psra_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psra_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> zeroinitializer)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psra_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psra_d_15
; CHECK-NEXT: %1 = ashr <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psra_d_15_splat(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psra_d_15_splat
; CHECK-NEXT: %1 = ashr <8 x i32> %v, <i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psra_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psra_d_64
; CHECK-NEXT: %1 = ashr <8 x i32> %v, <i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31, i32 31>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

;
; LSHR - Constant Vector
;

define <8 x i16> @sse2_psrl_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrl_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> zeroinitializer)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrl_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrl_w_15
; CHECK-NEXT: %1 = lshr <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrl_w_15_splat(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrl_w_15_splat
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psrl_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psrl_w_64
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_psrl_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrl_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> zeroinitializer)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrl_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrl_d_15
; CHECK-NEXT: %1 = lshr <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrl_d_15_splat(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrl_d_15_splat
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psrl_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psrl_d_64
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <2 x i64> @sse2_psrl_q_0(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrl_q_0
; CHECK-NEXT: ret <2 x i64> %v
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %v, <2 x i64> zeroinitializer)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psrl_q_15(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrl_q_15
; CHECK-NEXT: %1 = lshr <2 x i64> %v, <i64 15, i64 15>
; CHECK-NEXT: ret <2 x i64> %1
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %v, <2 x i64> <i64 15, i64 9999>)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psrl_q_64(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psrl_q_64
; CHECK-NEXT: ret <2 x i64> zeroinitializer
  %1 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %v, <2 x i64> <i64 64, i64 9999>)
  ret <2 x i64> %1
}

define <16 x i16> @avx2_psrl_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrl_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> zeroinitializer)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrl_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrl_w_15
; CHECK-NEXT: %1 = lshr <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrl_w_15_splat(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrl_w_15_splat
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psrl_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psrl_w_64
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_psrl_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrl_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> zeroinitializer)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrl_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrl_d_15
; CHECK-NEXT: %1 = lshr <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrl_d_15_splat(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrl_d_15_splat
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psrl_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psrl_d_64
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

define <4 x i64> @avx2_psrl_q_0(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrl_q_0
; CHECK-NEXT: ret <4 x i64> %v
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %v, <2 x i64> zeroinitializer)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psrl_q_15(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrl_q_15
; CHECK-NEXT: %1 = lshr <4 x i64> %v, <i64 15, i64 15, i64 15, i64 15>
; CHECK-NEXT: ret <4 x i64> %1
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %v, <2 x i64> <i64 15, i64 9999>)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psrl_q_64(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psrl_q_64
; CHECK-NEXT: ret <4 x i64> zeroinitializer
  %1 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %v, <2 x i64> <i64 64, i64 9999>)
  ret <4 x i64> %1
}

;
; SHL - Constant Vector
;

define <8 x i16> @sse2_psll_w_0(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psll_w_0
; CHECK-NEXT: ret <8 x i16> %v
  %1 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> zeroinitializer)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psll_w_15(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psll_w_15
; CHECK-NEXT: %1 = shl <8 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <8 x i16> %1
  %1 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psll_w_15_splat(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psll_w_15_splat
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <8 x i16> %1
}

define <8 x i16> @sse2_psll_w_64(<8 x i16> %v) {
; CHECK-LABEL: @sse2_psll_w_64
; CHECK-NEXT: ret <8 x i16> zeroinitializer
  %1 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <8 x i16> %1
}

define <4 x i32> @sse2_psll_d_0(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psll_d_0
; CHECK-NEXT: ret <4 x i32> %v
  %1 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> zeroinitializer)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psll_d_15(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psll_d_15
; CHECK-NEXT: %1 = shl <4 x i32> %v, <i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <4 x i32> %1
  %1 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psll_d_15_splat(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psll_d_15_splat
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <4 x i32> %1
}

define <4 x i32> @sse2_psll_d_64(<4 x i32> %v) {
; CHECK-LABEL: @sse2_psll_d_64
; CHECK-NEXT: ret <4 x i32> zeroinitializer
  %1 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <4 x i32> %1
}

define <2 x i64> @sse2_psll_q_0(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psll_q_0
; CHECK-NEXT: ret <2 x i64> %v
  %1 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %v, <2 x i64> zeroinitializer)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psll_q_15(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psll_q_15
; CHECK-NEXT: %1 = shl <2 x i64> %v, <i64 15, i64 15>
; CHECK-NEXT: ret <2 x i64> %1
  %1 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %v, <2 x i64> <i64 15, i64 9999>)
  ret <2 x i64> %1
}

define <2 x i64> @sse2_psll_q_64(<2 x i64> %v) {
; CHECK-LABEL: @sse2_psll_q_64
; CHECK-NEXT: ret <2 x i64> zeroinitializer
  %1 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %v, <2 x i64> <i64 64, i64 9999>)
  ret <2 x i64> %1
}

define <16 x i16> @avx2_psll_w_0(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psll_w_0
; CHECK-NEXT: ret <16 x i16> %v
  %1 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> zeroinitializer)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psll_w_15(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psll_w_15
; CHECK-NEXT: %1 = shl <16 x i16> %v, <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>
; CHECK-NEXT: ret <16 x i16> %1
  %1 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> <i16 15, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psll_w_15_splat(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psll_w_15_splat
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> <i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15, i16 15>)
  ret <16 x i16> %1
}

define <16 x i16> @avx2_psll_w_64(<16 x i16> %v) {
; CHECK-LABEL: @avx2_psll_w_64
; CHECK-NEXT: ret <16 x i16> zeroinitializer
  %1 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> <i16 64, i16 0, i16 0, i16 0, i16 9999, i16 9999, i16 9999, i16 9999>)
  ret <16 x i16> %1
}

define <8 x i32> @avx2_psll_d_0(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psll_d_0
; CHECK-NEXT: ret <8 x i32> %v
  %1 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> zeroinitializer)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psll_d_15(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psll_d_15
; CHECK-NEXT: %1 = shl <8 x i32> %v, <i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15, i32 15>
; CHECK-NEXT: ret <8 x i32> %1
  %1 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> <i32 15, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psll_d_15_splat(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psll_d_15_splat
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> <i32 15, i32 15, i32 15, i32 15>)
  ret <8 x i32> %1
}

define <8 x i32> @avx2_psll_d_64(<8 x i32> %v) {
; CHECK-LABEL: @avx2_psll_d_64
; CHECK-NEXT: ret <8 x i32> zeroinitializer
  %1 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> <i32 64, i32 0, i32 9999, i32 9999>)
  ret <8 x i32> %1
}

define <4 x i64> @avx2_psll_q_0(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psll_q_0
; CHECK-NEXT: ret <4 x i64> %v
  %1 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %v, <2 x i64> zeroinitializer)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psll_q_15(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psll_q_15
; CHECK-NEXT: %1 = shl <4 x i64> %v, <i64 15, i64 15, i64 15, i64 15>
; CHECK-NEXT: ret <4 x i64> %1
  %1 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %v, <2 x i64> <i64 15, i64 9999>)
  ret <4 x i64> %1
}

define <4 x i64> @avx2_psll_q_64(<4 x i64> %v) {
; CHECK-LABEL: @avx2_psll_q_64
; CHECK-NEXT: ret <4 x i64> zeroinitializer
  %1 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %v, <2 x i64> <i64 64, i64 9999>)
  ret <4 x i64> %1
}

;
; Vector Demanded Bits
;

define <8 x i16> @sse2_psra_w_var(<8 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @sse2_psra_w_var
; CHECK-NEXT: %1 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <8 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> %1)
  ret <8 x i16> %2
}

define <8 x i16> @sse2_psra_w_var_bc(<8 x i16> %v, <2 x i64> %a) {
; CHECK-LABEL: @sse2_psra_w_var_bc
; CHECK-NEXT: %1 = bitcast <2 x i64> %a to <8 x i16>
; CHECK-NEXT: %2 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> %1)
; CHECK-NEXT: ret <8 x i16> %2
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = bitcast <2 x i64> %1 to <8 x i16>
  %3 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %v, <8 x i16> %2)
  ret <8 x i16> %3
}

define <4 x i32> @sse2_psra_d_var(<4 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @sse2_psra_d_var
; CHECK-NEXT: %1 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <4 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> %1)
  ret <4 x i32> %2
}

define <4 x i32> @sse2_psra_d_var_bc(<4 x i32> %v, <8 x i16> %a) {
; CHECK-LABEL: @sse2_psra_d_var_bc
; CHECK-NEXT: %1 = bitcast <8 x i16> %a to <4 x i32>
; CHECK-NEXT: %2 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> %1)
; CHECK-NEXT: ret <4 x i32> %2
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = bitcast <8 x i16> %1 to <4 x i32>
  %3 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %v, <4 x i32> %2)
  ret <4 x i32> %3
}

define <16 x i16> @avx2_psra_w_var(<16 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @avx2_psra_w_var
; CHECK-NEXT: %1 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <16 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %v, <8 x i16> %1)
  ret <16 x i16> %2
}

define <8 x i32> @avx2_psra_d_var(<8 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @avx2_psra_d_var
; CHECK-NEXT: %1 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <8 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %v, <4 x i32> %1)
  ret <8 x i32> %2
}

define <8 x i16> @sse2_psrl_w_var(<8 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @sse2_psrl_w_var
; CHECK-NEXT: %1 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <8 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> %v, <8 x i16> %1)
  ret <8 x i16> %2
}

define <4 x i32> @sse2_psrl_d_var(<4 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @sse2_psrl_d_var
; CHECK-NEXT: %1 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <4 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %v, <4 x i32> %1)
  ret <4 x i32> %2
}

define <2 x i64> @sse2_psrl_q_var(<2 x i64> %v, <2 x i64> %a) {
; CHECK-LABEL: @sse2_psrl_q_var
; CHECK-NEXT: %1 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %v, <2 x i64> %a)
; CHECK-NEXT: ret <2 x i64> %1
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %v, <2 x i64> %1)
  ret <2 x i64> %2
}

define <16 x i16> @avx2_psrl_w_var(<16 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @avx2_psrl_w_var
; CHECK-NEXT: %1 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <16 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> %1)
  ret <16 x i16> %2
}

define <16 x i16> @avx2_psrl_w_var_bc(<16 x i16> %v, <16 x i8> %a) {
; CHECK-LABEL: @avx2_psrl_w_var_bc
; CHECK-NEXT: %1 = bitcast <16 x i8> %a to <8 x i16>
; CHECK-NEXT: %2 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> %1)
; CHECK-NEXT: ret <16 x i16> %2
  %1 = shufflevector <16 x i8> %a, <16 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = bitcast <16 x i8> %1 to <8 x i16>
  %3 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> %v, <8 x i16> %2)
  ret <16 x i16> %3
}

define <8 x i32> @avx2_psrl_d_var(<8 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @avx2_psrl_d_var
; CHECK-NEXT: %1 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <8 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> %1)
  ret <8 x i32> %2
}

define <8 x i32> @avx2_psrl_d_var_bc(<8 x i32> %v, <2 x i64> %a) {
; CHECK-LABEL: @avx2_psrl_d_var_bc
; CHECK-NEXT: %1 = bitcast <2 x i64> %a to <4 x i32>
; CHECK-NEXT: %2 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> %1)
; CHECK-NEXT: ret <8 x i32> %2
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = bitcast <2 x i64> %1 to <4 x i32>
  %3 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %v, <4 x i32> %2)
  ret <8 x i32> %3
}

define <4 x i64> @avx2_psrl_q_var(<4 x i64> %v, <2 x i64> %a) {
; CHECK-LABEL: @avx2_psrl_q_var
; CHECK-NEXT: %1 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %v, <2 x i64> %a)
; CHECK-NEXT: ret <4 x i64> %1
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %v, <2 x i64> %1)
  ret <4 x i64> %2
}

define <8 x i16> @sse2_psll_w_var(<8 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @sse2_psll_w_var
; CHECK-NEXT: %1 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <8 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> %v, <8 x i16> %1)
  ret <8 x i16> %2
}

define <4 x i32> @sse2_psll_d_var(<4 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @sse2_psll_d_var
; CHECK-NEXT: %1 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <4 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %v, <4 x i32> %1)
  ret <4 x i32> %2
}

define <2 x i64> @sse2_psll_q_var(<2 x i64> %v, <2 x i64> %a) {
; CHECK-LABEL: @sse2_psll_q_var
; CHECK-NEXT: %1 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %v, <2 x i64> %a)
; CHECK-NEXT: ret <2 x i64> %1
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %v, <2 x i64> %1)
  ret <2 x i64> %2
}

define <16 x i16> @avx2_psll_w_var(<16 x i16> %v, <8 x i16> %a) {
; CHECK-LABEL: @avx2_psll_w_var
; CHECK-NEXT: %1 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> %a)
; CHECK-NEXT: ret <16 x i16> %1
  %1 = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 0, i32 1, i32 2, i32 3>
  %2 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> %v, <8 x i16> %1)
  ret <16 x i16> %2
}

define <8 x i32> @avx2_psll_d_var(<8 x i32> %v, <4 x i32> %a) {
; CHECK-LABEL: @avx2_psll_d_var
; CHECK-NEXT: %1 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> %a)
; CHECK-NEXT: ret <8 x i32> %1
  %1 = shufflevector <4 x i32> %a, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 1>
  %2 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %v, <4 x i32> %1)
  ret <8 x i32> %2
}

define <4 x i64> @avx2_psll_q_var(<4 x i64> %v, <2 x i64> %a) {
; CHECK-LABEL: @avx2_psll_q_var
; CHECK-NEXT: %1 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %v, <2 x i64> %a)
; CHECK-NEXT: ret <4 x i64> %1
  %1 = shufflevector <2 x i64> %a, <2 x i64> undef, <2 x i32> <i32 0, i32 0>
  %2 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %v, <2 x i64> %1)
  ret <4 x i64> %2
}

;
; Constant Folding
;

define <8 x i16> @test_sse2_psra_w_0(<8 x i16> %A) {
; CHECK-LABEL: @test_sse2_psra_w_0
; CHECK-NEXT: ret <8 x i16> %A
  %1 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %A, i32 0)
  %2 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %1, <8 x i16> <i16 0, i16 0, i16 0, i16 0, i16 7, i16 0, i16 0, i16 0>)
  %3 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %2, i32 0)
  ret <8 x i16> %3
}

define <8 x i16> @test_sse2_psra_w_8() {
; CHECK-LABEL: @test_sse2_psra_w_8
; CHECK-NEXT: ret <8 x i16> <i16 -128, i16 64, i16 32, i16 16, i16 -128, i16 64, i16 32, i16 16>
  %1 = bitcast <2 x i64> <i64 1152956690052710400, i64 1152956690052710400> to <8 x i16>
  %2 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %1, i32 3)
  %3 = tail call <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16> %2, <8 x i16> <i16 3, i16 0, i16 0, i16 0, i16 7, i16 0, i16 0, i16 0>)
  %4 = tail call <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16> %3, i32 2)
  ret <8 x i16> %4
}

define <4 x i32> @test_sse2_psra_d_0(<4 x i32> %A) {
; CHECK-LABEL: @test_sse2_psra_d_0
; CHECK-NEXT: ret <4 x i32> %A
  %1 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %A, i32 0)
  %2 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %1, <4 x i32> <i32 0, i32 0, i32 7, i32 0>)
  %3 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %1, i32 0)
  ret <4 x i32> %3
}

define <4 x i32> @sse2_psra_d_8() {
; CHECK-LABEL: @sse2_psra_d_8
; CHECK-NEXT: ret <4 x i32> <i32 4194432, i32 1048608, i32 4194432, i32 1048608>
  %1 = bitcast <2 x i64> <i64 1152956690052710400, i64 1152956690052710400> to <4 x i32>
  %2 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %1, i32 3)
  %3 = tail call <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32> %2, <4 x i32> <i32 3, i32 0, i32 7, i32 0>)
  %4 = tail call <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32> %3, i32 2)
  ret <4 x i32> %4
}

define <16 x i16> @test_avx2_psra_w_0(<16 x i16> %A) {
; CHECK-LABEL: @test_avx2_psra_w_0
; CHECK-NEXT: ret <16 x i16> %A
  %1 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %A, i32 0)
  %2 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %1, <8 x i16> <i16 0, i16 0, i16 0, i16 0, i16 7, i16 0, i16 0, i16 0>)
  %3 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %2, i32 0)
  ret <16 x i16> %3
}

define <16 x i16> @test_avx2_psra_w_8(<16 x i16> %A) {
; CHECK-LABEL: @test_avx2_psra_w_8
; CHECK-NEXT: ret <16 x i16> <i16 -128, i16 64, i16 32, i16 16, i16 -128, i16 64, i16 32, i16 16, i16 -128, i16 64, i16 32, i16 16, i16 -128, i16 64, i16 32, i16 16>
  %1 = bitcast <4 x i64> <i64 1152956690052710400, i64 1152956690052710400, i64 1152956690052710400, i64 1152956690052710400> to <16 x i16>
  %2 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %1, i32 3)
  %3 = tail call <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16> %2, <8 x i16> <i16 3, i16 0, i16 0, i16 0, i16 7, i16 0, i16 0, i16 0>)
  %4 = tail call <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16> %3, i32 2)
  ret <16 x i16> %4
}

define <8 x i32> @test_avx2_psra_d_0(<8 x i32> %A) {
; CHECK-LABEL: @test_avx2_psra_d_0
; CHECK-NEXT: ret <8 x i32> %A
  %1 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %A, i32 0)
  %2 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %1, <4 x i32> <i32 0, i32 0, i32 7, i32 0>)
  %3 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %2, i32 0)
  ret <8 x i32> %3
}

define <8 x i32> @test_avx2_psra_d_8() {
; CHECK-LABEL: @test_avx2_psra_d_8
; CHECK-NEXT: ret <8 x i32> <i32 4194432, i32 1048608, i32 4194432, i32 1048608, i32 4194432, i32 1048608, i32 4194432, i32 1048608>
  %1 = bitcast <4 x i64> <i64 1152956690052710400, i64 1152956690052710400, i64 1152956690052710400, i64 1152956690052710400> to <8 x i32>
  %2 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %1, i32 3)
  %3 = tail call <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32> %2, <4 x i32> <i32 3, i32 0, i32 7, i32 0>)
  %4 = tail call <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32> %3, i32 2)
  ret <8 x i32> %4
}

define <2 x i64> @test_sse2_1() {
  %S = bitcast i32 1 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> <i16 1, i16 2, i16 3, i16 4, i16 5, i16 6, i16 7, i16 8>, <8 x i16> %4)
  %6 = bitcast <8 x i16> %5 to <4 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %6, <4 x i32> %7)
  %9 = bitcast <4 x i32> %8 to <2 x i64>
  %10 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %9, <2 x i64> %3)
  %11 = bitcast <2 x i64> %10 to <8 x i16>
  %12 = tail call <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16> %11, i32 %S)
  %13 = bitcast <8 x i16> %12 to <4 x i32>
  %14 = tail call <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32> %13, i32 %S)
  %15 = bitcast <4 x i32> %14 to <2 x i64>
  %16 = tail call <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64> %15, i32 %S)
  ret <2 x i64> %16
; CHECK: test_sse2_1
; CHECK: ret <2 x i64> <i64 72058418680037440, i64 144117112246370624>
}

define <4 x i64> @test_avx2_1() {
  %S = bitcast i32 1 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> <i16 1, i16 0, i16 0, i16 0, i16 2, i16 0, i16 0, i16 0, i16 3, i16 0, i16 0, i16 0, i16 4, i16 0, i16 0, i16 0>, <8 x i16> %4)
  %6 = bitcast <16 x i16> %5 to <8 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %6, <4 x i32> %7)
  %9 = bitcast <8 x i32> %8 to <4 x i64>
  %10 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %9, <2 x i64> %3)
  %11 = bitcast <4 x i64> %10 to <16 x i16>
  %12 = tail call <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16> %11, i32 %S)
  %13 = bitcast <16 x i16> %12 to <8 x i32>
  %14 = tail call <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32> %13, i32 %S)
  %15 = bitcast <8 x i32> %14 to <4 x i64>
  %16 = tail call <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64> %15, i32 %S)
  ret <4 x i64> %16
; CHECK: test_avx2_1
; CHECK: ret <4 x i64> <i64 64, i64 128, i64 192, i64 256>
}

define <2 x i64> @test_sse2_0() {
  %S = bitcast i32 128 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16> <i16 1, i16 2, i16 3, i16 4, i16 5, i16 6, i16 7, i16 8>, <8 x i16> %4)
  %6 = bitcast <8 x i16> %5 to <4 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32> %6, <4 x i32> %7)
  %9 = bitcast <4 x i32> %8 to <2 x i64>
  %10 = tail call <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64> %9, <2 x i64> %3)
  %11 = bitcast <2 x i64> %10 to <8 x i16>
  %12 = tail call <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16> %11, i32 %S)
  %13 = bitcast <8 x i16> %12 to <4 x i32>
  %14 = tail call <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32> %13, i32 %S)
  %15 = bitcast <4 x i32> %14 to <2 x i64>
  %16 = tail call <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64> %15, i32 %S)
  ret <2 x i64> %16
; CHECK: test_sse2_0
; CHECK: ret <2 x i64> zeroinitializer
}

define <4 x i64> @test_avx2_0() {
  %S = bitcast i32 128 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16> <i16 1, i16 0, i16 0, i16 0, i16 2, i16 0, i16 0, i16 0, i16 3, i16 0, i16 0, i16 0, i16 4, i16 0, i16 0, i16 0>, <8 x i16> %4)
  %6 = bitcast <16 x i16> %5 to <8 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32> %6, <4 x i32> %7)
  %9 = bitcast <8 x i32> %8 to <4 x i64>
  %10 = tail call <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64> %9, <2 x i64> %3)
  %11 = bitcast <4 x i64> %10 to <16 x i16>
  %12 = tail call <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16> %11, i32 %S)
  %13 = bitcast <16 x i16> %12 to <8 x i32>
  %14 = tail call <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32> %13, i32 %S)
  %15 = bitcast <8 x i32> %14 to <4 x i64>
  %16 = tail call <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64> %15, i32 %S)
  ret <4 x i64> %16
; CHECK: test_avx2_0
; CHECK: ret <4 x i64> zeroinitializer
}
define <2 x i64> @test_sse2_psrl_1() {
  %S = bitcast i32 1 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> <i16 16, i16 32, i16 64, i16 128, i16 256, i16 512, i16 1024, i16 2048>, <8 x i16> %4)
  %6 = bitcast <8 x i16> %5 to <4 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %6, <4 x i32> %7)
  %9 = bitcast <4 x i32> %8 to <2 x i64>
  %10 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %9, <2 x i64> %3)
  %11 = bitcast <2 x i64> %10 to <8 x i16>
  %12 = tail call <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16> %11, i32 %S)
  %13 = bitcast <8 x i16> %12 to <4 x i32>
  %14 = tail call <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32> %13, i32 %S)
  %15 = bitcast <4 x i32> %14 to <2 x i64>
  %16 = tail call <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64> %15, i32 %S)
  ret <2 x i64> %16
; CHECK: test_sse2_psrl_1
; CHECK: ret <2 x i64> <i64 562954248421376, i64 9007267974742020>
}

define <4 x i64> @test_avx2_psrl_1() {
  %S = bitcast i32 1 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> <i16 1024, i16 0, i16 0, i16 0, i16 2048, i16 0, i16 0, i16 0, i16 4096, i16 0, i16 0, i16 0, i16 8192, i16 0, i16 0, i16 0>, <8 x i16> %4)
  %6 = bitcast <16 x i16> %5 to <8 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %6, <4 x i32> %7)
  %9 = bitcast <8 x i32> %8 to <4 x i64>
  %10 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %9, <2 x i64> %3)
  %11 = bitcast <4 x i64> %10 to <16 x i16>
  %12 = tail call <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16> %11, i32 %S)
  %13 = bitcast <16 x i16> %12 to <8 x i32>
  %14 = tail call <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32> %13, i32 %S)
  %15 = bitcast <8 x i32> %14 to <4 x i64>
  %16 = tail call <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64> %15, i32 %S)
  ret <4 x i64> %16
; CHECK: test_avx2_psrl_1
; CHECK: ret <4 x i64> <i64 16, i64 32, i64 64, i64 128>
}

define <2 x i64> @test_sse2_psrl_0() {
  %S = bitcast i32 128 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16> <i16 32, i16 64, i16 128, i16 256, i16 512, i16 1024, i16 2048, i16 4096>, <8 x i16> %4)
  %6 = bitcast <8 x i16> %5 to <4 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32> %6, <4 x i32> %7)
  %9 = bitcast <4 x i32> %8 to <2 x i64>
  %10 = tail call <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64> %9, <2 x i64> %3)
  %11 = bitcast <2 x i64> %10 to <8 x i16>
  %12 = tail call <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16> %11, i32 %S)
  %13 = bitcast <8 x i16> %12 to <4 x i32>
  %14 = tail call <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32> %13, i32 %S)
  %15 = bitcast <4 x i32> %14 to <2 x i64>
  %16 = tail call <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64> %15, i32 %S)
  ret <2 x i64> %16
; CHECK: test_sse2_psrl_0
; CHECK: ret <2 x i64> zeroinitializer
}

define <4 x i64> @test_avx2_psrl_0() {
  %S = bitcast i32 128 to i32
  %1 = zext i32 %S to i64
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 0, i32 1
  %4 = bitcast <2 x i64> %3 to <8 x i16>
  %5 = tail call <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16> <i16 1024, i16 0, i16 0, i16 0, i16 2048, i16 0, i16 0, i16 0, i16 4096, i16 0, i16 0, i16 0, i16 8192, i16 0, i16 0, i16 0>, <8 x i16> %4)
  %6 = bitcast <16 x i16> %5 to <8 x i32>
  %7 = bitcast <2 x i64> %3 to <4 x i32>
  %8 = tail call <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32> %6, <4 x i32> %7)
  %9 = bitcast <8 x i32> %8 to <4 x i64>
  %10 = tail call <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64> %9, <2 x i64> %3)
  %11 = bitcast <4 x i64> %10 to <16 x i16>
  %12 = tail call <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16> %11, i32 %S)
  %13 = bitcast <16 x i16> %12 to <8 x i32>
  %14 = tail call <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32> %13, i32 %S)
  %15 = bitcast <8 x i32> %14 to <4 x i64>
  %16 = tail call <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64> %15, i32 %S)
  ret <4 x i64> %16
; CHECK: test_avx2_psrl_0
; CHECK: ret <4 x i64> zeroinitializer
}

declare <4 x i64> @llvm.x86.avx2.pslli.q(<4 x i64>, i32) #1
declare <8 x i32> @llvm.x86.avx2.pslli.d(<8 x i32>, i32) #1
declare <16 x i16> @llvm.x86.avx2.pslli.w(<16 x i16>, i32) #1
declare <4 x i64> @llvm.x86.avx2.psll.q(<4 x i64>, <2 x i64>) #1
declare <8 x i32> @llvm.x86.avx2.psll.d(<8 x i32>, <4 x i32>) #1
declare <16 x i16> @llvm.x86.avx2.psll.w(<16 x i16>, <8 x i16>) #1
declare <2 x i64> @llvm.x86.sse2.pslli.q(<2 x i64>, i32) #1
declare <4 x i32> @llvm.x86.sse2.pslli.d(<4 x i32>, i32) #1
declare <8 x i16> @llvm.x86.sse2.pslli.w(<8 x i16>, i32) #1
declare <2 x i64> @llvm.x86.sse2.psll.q(<2 x i64>, <2 x i64>) #1
declare <4 x i32> @llvm.x86.sse2.psll.d(<4 x i32>, <4 x i32>) #1
declare <8 x i16> @llvm.x86.sse2.psll.w(<8 x i16>, <8 x i16>) #1

declare <4 x i64> @llvm.x86.avx2.psrli.q(<4 x i64>, i32) #1
declare <8 x i32> @llvm.x86.avx2.psrli.d(<8 x i32>, i32) #1
declare <16 x i16> @llvm.x86.avx2.psrli.w(<16 x i16>, i32) #1
declare <4 x i64> @llvm.x86.avx2.psrl.q(<4 x i64>, <2 x i64>) #1
declare <8 x i32> @llvm.x86.avx2.psrl.d(<8 x i32>, <4 x i32>) #1
declare <16 x i16> @llvm.x86.avx2.psrl.w(<16 x i16>, <8 x i16>) #1
declare <2 x i64> @llvm.x86.sse2.psrli.q(<2 x i64>, i32) #1
declare <4 x i32> @llvm.x86.sse2.psrli.d(<4 x i32>, i32) #1
declare <8 x i16> @llvm.x86.sse2.psrli.w(<8 x i16>, i32) #1
declare <2 x i64> @llvm.x86.sse2.psrl.q(<2 x i64>, <2 x i64>) #1
declare <4 x i32> @llvm.x86.sse2.psrl.d(<4 x i32>, <4 x i32>) #1
declare <8 x i16> @llvm.x86.sse2.psrl.w(<8 x i16>, <8 x i16>) #1

declare <8 x i32> @llvm.x86.avx2.psrai.d(<8 x i32>, i32) #1
declare <16 x i16> @llvm.x86.avx2.psrai.w(<16 x i16>, i32) #1
declare <8 x i32> @llvm.x86.avx2.psra.d(<8 x i32>, <4 x i32>) #1
declare <16 x i16> @llvm.x86.avx2.psra.w(<16 x i16>, <8 x i16>) #1
declare <4 x i32> @llvm.x86.sse2.psrai.d(<4 x i32>, i32) #1
declare <8 x i16> @llvm.x86.sse2.psrai.w(<8 x i16>, i32) #1
declare <4 x i32> @llvm.x86.sse2.psra.d(<4 x i32>, <4 x i32>) #1
declare <8 x i16> @llvm.x86.sse2.psra.w(<8 x i16>, <8 x i16>) #1

attributes #1 = { nounwind readnone }
