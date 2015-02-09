; RUN: llc < %s -mtriple=x86_64-darwin -x86-experimental-vector-widening-legalization -mattr=+mmx,+sse2 | FileCheck %s

define i32 @test0(<1 x i64>* %v4) {
; CHECK-LABEL: test0:
; CHECK:       ## BB#0:
; CHECK-NEXT:    pshufw $238, (%rdi), %mm0
; CHECK-NEXT:    movd %mm0, %eax
; CHECK-NEXT:    addl $32, %eax
; CHECK-NEXT:    retq
  %v5 = load <1 x i64>* %v4, align 8
  %v12 = bitcast <1 x i64> %v5 to <4 x i16>
  %v13 = bitcast <4 x i16> %v12 to x86_mmx
  %v14 = tail call x86_mmx @llvm.x86.sse.pshuf.w(x86_mmx %v13, i8 -18)
  %v15 = bitcast x86_mmx %v14 to <4 x i16>
  %v16 = bitcast <4 x i16> %v15 to <1 x i64>
  %v17 = extractelement <1 x i64> %v16, i32 0
  %v18 = bitcast i64 %v17 to <2 x i32>
  %v19 = extractelement <2 x i32> %v18, i32 0
  %v20 = add i32 %v19, 32
  ret i32 %v20
}

define i32 @test1(i32* nocapture readonly %ptr) {
; CHECK-LABEL: test1:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    movd (%rdi), %mm0
; CHECK-NEXT:    pshufw $232, %mm0, %mm0
; CHECK-NEXT:    movd %mm0, %eax
; CHECK-NEXT:    emms
; CHECK-NEXT:    retq
entry:
  %0 = load i32* %ptr, align 4
  %1 = insertelement <2 x i32> undef, i32 %0, i32 0
  %2 = insertelement <2 x i32> %1, i32 0, i32 1
  %3 = bitcast <2 x i32> %2 to x86_mmx
  %4 = bitcast x86_mmx %3 to i64
  %5 = bitcast i64 %4 to <4 x i16>
  %6 = bitcast <4 x i16> %5 to x86_mmx
  %7 = tail call x86_mmx @llvm.x86.sse.pshuf.w(x86_mmx %6, i8 -24)
  %8 = bitcast x86_mmx %7 to <4 x i16>
  %9 = bitcast <4 x i16> %8 to <1 x i64>
  %10 = extractelement <1 x i64> %9, i32 0
  %11 = bitcast i64 %10 to <2 x i32>
  %12 = extractelement <2 x i32> %11, i32 0
  tail call void @llvm.x86.mmx.emms()
  ret i32 %12
}

define i32 @test2(i32* nocapture readonly %ptr) {
; CHECK-LABEL: test2:
; CHECK:       ## BB#0: ## %entry
; CHECK-NEXT:    movq (%rdi), %mm0
; CHECK-NEXT:    pshufw $232, %mm0, %mm0
; CHECK-NEXT:    movd %mm0, %eax
; CHECK-NEXT:    emms
; CHECK-NEXT:    retq
entry:
  %0 = bitcast i32* %ptr to x86_mmx*
  %1 = load x86_mmx* %0, align 8
  %2 = tail call x86_mmx @llvm.x86.sse.pshuf.w(x86_mmx %1, i8 -24)
  %3 = bitcast x86_mmx %2 to <4 x i16>
  %4 = bitcast <4 x i16> %3 to <1 x i64>
  %5 = extractelement <1 x i64> %4, i32 0
  %6 = bitcast i64 %5 to <2 x i32>
  %7 = extractelement <2 x i32> %6, i32 0
  tail call void @llvm.x86.mmx.emms()
  ret i32 %7
}

declare x86_mmx @llvm.x86.sse.pshuf.w(x86_mmx, i8)
declare void @llvm.x86.mmx.emms()
