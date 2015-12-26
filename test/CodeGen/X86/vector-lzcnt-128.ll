; RUN: llc < %s -mtriple=x86_64-unknown-unknown | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE2
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+sse3 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE3
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+ssse3 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSSE3
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+sse4.1 | FileCheck %s --check-prefix=ALL --check-prefix=SSE --check-prefix=SSE41
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+avx | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX1
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+avx2 | FileCheck %s --check-prefix=ALL --check-prefix=AVX --check-prefix=AVX2
; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=knl -mattr=+avx512cd -mattr=+avx512vl | FileCheck %s --check-prefix=AVX512VLCD --check-prefix=ALL --check-prefix=AVX512
; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=knl -mattr=+avx512cd | FileCheck %s --check-prefix=AVX512CD --check-prefix=ALL --check-prefix=AVX512

define <2 x i64> @testv2i64(<2 x i64> %in) nounwind {
; SSE2-LABEL: testv2i64:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd %xmm0, %rax
; SSE2-NEXT:    bsrq %rax, %rax
; SSE2-NEXT:    movl $127, %ecx
; SSE2-NEXT:    cmoveq %rcx, %rax
; SSE2-NEXT:    xorq $63, %rax
; SSE2-NEXT:    movd %rax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    movd %xmm0, %rax
; SSE2-NEXT:    bsrq %rax, %rax
; SSE2-NEXT:    cmoveq %rcx, %rax
; SSE2-NEXT:    xorq $63, %rax
; SSE2-NEXT:    movd %rax, %xmm0
; SSE2-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSE2-NEXT:    movdqa %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv2i64:
; SSE3:       # BB#0:
; SSE3-NEXT:    movd %xmm0, %rax
; SSE3-NEXT:    bsrq %rax, %rax
; SSE3-NEXT:    movl $127, %ecx
; SSE3-NEXT:    cmoveq %rcx, %rax
; SSE3-NEXT:    xorq $63, %rax
; SSE3-NEXT:    movd %rax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE3-NEXT:    movd %xmm0, %rax
; SSE3-NEXT:    bsrq %rax, %rax
; SSE3-NEXT:    cmoveq %rcx, %rax
; SSE3-NEXT:    xorq $63, %rax
; SSE3-NEXT:    movd %rax, %xmm0
; SSE3-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSE3-NEXT:    movdqa %xmm1, %xmm0
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv2i64:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd %xmm0, %rax
; SSSE3-NEXT:    bsrq %rax, %rax
; SSSE3-NEXT:    movl $127, %ecx
; SSSE3-NEXT:    cmoveq %rcx, %rax
; SSSE3-NEXT:    xorq $63, %rax
; SSSE3-NEXT:    movd %rax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSSE3-NEXT:    movd %xmm0, %rax
; SSSE3-NEXT:    bsrq %rax, %rax
; SSSE3-NEXT:    cmoveq %rcx, %rax
; SSSE3-NEXT:    xorq $63, %rax
; SSSE3-NEXT:    movd %rax, %xmm0
; SSSE3-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv2i64:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrq $1, %xmm0, %rax
; SSE41-NEXT:    bsrq %rax, %rax
; SSE41-NEXT:    movl $127, %ecx
; SSE41-NEXT:    cmoveq %rcx, %rax
; SSE41-NEXT:    xorq $63, %rax
; SSE41-NEXT:    movd %rax, %xmm1
; SSE41-NEXT:    movd %xmm0, %rax
; SSE41-NEXT:    bsrq %rax, %rax
; SSE41-NEXT:    cmoveq %rcx, %rax
; SSE41-NEXT:    xorq $63, %rax
; SSE41-NEXT:    movd %rax, %xmm0
; SSE41-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv2i64:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrq $1, %xmm0, %rax
; AVX-NEXT:    bsrq %rax, %rax
; AVX-NEXT:    movl $127, %ecx
; AVX-NEXT:    cmoveq %rcx, %rax
; AVX-NEXT:    xorq $63, %rax
; AVX-NEXT:    vmovq %rax, %xmm1
; AVX-NEXT:    vmovq %xmm0, %rax
; AVX-NEXT:    bsrq %rax, %rax
; AVX-NEXT:    cmoveq %rcx, %rax
; AVX-NEXT:    xorq $63, %rax
; AVX-NEXT:    vmovq %rax, %xmm0
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv2i64:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vplzcntq %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv2i64:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vplzcntq %zmm0, %zmm0
; AVX512CD-NEXT:    retq

  %out = call <2 x i64> @llvm.ctlz.v2i64(<2 x i64> %in, i1 0)
  ret <2 x i64> %out
}

define <2 x i64> @testv2i64u(<2 x i64> %in) nounwind {
; SSE2-LABEL: testv2i64u:
; SSE2:       # BB#0:
; SSE2-NEXT:    movd %xmm0, %rax
; SSE2-NEXT:    bsrq %rax, %rax
; SSE2-NEXT:    xorq $63, %rax
; SSE2-NEXT:    movd %rax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    movd %xmm0, %rax
; SSE2-NEXT:    bsrq %rax, %rax
; SSE2-NEXT:    xorq $63, %rax
; SSE2-NEXT:    movd %rax, %xmm0
; SSE2-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSE2-NEXT:    movdqa %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv2i64u:
; SSE3:       # BB#0:
; SSE3-NEXT:    movd %xmm0, %rax
; SSE3-NEXT:    bsrq %rax, %rax
; SSE3-NEXT:    xorq $63, %rax
; SSE3-NEXT:    movd %rax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE3-NEXT:    movd %xmm0, %rax
; SSE3-NEXT:    bsrq %rax, %rax
; SSE3-NEXT:    xorq $63, %rax
; SSE3-NEXT:    movd %rax, %xmm0
; SSE3-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSE3-NEXT:    movdqa %xmm1, %xmm0
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv2i64u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    movd %xmm0, %rax
; SSSE3-NEXT:    bsrq %rax, %rax
; SSSE3-NEXT:    xorq $63, %rax
; SSSE3-NEXT:    movd %rax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSSE3-NEXT:    movd %xmm0, %rax
; SSSE3-NEXT:    bsrq %rax, %rax
; SSSE3-NEXT:    xorq $63, %rax
; SSSE3-NEXT:    movd %rax, %xmm0
; SSSE3-NEXT:    punpcklqdq {{.*#+}} xmm1 = xmm1[0],xmm0[0]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv2i64u:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrq $1, %xmm0, %rax
; SSE41-NEXT:    bsrq %rax, %rax
; SSE41-NEXT:    xorq $63, %rax
; SSE41-NEXT:    movd %rax, %xmm1
; SSE41-NEXT:    movd %xmm0, %rax
; SSE41-NEXT:    bsrq %rax, %rax
; SSE41-NEXT:    xorq $63, %rax
; SSE41-NEXT:    movd %rax, %xmm0
; SSE41-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv2i64u:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrq $1, %xmm0, %rax
; AVX-NEXT:    bsrq %rax, %rax
; AVX-NEXT:    xorq $63, %rax
; AVX-NEXT:    vmovq %rax, %xmm1
; AVX-NEXT:    vmovq %xmm0, %rax
; AVX-NEXT:    bsrq %rax, %rax
; AVX-NEXT:    xorq $63, %rax
; AVX-NEXT:    vmovq %rax, %xmm0
; AVX-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv2i64u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vplzcntq %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv2i64u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vplzcntq %zmm0, %zmm0
; AVX512CD-NEXT:    retq

  %out = call <2 x i64> @llvm.ctlz.v2i64(<2 x i64> %in, i1 -1)
  ret <2 x i64> %out
}

define <4 x i32> @testv4i32(<4 x i32> %in) nounwind {
; SSE2-LABEL: testv4i32:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSE2-NEXT:    movd %xmm1, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    movl $63, %ecx
; SSE2-NEXT:    cmovel %ecx, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSE2-NEXT:    movd %xmm2, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    cmovel %ecx, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSE2-NEXT:    movd %xmm0, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    cmovel %ecx, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    movd %xmm0, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    cmovel %ecx, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSE2-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE2-NEXT:    movdqa %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv4i32:
; SSE3:       # BB#0:
; SSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSE3-NEXT:    movd %xmm1, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    movl $63, %ecx
; SSE3-NEXT:    cmovel %ecx, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSE3-NEXT:    movd %xmm2, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    cmovel %ecx, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSE3-NEXT:    movd %xmm0, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    cmovel %ecx, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE3-NEXT:    movd %xmm0, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    cmovel %ecx, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE3-NEXT:    movdqa %xmm1, %xmm0
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv4i32:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSSE3-NEXT:    movd %xmm1, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    movl $63, %ecx
; SSSE3-NEXT:    cmovel %ecx, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSSE3-NEXT:    movd %xmm2, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    cmovel %ecx, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSSE3-NEXT:    movd %xmm0, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    cmovel %ecx, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSSE3-NEXT:    movd %xmm0, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    cmovel %ecx, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv4i32:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrd $1, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    movl $63, %ecx
; SSE41-NEXT:    cmovel %ecx, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    movd %xmm0, %edx
; SSE41-NEXT:    bsrl %edx, %edx
; SSE41-NEXT:    cmovel %ecx, %edx
; SSE41-NEXT:    xorl $31, %edx
; SSE41-NEXT:    movd %edx, %xmm1
; SSE41-NEXT:    pinsrd $1, %eax, %xmm1
; SSE41-NEXT:    pextrd $2, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    cmovel %ecx, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    pinsrd $2, %eax, %xmm1
; SSE41-NEXT:    pextrd $3, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    cmovel %ecx, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    pinsrd $3, %eax, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv4i32:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrd $1, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    movl $63, %ecx
; AVX-NEXT:    cmovel %ecx, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vmovd %xmm0, %edx
; AVX-NEXT:    bsrl %edx, %edx
; AVX-NEXT:    cmovel %ecx, %edx
; AVX-NEXT:    xorl $31, %edx
; AVX-NEXT:    vmovd %edx, %xmm1
; AVX-NEXT:    vpinsrd $1, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrd $2, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    cmovel %ecx, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vpinsrd $2, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrd $3, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    cmovel %ecx, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vpinsrd $3, %eax, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv4i32:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vplzcntd %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv4i32:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    retq

  %out = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> %in, i1 0)
  ret <4 x i32> %out
}

define <4 x i32> @testv4i32u(<4 x i32> %in) nounwind {
; SSE2-LABEL: testv4i32u:
; SSE2:       # BB#0:
; SSE2-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSE2-NEXT:    movd %xmm1, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSE2-NEXT:    movd %xmm2, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSE2-NEXT:    movd %xmm0, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE2-NEXT:    movd %xmm0, %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $31, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSE2-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE2-NEXT:    movdqa %xmm1, %xmm0
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv4i32u:
; SSE3:       # BB#0:
; SSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSE3-NEXT:    movd %xmm1, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSE3-NEXT:    movd %xmm2, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSE3-NEXT:    movd %xmm0, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSE3-NEXT:    movd %xmm0, %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $31, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSE3-NEXT:    movdqa %xmm1, %xmm0
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv4i32u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[3,1,2,3]
; SSSE3-NEXT:    movd %xmm1, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[1,1,2,3]
; SSSE3-NEXT:    movd %xmm2, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
; SSSE3-NEXT:    movd %xmm0, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; SSSE3-NEXT:    movd %xmm0, %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $31, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; SSSE3-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1]
; SSSE3-NEXT:    movdqa %xmm1, %xmm0
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv4i32u:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrd $1, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    movd %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    xorl $31, %ecx
; SSE41-NEXT:    movd %ecx, %xmm1
; SSE41-NEXT:    pinsrd $1, %eax, %xmm1
; SSE41-NEXT:    pextrd $2, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    pinsrd $2, %eax, %xmm1
; SSE41-NEXT:    pextrd $3, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $31, %eax
; SSE41-NEXT:    pinsrd $3, %eax, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv4i32u:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrd $1, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vmovd %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    xorl $31, %ecx
; AVX-NEXT:    vmovd %ecx, %xmm1
; AVX-NEXT:    vpinsrd $1, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrd $2, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vpinsrd $2, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrd $3, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $31, %eax
; AVX-NEXT:    vpinsrd $3, %eax, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv4i32u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vplzcntd %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv4i32u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    retq

  %out = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> %in, i1 -1)
  ret <4 x i32> %out
}

define <8 x i16> @testv8i16(<8 x i16> %in) nounwind {
; SSE2-LABEL: testv8i16:
; SSE2:       # BB#0:
; SSE2-NEXT:    pextrw $7, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %cx
; SSE2-NEXT:    movw $31, %ax
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm1
; SSE2-NEXT:    pextrw $3, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSE2-NEXT:    pextrw $5, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm3
; SSE2-NEXT:    pextrw $1, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm1
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSE2-NEXT:    pextrw $6, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    pextrw $2, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm3
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSE2-NEXT:    pextrw $4, %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    movd %xmm0, %ecx
; SSE2-NEXT:    bsrw %cx, %cx
; SSE2-NEXT:    cmovew %ax, %cx
; SSE2-NEXT:    xorl $15, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv8i16:
; SSE3:       # BB#0:
; SSE3-NEXT:    pextrw $7, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %cx
; SSE3-NEXT:    movw $31, %ax
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm1
; SSE3-NEXT:    pextrw $3, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSE3-NEXT:    pextrw $5, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm3
; SSE3-NEXT:    pextrw $1, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm1
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSE3-NEXT:    pextrw $6, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    pextrw $2, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm3
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSE3-NEXT:    pextrw $4, %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    movd %xmm0, %ecx
; SSE3-NEXT:    bsrw %cx, %cx
; SSE3-NEXT:    cmovew %ax, %cx
; SSE3-NEXT:    xorl $15, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv8i16:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pextrw $7, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %cx
; SSSE3-NEXT:    movw $31, %ax
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm1
; SSSE3-NEXT:    pextrw $3, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSSE3-NEXT:    pextrw $5, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm3
; SSSE3-NEXT:    pextrw $1, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm1
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSSE3-NEXT:    pextrw $6, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    pextrw $2, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm3
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSSE3-NEXT:    pextrw $4, %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    movd %xmm0, %ecx
; SSSE3-NEXT:    bsrw %cx, %cx
; SSSE3-NEXT:    cmovew %ax, %cx
; SSSE3-NEXT:    xorl $15, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv8i16:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrw $1, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %cx
; SSE41-NEXT:    movw $31, %ax
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    movd %xmm0, %edx
; SSE41-NEXT:    bsrw %dx, %dx
; SSE41-NEXT:    cmovew %ax, %dx
; SSE41-NEXT:    xorl $15, %edx
; SSE41-NEXT:    movd %edx, %xmm1
; SSE41-NEXT:    pinsrw $1, %ecx, %xmm1
; SSE41-NEXT:    pextrw $2, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $2, %ecx, %xmm1
; SSE41-NEXT:    pextrw $3, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $3, %ecx, %xmm1
; SSE41-NEXT:    pextrw $4, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $4, %ecx, %xmm1
; SSE41-NEXT:    pextrw $5, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $5, %ecx, %xmm1
; SSE41-NEXT:    pextrw $6, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $6, %ecx, %xmm1
; SSE41-NEXT:    pextrw $7, %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    cmovew %ax, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    pinsrw $7, %ecx, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv8i16:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrw $1, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %cx
; AVX-NEXT:    movw $31, %ax
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vmovd %xmm0, %edx
; AVX-NEXT:    bsrw %dx, %dx
; AVX-NEXT:    cmovew %ax, %dx
; AVX-NEXT:    xorl $15, %edx
; AVX-NEXT:    vmovd %edx, %xmm1
; AVX-NEXT:    vpinsrw $1, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $2, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $2, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $3, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $3, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $4, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $4, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $5, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $5, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $6, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $6, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $7, %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    cmovew %ax, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vpinsrw $7, %ecx, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv8i16:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vpmovzxwd %xmm0, %ymm0
; AVX512VLCD-NEXT:    vplzcntd %ymm0, %ymm0
; AVX512VLCD-NEXT:    vpmovdw %ymm0, %xmm0
; AVX512VLCD-NEXT:    vpsubw {{.*}}(%rip), %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv8i16:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vpmovzxwd {{.*#+}} ymm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero,xmm0[4],zero,xmm0[5],zero,xmm0[6],zero,xmm0[7],zero
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    vpmovdw %zmm0, %ymm0
; AVX512CD-NEXT:    vpsubw {{.*}}(%rip), %xmm0, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> %in, i1 0)
  ret <8 x i16> %out
}

define <8 x i16> @testv8i16u(<8 x i16> %in) nounwind {
; SSE2-LABEL: testv8i16u:
; SSE2:       # BB#0:
; SSE2-NEXT:    pextrw $7, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    pextrw $3, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSE2-NEXT:    pextrw $5, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm3
; SSE2-NEXT:    pextrw $1, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm1
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSE2-NEXT:    pextrw $6, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    pextrw $2, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm3
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSE2-NEXT:    pextrw $4, %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    movd %xmm0, %eax
; SSE2-NEXT:    bsrw %ax, %ax
; SSE2-NEXT:    xorl $15, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE2-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv8i16u:
; SSE3:       # BB#0:
; SSE3-NEXT:    pextrw $7, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    pextrw $3, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSE3-NEXT:    pextrw $5, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm3
; SSE3-NEXT:    pextrw $1, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm1
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSE3-NEXT:    pextrw $6, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    pextrw $2, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm3
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSE3-NEXT:    pextrw $4, %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    movd %xmm0, %eax
; SSE3-NEXT:    bsrw %ax, %ax
; SSE3-NEXT:    xorl $15, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv8i16u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pextrw $7, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    pextrw $3, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3]
; SSSE3-NEXT:    pextrw $5, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm3
; SSSE3-NEXT:    pextrw $1, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm1
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3]
; SSSE3-NEXT:    pextrw $6, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    pextrw $2, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm3
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3]
; SSSE3-NEXT:    pextrw $4, %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    movd %xmm0, %eax
; SSSE3-NEXT:    bsrw %ax, %ax
; SSSE3-NEXT:    xorl $15, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3]
; SSSE3-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv8i16u:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrw $1, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    movd %xmm0, %ecx
; SSE41-NEXT:    bsrw %cx, %cx
; SSE41-NEXT:    xorl $15, %ecx
; SSE41-NEXT:    movd %ecx, %xmm1
; SSE41-NEXT:    pinsrw $1, %eax, %xmm1
; SSE41-NEXT:    pextrw $2, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $2, %eax, %xmm1
; SSE41-NEXT:    pextrw $3, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $3, %eax, %xmm1
; SSE41-NEXT:    pextrw $4, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $4, %eax, %xmm1
; SSE41-NEXT:    pextrw $5, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $5, %eax, %xmm1
; SSE41-NEXT:    pextrw $6, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $6, %eax, %xmm1
; SSE41-NEXT:    pextrw $7, %xmm0, %eax
; SSE41-NEXT:    bsrw %ax, %ax
; SSE41-NEXT:    xorl $15, %eax
; SSE41-NEXT:    pinsrw $7, %eax, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv8i16u:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrw $1, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vmovd %xmm0, %ecx
; AVX-NEXT:    bsrw %cx, %cx
; AVX-NEXT:    xorl $15, %ecx
; AVX-NEXT:    vmovd %ecx, %xmm1
; AVX-NEXT:    vpinsrw $1, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $2, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $2, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $3, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $3, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $4, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $4, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $5, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $5, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $6, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $6, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrw $7, %xmm0, %eax
; AVX-NEXT:    bsrw %ax, %ax
; AVX-NEXT:    xorl $15, %eax
; AVX-NEXT:    vpinsrw $7, %eax, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv8i16u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vpmovzxwd %xmm0, %ymm0
; AVX512VLCD-NEXT:    vplzcntd %ymm0, %ymm0
; AVX512VLCD-NEXT:    vpmovdw %ymm0, %xmm0
; AVX512VLCD-NEXT:    vpsubw {{.*}}(%rip), %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv8i16u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vpmovzxwd {{.*#+}} ymm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero,xmm0[4],zero,xmm0[5],zero,xmm0[6],zero,xmm0[7],zero
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    vpmovdw %zmm0, %ymm0
; AVX512CD-NEXT:    vpsubw {{.*}}(%rip), %xmm0, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> %in, i1 -1)
  ret <8 x i16> %out
}

define <16 x i8> @testv16i8(<16 x i8> %in) nounwind {
; SSE2-LABEL: testv16i8:
; SSE2:       # BB#0:
; SSE2-NEXT:    pushq %rbp
; SSE2-NEXT:    pushq %rbx
; SSE2-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE2-NEXT:    bsrl %eax, %ecx
; SSE2-NEXT:    movl $15, %eax
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    bsrl %ecx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm1
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    bsrl %edx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebp
; SSE2-NEXT:    bsrl %ebp, %ebp
; SSE2-NEXT:    cmovel %eax, %ebp
; SSE2-NEXT:    xorl $7, %ebp
; SSE2-NEXT:    movd %ebp, %xmm0
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE2-NEXT:    bsrl %edi, %edi
; SSE2-NEXT:    cmovel %eax, %edi
; SSE2-NEXT:    xorl $7, %edi
; SSE2-NEXT:    movd %edi, %xmm1
; SSE2-NEXT:    bsrl %ecx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSE2-NEXT:    bsrl %esi, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm3
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    bsrl %ecx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm1
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    bsrl %ebx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    bsrl %edx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm3
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE2-NEXT:    bsrl %r11d, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    bsrl %esi, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm2
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm3[0],xmm2[1],xmm3[1],xmm2[2],xmm3[2],xmm2[3],xmm3[3],xmm2[4],xmm3[4],xmm2[5],xmm3[5],xmm2[6],xmm3[6],xmm2[7],xmm3[7]
; SSE2-NEXT:    bsrl %r9d, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    bsrl %r10d, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm3
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE2-NEXT:    bsrl %r8d, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm4
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    bsrl %ecx, %ecx
; SSE2-NEXT:    cmovel %eax, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE2-NEXT:    popq %rbx
; SSE2-NEXT:    popq %rbp
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv16i8:
; SSE3:       # BB#0:
; SSE3-NEXT:    pushq %rbp
; SSE3-NEXT:    pushq %rbx
; SSE3-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE3-NEXT:    bsrl %eax, %ecx
; SSE3-NEXT:    movl $15, %eax
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    bsrl %ecx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm1
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE3-NEXT:    bsrl %edx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebp
; SSE3-NEXT:    bsrl %ebp, %ebp
; SSE3-NEXT:    cmovel %eax, %ebp
; SSE3-NEXT:    xorl $7, %ebp
; SSE3-NEXT:    movd %ebp, %xmm0
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE3-NEXT:    bsrl %edi, %edi
; SSE3-NEXT:    cmovel %eax, %edi
; SSE3-NEXT:    xorl $7, %edi
; SSE3-NEXT:    movd %edi, %xmm1
; SSE3-NEXT:    bsrl %ecx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSE3-NEXT:    bsrl %esi, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm3
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    bsrl %ecx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm1
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE3-NEXT:    bsrl %ebx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    bsrl %edx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm3
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE3-NEXT:    bsrl %r11d, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    bsrl %esi, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm2
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm3[0],xmm2[1],xmm3[1],xmm2[2],xmm3[2],xmm2[3],xmm3[3],xmm2[4],xmm3[4],xmm2[5],xmm3[5],xmm2[6],xmm3[6],xmm2[7],xmm3[7]
; SSE3-NEXT:    bsrl %r9d, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    bsrl %r10d, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm3
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE3-NEXT:    bsrl %r8d, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm4
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    bsrl %ecx, %ecx
; SSE3-NEXT:    cmovel %eax, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE3-NEXT:    popq %rbx
; SSE3-NEXT:    popq %rbp
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv16i8:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pushq %rbp
; SSSE3-NEXT:    pushq %rbx
; SSSE3-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSSE3-NEXT:    bsrl %eax, %ecx
; SSSE3-NEXT:    movl $15, %eax
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    bsrl %ecx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm1
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    bsrl %edx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebp
; SSSE3-NEXT:    bsrl %ebp, %ebp
; SSSE3-NEXT:    cmovel %eax, %ebp
; SSSE3-NEXT:    xorl $7, %ebp
; SSSE3-NEXT:    movd %ebp, %xmm0
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSSE3-NEXT:    bsrl %edi, %edi
; SSSE3-NEXT:    cmovel %eax, %edi
; SSSE3-NEXT:    xorl $7, %edi
; SSSE3-NEXT:    movd %edi, %xmm1
; SSSE3-NEXT:    bsrl %ecx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSSE3-NEXT:    bsrl %esi, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm3
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    bsrl %ecx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm1
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    bsrl %ebx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    bsrl %edx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm3
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSSE3-NEXT:    bsrl %r11d, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    bsrl %esi, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm2
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm3[0],xmm2[1],xmm3[1],xmm2[2],xmm3[2],xmm2[3],xmm3[3],xmm2[4],xmm3[4],xmm2[5],xmm3[5],xmm2[6],xmm3[6],xmm2[7],xmm3[7]
; SSSE3-NEXT:    bsrl %r9d, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    bsrl %r10d, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm3
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSSE3-NEXT:    bsrl %r8d, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm4
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    bsrl %ecx, %ecx
; SSSE3-NEXT:    cmovel %eax, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSSE3-NEXT:    popq %rbx
; SSSE3-NEXT:    popq %rbp
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv16i8:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrb $1, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %ecx
; SSE41-NEXT:    movl $15, %eax
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pextrb $0, %xmm0, %edx
; SSE41-NEXT:    bsrl %edx, %edx
; SSE41-NEXT:    cmovel %eax, %edx
; SSE41-NEXT:    xorl $7, %edx
; SSE41-NEXT:    movd %edx, %xmm1
; SSE41-NEXT:    pinsrb $1, %ecx, %xmm1
; SSE41-NEXT:    pextrb $2, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $2, %ecx, %xmm1
; SSE41-NEXT:    pextrb $3, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $3, %ecx, %xmm1
; SSE41-NEXT:    pextrb $4, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $4, %ecx, %xmm1
; SSE41-NEXT:    pextrb $5, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $5, %ecx, %xmm1
; SSE41-NEXT:    pextrb $6, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $6, %ecx, %xmm1
; SSE41-NEXT:    pextrb $7, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $7, %ecx, %xmm1
; SSE41-NEXT:    pextrb $8, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $8, %ecx, %xmm1
; SSE41-NEXT:    pextrb $9, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $9, %ecx, %xmm1
; SSE41-NEXT:    pextrb $10, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $10, %ecx, %xmm1
; SSE41-NEXT:    pextrb $11, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $11, %ecx, %xmm1
; SSE41-NEXT:    pextrb $12, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $12, %ecx, %xmm1
; SSE41-NEXT:    pextrb $13, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $13, %ecx, %xmm1
; SSE41-NEXT:    pextrb $14, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $14, %ecx, %xmm1
; SSE41-NEXT:    pextrb $15, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    cmovel %eax, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    pinsrb $15, %ecx, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv16i8:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrb $1, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %ecx
; AVX-NEXT:    movl $15, %eax
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpextrb $0, %xmm0, %edx
; AVX-NEXT:    bsrl %edx, %edx
; AVX-NEXT:    cmovel %eax, %edx
; AVX-NEXT:    xorl $7, %edx
; AVX-NEXT:    vmovd %edx, %xmm1
; AVX-NEXT:    vpinsrb $1, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $2, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $2, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $3, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $3, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $4, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $4, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $5, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $5, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $6, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $6, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $7, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $7, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $8, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $8, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $9, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $9, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $10, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $10, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $11, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $11, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $12, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $12, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $13, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $13, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $14, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $14, %ecx, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $15, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    cmovel %eax, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vpinsrb $15, %ecx, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv16i8:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vpmovzxbd %xmm0, %zmm0
; AVX512VLCD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512VLCD-NEXT:    vpmovdb %zmm0, %xmm0
; AVX512VLCD-NEXT:    vpsubb {{.*}}(%rip), %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv16i8:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vpmovzxbd %xmm0, %zmm0
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    vpmovdb %zmm0, %xmm0
; AVX512CD-NEXT:    vpsubb {{.*}}(%rip), %xmm0, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <16 x i8> @llvm.ctlz.v16i8(<16 x i8> %in, i1 0)
  ret <16 x i8> %out
}

define <16 x i8> @testv16i8u(<16 x i8> %in) nounwind {
; SSE2-LABEL: testv16i8u:
; SSE2:       # BB#0:
; SSE2-NEXT:    pushq %rbx
; SSE2-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE2-NEXT:    bsrl %esi, %esi
; SSE2-NEXT:    xorl $7, %esi
; SSE2-NEXT:    movd %esi, %xmm1
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSE2-NEXT:    bsrl %ebx, %ebx
; SSE2-NEXT:    xorl $7, %ebx
; SSE2-NEXT:    movd %ebx, %xmm2
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSE2-NEXT:    bsrl %edx, %edx
; SSE2-NEXT:    xorl $7, %edx
; SSE2-NEXT:    movd %edx, %xmm0
; SSE2-NEXT:    bsrl %esi, %edx
; SSE2-NEXT:    xorl $7, %edx
; SSE2-NEXT:    movd %edx, %xmm3
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE2-NEXT:    bsrl %ecx, %ecx
; SSE2-NEXT:    xorl $7, %ecx
; SSE2-NEXT:    movd %ecx, %xmm0
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE2-NEXT:    bsrl %edx, %edx
; SSE2-NEXT:    xorl $7, %edx
; SSE2-NEXT:    movd %edx, %xmm1
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE2-NEXT:    bsrl %edi, %edx
; SSE2-NEXT:    xorl $7, %edx
; SSE2-NEXT:    movd %edx, %xmm0
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE2-NEXT:    bsrl %r10d, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    bsrl %ecx, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm3
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3],xmm3[4],xmm2[4],xmm3[5],xmm2[5],xmm3[6],xmm2[6],xmm3[7],xmm2[7]
; SSE2-NEXT:    bsrl %r9d, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    bsrl %r11d, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm2
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE2-NEXT:    bsrl %r8d, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm4
; SSE2-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE2-NEXT:    bsrl %eax, %eax
; SSE2-NEXT:    xorl $7, %eax
; SSE2-NEXT:    movd %eax, %xmm0
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSE2-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE2-NEXT:    popq %rbx
; SSE2-NEXT:    retq
;
; SSE3-LABEL: testv16i8u:
; SSE3:       # BB#0:
; SSE3-NEXT:    pushq %rbx
; SSE3-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE3-NEXT:    bsrl %esi, %esi
; SSE3-NEXT:    xorl $7, %esi
; SSE3-NEXT:    movd %esi, %xmm1
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSE3-NEXT:    bsrl %ebx, %ebx
; SSE3-NEXT:    xorl $7, %ebx
; SSE3-NEXT:    movd %ebx, %xmm2
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSE3-NEXT:    bsrl %edx, %edx
; SSE3-NEXT:    xorl $7, %edx
; SSE3-NEXT:    movd %edx, %xmm0
; SSE3-NEXT:    bsrl %esi, %edx
; SSE3-NEXT:    xorl $7, %edx
; SSE3-NEXT:    movd %edx, %xmm3
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE3-NEXT:    bsrl %ecx, %ecx
; SSE3-NEXT:    xorl $7, %ecx
; SSE3-NEXT:    movd %ecx, %xmm0
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSE3-NEXT:    bsrl %edx, %edx
; SSE3-NEXT:    xorl $7, %edx
; SSE3-NEXT:    movd %edx, %xmm1
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSE3-NEXT:    bsrl %edi, %edx
; SSE3-NEXT:    xorl $7, %edx
; SSE3-NEXT:    movd %edx, %xmm0
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE3-NEXT:    bsrl %r10d, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    bsrl %ecx, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm3
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3],xmm3[4],xmm2[4],xmm3[5],xmm2[5],xmm3[6],xmm2[6],xmm3[7],xmm2[7]
; SSE3-NEXT:    bsrl %r9d, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    bsrl %r11d, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm2
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSE3-NEXT:    bsrl %r8d, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm4
; SSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSE3-NEXT:    bsrl %eax, %eax
; SSE3-NEXT:    xorl $7, %eax
; SSE3-NEXT:    movd %eax, %xmm0
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSE3-NEXT:    popq %rbx
; SSE3-NEXT:    retq
;
; SSSE3-LABEL: testv16i8u:
; SSSE3:       # BB#0:
; SSSE3-NEXT:    pushq %rbx
; SSSE3-NEXT:    movaps %xmm0, -{{[0-9]+}}(%rsp)
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edi
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r9d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r10d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r8d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSSE3-NEXT:    bsrl %esi, %esi
; SSSE3-NEXT:    xorl $7, %esi
; SSSE3-NEXT:    movd %esi, %xmm1
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %esi
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %r11d
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ebx
; SSSE3-NEXT:    bsrl %ebx, %ebx
; SSSE3-NEXT:    xorl $7, %ebx
; SSSE3-NEXT:    movd %ebx, %xmm2
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1],xmm2[2],xmm1[2],xmm2[3],xmm1[3],xmm2[4],xmm1[4],xmm2[5],xmm1[5],xmm2[6],xmm1[6],xmm2[7],xmm1[7]
; SSSE3-NEXT:    bsrl %edx, %edx
; SSSE3-NEXT:    xorl $7, %edx
; SSSE3-NEXT:    movd %edx, %xmm0
; SSSE3-NEXT:    bsrl %esi, %edx
; SSSE3-NEXT:    xorl $7, %edx
; SSSE3-NEXT:    movd %edx, %xmm3
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSSE3-NEXT:    bsrl %ecx, %ecx
; SSSE3-NEXT:    xorl $7, %ecx
; SSSE3-NEXT:    movd %ecx, %xmm0
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %ecx
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %edx
; SSSE3-NEXT:    bsrl %edx, %edx
; SSSE3-NEXT:    xorl $7, %edx
; SSSE3-NEXT:    movd %edx, %xmm1
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3],xmm1[4],xmm0[4],xmm1[5],xmm0[5],xmm1[6],xmm0[6],xmm1[7],xmm0[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm3[0],xmm1[1],xmm3[1],xmm1[2],xmm3[2],xmm1[3],xmm3[3],xmm1[4],xmm3[4],xmm1[5],xmm3[5],xmm1[6],xmm3[6],xmm1[7],xmm3[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; SSSE3-NEXT:    bsrl %edi, %edx
; SSSE3-NEXT:    xorl $7, %edx
; SSSE3-NEXT:    movd %edx, %xmm0
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSSE3-NEXT:    bsrl %r10d, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    bsrl %ecx, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm3
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm0[0],xmm3[1],xmm0[1],xmm3[2],xmm0[2],xmm3[3],xmm0[3],xmm3[4],xmm0[4],xmm3[5],xmm0[5],xmm3[6],xmm0[6],xmm3[7],xmm0[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm3 = xmm3[0],xmm2[0],xmm3[1],xmm2[1],xmm3[2],xmm2[2],xmm3[3],xmm2[3],xmm3[4],xmm2[4],xmm3[5],xmm2[5],xmm3[6],xmm2[6],xmm3[7],xmm2[7]
; SSSE3-NEXT:    bsrl %r9d, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    bsrl %r11d, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm2
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1],xmm2[2],xmm0[2],xmm2[3],xmm0[3],xmm2[4],xmm0[4],xmm2[5],xmm0[5],xmm2[6],xmm0[6],xmm2[7],xmm0[7]
; SSSE3-NEXT:    bsrl %r8d, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm4
; SSSE3-NEXT:    movzbl -{{[0-9]+}}(%rsp), %eax
; SSSE3-NEXT:    bsrl %eax, %eax
; SSSE3-NEXT:    xorl $7, %eax
; SSSE3-NEXT:    movd %eax, %xmm0
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm4[0],xmm0[1],xmm4[1],xmm0[2],xmm4[2],xmm0[3],xmm4[3],xmm0[4],xmm4[4],xmm0[5],xmm4[5],xmm0[6],xmm4[6],xmm0[7],xmm4[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1],xmm0[2],xmm3[2],xmm0[3],xmm3[3],xmm0[4],xmm3[4],xmm0[5],xmm3[5],xmm0[6],xmm3[6],xmm0[7],xmm3[7]
; SSSE3-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3],xmm0[4],xmm1[4],xmm0[5],xmm1[5],xmm0[6],xmm1[6],xmm0[7],xmm1[7]
; SSSE3-NEXT:    popq %rbx
; SSSE3-NEXT:    retq
;
; SSE41-LABEL: testv16i8u:
; SSE41:       # BB#0:
; SSE41-NEXT:    pextrb $1, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pextrb $0, %xmm0, %ecx
; SSE41-NEXT:    bsrl %ecx, %ecx
; SSE41-NEXT:    xorl $7, %ecx
; SSE41-NEXT:    movd %ecx, %xmm1
; SSE41-NEXT:    pinsrb $1, %eax, %xmm1
; SSE41-NEXT:    pextrb $2, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $2, %eax, %xmm1
; SSE41-NEXT:    pextrb $3, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $3, %eax, %xmm1
; SSE41-NEXT:    pextrb $4, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $4, %eax, %xmm1
; SSE41-NEXT:    pextrb $5, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $5, %eax, %xmm1
; SSE41-NEXT:    pextrb $6, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $6, %eax, %xmm1
; SSE41-NEXT:    pextrb $7, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $7, %eax, %xmm1
; SSE41-NEXT:    pextrb $8, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $8, %eax, %xmm1
; SSE41-NEXT:    pextrb $9, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $9, %eax, %xmm1
; SSE41-NEXT:    pextrb $10, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $10, %eax, %xmm1
; SSE41-NEXT:    pextrb $11, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $11, %eax, %xmm1
; SSE41-NEXT:    pextrb $12, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $12, %eax, %xmm1
; SSE41-NEXT:    pextrb $13, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $13, %eax, %xmm1
; SSE41-NEXT:    pextrb $14, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $14, %eax, %xmm1
; SSE41-NEXT:    pextrb $15, %xmm0, %eax
; SSE41-NEXT:    bsrl %eax, %eax
; SSE41-NEXT:    xorl $7, %eax
; SSE41-NEXT:    pinsrb $15, %eax, %xmm1
; SSE41-NEXT:    movdqa %xmm1, %xmm0
; SSE41-NEXT:    retq
;
; AVX-LABEL: testv16i8u:
; AVX:       # BB#0:
; AVX-NEXT:    vpextrb $1, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpextrb $0, %xmm0, %ecx
; AVX-NEXT:    bsrl %ecx, %ecx
; AVX-NEXT:    xorl $7, %ecx
; AVX-NEXT:    vmovd %ecx, %xmm1
; AVX-NEXT:    vpinsrb $1, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $2, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $2, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $3, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $3, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $4, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $4, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $5, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $5, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $6, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $6, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $7, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $7, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $8, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $8, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $9, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $9, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $10, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $10, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $11, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $11, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $12, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $12, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $13, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $13, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $14, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $14, %eax, %xmm1, %xmm1
; AVX-NEXT:    vpextrb $15, %xmm0, %eax
; AVX-NEXT:    bsrl %eax, %eax
; AVX-NEXT:    xorl $7, %eax
; AVX-NEXT:    vpinsrb $15, %eax, %xmm1, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: testv16i8u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vpmovzxbd %xmm0, %zmm0
; AVX512VLCD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512VLCD-NEXT:    vpmovdb %zmm0, %xmm0
; AVX512VLCD-NEXT:    vpsubb {{.*}}(%rip), %xmm0, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: testv16i8u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vpmovzxbd %xmm0, %zmm0
; AVX512CD-NEXT:    vplzcntd %zmm0, %zmm0
; AVX512CD-NEXT:    vpmovdb %zmm0, %xmm0
; AVX512CD-NEXT:    vpsubb {{.*}}(%rip), %xmm0, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <16 x i8> @llvm.ctlz.v16i8(<16 x i8> %in, i1 -1)
  ret <16 x i8> %out
}

define <2 x i64> @foldv2i64() nounwind {
; SSE-LABEL: foldv2i64:
; SSE:       # BB#0:
; SSE-NEXT:    movl $55, %eax
; SSE-NEXT:    movd %rax, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv2i64:
; AVX:       # BB#0:
; AVX-NEXT:    movl $55, %eax
; AVX-NEXT:    vmovq %rax, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv2i64:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    movl $55, %eax
; AVX512VLCD-NEXT:    vmovq %rax, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv2i64:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    movl $55, %eax
; AVX512CD-NEXT:    vmovq %rax, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <2 x i64> @llvm.ctlz.v2i64(<2 x i64> <i64 256, i64 -1>, i1 0)
  ret <2 x i64> %out
}

define <2 x i64> @foldv2i64u() nounwind {
; SSE-LABEL: foldv2i64u:
; SSE:       # BB#0:
; SSE-NEXT:    movl $55, %eax
; SSE-NEXT:    movd %rax, %xmm0
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv2i64u:
; AVX:       # BB#0:
; AVX-NEXT:    movl $55, %eax
; AVX-NEXT:    vmovq %rax, %xmm0
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv2i64u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    movl $55, %eax
; AVX512VLCD-NEXT:    vmovq %rax, %xmm0
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv2i64u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    movl $55, %eax
; AVX512CD-NEXT:    vmovq %rax, %xmm0
; AVX512CD-NEXT:    retq
  %out = call <2 x i64> @llvm.ctlz.v2i64(<2 x i64> <i64 256, i64 -1>, i1 -1)
  ret <2 x i64> %out
}

define <4 x i32> @foldv4i32() nounwind {
; SSE-LABEL: foldv4i32:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [23,0,32,24]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv4i32:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [23,0,32,24]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv4i32:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa32 {{.*#+}} xmm0 = [23,0,32,24]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv4i32:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [23,0,32,24]
; AVX512CD-NEXT:    retq
  %out = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> <i32 256, i32 -1, i32 0, i32 255>, i1 0)
  ret <4 x i32> %out
}

define <4 x i32> @foldv4i32u() nounwind {
; SSE-LABEL: foldv4i32u:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [23,0,32,24]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv4i32u:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [23,0,32,24]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv4i32u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa32 {{.*#+}} xmm0 = [23,0,32,24]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv4i32u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [23,0,32,24]
; AVX512CD-NEXT:    retq
  %out = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> <i32 256, i32 -1, i32 0, i32 255>, i1 -1)
  ret <4 x i32> %out
}

define <8 x i16> @foldv8i16() nounwind {
; SSE-LABEL: foldv8i16:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv8i16:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv8i16:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa64 {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv8i16:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX512CD-NEXT:    retq
  %out = call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> <i16 256, i16 -1, i16 0, i16 255, i16 -65536, i16 7, i16 24, i16 88>, i1 0)
  ret <8 x i16> %out
}

define <8 x i16> @foldv8i16u() nounwind {
; SSE-LABEL: foldv8i16u:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv8i16u:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv8i16u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa64 {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv8i16u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [7,0,16,8,16,13,11,9]
; AVX512CD-NEXT:    retq
  %out = call <8 x i16> @llvm.ctlz.v8i16(<8 x i16> <i16 256, i16 -1, i16 0, i16 255, i16 -65536, i16 7, i16 24, i16 88>, i1 -1)
  ret <8 x i16> %out
}

define <16 x i8> @foldv16i8() nounwind {
; SSE-LABEL: foldv16i8:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv16i8:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv16i8:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa64 {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv16i8:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX512CD-NEXT:    retq
  %out = call <16 x i8> @llvm.ctlz.v16i8(<16 x i8> <i8 256, i8 -1, i8 0, i8 255, i8 -65536, i8 7, i8 24, i8 88, i8 -2, i8 254, i8 1, i8 2, i8 4, i8 8, i8 16, i8 32>, i1 0)
  ret <16 x i8> %out
}

define <16 x i8> @foldv16i8u() nounwind {
; SSE-LABEL: foldv16i8u:
; SSE:       # BB#0:
; SSE-NEXT:    movaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; SSE-NEXT:    retq
;
; AVX-LABEL: foldv16i8u:
; AVX:       # BB#0:
; AVX-NEXT:    vmovaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX-NEXT:    retq
;
; AVX512VLCD-LABEL: foldv16i8u:
; AVX512VLCD:       ## BB#0:
; AVX512VLCD-NEXT:    vmovdqa64 {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX512VLCD-NEXT:    retq
;
; AVX512CD-LABEL: foldv16i8u:
; AVX512CD:       ## BB#0:
; AVX512CD-NEXT:    vmovaps {{.*#+}} xmm0 = [8,0,8,0,8,5,3,1,0,0,7,6,5,4,3,2]
; AVX512CD-NEXT:    retq
  %out = call <16 x i8> @llvm.ctlz.v16i8(<16 x i8> <i8 256, i8 -1, i8 0, i8 255, i8 -65536, i8 7, i8 24, i8 88, i8 -2, i8 254, i8 1, i8 2, i8 4, i8 8, i8 16, i8 32>, i1 -1)
  ret <16 x i8> %out
}

declare <2 x i64> @llvm.ctlz.v2i64(<2 x i64>, i1)
declare <4 x i32> @llvm.ctlz.v4i32(<4 x i32>, i1)
declare <8 x i16> @llvm.ctlz.v8i16(<8 x i16>, i1)
declare <16 x i8> @llvm.ctlz.v16i8(<16 x i8>, i1)
