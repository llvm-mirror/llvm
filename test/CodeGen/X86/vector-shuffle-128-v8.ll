; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE2
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+ssse3 -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSSE3
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+sse4.1 -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE41
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+avx -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX1
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mcpu=x86-64 -mattr=+avx2 -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX2

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-unknown"

define <8 x i16> @shuffle_v8i16_01012323(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_01012323:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,0,1,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_01012323:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,0,1,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 1, i32 0, i32 1, i32 2, i32 3, i32 2, i32 3>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_67452301(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_67452301:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,2,1,0]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_67452301:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[3,2,1,0]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 6, i32 7, i32 4, i32 5, i32 2, i32 3, i32 0, i32 1>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_456789AB(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_456789AB:
; SSE2:       # BB#0:
; SSE2-NEXT:    shufpd {{.*#+}} xmm0 = xmm0[1],xmm1[0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_456789AB:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm1 = xmm0[8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_456789AB:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm1 = xmm0[8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_456789AB:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_00000000(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_00000000:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,0,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_00000000:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_00000000:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: shuffle_v8i16_00000000:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: shuffle_v8i16_00000000:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastw %xmm0, %xmm0
; AVX2-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_00004444(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_00004444:
; SSE:       # BB#0:
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,0,0,4,5,6,7]
; SSE-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_00004444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[0,0,0,0,4,5,6,7]
; AVX-NEXT:    vpshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 0, i32 0, i32 0, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_u0u1u2u3(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_u0u1u2u3:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u0u1u2u3:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 0, i32 undef, i32 1, i32 undef, i32 2, i32 undef, i32 3>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_u4u5u6u7(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_u4u5u6u7:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhwd {{.*#+}} xmm0 = xmm0[4,4,5,5,6,6,7,7]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u4u5u6u7:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm0[4,4,5,5,6,6,7,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 4, i32 undef, i32 5, i32 undef, i32 6, i32 undef, i32 7>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_31206745(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_31206745:
; SSE:       # BB#0:
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,1,2,0,4,5,6,7]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,3,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_31206745:
; AVX:       # BB#0:
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[3,1,2,0,4,5,6,7]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,3,2]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 3, i32 1, i32 2, i32 0, i32 6, i32 7, i32 4, i32 5>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_44440000(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_44440000:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,0,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_44440000:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,8,9,0,1,0,1,0,1,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_44440000:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,8,9,0,1,0,1,0,1,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_44440000:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,8,9,0,1,0,1,0,1,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 4, i32 4, i32 4, i32 0, i32 0, i32 0, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_23016745(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_23016745:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,0,3,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_23016745:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,0,3,2]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 3, i32 0, i32 1, i32 6, i32 7, i32 4, i32 5>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_23026745(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_23026745:
; SSE:       # BB#0:
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,3,0,2,4,5,6,7]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,3,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_23026745:
; AVX:       # BB#0:
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[2,3,0,2,4,5,6,7]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,3,2]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 3, i32 0, i32 2, i32 6, i32 7, i32 4, i32 5>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_23016747(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_23016747:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,0,2,3]
; SSE-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,7,4,7]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_23016747:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,0,2,3]
; AVX-NEXT:    vpshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,7,4,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 3, i32 0, i32 1, i32 6, i32 7, i32 4, i32 7>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_75643120(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_75643120:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,1,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_75643120:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[14,15,10,11,12,13,8,9,6,7,2,3,4,5,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_75643120:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[14,15,10,11,12,13,8,9,6,7,2,3,4,5,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_75643120:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[14,15,10,11,12,13,8,9,6,7,2,3,4,5,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 7, i32 5, i32 6, i32 4, i32 3, i32 1, i32 2, i32 0>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_10545410(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_10545410:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,0,3,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,4,7,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_10545410:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[2,3,0,1,10,11,8,9,10,11,8,9,2,3,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_10545410:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[2,3,0,1,10,11,8,9,10,11,8,9,2,3,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_10545410:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[2,3,0,1,10,11,8,9,10,11,8,9,2,3,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 1, i32 0, i32 5, i32 4, i32 5, i32 4, i32 1, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_54105410(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_54105410:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,4,7,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_54105410:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,10,11,8,9,2,3,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_54105410:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,10,11,8,9,2,3,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_54105410:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,10,11,8,9,2,3,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 5, i32 4, i32 1, i32 0, i32 5, i32 4, i32 1, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_54101054(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_54101054:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_54101054:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,2,3,0,1,10,11,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_54101054:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,2,3,0,1,10,11,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_54101054:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[10,11,8,9,2,3,0,1,2,3,0,1,10,11,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 5, i32 4, i32 1, i32 0, i32 1, i32 0, i32 5, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_04400440(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_04400440:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,4,4,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_04400440:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,8,9,8,9,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_04400440:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,8,9,8,9,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_04400440:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,8,9,8,9,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 4, i32 4, i32 0, i32 0, i32 4, i32 4, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_40044004(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_40044004:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,0,0,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_40044004:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,0,1,0,1,8,9,8,9,0,1,0,1,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_40044004:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,0,1,0,1,8,9,8,9,0,1,0,1,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_40044004:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,0,1,0,1,8,9,8,9,0,1,0,1,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 0, i32 0, i32 4, i32 4, i32 0, i32 0, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_26405173(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_26405173:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,1,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,4]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,2,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,2,3,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,6,4,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_26405173:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,10,11,2,3,14,15,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_26405173:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,10,11,2,3,14,15,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_26405173:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,10,11,2,3,14,15,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 6, i32 4, i32 0, i32 5, i32 1, i32 7, i32 3>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_20645173(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_20645173:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,1,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,4]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,2,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,0,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,6,4,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_20645173:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,0,1,12,13,8,9,10,11,2,3,14,15,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_20645173:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,0,1,12,13,8,9,10,11,2,3,14,15,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_20645173:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[4,5,0,1,12,13,8,9,10,11,2,3,14,15,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 0, i32 6, i32 4, i32 5, i32 1, i32 7, i32 3>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_26401375(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_26401375:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,1,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,4]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,1,2]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,2,3,0,4,5,6,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_26401375:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,2,3,6,7,14,15,10,11]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_26401375:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,2,3,6,7,14,15,10,11]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_26401375:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[4,5,12,13,8,9,0,1,2,3,6,7,14,15,10,11]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 2, i32 6, i32 4, i32 0, i32 1, i32 3, i32 7, i32 5>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_66751643(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_66751643:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,1,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,5,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,1,3,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,4,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_66751643:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,12,13,14,15,10,11,2,3,12,13,8,9,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_66751643:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,12,13,14,15,10,11,2,3,12,13,8,9,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_66751643:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[12,13,12,13,14,15,10,11,2,3,12,13,8,9,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 6, i32 6, i32 7, i32 5, i32 1, i32 6, i32 4, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_60514754(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_60514754:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,5,4,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,0,3,1,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,7,5,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_60514754:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,0,1,10,11,2,3,8,9,14,15,10,11,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_60514754:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,0,1,10,11,2,3,8,9,14,15,10,11,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_60514754:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[12,13,0,1,10,11,2,3,8,9,14,15,10,11,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> undef, <8 x i32> <i32 6, i32 0, i32 5, i32 1, i32 4, i32 7, i32 5, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_00444444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_00444444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,2,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_00444444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,8,9,8,9,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_00444444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,8,9,8,9,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_00444444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,0,1,8,9,8,9,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 0, i32 4, i32 4, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_44004444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_44004444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,2,0,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_44004444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,0,1,0,1,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_44004444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,0,1,0,1,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_44004444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,0,1,0,1,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 4, i32 0, i32 0, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_04404444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_04404444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_04404444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_04404444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_04404444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 4, i32 4, i32 0, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_04400000(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_04400000:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,0,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_04400000:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,0,1,0,1,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_04400000:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,0,1,0,1,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_04400000:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,0,1,0,1,0,1,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 4, i32 4, i32 0, i32 0, i32 0, i32 0, i32 0>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_04404567(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_04404567:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_04404567:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 4, i32 4, i32 0, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0X444444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_0X444444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,1,2,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0X444444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,8,9,8,9,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0X444444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,8,9,8,9,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0X444444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,8,9,8,9,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 undef, i32 4, i32 4, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_44X04444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_44X04444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,2,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_44X04444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_44X04444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_44X04444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 4, i32 undef, i32 0, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_X4404444(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_X4404444:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,4,4]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_X4404444:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_X4404444:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_X4404444:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,8,9,8,9,0,1,8,9,8,9,8,9,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 4, i32 4, i32 0, i32 4, i32 4, i32 4, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0127XXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_0127XXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,7,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0127XXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,4,5,14,15,12,13,14,15]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0127XXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,4,5,14,15,12,13,14,15]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0127XXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,4,5,14,15,12,13,14,15]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 1, i32 2, i32 7, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_XXXX4563(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_XXXX4563:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,3,4,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,2,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_XXXX4563:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,6,7,4,5,6,7,8,9,10,11,12,13,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_XXXX4563:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[12,13,6,7,4,5,6,7,8,9,10,11,12,13,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_XXXX4563:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[12,13,6,7,4,5,6,7,8,9,10,11,12,13,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 undef, i32 4, i32 5, i32 6, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_4563XXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_4563XXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,3,4,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,0,2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_4563XXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,8,9,10,11,0,1,2,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_4563XXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,8,9,10,11,0,1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_4563XXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,8,9,10,11,0,1,2,3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 5, i32 6, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_01274563(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_01274563:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,5,4,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,1,2]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_01274563:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,8,9,10,11,12,13,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_01274563:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,8,9,10,11,12,13,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_01274563:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,14,15,8,9,10,11,12,13,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 1, i32 2, i32 7, i32 4, i32 5, i32 6, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_45630127(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_45630127:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,1,4,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,0,3,1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_45630127:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,0,1,2,3,4,5,14,15]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_45630127:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,0,1,2,3,4,5,14,15]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_45630127:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[8,9,10,11,12,13,6,7,0,1,2,3,4,5,14,15]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 5, i32 6, i32 3, i32 0, i32 1, i32 2, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_37102735(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_37102735:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,5,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,4]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,4,5,6]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_37102735:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[6,7,14,15,2,3,0,1,4,5,14,15,6,7,10,11]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_37102735:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[6,7,14,15,2,3,0,1,4,5,14,15,6,7,10,11]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_37102735:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[6,7,14,15,2,3,0,1,4,5,14,15,6,7,10,11]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 3, i32 7, i32 1, i32 0, i32 2, i32 7, i32 3, i32 5>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_08192a3b(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_08192a3b:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_08192a3b:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 8, i32 1, i32 9, i32 2, i32 10, i32 3, i32 11>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0c1d2e3f(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_0c1d2e3f:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0c1d2e3f:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 12, i32 1, i32 13, i32 2, i32 14, i32 3, i32 15>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_4c5d6e7f(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_4c5d6e7f:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhwd {{.*#+}} xmm0 = xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_4c5d6e7f:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 12, i32 5, i32 13, i32 6, i32 14, i32 7, i32 15>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_48596a7b(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_48596a7b:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_48596a7b:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 8, i32 5, i32 9, i32 6, i32 10, i32 7, i32 11>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_08196e7f(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_08196e7f:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[0,3,2,3]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,2,3]
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_08196e7f:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[0,3,2,3]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,3,2,3]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 8, i32 1, i32 9, i32 6, i32 14, i32 7, i32 15>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0c1d6879(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_0c1d6879:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,0,2,3]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,3,2,3]
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0c1d6879:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,0,2,3]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,3,2,3]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 12, i32 1, i32 13, i32 6, i32 8, i32 7, i32 9>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_109832ba(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_109832ba:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE-NEXT:    pshuflw {{.*#+}} xmm1 = xmm0[2,0,3,1,4,5,6,7]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,0,3,1,4,5,6,7]
; SSE-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_109832ba:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm1 = xmm0[2,0,3,1,4,5,6,7]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[2,0,3,1,4,5,6,7]
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm1[0],xmm0[0]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 1, i32 0, i32 9, i32 8, i32 3, i32 2, i32 11, i32 10>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_8091a2b3(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_8091a2b3:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_8091a2b3:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 8, i32 0, i32 9, i32 1, i32 10, i32 2, i32 11, i32 3>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_c4d5e6f7(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_c4d5e6f7:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_c4d5e6f7:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 12, i32 4, i32 13, i32 5, i32 14, i32 6, i32 15, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0213cedf(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_0213cedf:
; SSE:       # BB#0:
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,1,3,4,5,6,7]
; SSE-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,2,3]
; SSE-NEXT:    pshuflw {{.*#+}} xmm1 = xmm1[0,2,1,3,4,5,6,7]
; SSE-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0213cedf:
; AVX:       # BB#0:
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[0,2,1,3,4,5,6,7]
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,2,3]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm1 = xmm1[0,2,1,3,4,5,6,7]
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 2, i32 1, i32 3, i32 12, i32 14, i32 13, i32 15>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_443aXXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_443aXXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,2,3,4,5,6,7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_443aXXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; SSSE3-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,2,3,4,5,6,7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,12,13,10,11,12,13,10,11,12,13,14,15]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_443aXXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; SSE41-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,2,3,4,5,6,7]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,12,13,10,11,12,13,10,11,12,13,14,15]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_443aXXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[0,0,2,3,4,5,6,7]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,12,13,10,11,12,13,10,11,12,13,14,15]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 4, i32 4, i32 3, i32 10, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_032dXXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_032dXXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,1,4,5,6,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_032dXXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,12,13,8,9,6,7,8,9,12,13,12,13,14,15]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_032dXXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,12,13,8,9,6,7,8,9,12,13,12,13,14,15]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_032dXXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,12,13,8,9,6,7,8,9,12,13,12,13,14,15]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 3, i32 2, i32 13, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}
define <8 x i16> @shuffle_v8i16_XXXdXXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_XXXdXXXX:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[2,2,3,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_XXXdXXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm1[2,2,3,3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 13, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_012dXXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_012dXXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,2,0,3,4,5,6,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_012dXXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_012dXXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_012dXXXX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 1, i32 2, i32 13, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_XXXXcde3(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_XXXXcde3:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE2-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,7,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,2]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_XXXXcde3:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSSE3-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,4,5,6,7,0,1,4,5,8,9,14,15]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_XXXXcde3:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE41-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE41-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,4,5,6,7,0,1,4,5,8,9,14,15]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX1-LABEL: shuffle_v8i16_XXXXcde3:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; AVX1-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,4,5,6,7,0,1,4,5,8,9,14,15]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: shuffle_v8i16_XXXXcde3:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; AVX2-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,4,5,6,7,0,1,4,5,8,9,14,15]
; AVX2-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 undef, i32 12, i32 13, i32 14, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_cde3XXXX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_cde3XXXX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE2-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,7,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_cde3XXXX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSSE3-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_cde3XXXX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE41-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE41-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX1-LABEL: shuffle_v8i16_cde3XXXX:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; AVX1-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: shuffle_v8i16_cde3XXXX:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    vpunpckhwd {{.*#+}} xmm0 = xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; AVX2-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; AVX2-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 12, i32 13, i32 14, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_012dcde3(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_012dcde3:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[0,1,0,1]
; SSE2-NEXT:    pshufd {{.*#+}} xmm3 = xmm1[2,3,0,1]
; SSE2-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm1 = xmm1[0,2,2,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm1 = xmm1[0,1,2,3,4,7,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[0,2,2,3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,2,0,3,4,5,6,7]
; SSE2-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_012dcde3:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[0,1,0,1]
; SSSE3-NEXT:    pshufd {{.*#+}} xmm3 = xmm1[2,3,0,1]
; SSSE3-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; SSSE3-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_012dcde3:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[0,1,0,1]
; SSE41-NEXT:    pshufd {{.*#+}} xmm3 = xmm1[2,3,0,1]
; SSE41-NEXT:    punpckhwd {{.*#+}} xmm1 = xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE41-NEXT:    pshufb {{.*#+}} xmm1 = xmm1[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; SSE41-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: shuffle_v8i16_012dcde3:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm2 = xmm0[0,1,0,1]
; AVX1-NEXT:    vpunpckhwd {{.*#+}} xmm2 = xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; AVX1-NEXT:    vpshufb {{.*#+}} xmm2 = xmm2[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; AVX1-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; AVX1-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX1-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; AVX1-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm2[0]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: shuffle_v8i16_012dcde3:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm2
; AVX2-NEXT:    vpunpckhwd {{.*#+}} xmm2 = xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; AVX2-NEXT:    vpshufb {{.*#+}} xmm2 = xmm2[0,1,4,5,8,9,14,15,8,9,14,15,12,13,14,15]
; AVX2-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[2,3,0,1]
; AVX2-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX2-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,4,5,8,9,6,7,8,9,0,1,0,1,2,3]
; AVX2-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm2[0]
; AVX2-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 0, i32 1, i32 2, i32 13, i32 12, i32 13, i32 14, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_XXX1X579(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_XXX1X579:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,1,3,2,4,5,6,7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,2,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,1,2,2,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,4,5,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_XXX1X579:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,10,11,14,15,14,15,10,11,12,13,14,15]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,4,5,8,9,8,9,12,13,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_XXX1X579:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,10,11,14,15,14,15,10,11,12,13,14,15]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,4,5,8,9,8,9,12,13,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_XXX1X579:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,10,11,14,15,14,15,10,11,12,13,14,15]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,4,5,8,9,8,9,12,13,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 1, i32 undef, i32 5, i32 7, i32 9>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_XX4X8acX(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_XX4X8acX:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm1 = xmm1[0,2,2,3,4,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[0,2,2,3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,1,2,0,4,5,6,7]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,2,1]
; SSE2-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,1,1,3,4,5,6,7]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,4,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_XX4X8acX:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[2,3,0,1]
; SSSE3-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSSE3-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,2,3,0,1,0,1,4,5,8,9,0,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_XX4X8acX:
; SSE41:       # BB#0:
; SSE41-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[2,3,0,1]
; SSE41-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; SSE41-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; SSE41-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSE41-NEXT:    pshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,2,3,0,1,0,1,4,5,8,9,0,1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_XX4X8acX:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; AVX-NEXT:    vpshuflw {{.*#+}} xmm1 = xmm1[0,2,2,3,4,5,6,7]
; AVX-NEXT:    vpshufd {{.*#+}} xmm1 = xmm1[0,2,2,3]
; AVX-NEXT:    vpunpcklwd {{.*#+}} xmm0 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; AVX-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,1,2,3,2,3,0,1,0,1,4,5,8,9,0,1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 4, i32 undef, i32 8, i32 10, i32 12, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_8zzzzzzz(i16 %i) {
; SSE-LABEL: shuffle_v8i16_8zzzzzzz:
; SSE:       # BB#0:
; SSE-NEXT:    movzwl %di, %eax
; SSE-NEXT:    movd %eax, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_8zzzzzzz:
; AVX:       # BB#0:
; AVX-NEXT:    movzwl %di, %eax
; AVX-NEXT:    vmovd %eax, %xmm0
; AVX-NEXT:    retq
  %a = insertelement <8 x i16> undef, i16 %i, i32 0
  %shuffle = shufflevector <8 x i16> zeroinitializer, <8 x i16> %a, <8 x i32> <i32 8, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_z8zzzzzz(i16 %i) {
; SSE-LABEL: shuffle_v8i16_z8zzzzzz:
; SSE:       # BB#0:
; SSE-NEXT:    movzwl %di, %eax
; SSE-NEXT:    movd %eax, %xmm0
; SSE-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9,10,11,12,13]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_z8zzzzzz:
; AVX:       # BB#0:
; AVX-NEXT:    movzwl %di, %eax
; AVX-NEXT:    vmovd %eax, %xmm0
; AVX-NEXT:    vpslldq {{.*#+}} xmm0 = zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9,10,11,12,13]
; AVX-NEXT:    retq
  %a = insertelement <8 x i16> undef, i16 %i, i32 0
  %shuffle = shufflevector <8 x i16> zeroinitializer, <8 x i16> %a, <8 x i32> <i32 2, i32 8, i32 3, i32 7, i32 6, i32 5, i32 4, i32 3>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_zzzzz8zz(i16 %i) {
; SSE-LABEL: shuffle_v8i16_zzzzz8zz:
; SSE:       # BB#0:
; SSE-NEXT:    movzwl %di, %eax
; SSE-NEXT:    movd %eax, %xmm0
; SSE-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_zzzzz8zz:
; AVX:       # BB#0:
; AVX-NEXT:    movzwl %di, %eax
; AVX-NEXT:    vmovd %eax, %xmm0
; AVX-NEXT:    vpslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; AVX-NEXT:    retq
  %a = insertelement <8 x i16> undef, i16 %i, i32 0
  %shuffle = shufflevector <8 x i16> zeroinitializer, <8 x i16> %a, <8 x i32> <i32 0, i32 0, i32 0, i32 0, i32 0, i32 8, i32 0, i32 0>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_zuuzuuz8(i16 %i) {
; SSE-LABEL: shuffle_v8i16_zuuzuuz8:
; SSE:       # BB#0:
; SSE-NEXT:    movd %edi, %xmm0
; SSE-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_zuuzuuz8:
; AVX:       # BB#0:
; AVX-NEXT:    vmovd %edi, %xmm0
; AVX-NEXT:    vpslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1]
; AVX-NEXT:    retq
  %a = insertelement <8 x i16> undef, i16 %i, i32 0
  %shuffle = shufflevector <8 x i16> zeroinitializer, <8 x i16> %a, <8 x i32> <i32 0, i32 undef, i32 undef, i32 3, i32 undef, i32 undef, i32 6, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_zzBzzzzz(i16 %i) {
; SSE-LABEL: shuffle_v8i16_zzBzzzzz:
; SSE:       # BB#0:
; SSE-NEXT:    movzwl %di, %eax
; SSE-NEXT:    movd %eax, %xmm0
; SSE-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9,10,11]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_zzBzzzzz:
; AVX:       # BB#0:
; AVX-NEXT:    movzwl %di, %eax
; AVX-NEXT:    vmovd %eax, %xmm0
; AVX-NEXT:    vpslldq {{.*#+}} xmm0 = zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9,10,11]
; AVX-NEXT:    retq
  %a = insertelement <8 x i16> undef, i16 %i, i32 3
  %shuffle = shufflevector <8 x i16> zeroinitializer, <8 x i16> %a, <8 x i32> <i32 0, i32 1, i32 11, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_def01234(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_def01234:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_def01234:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_def01234:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_def01234:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 13, i32 14, i32 15, i32 0, i32 1, i32 2, i32 3, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_ueuu123u(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_ueuu123u:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_ueuu123u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_ueuu123u:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_ueuu123u:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm1[10,11,12,13,14,15],xmm0[0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 14, i32 undef, i32 undef, i32 1, i32 2, i32 3, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_56701234(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_56701234:
; SSE2:       # BB#0:
; SSE2-NEXT:    movdqa %xmm0, %xmm1
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_56701234:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_56701234:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_56701234:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 5, i32 6, i32 7, i32 0, i32 1, i32 2, i32 3, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_u6uu123u(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_u6uu123u:
; SSE2:       # BB#0:
; SSE2-NEXT:    movdqa %xmm0, %xmm1
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_u6uu123u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_u6uu123u:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u6uu123u:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 6, i32 undef, i32 undef, i32 1, i32 2, i32 3, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_uuuu123u(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_uuuu123u:
; SSE:       # BB#0:
; SSE-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_uuuu123u:
; AVX:       # BB#0:
; AVX-NEXT:    vpslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 undef, i32 1, i32 2, i32 3, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_bcdef012(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_bcdef012:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_bcdef012:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_bcdef012:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_bcdef012:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 11, i32 12, i32 13, i32 14, i32 15, i32 0, i32 1, i32 2>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_ucdeuu1u(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_ucdeuu1u:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_ucdeuu1u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_ucdeuu1u:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_ucdeuu1u:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm1[6,7,8,9,10,11,12,13,14,15],xmm0[0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 12, i32 13, i32 14, i32 undef, i32 undef, i32 1, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_34567012(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_34567012:
; SSE2:       # BB#0:
; SSE2-NEXT:    movdqa %xmm0, %xmm1
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_34567012:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_34567012:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_34567012:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 3, i32 4, i32 5, i32 6, i32 7, i32 0, i32 1, i32 2>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_u456uu1u(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_u456uu1u:
; SSE2:       # BB#0:
; SSE2-NEXT:    movdqa %xmm0, %xmm1
; SSE2-NEXT:    psrldq {{.*#+}} xmm1 = xmm1[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm0 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm0[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_u456uu1u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_u456uu1u:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u456uu1u:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 4, i32 5, i32 6, i32 undef, i32 undef, i32 1, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_u456uuuu(<8 x i16> %a, <8 x i16> %b) {
; SSE-LABEL: shuffle_v8i16_u456uuuu:
; SSE:       # BB#0:
; SSE-NEXT:    psrldq {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u456uuuu:
; AVX:       # BB#0:
; AVX-NEXT:    vpsrldq {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 4, i32 5, i32 6, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_3456789a(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_3456789a:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm1 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm1[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_3456789a:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm1 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_3456789a:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm1 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_3456789a:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_u456uu9u(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_u456uu9u:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm1 = zero,zero,zero,zero,zero,zero,zero,zero,zero,zero,xmm1[0,1,2,3,4,5]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_u456uu9u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm1 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_u456uu9u:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm1 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u456uu9u:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[6,7,8,9,10,11,12,13,14,15],xmm1[0,1,2,3,4,5]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 4, i32 5, i32 6, i32 undef, i32 undef, i32 9, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_56789abc(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_56789abc:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm1 = zero,zero,zero,zero,zero,zero,xmm1[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_56789abc:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm1 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_56789abc:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm1 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_56789abc:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_u6uu9abu(<8 x i16> %a, <8 x i16> %b) {
; SSE2-LABEL: shuffle_v8i16_u6uu9abu:
; SSE2:       # BB#0:
; SSE2-NEXT:    psrldq {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15],zero,zero,zero,zero,zero,zero,zero,zero,zero,zero
; SSE2-NEXT:    pslldq {{.*#+}} xmm1 = zero,zero,zero,zero,zero,zero,xmm1[0,1,2,3,4,5,6,7,8,9]
; SSE2-NEXT:    por %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_u6uu9abu:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    palignr {{.*#+}} xmm1 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_u6uu9abu:
; SSE41:       # BB#0:
; SSE41-NEXT:    palignr {{.*#+}} xmm1 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_u6uu9abu:
; AVX:       # BB#0:
; AVX-NEXT:    vpalignr {{.*#+}} xmm0 = xmm0[10,11,12,13,14,15],xmm1[0,1,2,3,4,5,6,7,8,9]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> %b, <8 x i32> <i32 undef, i32 6, i32 undef, i32 undef, i32 9, i32 10, i32 11, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0uuu1uuu(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0uuu1uuu:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,3]
; SSE2-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,5,6,7]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0uuu1uuu:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,3]
; SSSE3-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,5,6,7]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0uuu1uuu:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxwq {{.*#+}} xmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0uuu1uuu:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxwq {{.*#+}} xmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 1, i32 undef, i32 undef, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0zzz1zzz(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0zzz1zzz:
; SSE2:       # BB#0:
; SSE2-NEXT:    pxor %xmm1, %xmm1
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    punpckldq {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0zzz1zzz:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pxor %xmm1, %xmm1
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0zzz1zzz:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxwq {{.*#+}} xmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0zzz1zzz:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxwq {{.*#+}} xmm0 = xmm0[0],zero,zero,zero,xmm0[1],zero,zero,zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 9, i32 10, i32 11, i32 1, i32 13, i32 14, i32 15>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0u1u2u3u(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0u1u2u3u:
; SSE2:       # BB#0:
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0u1u2u3u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0u1u2u3u:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxwd {{.*#+}} xmm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0u1u2u3u:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxwd {{.*#+}} xmm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 undef, i32 1, i32 undef, i32 2, i32 undef, i32 3, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0z1z2z3z(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0z1z2z3z:
; SSE2:       # BB#0:
; SSE2-NEXT:    pxor %xmm1, %xmm1
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0z1z2z3z:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pxor %xmm1, %xmm1
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0z1z2z3z:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxwd {{.*#+}} xmm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0z1z2z3z:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxwd {{.*#+}} xmm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 9, i32 1, i32 11, i32 2, i32 13, i32 3, i32 15>
  ret <8 x i16> %shuffle
}

;
; Shuffle to logical bit shifts
;
define <8 x i16> @shuffle_v8i16_z0z2z4z6(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_z0z2z4z6:
; SSE:       # BB#0:
; SSE-NEXT:    pslld $16, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_z0z2z4z6:
; AVX:       # BB#0:
; AVX-NEXT:    vpslld $16, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 8, i32 0, i32 8, i32 2, i32 8, i32 4, i32 8, i32 6>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_zzz0zzz4(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_zzz0zzz4:
; SSE:       # BB#0:
; SSE-NEXT:    psllq $48, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_zzz0zzz4:
; AVX:       # BB#0:
; AVX-NEXT:    vpsllq $48, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 8, i32 8, i32 8, i32 0, i32 8, i32 8, i32 8, i32 4>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_zz01zX4X(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_zz01zX4X:
; SSE:       # BB#0:
; SSE-NEXT:    psllq $32, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_zz01zX4X:
; AVX:       # BB#0:
; AVX-NEXT:    vpsllq $32, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 8, i32 8, i32 0, i32 1, i32 8, i32 undef, i32 4, i32 undef>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_z0X2z456(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_z0X2z456:
; SSE:       # BB#0:
; SSE-NEXT:    psllq $16, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_z0X2z456:
; AVX:       # BB#0:
; AVX-NEXT:    vpsllq $16, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 8, i32 0, i32 undef, i32 2, i32 8, i32 4, i32 5, i32 6>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_1z3zXz7z(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_1z3zXz7z:
; SSE:       # BB#0:
; SSE-NEXT:    psrld $16, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_1z3zXz7z:
; AVX:       # BB#0:
; AVX-NEXT:    vpsrld $16, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 1, i32 8, i32 3, i32 8, i32 undef, i32 8, i32 7, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_1X3z567z(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_1X3z567z:
; SSE:       # BB#0:
; SSE-NEXT:    psrlq $16, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_1X3z567z:
; AVX:       # BB#0:
; AVX-NEXT:    vpsrlq $16, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 1, i32 undef, i32 3, i32 8, i32 5, i32 6, i32 7, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_23zz67zz(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_23zz67zz:
; SSE:       # BB#0:
; SSE-NEXT:    psrlq $32, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_23zz67zz:
; AVX:       # BB#0:
; AVX-NEXT:    vpsrlq $32, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 2, i32 3, i32 8, i32 8, i32 6, i32 7, i32 8, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_3zXXXzzz(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_3zXXXzzz:
; SSE:       # BB#0:
; SSE-NEXT:    psrlq $48, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_3zXXXzzz:
; AVX:       # BB#0:
; AVX-NEXT:    vpsrlq $48, %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32><i32 3, i32 8, i32 undef, i32 undef, i32 undef, i32 8, i32 8, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_01u3zzuz(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_01u3zzuz:
; SSE:       # BB#0:
; SSE-NEXT:    movq {{.*#+}} xmm0 = xmm0[0],zero
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_01u3zzuz:
; AVX:       # BB#0:
; AVX-NEXT:    vmovq {{.*#+}} xmm0 = xmm0[0],zero
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 1, i32 undef, i32 3, i32 8, i32 8, i32 undef, i32 8>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0z234567(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0z234567:
; SSE2:       # BB#0:
; SSE2-NEXT:    andps {{.*}}(%rip), %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0z234567:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    andps {{.*}}(%rip), %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0z234567:
; SSE41:       # BB#0:
; SSE41-NEXT:    pxor %xmm1, %xmm1
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2,3,4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0z234567:
; AVX:       # BB#0:
; AVX-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2,3,4,5,6,7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 9, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0zzzz5z7(<8 x i16> %a) {
; SSE-LABEL: shuffle_v8i16_0zzzz5z7:
; SSE:       # BB#0:
; SSE-NEXT:    andps {{.*}}(%rip), %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0zzzz5z7:
; AVX:       # BB#0:
; AVX-NEXT:    vandps {{.*}}(%rip), %xmm0, %xmm0
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 8, i32 8, i32 8, i32 8, i32 5, i32 8, i32 7>
  ret <8 x i16> %shuffle
}

define <8 x i16> @shuffle_v8i16_0123456z(<8 x i16> %a) {
; SSE2-LABEL: shuffle_v8i16_0123456z:
; SSE2:       # BB#0:
; SSE2-NEXT:    andps {{.*}}(%rip), %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: shuffle_v8i16_0123456z:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    andps {{.*}}(%rip), %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: shuffle_v8i16_0123456z:
; SSE41:       # BB#0:
; SSE41-NEXT:    pxor %xmm1, %xmm1
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2,3,4,5,6],xmm1[7]
; SSE41-NEXT:    retq
;
; AVX-LABEL: shuffle_v8i16_0123456z:
; AVX:       # BB#0:
; AVX-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2,3,4,5,6],xmm1[7]
; AVX-NEXT:    retq
  %shuffle = shufflevector <8 x i16> %a, <8 x i16> zeroinitializer, <8 x i32> <i32 0, i32 9, i32 2, i32 3, i32 4, i32 5, i32 6, i32 15>
  ret <8 x i16> %shuffle
}
