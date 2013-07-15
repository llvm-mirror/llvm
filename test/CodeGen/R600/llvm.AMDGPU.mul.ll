;RUN: llc < %s -march=r600 -mcpu=redwood | FileCheck %s

;CHECK: MUL NON-IEEE * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

define void @test() {
   %r0 = call float @llvm.R600.load.input(i32 0)
   %r1 = call float @llvm.R600.load.input(i32 1)
   %r2 = call float @llvm.AMDGPU.mul( float %r0, float %r1)
   call void @llvm.AMDGPU.store.output(float %r2, i32 0)
   ret void
}

declare float @llvm.R600.load.input(i32) readnone

declare void @llvm.AMDGPU.store.output(float, i32)

declare float @llvm.AMDGPU.mul(float ,float ) readnone
