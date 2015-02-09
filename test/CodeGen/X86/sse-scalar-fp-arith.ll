; RUN: llc -mcpu=x86-64 -mattr=+sse2 < %s | FileCheck --check-prefix=SSE --check-prefix=SSE2 %s
; RUN: llc -mcpu=x86-64 -mattr=+sse4.1 < %s | FileCheck --check-prefix=SSE --check-prefix=SSE41 %s
; RUN: llc -mcpu=x86-64 -mattr=+avx < %s | FileCheck --check-prefix=AVX %s

target triple = "x86_64-unknown-unknown"

; Ensure that the backend no longer emits unnecessary vector insert
; instructions immediately after SSE scalar fp instructions
; like addss or mulss.

define <4 x float> @test_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %add = fadd float %2, %1
  %3 = insertelement <4 x float> %a, float %add, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %sub = fsub float %2, %1
  %3 = insertelement <4 x float> %a, float %sub, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %mul = fmul float %2, %1
  %3 = insertelement <4 x float> %a, float %mul, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %div = fdiv float %2, %1
  %3 = insertelement <4 x float> %a, float %div, i32 0
  ret <4 x float> %3
}

define <2 x double> @test_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %b, i32 0
  %2 = extractelement <2 x double> %a, i32 0
  %add = fadd double %2, %1
  %3 = insertelement <2 x double> %a, double %add, i32 0
  ret <2 x double> %3
}

define <2 x double> @test_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %b, i32 0
  %2 = extractelement <2 x double> %a, i32 0
  %sub = fsub double %2, %1
  %3 = insertelement <2 x double> %a, double %sub, i32 0
  ret <2 x double> %3
}

define <2 x double> @test_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %b, i32 0
  %2 = extractelement <2 x double> %a, i32 0
  %mul = fmul double %2, %1
  %3 = insertelement <2 x double> %a, double %mul, i32 0
  ret <2 x double> %3
}

define <2 x double> @test_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %b, i32 0
  %2 = extractelement <2 x double> %a, i32 0
  %div = fdiv double %2, %1
  %3 = insertelement <2 x double> %a, double %div, i32 0
  ret <2 x double> %3
}

define <4 x float> @test2_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test2_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %a, i32 0
  %2 = extractelement <4 x float> %b, i32 0
  %add = fadd float %1, %2
  %3 = insertelement <4 x float> %b, float %add, i32 0
  ret <4 x float> %3
}

define <4 x float> @test2_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test2_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %a, i32 0
  %2 = extractelement <4 x float> %b, i32 0
  %sub = fsub float %2, %1
  %3 = insertelement <4 x float> %b, float %sub, i32 0
  ret <4 x float> %3
}

define <4 x float> @test2_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test2_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %a, i32 0
  %2 = extractelement <4 x float> %b, i32 0
  %mul = fmul float %1, %2
  %3 = insertelement <4 x float> %b, float %mul, i32 0
  ret <4 x float> %3
}

define <4 x float> @test2_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test2_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %a, i32 0
  %2 = extractelement <4 x float> %b, i32 0
  %div = fdiv float %2, %1
  %3 = insertelement <4 x float> %b, float %div, i32 0
  ret <4 x float> %3
}

define <2 x double> @test2_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test2_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %a, i32 0
  %2 = extractelement <2 x double> %b, i32 0
  %add = fadd double %1, %2
  %3 = insertelement <2 x double> %b, double %add, i32 0
  ret <2 x double> %3
}

define <2 x double> @test2_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test2_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %a, i32 0
  %2 = extractelement <2 x double> %b, i32 0
  %sub = fsub double %2, %1
  %3 = insertelement <2 x double> %b, double %sub, i32 0
  ret <2 x double> %3
}

define <2 x double> @test2_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test2_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %a, i32 0
  %2 = extractelement <2 x double> %b, i32 0
  %mul = fmul double %1, %2
  %3 = insertelement <2 x double> %b, double %mul, i32 0
  ret <2 x double> %3
}

define <2 x double> @test2_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: test2_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test2_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <2 x double> %a, i32 0
  %2 = extractelement <2 x double> %b, i32 0
  %div = fdiv double %2, %1
  %3 = insertelement <2 x double> %b, double %div, i32 0
  ret <2 x double> %3
}

define <4 x float> @test_multiple_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_multiple_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm0, %xmm1
; SSE-NEXT:    addss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_multiple_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm1, %xmm0, %xmm1
; AVX-NEXT:    vaddss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %add = fadd float %2, %1
  %add2 = fadd float %2, %add
  %3 = insertelement <4 x float> %a, float %add2, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_multiple_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_multiple_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm0, %xmm2
; SSE-NEXT:    subss %xmm1, %xmm2
; SSE-NEXT:    subss %xmm2, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_multiple_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm1, %xmm0, %xmm1
; AVX-NEXT:    vsubss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %sub = fsub float %2, %1
  %sub2 = fsub float %2, %sub
  %3 = insertelement <4 x float> %a, float %sub2, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_multiple_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_multiple_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm0, %xmm1
; SSE-NEXT:    mulss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_multiple_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm1, %xmm0, %xmm1
; AVX-NEXT:    vmulss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %mul = fmul float %2, %1
  %mul2 = fmul float %2, %mul
  %3 = insertelement <4 x float> %a, float %mul2, i32 0
  ret <4 x float> %3
}

define <4 x float> @test_multiple_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: test_multiple_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    movaps %xmm0, %xmm2
; SSE-NEXT:    divss %xmm1, %xmm2
; SSE-NEXT:    divss %xmm2, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: test_multiple_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm1, %xmm0, %xmm1
; AVX-NEXT:    vdivss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = extractelement <4 x float> %b, i32 0
  %2 = extractelement <4 x float> %a, i32 0
  %div = fdiv float %2, %1
  %div2 = fdiv float %2, %div
  %3 = insertelement <4 x float> %a, float %div2, i32 0
  ret <4 x float> %3
}

; Ensure that the backend selects SSE/AVX scalar fp instructions
; from a packed fp instrution plus a vector insert.

define <4 x float> @insert_test_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <4 x float> %a, %b
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <4 x float> %a, %b
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <4 x float> %a, %b
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <4 x float> %a, %b
  %2 = shufflevector <4 x float> %1, <4 x float> %a, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <2 x double> @insert_test_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <2 x double> %a, %b
  %2 = shufflevector <2 x double> %1, <2 x double> %a, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <2 x double> %a, %b
  %2 = shufflevector <2 x double> %1, <2 x double> %a, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <2 x double> %a, %b
  %2 = shufflevector <2 x double> %1, <2 x double> %a, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <2 x double> %a, %b
  %2 = shufflevector <2 x double> %1, <2 x double> %a, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <4 x float> @insert_test2_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test2_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <4 x float> %b, %a
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test2_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test2_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <4 x float> %b, %a
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test2_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test2_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <4 x float> %b, %a
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <4 x float> @insert_test2_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test2_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <4 x float> %b, %a
  %2 = shufflevector <4 x float> %1, <4 x float> %b, <4 x i32> <i32 0, i32 5, i32 6, i32 7>
  ret <4 x float> %2
}

define <2 x double> @insert_test2_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test2_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <2 x double> %b, %a
  %2 = shufflevector <2 x double> %1, <2 x double> %b, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test2_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test2_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <2 x double> %b, %a
  %2 = shufflevector <2 x double> %1, <2 x double> %b, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test2_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test2_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <2 x double> %b, %a
  %2 = shufflevector <2 x double> %1, <2 x double> %b, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <2 x double> @insert_test2_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test2_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test2_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <2 x double> %b, %a
  %2 = shufflevector <2 x double> %1, <2 x double> %b, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %2
}

define <4 x float> @insert_test3_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test3_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <4 x float> %a, %b
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %a, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test3_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test3_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <4 x float> %a, %b
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %a, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test3_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test3_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <4 x float> %a, %b
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %a, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test3_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test3_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <4 x float> %a, %b
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %a, <4 x float> %1
  ret <4 x float> %2
}

define <2 x double> @insert_test3_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test3_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <2 x double> %a, %b
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %a, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test3_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test3_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <2 x double> %a, %b
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %a, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test3_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test3_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <2 x double> %a, %b
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %a, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test3_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test3_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test3_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm1, %xmm0, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <2 x double> %a, %b
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %a, <2 x double> %1
  ret <2 x double> %2
}

define <4 x float> @insert_test4_add_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test4_add_ss:
; SSE:       # BB#0:
; SSE-NEXT:    addss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_add_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vaddss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <4 x float> %b, %a
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %b, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test4_sub_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test4_sub_ss:
; SSE:       # BB#0:
; SSE-NEXT:    subss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_sub_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vsubss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <4 x float> %b, %a
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %b, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test4_mul_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test4_mul_ss:
; SSE:       # BB#0:
; SSE-NEXT:    mulss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_mul_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vmulss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <4 x float> %b, %a
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %b, <4 x float> %1
  ret <4 x float> %2
}

define <4 x float> @insert_test4_div_ss(<4 x float> %a, <4 x float> %b) {
; SSE-LABEL: insert_test4_div_ss:
; SSE:       # BB#0:
; SSE-NEXT:    divss %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_div_ss:
; AVX:       # BB#0:
; AVX-NEXT:    vdivss %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <4 x float> %b, %a
  %2 = select <4 x i1> <i1 false, i1 true, i1 true, i1 true>, <4 x float> %b, <4 x float> %1
  ret <4 x float> %2
}

define <2 x double> @insert_test4_add_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test4_add_sd:
; SSE:       # BB#0:
; SSE-NEXT:    addsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_add_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vaddsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fadd <2 x double> %b, %a
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %b, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test4_sub_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test4_sub_sd:
; SSE:       # BB#0:
; SSE-NEXT:    subsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_sub_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vsubsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fsub <2 x double> %b, %a
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %b, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test4_mul_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test4_mul_sd:
; SSE:       # BB#0:
; SSE-NEXT:    mulsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_mul_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vmulsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fmul <2 x double> %b, %a
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %b, <2 x double> %1
  ret <2 x double> %2
}

define <2 x double> @insert_test4_div_sd(<2 x double> %a, <2 x double> %b) {
; SSE-LABEL: insert_test4_div_sd:
; SSE:       # BB#0:
; SSE-NEXT:    divsd %xmm0, %xmm1
; SSE-NEXT:    movaps %xmm1, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: insert_test4_div_sd:
; AVX:       # BB#0:
; AVX-NEXT:    vdivsd %xmm0, %xmm1, %xmm0
; AVX-NEXT:    retq
  %1 = fdiv <2 x double> %b, %a
  %2 = select <2 x i1> <i1 false, i1 true>, <2 x double> %b, <2 x double> %1
  ret <2 x double> %2
}
