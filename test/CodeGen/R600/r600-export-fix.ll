; RUN: llc < %s -march=r600 -mcpu=cedar | FileCheck %s

;CHECK:	EXPORT T{{[0-9]}}.XYZW
;CHECK:	EXPORT T{{[0-9]}}.0000
;CHECK: EXPORT T{{[0-9]}}.0000
;CHECK: EXPORT T{{[0-9]}}.0XYZ
;CHECK: EXPORT T{{[0-9]}}.XYZW
;CHECK: EXPORT T{{[0-9]}}.YZ00
;CHECK: EXPORT T{{[0-9]}}.0000
;CHECK: EXPORT T{{[0-9]}}.0000


define void @main(<4 x float> inreg %reg0, <4 x float> inreg %reg1) #0 {
main_body:
  %0 = extractelement <4 x float> %reg1, i32 0
  %1 = extractelement <4 x float> %reg1, i32 1
  %2 = extractelement <4 x float> %reg1, i32 2
  %3 = extractelement <4 x float> %reg1, i32 3
  %4 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 4)
  %5 = extractelement <4 x float> %4, i32 0
  %6 = fmul float %5, %0
  %7 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 4)
  %8 = extractelement <4 x float> %7, i32 1
  %9 = fmul float %8, %0
  %10 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 4)
  %11 = extractelement <4 x float> %10, i32 2
  %12 = fmul float %11, %0
  %13 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 4)
  %14 = extractelement <4 x float> %13, i32 3
  %15 = fmul float %14, %0
  %16 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 5)
  %17 = extractelement <4 x float> %16, i32 0
  %18 = fmul float %17, %1
  %19 = fadd float %18, %6
  %20 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 5)
  %21 = extractelement <4 x float> %20, i32 1
  %22 = fmul float %21, %1
  %23 = fadd float %22, %9
  %24 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 5)
  %25 = extractelement <4 x float> %24, i32 2
  %26 = fmul float %25, %1
  %27 = fadd float %26, %12
  %28 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 5)
  %29 = extractelement <4 x float> %28, i32 3
  %30 = fmul float %29, %1
  %31 = fadd float %30, %15
  %32 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 6)
  %33 = extractelement <4 x float> %32, i32 0
  %34 = fmul float %33, %2
  %35 = fadd float %34, %19
  %36 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 6)
  %37 = extractelement <4 x float> %36, i32 1
  %38 = fmul float %37, %2
  %39 = fadd float %38, %23
  %40 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 6)
  %41 = extractelement <4 x float> %40, i32 2
  %42 = fmul float %41, %2
  %43 = fadd float %42, %27
  %44 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 6)
  %45 = extractelement <4 x float> %44, i32 3
  %46 = fmul float %45, %2
  %47 = fadd float %46, %31
  %48 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 7)
  %49 = extractelement <4 x float> %48, i32 0
  %50 = fmul float %49, %3
  %51 = fadd float %50, %35
  %52 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 7)
  %53 = extractelement <4 x float> %52, i32 1
  %54 = fmul float %53, %3
  %55 = fadd float %54, %39
  %56 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 7)
  %57 = extractelement <4 x float> %56, i32 2
  %58 = fmul float %57, %3
  %59 = fadd float %58, %43
  %60 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 7)
  %61 = extractelement <4 x float> %60, i32 3
  %62 = fmul float %61, %3
  %63 = fadd float %62, %47
  %64 = load <4 x float> addrspace(8)* null
  %65 = extractelement <4 x float> %64, i32 0
  %66 = load <4 x float> addrspace(8)* null
  %67 = extractelement <4 x float> %66, i32 1
  %68 = load <4 x float> addrspace(8)* null
  %69 = extractelement <4 x float> %68, i32 2
  %70 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 2)
  %71 = extractelement <4 x float> %70, i32 0
  %72 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 2)
  %73 = extractelement <4 x float> %72, i32 1
  %74 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 2)
  %75 = extractelement <4 x float> %74, i32 2
  %76 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 3)
  %77 = extractelement <4 x float> %76, i32 0
  %78 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 3)
  %79 = extractelement <4 x float> %78, i32 1
  %80 = load <4 x float> addrspace(8)* getelementptr ([1024 x <4 x float>] addrspace(8)* null, i64 0, i32 3)
  %81 = extractelement <4 x float> %80, i32 2
  %82 = insertelement <4 x float> undef, float %51, i32 0
  %83 = insertelement <4 x float> %82, float %55, i32 1
  %84 = insertelement <4 x float> %83, float %59, i32 2
  %85 = insertelement <4 x float> %84, float %63, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %85, i32 60, i32 1)
  %86 = insertelement <4 x float> undef, float 0.000000e+00, i32 0
  %87 = insertelement <4 x float> %86, float 0.000000e+00, i32 1
  %88 = insertelement <4 x float> %87, float 0.000000e+00, i32 2
  %89 = insertelement <4 x float> %88, float 0.000000e+00, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %89, i32 0, i32 2)
  %90 = insertelement <4 x float> undef, float 0.000000e+00, i32 0
  %91 = insertelement <4 x float> %90, float 0.000000e+00, i32 1
  %92 = insertelement <4 x float> %91, float 0.000000e+00, i32 2
  %93 = insertelement <4 x float> %92, float 0.000000e+00, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %93, i32 1, i32 2)
  %94 = insertelement <4 x float> undef, float 0.000000e+00, i32 0
  %95 = insertelement <4 x float> %94, float %65, i32 1
  %96 = insertelement <4 x float> %95, float %67, i32 2
  %97 = insertelement <4 x float> %96, float %69, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %97, i32 2, i32 2)
  %98 = insertelement <4 x float> undef, float %77, i32 0
  %99 = insertelement <4 x float> %98, float %79, i32 1
  %100 = insertelement <4 x float> %99, float %81, i32 2
  %101 = insertelement <4 x float> %100, float %71, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %101, i32 3, i32 2)
  %102 = insertelement <4 x float> undef, float %73, i32 0
  %103 = insertelement <4 x float> %102, float %75, i32 1
  %104 = insertelement <4 x float> %103, float 0.000000e+00, i32 2
  %105 = insertelement <4 x float> %104, float 0.000000e+00, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %105, i32 4, i32 2)
  %106 = insertelement <4 x float> undef, float 0.000000e+00, i32 0
  %107 = insertelement <4 x float> %106, float 0.000000e+00, i32 1
  %108 = insertelement <4 x float> %107, float 0.000000e+00, i32 2
  %109 = insertelement <4 x float> %108, float 0.000000e+00, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %109, i32 5, i32 2)
  %110 = insertelement <4 x float> undef, float 0.000000e+00, i32 0
  %111 = insertelement <4 x float> %110, float 0.000000e+00, i32 1
  %112 = insertelement <4 x float> %111, float 0.000000e+00, i32 2
  %113 = insertelement <4 x float> %112, float 0.000000e+00, i32 3
  call void @llvm.R600.store.swizzle(<4 x float> %113, i32 6, i32 2)
  ret void
}

declare void @llvm.R600.store.swizzle(<4 x float>, i32, i32)

attributes #0 = { "ShaderType"="1" }
