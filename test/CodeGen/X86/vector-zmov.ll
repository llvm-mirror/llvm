; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+sse2 | FileCheck %s --check-prefix=SSE --check-prefix=SSE2
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+ssse3 | FileCheck %s --check-prefix=SSE --check-prefix=SSSE3
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+sse4.1 | FileCheck %s --check-prefix=SSE --check-prefix=SSE41
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+avx | FileCheck %s --check-prefix=AVX --check-prefix=AVX1
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+avx2 | FileCheck %s --check-prefix=AVX --check-prefix=AVX2

define <4 x i32> @load_zmov_4i32_to_0zzz(<4 x i32> *%ptr) {
; SSE-LABEL:  load_zmov_4i32_to_0zzz:
; SSE:        # BB#0: # %entry
; SSE-NEXT:   movd (%rdi), %xmm0
; SSE-NEXT:   retq

; AVX-LABEL:  load_zmov_4i32_to_0zzz:
; AVX:        # BB#0: # %entry
; AVX-NEXT:   vmovd (%rdi), %xmm0
; AVX-NEXT:   retq
entry:
  %X = load <4 x i32>* %ptr
  %Y = shufflevector <4 x i32> %X, <4 x i32> zeroinitializer, <4 x i32> <i32 0, i32 4, i32 4, i32 4>
  ret <4 x i32>%Y
}

define <2 x i64> @load_zmov_2i64_to_0z(<2 x i64> *%ptr) {
; SSE-LABEL:  load_zmov_2i64_to_0z:
; SSE:        # BB#0: # %entry
; SSE-NEXT:   movq (%rdi), %xmm0
; SSE-NEXT:   retq

; AVX-LABEL:  load_zmov_2i64_to_0z:
; AVX:        # BB#0: # %entry
; AVX-NEXT:   vmovq (%rdi), %xmm0
; AVX-NEXT:   retq
entry:
  %X = load <2 x i64>* %ptr
  %Y = shufflevector <2 x i64> %X, <2 x i64> zeroinitializer, <2 x i32> <i32 0, i32 2>
  ret <2 x i64>%Y
}
