; RUN: llc < %s -mtriple=x86_64-apple-darwin -mattr=+avx | FileCheck %s

define <32 x i8> @funcA(<32 x i8> %a) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcA:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %shuffle = shufflevector <32 x i8> %a, <32 x i8> undef, <32 x i32> <i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5>
  ret <32 x i8> %shuffle
}

define <16 x i16> @funcB(<16 x i16> %a) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcB:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[10,11,10,11,10,11,10,11,10,11,10,11,10,11,10,11]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %shuffle = shufflevector <16 x i16> %a, <16 x i16> undef, <16 x i32> <i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5>
  ret <16 x i16> %shuffle
}

define <4 x i64> @funcC(i64 %q) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcC:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vmovq %rdi, %xmm0
; CHECK-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %vecinit.i = insertelement <4 x i64> undef, i64 %q, i32 0
  %vecinit2.i = insertelement <4 x i64> %vecinit.i, i64 %q, i32 1
  %vecinit4.i = insertelement <4 x i64> %vecinit2.i, i64 %q, i32 2
  %vecinit6.i = insertelement <4 x i64> %vecinit4.i, i64 %q, i32 3
  ret <4 x i64> %vecinit6.i
}

define <4 x double> @funcD(double %q) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcD:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vmovddup {{.*#+}} xmm0 = xmm0[0,0]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %vecinit.i = insertelement <4 x double> undef, double %q, i32 0
  %vecinit2.i = insertelement <4 x double> %vecinit.i, double %q, i32 1
  %vecinit4.i = insertelement <4 x double> %vecinit2.i, double %q, i32 2
  %vecinit6.i = insertelement <4 x double> %vecinit4.i, double %q, i32 3
  ret <4 x double> %vecinit6.i
}

; Test this turns into a broadcast:
;   shuffle (scalar_to_vector (load (ptr + 4))), undef, <0, 0, 0, 0>
;
define <8 x float> @funcE() nounwind {
; CHECK-LABEL: funcE:
; CHECK:       ## BB#0: ## %for_exit499
; CHECK-NEXT:    xorl %eax, %eax
; CHECK-NEXT:    ## implicit-def: %YMM0
; CHECK-NEXT:    testb %al, %al
; CHECK-NEXT:    jne LBB4_2
; CHECK-NEXT:  ## BB#1: ## %load.i1247
; CHECK-NEXT:    pushq %rbp
; CHECK-NEXT:    movq %rsp, %rbp
; CHECK-NEXT:    andq $-32, %rsp
; CHECK-NEXT:    subq $1312, %rsp ## imm = 0x520
; CHECK-NEXT:    vbroadcastss {{[0-9]+}}(%rsp), %ymm0
; CHECK-NEXT:    movq %rbp, %rsp
; CHECK-NEXT:    popq %rbp
; CHECK-NEXT:  LBB4_2: ## %__load_and_broadcast_32.exit1249
; CHECK-NEXT:    retq
allocas:
  %udx495 = alloca [18 x [18 x float]], align 32
  br label %for_test505.preheader

for_test505.preheader:                            ; preds = %for_test505.preheader, %allocas
  br i1 undef, label %for_exit499, label %for_test505.preheader

for_exit499:                                      ; preds = %for_test505.preheader
  br i1 undef, label %__load_and_broadcast_32.exit1249, label %load.i1247

load.i1247:                                       ; preds = %for_exit499
  %ptr1227 = getelementptr [18 x [18 x float]], [18 x [18 x float]]* %udx495, i64 0, i64 1, i64 1
  %ptr.i1237 = bitcast float* %ptr1227 to i32*
  %val.i1238 = load i32, i32* %ptr.i1237, align 4
  %ret6.i1245 = insertelement <8 x i32> undef, i32 %val.i1238, i32 6
  %ret7.i1246 = insertelement <8 x i32> %ret6.i1245, i32 %val.i1238, i32 7
  %phitmp = bitcast <8 x i32> %ret7.i1246 to <8 x float>
  br label %__load_and_broadcast_32.exit1249

__load_and_broadcast_32.exit1249:                 ; preds = %load.i1247, %for_exit499
  %load_broadcast12281250 = phi <8 x float> [ %phitmp, %load.i1247 ], [ undef, %for_exit499 ]
  ret <8 x float> %load_broadcast12281250
}

define <8 x float> @funcF(i32 %val) nounwind {
; CHECK-LABEL: funcF:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vmovd %edi, %xmm0
; CHECK-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,1,0,0]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
  %ret6 = insertelement <8 x i32> undef, i32 %val, i32 6
  %ret7 = insertelement <8 x i32> %ret6, i32 %val, i32 7
  %tmp = bitcast <8 x i32> %ret7 to <8 x float>
  ret <8 x float> %tmp
}

define <8 x float> @funcG(<8 x float> %a) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcG:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[0,0,0,0]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %shuffle = shufflevector <8 x float> %a, <8 x float> undef, <8 x i32> <i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>
  ret <8 x float> %shuffle
}

define <8 x float> @funcH(<8 x float> %a) nounwind uwtable readnone ssp {
; CHECK-LABEL: funcH:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    vextractf128 $1, %ymm0, %xmm0
; CHECK-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[1,1,1,1]
; CHECK-NEXT:    vinsertf128 $1, %xmm0, %ymm0, %ymm0
; CHECK-NEXT:    retq
entry:
  %shuffle = shufflevector <8 x float> %a, <8 x float> undef, <8 x i32> <i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5, i32 5>
  ret <8 x float> %shuffle
}

define <2 x double> @splat_load_2f64_11(<2 x double>* %ptr) {
; CHECK-LABEL: splat_load_2f64_11:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vmovaps (%rdi), %xmm0
; CHECK-NEXT:    vmovhlps {{.*#+}} xmm0 = xmm0[1,1]
; CHECK-NEXT:    retq
  %x = load <2 x double>, <2 x double>* %ptr
  %x1 = shufflevector <2 x double> %x, <2 x double> undef, <2 x i32> <i32 1, i32 1>
  ret <2 x double> %x1
}

define <4 x double> @splat_load_4f64_2222(<4 x double>* %ptr) {
; CHECK-LABEL: splat_load_4f64_2222:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vbroadcastsd 16(%rdi), %ymm0
; CHECK-NEXT:    retq
  %x = load <4 x double>, <4 x double>* %ptr
  %x1 = shufflevector <4 x double> %x, <4 x double> undef, <4 x i32> <i32 2, i32 2, i32 2, i32 2>
  ret <4 x double> %x1
}

define <4 x float> @splat_load_4f32_0000(<4 x float>* %ptr) {
; CHECK-LABEL: splat_load_4f32_0000:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vbroadcastss (%rdi), %xmm0
; CHECK-NEXT:    retq
  %x = load <4 x float>, <4 x float>* %ptr
  %x1 = shufflevector <4 x float> %x, <4 x float> undef, <4 x i32> <i32 0, i32 0, i32 0, i32 0>
  ret <4 x float> %x1
}

define <8 x float> @splat_load_8f32_77777777(<8 x float>* %ptr) {
; CHECK-LABEL: splat_load_8f32_77777777:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vbroadcastss 28(%rdi), %ymm0
; CHECK-NEXT:    retq
  %x = load <8 x float>, <8 x float>* %ptr
  %x1 = shufflevector <8 x float> %x, <8 x float> undef, <8 x i32> <i32 7, i32 7, i32 7, i32 7, i32 7, i32 7, i32 7, i32 7>
  ret <8 x float> %x1
}
