; RUN: llc < %s -march=bpf | FileCheck %s

%struct.key_t = type { i32, [16 x i8] }

; Function Attrs: nounwind uwtable
define i32 @test() #0 {
  %key = alloca %struct.key_t, align 4
  %1 = bitcast %struct.key_t* %key to i8*
; CHECK: mov	r1, 0
; CHECK: stw	-8(r10), r1
; CHECK: std	-16(r10), r1
; CHECK: std	-24(r10), r1
  call void @llvm.memset.p0i8.i64(i8* %1, i8 0, i64 20, i32 4, i1 false)
; CHECK: mov	r1, r10
; CHECK: addi	r1, -20
  %2 = getelementptr inbounds %struct.key_t, %struct.key_t* %key, i64 0, i32 1, i64 0
; CHECK: call	test1
  call void @test1(i8* %2) #3
  ret i32 0
}

; Function Attrs: nounwind argmemonly
declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) #1

declare void @test1(i8*) #2
