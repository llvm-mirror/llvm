; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+avx512bw -mattr=+avx512dq -mattr=+avx512vl| FileCheck %s

define <8 x i1> @test(<2 x i1> %a) {
; CHECK-LABEL: test:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovq2m %xmm0, %k0
; CHECK-NEXT:    kshiftlb $2, %k0, %k0
; CHECK-NEXT:    vpmovm2w %k0, %xmm0
; CHECK-NEXT:    retq
  %res = shufflevector <2 x i1> %a, <2 x i1> undef, <8 x i32> <i32 undef, i32 undef, i32 0, i32 1, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i1> %res
}

define <8 x i1> @test1(<2 x i1> %a) {
; CHECK-LABEL: test1:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovq2m %xmm0, %k0
; CHECK-NEXT:    kshiftlb $4, %k0, %k0
; CHECK-NEXT:    vpmovm2w %k0, %xmm0
; CHECK-NEXT:    retq
  %res = shufflevector <2 x i1> %a, <2 x i1> undef, <8 x i32> <i32 undef, i32 undef, i32 undef, i32 undef, i32 0, i32 1, i32 undef, i32 undef>
  ret <8 x i1> %res
}

define <8 x i1> @test2(<2 x i1> %a) {
; CHECK-LABEL: test2:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovq2m %xmm0, %k0
; CHECK-NEXT:    vpmovm2q %k0, %zmm0
; CHECK-NEXT:    vpxord %zmm1, %zmm1, %zmm1
; CHECK-NEXT:    vshufi64x2 {{.*#+}} zmm0 = zmm1[0,1,0,1],zmm0[0,1,0,1]
; CHECK-NEXT:    vpmovq2m %zmm0, %k0
; CHECK-NEXT:    vpmovm2w %k0, %xmm0
; CHECK-NEXT:    retq
  %res = shufflevector <2 x i1> %a, <2 x i1> zeroinitializer, <8 x i32> <i32 3, i32 3, i32 undef, i32 undef, i32 0, i32 1, i32 undef, i32 undef>
  ret <8 x i1> %res
}

define <8 x i1> @test3(<4 x i1> %a) {
; CHECK-LABEL: test3:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovd2m %xmm0, %k0
; CHECK-NEXT:    kshiftlb $4, %k0, %k0
; CHECK-NEXT:    kshiftrb $4, %k0, %k0
; CHECK-NEXT:    vpmovm2w %k0, %xmm0
; CHECK-NEXT:    retq

  %res = shufflevector <4 x i1> %a, <4 x i1> zeroinitializer, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i1> %res
}

define <8 x i1> @test4(<4 x i1> %a, <4 x i1>%b) {
; CHECK-LABEL: test4:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovd2m %xmm0, %k0
; CHECK-NEXT:    kshiftlb $4, %k0, %k0
; CHECK-NEXT:    kshiftrb $4, %k0, %k1
; CHECK-NEXT:    korb %k0, %k1, %k0
; CHECK-NEXT:    vpmovm2w %k0, %xmm0
; CHECK-NEXT:    retq

  %res = shufflevector <4 x i1> %a, <4 x i1> %b, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i1> %res
}

define <4 x i1> @test5(<2 x i1> %a, <2 x i1>%b) {
; CHECK-LABEL: test5:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovq2m %xmm0, %k0
; CHECK-NEXT:    kshiftlw $2, %k0, %k0
; CHECK-NEXT:    kshiftrw $2, %k0, %k1
; CHECK-NEXT:    korw %k0, %k1, %k0
; CHECK-NEXT:    vpmovm2d %k0, %xmm0
; CHECK-NEXT:    retq

  %res = shufflevector <2 x i1> %a, <2 x i1> %b, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i1> %res
}

define <16 x i1> @test6(<2 x i1> %a, <2 x i1>%b) {
; CHECK-LABEL: test6:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovq2m %xmm0, %k0
; CHECK-NEXT:    kshiftlw $2, %k0, %k0
; CHECK-NEXT:    kshiftrw $2, %k0, %k1
; CHECK-NEXT:    korw %k0, %k1, %k0
; CHECK-NEXT:    kunpckbw %k0, %k0, %k0
; CHECK-NEXT:    vpmovm2b %k0, %xmm0
; CHECK-NEXT:    retq

  %res = shufflevector <2 x i1> %a, <2 x i1> %b, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <16 x i1> %res
}

define <32 x i1> @test7(<4 x i1> %a, <4 x i1>%b) {
; CHECK-LABEL: test7:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovd2m %xmm0, %k0
; CHECK-NEXT:    kshiftlb $4, %k0, %k0
; CHECK-NEXT:    kshiftrb $4, %k0, %k1
; CHECK-NEXT:    korb %k0, %k1, %k0
; CHECK-NEXT:    kunpckbw %k0, %k0, %k0
; CHECK-NEXT:    kunpckwd %k0, %k0, %k0
; CHECK-NEXT:    vpmovm2b %k0, %ymm0
; CHECK-NEXT:    retq

  %res = shufflevector <4 x i1> %a, <4 x i1> %b, <32 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <32 x i1> %res
}

define <64 x i1> @test8(<8 x i1> %a, <8 x i1>%b) {
; CHECK-LABEL: test8:
; CHECK:       # BB#0:
; CHECK-NEXT:    vpmovw2m %xmm1, %k0
; CHECK-NEXT:    vpmovw2m %xmm0, %k1
; CHECK-NEXT:    kunpckdq %k1, %k0, %k0
; CHECK-NEXT:    vpmovm2b %k0, %zmm0
; CHECK-NEXT:    retq

  %res = shufflevector <8 x i1> %a, <8 x i1> %b, <64 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <64 x i1> %res
}

