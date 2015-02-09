; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck %s

; CHECK: FLOOR * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
define void @test(<4 x float> inreg %reg0) #0 {
   %r0 = extractelement <4 x float> %reg0, i32 0
   %r1 = call float @floor(float %r0)
   %vec = insertelement <4 x float> undef, float %r1, i32 0
   call void @llvm.R600.store.swizzle(<4 x float> %vec, i32 0, i32 0)
   ret void
}

declare float @floor(float) readonly
declare void @llvm.R600.store.swizzle(<4 x float>, i32, i32)

attributes #0 = { "ShaderType"="0" }
