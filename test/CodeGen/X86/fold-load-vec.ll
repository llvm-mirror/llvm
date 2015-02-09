; RUN: llc < %s -march=x86-64 -mcpu=corei7 -mattr=+sse4.1 | FileCheck %s

; rdar://12721174
; We should not fold movss into pshufd since pshufd expects m128 while movss
; loads from m32.
define void @sample_test(<4 x float>* %source, <2 x float>* %dest) nounwind {
; CHECK: sample_test
; CHECK-NOT: movaps
; CHECK: insertps
entry:
  %source.addr = alloca <4 x float>*, align 8
  %dest.addr = alloca <2 x float>*, align 8
  %tmp = alloca <2 x float>, align 8
  store <4 x float>* %source, <4 x float>** %source.addr, align 8
  store <2 x float>* %dest, <2 x float>** %dest.addr, align 8
  store <2 x float> zeroinitializer, <2 x float>* %tmp, align 8
  %0 = load <4 x float>** %source.addr, align 8
  %arrayidx = getelementptr inbounds <4 x float>* %0, i64 0
  %1 = load <4 x float>* %arrayidx, align 16
  %2 = extractelement <4 x float> %1, i32 0
  %3 = load <2 x float>* %tmp, align 8
  %4 = insertelement <2 x float> %3, float %2, i32 1
  store <2 x float> %4, <2 x float>* %tmp, align 8
  %5 = load <2 x float>* %tmp, align 8
  %6 = load <2 x float>** %dest.addr, align 8
  %arrayidx1 = getelementptr inbounds <2 x float>* %6, i64 0
  store <2 x float> %5, <2 x float>* %arrayidx1, align 8
  %7 = load <2 x float>** %dest.addr, align 8
  %arrayidx2 = getelementptr inbounds <2 x float>* %7, i64 0
  %8 = load <2 x float>* %arrayidx2, align 8
  %vecext = extractelement <2 x float> %8, i32 0
  %9 = load <2 x float>** %dest.addr, align 8
  %arrayidx3 = getelementptr inbounds <2 x float>* %9, i64 0
  %10 = load <2 x float>* %arrayidx3, align 8
  %vecext4 = extractelement <2 x float> %10, i32 1
  call void @ext(float %vecext, float %vecext4)
  ret void
}
declare void @ext(float, float)
