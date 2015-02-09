; These are tests for SSE3 codegen.

; RUN: llc < %s -march=x86-64 -mcpu=nocona -mtriple=i686-apple-darwin9 -O3 | FileCheck %s --check-prefix=X64

; Test for v8xi16 lowering where we extract the first element of the vector and
; placed it in the second element of the result.

define void @t0(<8 x i16>* %dest, <8 x i16>* %old) nounwind {
; X64-LABEL: t0:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    movl $1, %eax
; X64-NEXT:    movd %eax, %xmm0
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],mem[0],xmm0[1],mem[1],xmm0[2],mem[2],xmm0[3],mem[3]
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
entry:
	%tmp3 = load <8 x i16>* %old
	%tmp6 = shufflevector <8 x i16> %tmp3,
                <8 x i16> < i16 1, i16 undef, i16 undef, i16 undef, i16 undef, i16 undef, i16 undef, i16 undef >,
                <8 x i32> < i32 8, i32 0, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef  >
	store <8 x i16> %tmp6, <8 x i16>* %dest
	ret void
}

define <8 x i16> @t1(<8 x i16>* %A, <8 x i16>* %B) nounwind {
; X64-LABEL: t1:
; X64:       ## BB#0:
; X64-NEXT:    movdqa (%rdi), %xmm0
; X64-NEXT:    pshufd {{.*#+}} xmm1 = xmm0[2,3,0,1]
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],mem[0],xmm0[1],mem[1],xmm0[2],mem[2],xmm0[3],mem[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,1,2,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,0,2,3,4,5,6,7]
; X64-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm1[0]
; X64-NEXT:    retq
	%tmp1 = load <8 x i16>* %A
	%tmp2 = load <8 x i16>* %B
	%tmp3 = shufflevector <8 x i16> %tmp1, <8 x i16> %tmp2, <8 x i32> < i32 8, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7 >
	ret <8 x i16> %tmp3

}

define <8 x i16> @t2(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t2:
; X64:       ## BB#0:
; X64-NEXT:    pshufd {{.*#+}} xmm2 = xmm0[2,3,0,1]
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,2,3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[3,2,0,3,4,5,6,7]
; X64-NEXT:    punpcklqdq {{.*#+}} xmm0 = xmm0[0],xmm2[0]
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %B, <8 x i32> < i32 9, i32 1, i32 2, i32 9, i32 4, i32 5, i32 6, i32 7 >
	ret <8 x i16> %tmp
}

define <8 x i16> @t3(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t3:
; X64:       ## BB#0:
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,5,6,5]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,3,2,1,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,7,6,5,4]
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %A, <8 x i32> < i32 8, i32 3, i32 2, i32 13, i32 7, i32 6, i32 5, i32 4 >
	ret <8 x i16> %tmp
}

define <8 x i16> @t4(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t4:
; X64:       ## BB#0:
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[2,1,0,3]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,5,4,7]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,0]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,7,4,7]
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %B, <8 x i32> < i32 0, i32 7, i32 2, i32 3, i32 1, i32 5, i32 6, i32 5 >
	ret <8 x i16> %tmp
}

define <8 x i16> @t5(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t5:
; X64:       ## BB#0:
; X64-NEXT:    punpckldq {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
; X64-NEXT:    movdqa %xmm1, %xmm0
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %B, <8 x i32> < i32 8, i32 9, i32 0, i32 1, i32 10, i32 11, i32 2, i32 3 >
	ret <8 x i16> %tmp
}

define <8 x i16> @t6(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t6:
; X64:       ## BB#0:
; X64-NEXT:    movss %xmm1, %xmm0
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %B, <8 x i32> < i32 8, i32 9, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7 >
	ret <8 x i16> %tmp
}

define <8 x i16> @t7(<8 x i16> %A, <8 x i16> %B) nounwind {
; X64-LABEL: t7:
; X64:       ## BB#0:
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,0,3,2,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,4,7]
; X64-NEXT:    retq
	%tmp = shufflevector <8 x i16> %A, <8 x i16> %B, <8 x i32> < i32 0, i32 0, i32 3, i32 2, i32 4, i32 6, i32 4, i32 7 >
	ret <8 x i16> %tmp
}

define void @t8(<2 x i64>* %res, <2 x i64>* %A) nounwind {
; X64-LABEL: t8:
; X64:       ## BB#0:
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = mem[2,1,0,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,5,4,7]
; X64-NEXT:    movdqa %xmm0, (%rdi)
; X64-NEXT:    retq
	%tmp = load <2 x i64>* %A
	%tmp.upgrd.1 = bitcast <2 x i64> %tmp to <8 x i16>
	%tmp0 = extractelement <8 x i16> %tmp.upgrd.1, i32 0
	%tmp1 = extractelement <8 x i16> %tmp.upgrd.1, i32 1
	%tmp2 = extractelement <8 x i16> %tmp.upgrd.1, i32 2
	%tmp3 = extractelement <8 x i16> %tmp.upgrd.1, i32 3
	%tmp4 = extractelement <8 x i16> %tmp.upgrd.1, i32 4
	%tmp5 = extractelement <8 x i16> %tmp.upgrd.1, i32 5
	%tmp6 = extractelement <8 x i16> %tmp.upgrd.1, i32 6
	%tmp7 = extractelement <8 x i16> %tmp.upgrd.1, i32 7
	%tmp8 = insertelement <8 x i16> undef, i16 %tmp2, i32 0
	%tmp9 = insertelement <8 x i16> %tmp8, i16 %tmp1, i32 1
	%tmp10 = insertelement <8 x i16> %tmp9, i16 %tmp0, i32 2
	%tmp11 = insertelement <8 x i16> %tmp10, i16 %tmp3, i32 3
	%tmp12 = insertelement <8 x i16> %tmp11, i16 %tmp6, i32 4
	%tmp13 = insertelement <8 x i16> %tmp12, i16 %tmp5, i32 5
	%tmp14 = insertelement <8 x i16> %tmp13, i16 %tmp4, i32 6
	%tmp15 = insertelement <8 x i16> %tmp14, i16 %tmp7, i32 7
	%tmp15.upgrd.2 = bitcast <8 x i16> %tmp15 to <2 x i64>
	store <2 x i64> %tmp15.upgrd.2, <2 x i64>* %res
	ret void
}

define void @t9(<4 x float>* %r, <2 x i32>* %A) nounwind {
; X64-LABEL: t9:
; X64:       ## BB#0:
; X64-NEXT:    movapd (%rdi), %xmm0
; X64-NEXT:    movhpd (%rsi), %xmm0
; X64-NEXT:    movapd %xmm0, (%rdi)
; X64-NEXT:    retq
	%tmp = load <4 x float>* %r
	%tmp.upgrd.3 = bitcast <2 x i32>* %A to double*
	%tmp.upgrd.4 = load double* %tmp.upgrd.3
	%tmp.upgrd.5 = insertelement <2 x double> undef, double %tmp.upgrd.4, i32 0
	%tmp5 = insertelement <2 x double> %tmp.upgrd.5, double undef, i32 1
	%tmp6 = bitcast <2 x double> %tmp5 to <4 x float>
	%tmp.upgrd.6 = extractelement <4 x float> %tmp, i32 0
	%tmp7 = extractelement <4 x float> %tmp, i32 1
	%tmp8 = extractelement <4 x float> %tmp6, i32 0
	%tmp9 = extractelement <4 x float> %tmp6, i32 1
	%tmp10 = insertelement <4 x float> undef, float %tmp.upgrd.6, i32 0
	%tmp11 = insertelement <4 x float> %tmp10, float %tmp7, i32 1
	%tmp12 = insertelement <4 x float> %tmp11, float %tmp8, i32 2
	%tmp13 = insertelement <4 x float> %tmp12, float %tmp9, i32 3
	store <4 x float> %tmp13, <4 x float>* %r
	ret void
}



; FIXME: This testcase produces icky code. It can be made much better!
; PR2585

@g1 = external constant <4 x i32>
@g2 = external constant <4 x i16>

define void @t10() nounwind {
; X64-LABEL: t10:
; X64:       ## BB#0:
; X64-NEXT:    movq _g1@{{.*}}(%rip), %rax
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = mem[0,2,2,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,6,6,7]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X64-NEXT:    movq _g2@{{.*}}(%rip), %rax
; X64-NEXT:    movq %xmm0, (%rax)
; X64-NEXT:    retq
  load <4 x i32>* @g1, align 16
  bitcast <4 x i32> %1 to <8 x i16>
  shufflevector <8 x i16> %2, <8 x i16> undef, <8 x i32> < i32 0, i32 2, i32 4, i32 6, i32 undef, i32 undef, i32 undef, i32 undef >
  bitcast <8 x i16> %3 to <2 x i64>
  extractelement <2 x i64> %4, i32 0
  bitcast i64 %5 to <4 x i16>
  store <4 x i16> %6, <4 x i16>* @g2, align 8
  ret void
}

; Pack various elements via shuffles.
define <8 x i16> @t11(<8 x i16> %T0, <8 x i16> %T1) nounwind readnone {
; X64-LABEL: t11:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[2,1,2,3,4,5,6,7]
; X64-NEXT:    retq
entry:
	%tmp7 = shufflevector <8 x i16> %T0, <8 x i16> %T1, <8 x i32> < i32 1, i32 8, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef , i32 undef >
	ret <8 x i16> %tmp7

}

define <8 x i16> @t12(<8 x i16> %T0, <8 x i16> %T1) nounwind readnone {
; X64-LABEL: t12:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,2,2,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,7,6,7]
; X64-NEXT:    retq
entry:
	%tmp9 = shufflevector <8 x i16> %T0, <8 x i16> %T1, <8 x i32> < i32 0, i32 1, i32 undef, i32 undef, i32 3, i32 11, i32 undef , i32 undef >
	ret <8 x i16> %tmp9

}

define <8 x i16> @t13(<8 x i16> %T0, <8 x i16> %T1) nounwind readnone {
; X64-LABEL: t13:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,6,7,6,7]
; X64-NEXT:    retq
entry:
	%tmp9 = shufflevector <8 x i16> %T0, <8 x i16> %T1, <8 x i32> < i32 8, i32 9, i32 undef, i32 undef, i32 11, i32 3, i32 undef , i32 undef >
	ret <8 x i16> %tmp9
}

define <8 x i16> @t14(<8 x i16> %T0, <8 x i16> %T1) nounwind readnone {
; X64-LABEL: t14:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; X64-NEXT:    retq
entry:
	%tmp9 = shufflevector <8 x i16> %T0, <8 x i16> %T1, <8 x i32> < i32 8, i32 9, i32 undef, i32 undef, i32 undef, i32 2, i32 undef , i32 undef >
	ret <8 x i16> %tmp9
}

; FIXME: t15 is worse off from disabling of scheduler 2-address hack.
define <8 x i16> @t15(<8 x i16> %T0, <8 x i16> %T1) nounwind readnone {
; X64-LABEL: t15:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[3,1,2,3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[1,1,2,3,4,5,6,7]
; X64-NEXT:    punpcklwd {{.*#+}} xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1],xmm0[2],xmm1[2],xmm0[3],xmm1[3]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,0,3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm0[0,1,0,2,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,5,5,6,7]
; X64-NEXT:    retq
entry:
  %tmp8 = shufflevector <8 x i16> %T0, <8 x i16> %T1, <8 x i32> < i32 undef, i32 undef, i32 7, i32 2, i32 8, i32 undef, i32 undef , i32 undef >
  ret <8 x i16> %tmp8
}

; Test yonah where we convert a shuffle to pextrw and pinrsw
define <16 x i8> @t16(<16 x i8> %T0) nounwind readnone {
; X64-LABEL: t16:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    movdqa {{.*#+}} xmm1 = [0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0]
; X64-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; X64-NEXT:    pxor %xmm2, %xmm2
; X64-NEXT:    punpcklbw {{.*#+}} xmm1 = xmm1[0],xmm2[0],xmm1[1],xmm2[1],xmm1[2],xmm2[2],xmm1[3],xmm2[3],xmm1[4],xmm2[4],xmm1[5],xmm2[5],xmm1[6],xmm2[6],xmm1[7],xmm2[7]
; X64-NEXT:    punpcklbw {{.*#+}} xmm0 = xmm0[0],xmm2[0],xmm0[1],xmm2[1],xmm0[2],xmm2[2],xmm0[3],xmm2[3],xmm0[4],xmm2[4],xmm0[5],xmm2[5],xmm0[6],xmm2[6],xmm0[7],xmm2[7]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,0,1,1]
; X64-NEXT:    punpcklwd {{.*#+}} xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1],xmm1[2],xmm0[2],xmm1[3],xmm0[3]
; X64-NEXT:    pshuflw {{.*#+}} xmm0 = xmm1[0,2,2,3,4,5,6,7]
; X64-NEXT:    pshufhw {{.*#+}} xmm0 = xmm0[0,1,2,3,4,7,6,7]
; X64-NEXT:    pshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X64-NEXT:    packuswb %xmm0, %xmm0
; X64-NEXT:    retq
entry:
  %tmp8 = shufflevector <16 x i8> <i8 0, i8 0, i8 0, i8 0, i8 1, i8 1, i8 1, i8 1, i8 0, i8 0, i8 0, i8 0,  i8 0, i8 0, i8 0, i8 0>, <16 x i8> %T0, <16 x i32> < i32 0, i32 1, i32 16, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef , i32 undef >
  %tmp9 = shufflevector <16 x i8> %tmp8, <16 x i8> %T0,  <16 x i32> < i32 0, i32 1, i32 2, i32 17,  i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef , i32 undef >
  ret <16 x i8> %tmp9
}

; rdar://8520311
define <4 x i32> @t17() nounwind {
; X64-LABEL: t17:
; X64:       ## BB#0: ## %entry
; X64-NEXT:    movddup (%rax), %xmm0
; X64-NEXT:    andpd {{.*}}(%rip), %xmm0
; X64-NEXT:    retq
entry:
  %tmp1 = load <4 x float>* undef, align 16
  %tmp2 = shufflevector <4 x float> %tmp1, <4 x float> undef, <4 x i32> <i32 4, i32 1, i32 2, i32 3>
  %tmp3 = load <4 x float>* undef, align 16
  %tmp4 = shufflevector <4 x float> %tmp2, <4 x float> undef, <4 x i32> <i32 undef, i32 undef, i32 0, i32 1>
  %tmp5 = bitcast <4 x float> %tmp3 to <4 x i32>
  %tmp6 = shufflevector <4 x i32> %tmp5, <4 x i32> undef, <4 x i32> <i32 undef, i32 undef, i32 0, i32 1>
  %tmp7 = and <4 x i32> %tmp6, <i32 undef, i32 undef, i32 -1, i32 0>
  ret <4 x i32> %tmp7
}
