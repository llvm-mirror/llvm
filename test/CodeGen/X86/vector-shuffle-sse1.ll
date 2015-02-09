; RUN: llc < %s -mcpu=x86-64 -mattr=-sse2 -x86-experimental-vector-shuffle-lowering | FileCheck %s --check-prefix=SSE1

target triple = "x86_64-unknown-unknown"

define <4 x float> @shuffle_v4f32_0001(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_0001:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0,0,1]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 0, i32 0, i32 1>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_0020(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_0020:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0,2,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 0, i32 2, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_0300(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_0300:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,3,0,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 3, i32 0, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_1000(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_1000:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,0,0,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 1, i32 0, i32 0, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_2200(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_2200:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,2,0,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 2, i32 2, i32 0, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_3330(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_3330:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[3,3,3,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 3, i32 3, i32 3, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_3210(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_3210:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[3,2,1,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_0011(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_0011:
; SSE1:       # BB#0:
; SSE1-NEXT:    unpcklps {{.*#+}} xmm0 = xmm0[0,0,1,1]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 0, i32 1, i32 1>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_2233(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_2233:
; SSE1:       # BB#0:
; SSE1-NEXT:    unpckhps {{.*#+}} xmm0 = xmm0[2,2,3,3]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 2, i32 2, i32 3, i32 3>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_0022(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_0022:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0,2,2]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 0, i32 0, i32 2, i32 2>
  ret <4 x float> %shuffle
}
define <4 x float> @shuffle_v4f32_1133(<4 x float> %a, <4 x float> %b) {
; SSE1-LABEL: shuffle_v4f32_1133:
; SSE1:       # BB#0:
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[1,1,3,3]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 1, i32 1, i32 3, i32 3>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_4zzz(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_4zzz:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 4, i32 1, i32 2, i32 3>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_z4zz(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_z4zz:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0],xmm1[2,0]
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm1[3,0]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 2, i32 4, i32 3, i32 0>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_zz4z(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_zz4z:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,0],xmm1[0,0]
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,0],xmm0[0,2]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 0, i32 0, i32 4, i32 0>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_zuu4(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_zuu4:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1],xmm0[2,0]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 0, i32 undef, i32 undef, i32 4>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_zzz7(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_zzz7:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[3,0],xmm1[2,0]
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1],xmm0[2,0]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_v4f32_z6zz(<4 x float> %a) {
; SSE1-LABEL: shuffle_v4f32_z6zz:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm1[0,0]
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[2,0],xmm1[2,3]
; SSE1-NEXT:    retq
  %shuffle = shufflevector <4 x float> zeroinitializer, <4 x float> %a, <4 x i32> <i32 0, i32 6, i32 2, i32 3>
  ret <4 x float> %shuffle
}

define <4 x float> @insert_reg_and_zero_v4f32(float %a) {
; SSE1-LABEL: insert_reg_and_zero_v4f32:
; SSE1:       # BB#0:
; SSE1-NEXT:    xorps %xmm1, %xmm1
; SSE1-NEXT:    movss {{.*#+}} xmm1 = xmm0[0],xmm1[1,2,3]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %v = insertelement <4 x float> undef, float %a, i32 0
  %shuffle = shufflevector <4 x float> %v, <4 x float> zeroinitializer, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %shuffle
}

define <4 x float> @insert_mem_and_zero_v4f32(float* %ptr) {
; SSE1-LABEL: insert_mem_and_zero_v4f32:
; SSE1:       # BB#0:
; SSE1-NEXT:    movss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; SSE1-NEXT:    retq
  %a = load float* %ptr
  %v = insertelement <4 x float> undef, float %a, i32 0
  %shuffle = shufflevector <4 x float> %v, <4 x float> zeroinitializer, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %shuffle
}

define <4 x float> @insert_mem_lo_v4f32(<2 x float>* %ptr, <4 x float> %b) {
; SSE1-LABEL: insert_mem_lo_v4f32:
; SSE1:       # BB#0:
; SSE1-NEXT:    movq (%rdi), %rax
; SSE1-NEXT:    movl %eax, -{{[0-9]+}}(%rsp)
; SSE1-NEXT:    shrq $32, %rax
; SSE1-NEXT:    movl %eax, -{{[0-9]+}}(%rsp)
; SSE1-NEXT:    movss {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE1-NEXT:    movss {{.*#+}} xmm2 = mem[0],zero,zero,zero
; SSE1-NEXT:    unpcklps {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE1-NEXT:    xorps %xmm2, %xmm2
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1],xmm2[0,1]
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1],xmm0[2,3]
; SSE1-NEXT:    movaps %xmm1, %xmm0
; SSE1-NEXT:    retq
  %a = load <2 x float>* %ptr
  %v = shufflevector <2 x float> %a, <2 x float> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %shuffle = shufflevector <4 x float> %v, <4 x float> %b, <4 x i32> <i32 0, i32 1, i32 6, i32 7>
  ret <4 x float> %shuffle
}

define <4 x float> @insert_mem_hi_v4f32(<2 x float>* %ptr, <4 x float> %b) {
; SSE1-LABEL: insert_mem_hi_v4f32:
; SSE1:       # BB#0:
; SSE1-NEXT:    movq (%rdi), %rax
; SSE1-NEXT:    movl %eax, -{{[0-9]+}}(%rsp)
; SSE1-NEXT:    shrq $32, %rax
; SSE1-NEXT:    movl %eax, -{{[0-9]+}}(%rsp)
; SSE1-NEXT:    movss {{.*#+}} xmm1 = mem[0],zero,zero,zero
; SSE1-NEXT:    movss {{.*#+}} xmm2 = mem[0],zero,zero,zero
; SSE1-NEXT:    unpcklps {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE1-NEXT:    xorps %xmm2, %xmm2
; SSE1-NEXT:    shufps {{.*#+}} xmm1 = xmm1[0,1],xmm2[0,1]
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[0,1],xmm1[0,1]
; SSE1-NEXT:    retq
  %a = load <2 x float>* %ptr
  %v = shufflevector <2 x float> %a, <2 x float> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %shuffle = shufflevector <4 x float> %v, <4 x float> %b, <4 x i32> <i32 4, i32 5, i32 0, i32 1>
  ret <4 x float> %shuffle
}

define <4 x float> @shuffle_mem_v4f32_3210(<4 x float>* %ptr) {
; SSE1-LABEL: shuffle_mem_v4f32_3210:
; SSE1:       # BB#0:
; SSE1-NEXT:    movaps (%rdi), %xmm0
; SSE1-NEXT:    shufps {{.*#+}} xmm0 = xmm0[3,2,1,0]
; SSE1-NEXT:    retq
  %a = load <4 x float>* %ptr
  %shuffle = shufflevector <4 x float> %a, <4 x float> undef, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  ret <4 x float> %shuffle
}
