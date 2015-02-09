; RUN: llc < %s -mcpu=x86-64 -mattr=+sse2 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE2
; RUN: llc < %s -mcpu=x86-64 -mattr=+ssse3 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSSE3
; RUN: llc < %s -mcpu=x86-64 -mattr=+sse4.1 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE41
; RUN: llc < %s -mcpu=x86-64 -mattr=+avx | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX1
; RUN: llc < %s -mcpu=x86-64 -mattr=+avx2 | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX2
;
; Verify that the DAG combiner correctly folds bitwise operations across
; shuffles, nested shuffles with undef, pairs of nested shuffles, and other
; basic and always-safe patterns. Also test that the DAG combiner will combine
; target-specific shuffle instructions where reasonable.

target triple = "x86_64-unknown-unknown"

declare <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32>, i8)
declare <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16>, i8)
declare <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16>, i8)

define <4 x i32> @combine_pshufd1(<4 x i32> %a) {
; ALL-LABEL: combine_pshufd1:
; ALL:       # BB#0: # %entry
; ALL-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 27)
  %c = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %b, i8 27)
  ret <4 x i32> %c
}

define <4 x i32> @combine_pshufd2(<4 x i32> %a) {
; ALL-LABEL: combine_pshufd2:
; ALL:       # BB#0: # %entry
; ALL-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 27)
  %b.cast = bitcast <4 x i32> %b to <8 x i16>
  %c = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %b.cast, i8 -28)
  %c.cast = bitcast <8 x i16> %c to <4 x i32>
  %d = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %c.cast, i8 27)
  ret <4 x i32> %d
}

define <4 x i32> @combine_pshufd3(<4 x i32> %a) {
; ALL-LABEL: combine_pshufd3:
; ALL:       # BB#0: # %entry
; ALL-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 27)
  %b.cast = bitcast <4 x i32> %b to <8 x i16>
  %c = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %b.cast, i8 -28)
  %c.cast = bitcast <8 x i16> %c to <4 x i32>
  %d = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %c.cast, i8 27)
  ret <4 x i32> %d
}

define <4 x i32> @combine_pshufd4(<4 x i32> %a) {
; SSE-LABEL: combine_pshufd4:
; SSE:       # BB#0: # %entry
; SSE-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_pshufd4:
; AVX:       # BB#0: # %entry
; AVX-NEXT:    vpshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; AVX-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 -31)
  %b.cast = bitcast <4 x i32> %b to <8 x i16>
  %c = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %b.cast, i8 27)
  %c.cast = bitcast <8 x i16> %c to <4 x i32>
  %d = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %c.cast, i8 -31)
  ret <4 x i32> %d
}

define <4 x i32> @combine_pshufd5(<4 x i32> %a) {
; SSE-LABEL: combine_pshufd5:
; SSE:       # BB#0: # %entry
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_pshufd5:
; AVX:       # BB#0: # %entry
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; AVX-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 -76)
  %b.cast = bitcast <4 x i32> %b to <8 x i16>
  %c = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %b.cast, i8 27)
  %c.cast = bitcast <8 x i16> %c to <4 x i32>
  %d = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %c.cast, i8 -76)
  ret <4 x i32> %d
}

define <4 x i32> @combine_pshufd6(<4 x i32> %a) {
; SSE-LABEL: combine_pshufd6:
; SSE:       # BB#0: # %entry
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,0,0,0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_pshufd6:
; AVX:       # BB#0: # %entry
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,0,0,0]
; AVX-NEXT:    retq
entry:
  %b = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %a, i8 0)
  %c = call <4 x i32> @llvm.x86.sse2.pshuf.d(<4 x i32> %b, i8 8)
  ret <4 x i32> %c
}

define <8 x i16> @combine_pshuflw1(<8 x i16> %a) {
; ALL-LABEL: combine_pshuflw1:
; ALL:       # BB#0: # %entry
; ALL-NEXT:    retq
entry:
  %b = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %a, i8 27)
  %c = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %b, i8 27)
  ret <8 x i16> %c
}

define <8 x i16> @combine_pshuflw2(<8 x i16> %a) {
; ALL-LABEL: combine_pshuflw2:
; ALL:       # BB#0: # %entry
; ALL-NEXT:    retq
entry:
  %b = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %a, i8 27)
  %c = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %b, i8 -28)
  %d = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %c, i8 27)
  ret <8 x i16> %d
}

define <8 x i16> @combine_pshuflw3(<8 x i16> %a) {
; SSE-LABEL: combine_pshuflw3:
; SSE:       # BB#0: # %entry
; SSE-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_pshuflw3:
; AVX:       # BB#0: # %entry
; AVX-NEXT:    vpshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; AVX-NEXT:    retq
entry:
  %b = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %a, i8 27)
  %c = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %b, i8 27)
  %d = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %c, i8 27)
  ret <8 x i16> %d
}

define <8 x i16> @combine_pshufhw1(<8 x i16> %a) {
; SSE-LABEL: combine_pshufhw1:
; SSE:       # BB#0: # %entry
; SSE-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_pshufhw1:
; AVX:       # BB#0: # %entry
; AVX-NEXT:    vpshuflw {{.*#+}} xmm0 = xmm0[3,2,1,0,4,5,6,7]
; AVX-NEXT:    retq
entry:
  %b = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %a, i8 27)
  %c = call <8 x i16> @llvm.x86.sse2.pshufl.w(<8 x i16> %b, i8 27)
  %d = call <8 x i16> @llvm.x86.sse2.pshufh.w(<8 x i16> %c, i8 27)
  ret <8 x i16> %d
}

define <4 x i32> @combine_bitwise_ops_test1(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test1:
; SSE:       # BB#0:
; SSE-NEXT:    pand %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test1:
; AVX:       # BB#0:
; AVX-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test2(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test2:
; SSE:       # BB#0:
; SSE-NEXT:    por %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test2:
; AVX:       # BB#0:
; AVX-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test3(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test3:
; SSE:       # BB#0:
; SSE-NEXT:    pxor %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test3:
; AVX:       # BB#0:
; AVX-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 1, i32 3>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}

define <4 x i32> @combine_bitwise_ops_test4(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test4:
; SSE:       # BB#0:
; SSE-NEXT:    pand %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test4:
; AVX:       # BB#0:
; AVX-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test5(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test5:
; SSE:       # BB#0:
; SSE-NEXT:    por %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test5:
; AVX:       # BB#0:
; AVX-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test6(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test6:
; SSE:       # BB#0:
; SSE-NEXT:    pxor %xmm1, %xmm0
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test6:
; AVX:       # BB#0:
; AVX-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 4, i32 6, i32 5, i32 7>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}


; Verify that DAGCombiner moves the shuffle after the xor/and/or even if shuffles
; are not performing a swizzle operations.

define <4 x i32> @combine_bitwise_ops_test1b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test1b:
; SSE2:       # BB#0:
; SSE2-NEXT:    andps %xmm1, %xmm0
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test1b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    andps %xmm1, %xmm0
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test1b:
; SSE41:       # BB#0:
; SSE41-NEXT:    pand %xmm1, %xmm0
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,3],xmm0[4,5],xmm2[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test1b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,3],xmm0[4,5],xmm2[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test1b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm2[1],xmm0[2],xmm2[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test2b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test2b:
; SSE2:       # BB#0:
; SSE2-NEXT:    orps %xmm1, %xmm0
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test2b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    orps %xmm1, %xmm0
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test2b:
; SSE41:       # BB#0:
; SSE41-NEXT:    por %xmm1, %xmm0
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,3],xmm0[4,5],xmm2[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test2b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,3],xmm0[4,5],xmm2[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test2b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm2[1],xmm0[2],xmm2[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test3b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test3b:
; SSE2:       # BB#0:
; SSE2-NEXT:    xorps %xmm1, %xmm0
; SSE2-NEXT:    andps {{.*}}(%rip), %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test3b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    xorps %xmm1, %xmm0
; SSSE3-NEXT:    andps {{.*}}(%rip), %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test3b:
; SSE41:       # BB#0:
; SSE41-NEXT:    pxor %xmm1, %xmm0
; SSE41-NEXT:    pxor %xmm1, %xmm1
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test3b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test3b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2],xmm1[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}

define <4 x i32> @combine_bitwise_ops_test4b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test4b:
; SSE2:       # BB#0:
; SSE2-NEXT:    andps %xmm1, %xmm0
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test4b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    andps %xmm1, %xmm0
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test4b:
; SSE41:       # BB#0:
; SSE41-NEXT:    pand %xmm1, %xmm0
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm2[0,1],xmm0[2,3],xmm2[4,5],xmm0[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test4b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm2[0,1],xmm0[2,3],xmm2[4,5],xmm0[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test4b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpand %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm2[0],xmm0[1],xmm2[2],xmm0[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test5b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test5b:
; SSE2:       # BB#0:
; SSE2-NEXT:    orps %xmm1, %xmm0
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test5b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    orps %xmm1, %xmm0
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test5b:
; SSE41:       # BB#0:
; SSE41-NEXT:    por %xmm1, %xmm0
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm2[0,1],xmm0[2,3],xmm2[4,5],xmm0[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test5b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm2[0,1],xmm0[2,3],xmm2[4,5],xmm0[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test5b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpor %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm2[0],xmm0[1],xmm2[2],xmm0[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test6b(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test6b:
; SSE2:       # BB#0:
; SSE2-NEXT:    xorps %xmm1, %xmm0
; SSE2-NEXT:    andps {{.*}}(%rip), %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test6b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    xorps %xmm1, %xmm0
; SSSE3-NEXT:    andps {{.*}}(%rip), %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test6b:
; SSE41:       # BB#0:
; SSE41-NEXT:    pxor %xmm1, %xmm0
; SSE41-NEXT:    pxor %xmm1, %xmm1
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5],xmm0[6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_bitwise_ops_test6b:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX1-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5],xmm0[6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_bitwise_ops_test6b:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; AVX2-NEXT:    vpxor %xmm1, %xmm1, %xmm1
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2],xmm0[3]
; AVX2-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 5, i32 2, i32 7>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}

define <4 x i32> @combine_bitwise_ops_test1c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test1c:
; SSE:       # BB#0:
; SSE-NEXT:    andps %xmm1, %xmm0
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test1c:
; AVX:       # BB#0:
; AVX-NEXT:    vandps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test2c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test2c:
; SSE:       # BB#0:
; SSE-NEXT:    orps %xmm1, %xmm0
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test2c:
; AVX:       # BB#0:
; AVX-NEXT:    vorps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm2[1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test3c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE2-LABEL: combine_bitwise_ops_test3c:
; SSE2:       # BB#0:
; SSE2-NEXT:    xorps %xmm1, %xmm0
; SSE2-NEXT:    xorps %xmm1, %xmm1
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_bitwise_ops_test3c:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    xorps %xmm1, %xmm0
; SSSE3-NEXT:    xorps %xmm1, %xmm1
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_bitwise_ops_test3c:
; SSE41:       # BB#0:
; SSE41-NEXT:    xorps %xmm1, %xmm0
; SSE41-NEXT:    insertps {{.*#+}} xmm0 = xmm0[0,2],zero,zero
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test3c:
; AVX:       # BB#0:
; AVX-NEXT:    vxorps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vinsertps {{.*#+}} xmm0 = xmm0[0,2],zero,zero
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %a, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %b, <4 x i32> %c, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}

define <4 x i32> @combine_bitwise_ops_test4c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test4c:
; SSE:       # BB#0:
; SSE-NEXT:    andps %xmm1, %xmm0
; SSE-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE-NEXT:    movaps %xmm2, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test4c:
; AVX:       # BB#0:
; AVX-NEXT:    vandps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm2[0,2],xmm0[1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %and = and <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %and
}

define <4 x i32> @combine_bitwise_ops_test5c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test5c:
; SSE:       # BB#0:
; SSE-NEXT:    orps %xmm1, %xmm0
; SSE-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE-NEXT:    movaps %xmm2, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test5c:
; AVX:       # BB#0:
; AVX-NEXT:    vorps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm2[0,2],xmm0[1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %or = or <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %or
}

define <4 x i32> @combine_bitwise_ops_test6c(<4 x i32> %a, <4 x i32> %b, <4 x i32> %c) {
; SSE-LABEL: combine_bitwise_ops_test6c:
; SSE:       # BB#0:
; SSE-NEXT:    xorps %xmm1, %xmm0
; SSE-NEXT:    xorps %xmm1, %xmm1
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,2],xmm0[1,3]
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_bitwise_ops_test6c:
; AVX:       # BB#0:
; AVX-NEXT:    vxorps %xmm1, %xmm0, %xmm0
; AVX-NEXT:    vxorps %xmm1, %xmm1, %xmm1
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm1[0,2],xmm0[1,3]
; AVX-NEXT:    retq
  %shuf1 = shufflevector <4 x i32> %c, <4 x i32> %a, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %shuf2 = shufflevector <4 x i32> %c, <4 x i32> %b, <4 x i32><i32 0, i32 2, i32 5, i32 7>
  %xor = xor <4 x i32> %shuf1, %shuf2
  ret <4 x i32> %xor
}

define <4 x i32> @combine_nested_undef_test1(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test1:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,0,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test1:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[3,1,0,1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 4, i32 3, i32 1>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 4, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test2(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test2:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test2:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 5, i32 2, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 4, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test3(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test3:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test3:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 6, i32 2, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 4, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test4(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test4:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_nested_undef_test4:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_nested_undef_test4:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 4, i32 7, i32 1>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 4, i32 4, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test5(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test5:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test5:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 5, i32 5, i32 2, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 4, i32 4, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test6(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test6:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test6:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 6, i32 2, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 4, i32 0, i32 4>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test7(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test7:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,0,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test7:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,0,2]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 2, i32 0, i32 2>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test8(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test8:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test8:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 1, i32 4, i32 3, i32 4>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test9(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test9:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,3,2,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test9:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,3,2,2]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 1, i32 3, i32 2, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 4, i32 2>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test10(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test10:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,1,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test10:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,1,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 1, i32 1, i32 5, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 4>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test11(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test11:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[1,1,2,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test11:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,2,1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 1, i32 2, i32 5, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 0>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test12(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test12:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_nested_undef_test12:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_nested_undef_test12:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 0, i32 2, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 1, i32 4, i32 0, i32 4>
  ret <4 x i32> %2
}

; The following pair of shuffles is folded into vector %A.
define <4 x i32> @combine_nested_undef_test13(<4 x i32> %A, <4 x i32> %B) {
; ALL-LABEL: combine_nested_undef_test13:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 1, i32 4, i32 2, i32 6>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 4, i32 0, i32 2, i32 4>
  ret <4 x i32> %2
}

; The following pair of shuffles is folded into vector %B.
define <4 x i32> @combine_nested_undef_test14(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test14:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test14:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 6, i32 2, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 3, i32 4, i32 1, i32 4>
  ret <4 x i32> %2
}


; Verify that we don't optimize the following cases. We expect more than one shuffle.
;
; FIXME: Many of these already don't make sense, and the rest should stop
; making sense with th enew vector shuffle lowering. Revisit at least testing for
; it.

define <4 x i32> @combine_nested_undef_test15(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test15:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[0,0]
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[2,0],xmm0[3,1]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[2,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test15:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[0,0]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm1[2,0],xmm0[3,1]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 4, i32 3, i32 1>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test16(<4 x i32> %A, <4 x i32> %B) {
; SSE2-LABEL: combine_nested_undef_test16:
; SSE2:       # BB#0:
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_nested_undef_test16:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_nested_undef_test16:
; SSE41:       # BB#0:
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; SSE41-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_nested_undef_test16:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_nested_undef_test16:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm1[1],xmm0[2],xmm1[3]
; AVX2-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test17(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test17:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[1,0]
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,2],xmm0[3,1]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[2,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test17:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[1,0]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm1[0,2],xmm0[3,1]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 1, i32 3, i32 1>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test18(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test18:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[1,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test18:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm1[1,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 1, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test19(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test19:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0],xmm1[0,0]
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,2]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,0,0,0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test19:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,0],xmm1[0,0]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,2]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,0,0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 0, i32 4, i32 5, i32 6>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 0, i32 0, i32 0>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test20(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test20:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[3,2],xmm1[0,0]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test20:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[3,2],xmm1[0,0]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 3, i32 2, i32 4, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test21(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test21:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[1,0]
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,2],xmm0[3,1]
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[0,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test21:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[1,0]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm1[0,2],xmm0[3,1]
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 1, i32 3, i32 1>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}


; Test that we correctly combine shuffles according to rule
;  shuffle(shuffle(x, y), undef) -> shuffle(y, undef)

define <4 x i32> @combine_nested_undef_test22(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test22:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[1,1,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test22:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm1[1,1,1,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 1, i32 1, i32 1, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test23(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test23:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[0,1,0,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test23:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm1[0,1,0,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test24(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test24:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[0,3,2,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test24:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm1[0,3,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %A, <4 x i32> %B, <4 x i32> <i32 4, i32 1, i32 6, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 0, i32 3, i32 2, i32 4>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test25(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test25:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_nested_undef_test25:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_nested_undef_test25:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %B, <4 x i32> %A, <4 x i32> <i32 1, i32 5, i32 2, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 3, i32 1, i32 3, i32 1>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test26(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test26:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test26:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %B, <4 x i32> %A, <4 x i32> <i32 1, i32 2, i32 6, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 3, i32 2, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test27(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test27:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_nested_undef_test27:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,1]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_nested_undef_test27:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpbroadcastq %xmm0, %xmm0
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %B, <4 x i32> %A, <4 x i32> <i32 2, i32 1, i32 5, i32 4>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 3, i32 2, i32 3, i32 2>
  ret <4 x i32> %2
}

define <4 x i32> @combine_nested_undef_test28(<4 x i32> %A, <4 x i32> %B) {
; SSE-LABEL: combine_nested_undef_test28:
; SSE:       # BB#0:
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,1,1,0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_nested_undef_test28:
; AVX:       # BB#0:
; AVX-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,1,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %B, <4 x i32> %A, <4 x i32> <i32 1, i32 2, i32 4, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> undef, <4 x i32> <i32 2, i32 3, i32 3, i32 2>
  ret <4 x i32> %2
}

define <4 x float> @combine_test1(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test1:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test1:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x float> %2
}

define <4 x float> @combine_test2(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_test2:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test2:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test2:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test2:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 6, i32 3>
  ret <4 x float> %2
}

define <4 x float> @combine_test3(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test3:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test3:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 2, i32 4, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_test4(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test4:
; SSE:       # BB#0:
; SSE-NEXT:    unpckhpd {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movapd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test4:
; AVX:       # BB#0:
; AVX-NEXT:    vunpckhpd {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_test5(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_test5:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm1, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm1 = xmm1[3,0],xmm2[2,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm1[2,0]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test5:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm1, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm1 = xmm1[3,0],xmm2[2,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm1[2,0]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test5:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test5:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x float> %2
}

define <4 x i32> @combine_test6(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test6:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test6:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test7(<4 x i32> %a, <4 x i32> %b) {
; SSE2-LABEL: combine_test7:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test7:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test7:
; SSE41:       # BB#0:
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3,4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test7:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3,4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test7:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 0, i32 1, i32 6, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test8(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test8:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test8:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 0, i32 2, i32 4, i32 1>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test9(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test9:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test9:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test10(<4 x i32> %a, <4 x i32> %b) {
; SSE2-LABEL: combine_test10:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm1, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm1 = xmm1[3,0],xmm2[2,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm1[2,0]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test10:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm1, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm1 = xmm1[3,0],xmm2[2,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm1[2,0]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test10:
; SSE41:       # BB#0:
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test10:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test10:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x i32> %2
}

define <4 x float> @combine_test11(<4 x float> %a, <4 x float> %b) {
; ALL-LABEL: combine_test11:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  ret <4 x float> %2
}

define <4 x float> @combine_test12(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_test12:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test12:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test12:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test12:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 4, i32 1, i32 2, i32 3>
  ret <4 x float> %2
}

define <4 x float> @combine_test13(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test13:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test13:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 4, i32 5, i32 2, i32 3>
  ret <4 x float> %2
}

define <4 x float> @combine_test14(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test14:
; SSE:       # BB#0:
; SSE-NEXT:    unpckhpd {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test14:
; AVX:       # BB#0:
; AVX-NEXT:    vunpckhpd {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 6, i32 7, i32 5, i32 5>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_test15(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_test15:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm0, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,0],xmm1[0,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[2,0],xmm1[2,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test15:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm0, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,0],xmm1[0,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[2,0],xmm1[2,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test15:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test15:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 5, i32 2, i32 3>
  ret <4 x float> %2
}

define <4 x i32> @combine_test16(<4 x i32> %a, <4 x i32> %b) {
; ALL-LABEL: combine_test16:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %a, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test17(<4 x i32> %a, <4 x i32> %b) {
; SSE2-LABEL: combine_test17:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test17:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test17:
; SSE41:       # BB#0:
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3,4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test17:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3,4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test17:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %a, <4 x i32> <i32 4, i32 1, i32 2, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test18(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test18:
; SSE:       # BB#0:
; SSE-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test18:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %a, <4 x i32> <i32 4, i32 5, i32 2, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test19(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test19:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test19:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 6, i32 7, i32 5, i32 5>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %a, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test20(<4 x i32> %a, <4 x i32> %b) {
; SSE2-LABEL: combine_test20:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm0, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,0],xmm1[0,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[2,0],xmm1[2,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test20:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm0, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,0],xmm1[0,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[2,0],xmm1[2,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test20:
; SSE41:       # BB#0:
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test20:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test20:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; AVX2-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 7>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %a, <4 x i32> <i32 0, i32 5, i32 2, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test21(<8 x i32> %a, <4 x i32>* %ptr) {
; SSE-LABEL: combine_test21:
; SSE:       # BB#0:
; SSE-NEXT:    movdqa %xmm0, %xmm2
; SSE-NEXT:    punpcklqdq {{.*#+}} xmm2 = xmm2[0],xmm1[0]
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSE-NEXT:    movdqa %xmm2, (%rdi)
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_test21:
; AVX1:       # BB#0:
; AVX1-NEXT:    vextractf128 $1, %ymm0, %xmm1
; AVX1-NEXT:    vpunpcklqdq {{.*#+}} xmm2 = xmm0[0],xmm1[0]
; AVX1-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; AVX1-NEXT:    vmovdqa %xmm2, (%rdi)
; AVX1-NEXT:    vzeroupper
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test21:
; AVX2:       # BB#0:
; AVX2-NEXT:    vextracti128 $1, %ymm0, %xmm1
; AVX2-NEXT:    vpunpcklqdq {{.*#+}} xmm2 = xmm0[0],xmm1[0]
; AVX2-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; AVX2-NEXT:    vmovdqa %xmm2, (%rdi)
; AVX2-NEXT:    vzeroupper
; AVX2-NEXT:    retq
  %1 = shufflevector <8 x i32> %a, <8 x i32> %a, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
  %2 = shufflevector <8 x i32> %a, <8 x i32> %a, <4 x i32> <i32 2, i32 3, i32 6, i32 7>
  store <4 x i32> %1, <4 x i32>* %ptr, align 16
  ret <4 x i32> %2
}

define <8 x float> @combine_test22(<2 x float>* %a, <2 x float>* %b) {
; SSE-LABEL: combine_test22:
; SSE:       # BB#0:
; SSE-NEXT:    movq {{.*#+}} xmm0 = mem[0],zero
; SSE-NEXT:    movhpd (%rsi), %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test22:
; AVX:       # BB#0:
; AVX-NEXT:    vmovq {{.*#+}} xmm0 = mem[0],zero
; AVX-NEXT:    vmovhpd (%rsi), %xmm0, %xmm0
; AVX-NEXT:    retq
; Current AVX2 lowering of this is still awful, not adding a test case.
  %1 = load <2 x float>* %a, align 8
  %2 = load <2 x float>* %b, align 8
  %3 = shufflevector <2 x float> %1, <2 x float> %2, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x float> %3
}

; Check some negative cases.
; FIXME: Do any of these really make sense? Are they redundant with the above tests?

define <4 x float> @combine_test1b(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test1b:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1,2,0]
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test1b:
; AVX:       # BB#0:
; AVX-NEXT:    vpermilps {{.*#+}} xmm0 = xmm1[0,1,2,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 0>
  ret <4 x float> %2
}

define <4 x float> @combine_test2b(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_test2b:
; SSE2:       # BB#0:
; SSE2-NEXT:    movlhps {{.*#+}} xmm1 = xmm1[0,0]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test2b:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movddup {{.*#+}} xmm0 = xmm1[0,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test2b:
; SSE41:       # BB#0:
; SSE41-NEXT:    movddup {{.*#+}} xmm0 = xmm1[0,0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test2b:
; AVX:       # BB#0:
; AVX-NEXT:    vmovddup {{.*#+}} xmm0 = xmm1[0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 0, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_test3b(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test3b:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm1, %xmm2
; SSE-NEXT:    shufps {{.*#+}} xmm2 = xmm2[2,0],xmm0[3,0]
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0],xmm2[0,2]
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[3,3]
; SSE-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test3b:
; AVX:       # BB#0:
; AVX-NEXT:    vshufps {{.*#+}} xmm2 = xmm1[2,0],xmm0[3,0]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,0],xmm2[0,2]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[3,3]
; AVX-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 0, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 7, i32 2, i32 7>
  ret <4 x float> %2
}

define <4 x float> @combine_test4b(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_test4b:
; SSE:       # BB#0:
; SSE-NEXT:    shufps {{.*#+}} xmm1 = xmm1[1,1,2,3]
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test4b:
; AVX:       # BB#0:
; AVX-NEXT:    vpermilps {{.*#+}} xmm0 = xmm1[1,1,2,3]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 5, i32 5, i32 2, i32 7>
  ret <4 x float> %2
}


; Verify that we correctly fold shuffles even when we use illegal vector types.

define <4 x i8> @combine_test1c(<4 x i8>* %a, <4 x i8>* %b) {
; SSE2-LABEL: combine_test1c:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE2-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSE2-NEXT:    movss {{.*#+}} xmm0 = xmm1[0],xmm0[1,2,3]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test1c:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSSE3-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSSE3-NEXT:    movss {{.*#+}} xmm0 = xmm1[0],xmm0[1,2,3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test1c:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3,4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test1c:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX1-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3,4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test1c:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX2-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX2-NEXT:    retq
  %A = load <4 x i8>* %a
  %B = load <4 x i8>* %b
  %1 = shufflevector <4 x i8> %A, <4 x i8> %B, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  %2 = shufflevector <4 x i8> %1, <4 x i8> %B, <4 x i32> <i32 0, i32 1, i32 6, i32 3>
  ret <4 x i8> %2
}

define <4 x i8> @combine_test2c(<4 x i8>* %a, <4 x i8>* %b) {
; SSE2-LABEL: combine_test2c:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSE2-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE2-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test2c:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSSE3-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSSE3-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test2c:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test2c:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %A = load <4 x i8>* %a
  %B = load <4 x i8>* %b
  %1 = shufflevector <4 x i8> %A, <4 x i8> %B, <4 x i32> <i32 0, i32 5, i32 1, i32 5>
  %2 = shufflevector <4 x i8> %1, <4 x i8> %B, <4 x i32> <i32 0, i32 2, i32 4, i32 1>
  ret <4 x i8> %2
}

define <4 x i8> @combine_test3c(<4 x i8>* %a, <4 x i8>* %b) {
; SSE2-LABEL: combine_test3c:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE2-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSE2-NEXT:    punpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test3c:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSSE3-NEXT:    movd {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0,0,1,1,2,2,3,3]
; SSSE3-NEXT:    punpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test3c:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    punpckhqdq {{.*#+}} xmm0 = xmm0[1],xmm1[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_test3c:
; AVX:       # BB#0:
; AVX-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %A = load <4 x i8>* %a
  %B = load <4 x i8>* %b
  %1 = shufflevector <4 x i8> %A, <4 x i8> %B, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x i8> %1, <4 x i8> %B, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x i8> %2
}

define <4 x i8> @combine_test4c(<4 x i8>* %a, <4 x i8>* %b) {
; SSE2-LABEL: combine_test4c:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSE2-NEXT:    movd {{.*#+}} xmm2 = mem[0],zero,zero,zero
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3]
; SSE2-NEXT:    movdqa %xmm2, %xmm0
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[3,0],xmm0[2,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_test4c:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; SSSE3-NEXT:    movd {{.*#+}} xmm2 = mem[0],zero,zero,zero
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3]
; SSSE3-NEXT:    movdqa %xmm2, %xmm0
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[3,0],xmm0[2,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,1],xmm2[2,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_test4c:
; SSE41:       # BB#0:
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; SSE41-NEXT:    pblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5,6,7]
; SSE41-NEXT:    retq
;
; AVX1-LABEL: combine_test4c:
; AVX1:       # BB#0:
; AVX1-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX1-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX1-NEXT:    vpblendw {{.*#+}} xmm0 = xmm1[0,1],xmm0[2,3],xmm1[4,5,6,7]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_test4c:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpmovzxbd {{.*#+}} xmm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX2-NEXT:    vpmovzxbd {{.*#+}} xmm1 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero
; AVX2-NEXT:    vpblendd {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2,3]
; AVX2-NEXT:    retq
  %A = load <4 x i8>* %a
  %B = load <4 x i8>* %b
  %1 = shufflevector <4 x i8> %A, <4 x i8> %B, <4 x i32> <i32 4, i32 1, i32 6, i32 3>
  %2 = shufflevector <4 x i8> %1, <4 x i8> %B, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x i8> %2
}


; The following test cases are generated from this C++ code
;
;__m128 blend_01(__m128 a, __m128 b)
;{
;  __m128 s = a;
;  s = _mm_blend_ps( s, b, 1<<0 );
;  s = _mm_blend_ps( s, b, 1<<1 );
;  return s;
;}
;
;__m128 blend_02(__m128 a, __m128 b)
;{
;  __m128 s = a;
;  s = _mm_blend_ps( s, b, 1<<0 );
;  s = _mm_blend_ps( s, b, 1<<2 );
;  return s;
;}
;
;__m128 blend_123(__m128 a, __m128 b)
;{
;  __m128 s = a;
;  s = _mm_blend_ps( s, b, 1<<1 );
;  s = _mm_blend_ps( s, b, 1<<2 );
;  s = _mm_blend_ps( s, b, 1<<3 );
;  return s;
;}

; Ideally, we should collapse the following shuffles into a single one.

define <4 x float> @combine_blend_01(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_blend_01:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_blend_01:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_blend_01:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_blend_01:
; AVX:       # BB#0:
; AVX-NEXT:    vblendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; AVX-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 undef, i32 2, i32 3>
  %shuffle6 = shufflevector <4 x float> %shuffle, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 2, i32 3>
  ret <4 x float> %shuffle6
}

define <4 x float> @combine_blend_02(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_blend_02:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm0 = xmm1[0],xmm0[1,2,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm1 = xmm1[2,0],xmm0[3,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,1],xmm1[0,2]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_blend_02:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm0 = xmm1[0],xmm0[1,2,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm1 = xmm1[2,0],xmm0[3,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,1],xmm1[0,2]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_blend_02:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2],xmm0[3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_blend_02:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm1[0],xmm0[1],xmm1[2],xmm0[3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 4, i32 1, i32 undef, i32 3>
  %shuffle6 = shufflevector <4 x float> %shuffle, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 6, i32 3>
  ret <4 x float> %shuffle6
}

define <4 x float> @combine_blend_123(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_blend_123:
; SSE2:       # BB#0:
; SSE2-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_blend_123:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_blend_123:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_blend_123:
; AVX:       # BB#0:
; AVX-NEXT:    vblendps {{.*#+}} xmm0 = xmm0[0],xmm1[1,2,3]
; AVX-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 undef, i32 undef>
  %shuffle6 = shufflevector <4 x float> %shuffle, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 6, i32 undef>
  %shuffle12 = shufflevector <4 x float> %shuffle6, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x float> %shuffle12
}

define <4 x i32> @combine_test_movhl_1(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test_movhl_1:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test_movhl_1:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 2, i32 7, i32 5, i32 3>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 6, i32 1, i32 0, i32 3>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test_movhl_2(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test_movhl_2:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test_movhl_2:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 2, i32 0, i32 3, i32 6>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 3, i32 7, i32 0, i32 2>
  ret <4 x i32> %2
}

define <4 x i32> @combine_test_movhl_3(<4 x i32> %a, <4 x i32> %b) {
; SSE-LABEL: combine_test_movhl_3:
; SSE:       # BB#0:
; SSE-NEXT:    punpckhqdq {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movdqa %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_test_movhl_3:
; AVX:       # BB#0:
; AVX-NEXT:    vpunpckhqdq {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x i32> %a, <4 x i32> %b, <4 x i32> <i32 7, i32 6, i32 3, i32 2>
  %2 = shufflevector <4 x i32> %1, <4 x i32> %b, <4 x i32> <i32 6, i32 0, i32 3, i32 2>
  ret <4 x i32> %2
}


; Verify that we fold shuffles according to rule:
;  (shuffle(shuffle A, Undef, M0), B, M1) -> (shuffle A, B, M2)

define <4 x float> @combine_undef_input_test1(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_undef_input_test1:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test1:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test1:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test1:
; AVX:       # BB#0:
; AVX-NEXT:    vblendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 4, i32 2, i32 3, i32 1>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 4, i32 5, i32 1, i32 2>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test2(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test2:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test2:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 6, i32 0, i32 1, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 1, i32 2, i32 4, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test3(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test3:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test3:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 2, i32 4, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test4(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test4:
; SSE:       # BB#0:
; SSE-NEXT:    unpckhpd {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movapd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test4:
; AVX:       # BB#0:
; AVX-NEXT:    vunpckhpd {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test5(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_undef_input_test5:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSE2-NEXT:    movapd %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test5:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSSE3-NEXT:    movapd %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test5:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendpd {{.*#+}} xmm0 = xmm0[0],xmm1[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test5:
; AVX:       # BB#0:
; AVX-NEXT:    vblendpd {{.*#+}} xmm0 = xmm0[0],xmm1[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 2, i32 6, i32 7>
  ret <4 x float> %2
}


; Verify that we fold shuffles according to rule:
;  (shuffle(shuffle A, Undef, M0), A, M1) -> (shuffle A, Undef, M2)

define <4 x float> @combine_undef_input_test6(<4 x float> %a) {
; ALL-LABEL: combine_undef_input_test6:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 4, i32 2, i32 3, i32 1>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 4, i32 5, i32 1, i32 2>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test7(<4 x float> %a) {
; SSE2-LABEL: combine_undef_input_test7:
; SSE2:       # BB#0:
; SSE2-NEXT:    movlhps {{.*#+}} xmm0 = xmm0[0,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test7:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test7:
; SSE41:       # BB#0:
; SSE41-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test7:
; AVX:       # BB#0:
; AVX-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 6, i32 0, i32 1, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 1, i32 2, i32 4, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test8(<4 x float> %a) {
; SSE2-LABEL: combine_undef_input_test8:
; SSE2:       # BB#0:
; SSE2-NEXT:    movlhps {{.*#+}} xmm0 = xmm0[0,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test8:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test8:
; SSE41:       # BB#0:
; SSE41-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test8:
; AVX:       # BB#0:
; AVX-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 2, i32 4, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test9(<4 x float> %a) {
; SSE-LABEL: combine_undef_input_test9:
; SSE:       # BB#0:
; SSE-NEXT:    movhlps {{.*#+}} xmm0 = xmm0[1,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test9:
; AVX:       # BB#0:
; AVX-NEXT:    vmovhlps {{.*#+}} xmm0 = xmm0[1,1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 6, i32 7, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test10(<4 x float> %a) {
; ALL-LABEL: combine_undef_input_test10:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 3>
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 2, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test11(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_undef_input_test11:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test11:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test11:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test11:
; AVX:       # BB#0:
; AVX-NEXT:    vblendpd {{.*#+}} xmm0 = xmm1[0],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 4, i32 2, i32 3, i32 1>
  %2 = shufflevector <4 x float> %b, <4 x float> %1, <4 x i32> <i32 0, i32 1, i32 5, i32 6>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test12(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test12:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test12:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 6, i32 0, i32 1, i32 7>
  %2 = shufflevector <4 x float> %b, <4 x float> %1, <4 x i32> <i32 5, i32 6, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test13(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test13:
; SSE:       # BB#0:
; SSE-NEXT:    unpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test13:
; AVX:       # BB#0:
; AVX-NEXT:    vunpcklpd {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x float> %b, <4 x float> %1, <4 x i32> <i32 4, i32 5, i32 0, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test14(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: combine_undef_input_test14:
; SSE:       # BB#0:
; SSE-NEXT:    unpckhpd {{.*#+}} xmm1 = xmm1[1],xmm0[1]
; SSE-NEXT:    movapd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test14:
; AVX:       # BB#0:
; AVX-NEXT:    vunpckhpd {{.*#+}} xmm0 = xmm1[1],xmm0[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x float> %b, <4 x float> %1, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test15(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_undef_input_test15:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSE2-NEXT:    movapd %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test15:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSSE3-NEXT:    movapd %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test15:
; SSE41:       # BB#0:
; SSE41-NEXT:    blendpd {{.*#+}} xmm0 = xmm0[0],xmm1[1]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test15:
; AVX:       # BB#0:
; AVX-NEXT:    vblendpd {{.*#+}} xmm0 = xmm0[0],xmm1[1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 3>
  %2 = shufflevector <4 x float> %b, <4 x float> %1, <4 x i32> <i32 4, i32 6, i32 2, i32 3>
  ret <4 x float> %2
}


; Verify that shuffles are canonicalized according to rules:
;  shuffle(B, shuffle(A, Undef)) -> shuffle(shuffle(A, Undef), B)
;
; This allows to trigger the following combine rule:
;  (shuffle(shuffle A, Undef, M0), A, M1) -> (shuffle A, Undef, M2)
;
; As a result, all the shuffle pairs in each function below should be
; combined into a single legal shuffle operation.

define <4 x float> @combine_undef_input_test16(<4 x float> %a) {
; ALL-LABEL: combine_undef_input_test16:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 4, i32 2, i32 3, i32 1>
  %2 = shufflevector <4 x float> %a, <4 x float> %1, <4 x i32> <i32 0, i32 1, i32 5, i32 3>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test17(<4 x float> %a) {
; SSE2-LABEL: combine_undef_input_test17:
; SSE2:       # BB#0:
; SSE2-NEXT:    movlhps {{.*#+}} xmm0 = xmm0[0,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test17:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test17:
; SSE41:       # BB#0:
; SSE41-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test17:
; AVX:       # BB#0:
; AVX-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 6, i32 0, i32 1, i32 7>
  %2 = shufflevector <4 x float> %a, <4 x float> %1, <4 x i32> <i32 5, i32 6, i32 0, i32 1>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test18(<4 x float> %a) {
; SSE2-LABEL: combine_undef_input_test18:
; SSE2:       # BB#0:
; SSE2-NEXT:    movlhps {{.*#+}} xmm0 = xmm0[0,0]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_undef_input_test18:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_undef_input_test18:
; SSE41:       # BB#0:
; SSE41-NEXT:    movddup {{.*#+}} xmm0 = xmm0[0,0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test18:
; AVX:       # BB#0:
; AVX-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 5, i32 1, i32 7>
  %2 = shufflevector <4 x float> %a, <4 x float> %1, <4 x i32> <i32 4, i32 6, i32 0, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test19(<4 x float> %a) {
; SSE-LABEL: combine_undef_input_test19:
; SSE:       # BB#0:
; SSE-NEXT:    movhlps {{.*#+}} xmm0 = xmm0[1,1]
; SSE-NEXT:    retq
;
; AVX-LABEL: combine_undef_input_test19:
; AVX:       # BB#0:
; AVX-NEXT:    vmovhlps {{.*#+}} xmm0 = xmm0[1,1]
; AVX-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 2, i32 3, i32 5, i32 5>
  %2 = shufflevector <4 x float> %a, <4 x float> %1, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  ret <4 x float> %2
}

define <4 x float> @combine_undef_input_test20(<4 x float> %a) {
; ALL-LABEL: combine_undef_input_test20:
; ALL:       # BB#0:
; ALL-NEXT:    retq
  %1 = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 0, i32 4, i32 1, i32 3>
  %2 = shufflevector <4 x float> %a, <4 x float> %1, <4 x i32> <i32 4, i32 6, i32 2, i32 3>
  ret <4 x float> %2
}

; These tests are designed to test the ability to combine away unnecessary
; operations feeding into a shuffle. The AVX cases are the important ones as
; they leverage operations which cannot be done naturally on the entire vector
; and thus are decomposed into multiple smaller operations.

define <8 x i32> @combine_unneeded_subvector1(<8 x i32> %a) {
; SSE-LABEL: combine_unneeded_subvector1:
; SSE:       # BB#0:
; SSE-NEXT:    paddd {{.*}}(%rip), %xmm1
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm1[3,2,1,0]
; SSE-NEXT:    movdqa %xmm0, %xmm1
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_unneeded_subvector1:
; AVX1:       # BB#0:
; AVX1-NEXT:    vextractf128 $1, %ymm0, %xmm0
; AVX1-NEXT:    vpaddd {{.*}}(%rip), %xmm0, %xmm0
; AVX1-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[3,2,1,0]
; AVX1-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_unneeded_subvector1:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpaddd {{.*}}(%rip), %ymm0, %ymm0
; AVX2-NEXT:    vmovdqa {{.*#+}} ymm1 = [7,6,5,4,7,6,5,4]
; AVX2-NEXT:    vpermd %ymm0, %ymm1, %ymm0
; AVX2-NEXT:    retq
  %b = add <8 x i32> %a, <i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8>
  %c = shufflevector <8 x i32> %b, <8 x i32> undef, <8 x i32> <i32 7, i32 6, i32 5, i32 4, i32 7, i32 6, i32 5, i32 4>
  ret <8 x i32> %c
}

define <8 x i32> @combine_unneeded_subvector2(<8 x i32> %a, <8 x i32> %b) {
; SSE-LABEL: combine_unneeded_subvector2:
; SSE:       # BB#0:
; SSE-NEXT:    paddd {{.*}}(%rip), %xmm1
; SSE-NEXT:    pshufd {{.*#+}} xmm0 = xmm3[3,2,1,0]
; SSE-NEXT:    pshufd {{.*#+}} xmm1 = xmm1[3,2,1,0]
; SSE-NEXT:    retq
;
; AVX1-LABEL: combine_unneeded_subvector2:
; AVX1:       # BB#0:
; AVX1-NEXT:    vextractf128 $1, %ymm0, %xmm0
; AVX1-NEXT:    vpaddd {{.*}}(%rip), %xmm0, %xmm0
; AVX1-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; AVX1-NEXT:    vperm2f128 {{.*#+}} ymm0 = ymm1[2,3],ymm0[2,3]
; AVX1-NEXT:    vpermilps {{.*#+}} ymm0 = ymm0[3,2,1,0,7,6,5,4]
; AVX1-NEXT:    retq
;
; AVX2-LABEL: combine_unneeded_subvector2:
; AVX2:       # BB#0:
; AVX2-NEXT:    vpaddd {{.*}}(%rip), %ymm0, %ymm0
; AVX2-NEXT:    vperm2i128 {{.*#+}} ymm0 = ymm1[2,3],ymm0[2,3]
; AVX2-NEXT:    vpshufd {{.*#+}} ymm0 = ymm0[3,2,1,0,7,6,5,4]
; AVX2-NEXT:    retq
  %c = add <8 x i32> %a, <i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8>
  %d = shufflevector <8 x i32> %b, <8 x i32> %c, <8 x i32> <i32 7, i32 6, i32 5, i32 4, i32 15, i32 14, i32 13, i32 12>
  ret <8 x i32> %d
}

define <4 x float> @combine_insertps1(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_insertps1:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm0, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[2,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,2],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_insertps1:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm0, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[2,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[1,2],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_insertps1:
; SSE41:       # BB#0:
; SSE41-NEXT:    insertps {{.*#+}} xmm0 = xmm1[2],xmm0[1,2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_insertps1:
; AVX:       # BB#0:
; AVX-NEXT:    vinsertps {{.*#+}} xmm0 = xmm1[2],xmm0[1,2,3]
; AVX-NEXT:    retq

  %c = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32><i32 0, i32 6, i32 2, i32 4>
  %d = shufflevector <4 x float> %a, <4 x float> %c, <4 x i32> <i32 5, i32 1, i32 6, i32 3>
  ret <4 x float> %d
}

define <4 x float> @combine_insertps2(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_insertps2:
; SSE2:       # BB#0:
; SSE2-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSE2-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,2],xmm0[2,3]
; SSE2-NEXT:    movaps %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_insertps2:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movsd {{.*#+}} xmm1 = xmm0[0],xmm1[1]
; SSSE3-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,2],xmm0[2,3]
; SSSE3-NEXT:    movaps %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_insertps2:
; SSE41:       # BB#0:
; SSE41-NEXT:    insertps {{.*#+}} xmm0 = xmm0[0],xmm1[2],xmm0[2,3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_insertps2:
; AVX:       # BB#0:
; AVX-NEXT:    vinsertps {{.*#+}} xmm0 = xmm0[0],xmm1[2],xmm0[2,3]
; AVX-NEXT:    retq

  %c = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32><i32 0, i32 1, i32 6, i32 7>
  %d = shufflevector <4 x float> %a, <4 x float> %c, <4 x i32> <i32 4, i32 6, i32 2, i32 3>
  ret <4 x float> %d
}

define <4 x float> @combine_insertps3(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_insertps3:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm0, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[0,1]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm0[1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    movaps %xmm2, %xmm0
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_insertps3:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm0, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[0,1]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,1],xmm0[1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    movaps %xmm2, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_insertps3:
; SSE41:       # BB#0:
; SSE41-NEXT:    insertps {{.*#+}} xmm0 = xmm0[0,1],xmm1[0],xmm0[3]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_insertps3:
; AVX:       # BB#0:
; AVX-NEXT:    vinsertps {{.*#+}} xmm0 = xmm0[0,1],xmm1[0],xmm0[3]
; AVX-NEXT:    retq

  %c = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32><i32 0, i32 4, i32 2, i32 5>
  %d = shufflevector <4 x float> %a, <4 x float> %c, <4 x i32><i32 4, i32 1, i32 5, i32 3>
  ret <4 x float> %d
}

define <4 x float> @combine_insertps4(<4 x float> %a, <4 x float> %b) {
; SSE2-LABEL: combine_insertps4:
; SSE2:       # BB#0:
; SSE2-NEXT:    movaps %xmm0, %xmm2
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[0,1]
; SSE2-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSE2-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,1]
; SSE2-NEXT:    retq
;
; SSSE3-LABEL: combine_insertps4:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movaps %xmm0, %xmm2
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2],xmm1[0,1]
; SSSE3-NEXT:    shufps {{.*#+}} xmm2 = xmm2[0,2,1,3]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0],xmm2[0,0]
; SSSE3-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm2[2,1]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: combine_insertps4:
; SSE41:       # BB#0:
; SSE41-NEXT:    insertps {{.*#+}} xmm0 = xmm0[0,1,2],xmm1[0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: combine_insertps4:
; AVX:       # BB#0:
; AVX-NEXT:    vinsertps {{.*#+}} xmm0 = xmm0[0,1,2],xmm1[0]
; AVX-NEXT:    retq

  %c = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32><i32 0, i32 4, i32 2, i32 5>
  %d = shufflevector <4 x float> %a, <4 x float> %c, <4 x i32><i32 4, i32 1, i32 6, i32 5>
  ret <4 x float> %d
}
