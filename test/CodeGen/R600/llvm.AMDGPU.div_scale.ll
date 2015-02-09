; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI %s

declare i32 @llvm.r600.read.tidig.x() nounwind readnone
declare { float, i1 } @llvm.AMDGPU.div.scale.f32(float, float, i1) nounwind readnone
declare { double, i1 } @llvm.AMDGPU.div.scale.f64(double, double, i1) nounwind readnone

; SI-LABEL @test_div_scale_f32_1:
; SI-DAG: buffer_load_dword [[A:v[0-9]+]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64
; SI-DAG: buffer_load_dword [[B:v[0-9]+]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64 offset:4
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_1(float addrspace(1)* %out, float addrspace(1)* %in) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.0 = getelementptr float addrspace(1)* %in, i32 %tid
  %gep.1 = getelementptr float addrspace(1)* %gep.0, i32 1

  %a = load float addrspace(1)* %gep.0, align 4
  %b = load float addrspace(1)* %gep.1, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 false) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f32_2:
; SI-DAG: buffer_load_dword [[A:v[0-9]+]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64
; SI-DAG: buffer_load_dword [[B:v[0-9]+]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64 offset:4
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_2(float addrspace(1)* %out, float addrspace(1)* %in) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.0 = getelementptr float addrspace(1)* %in, i32 %tid
  %gep.1 = getelementptr float addrspace(1)* %gep.0, i32 1

  %a = load float addrspace(1)* %gep.0, align 4
  %b = load float addrspace(1)* %gep.1, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 true) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f64_1:
; SI-DAG: buffer_load_dwordx2 [[A:v\[[0-9]+:[0-9]+\]]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64
; SI-DAG: buffer_load_dwordx2 [[B:v\[[0-9]+:[0-9]+\]]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64 offset:8
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_1(double addrspace(1)* %out, double addrspace(1)* %aptr, double addrspace(1)* %in) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.0 = getelementptr double addrspace(1)* %in, i32 %tid
  %gep.1 = getelementptr double addrspace(1)* %gep.0, i32 1

  %a = load double addrspace(1)* %gep.0, align 8
  %b = load double addrspace(1)* %gep.1, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 false) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f64_1:
; SI-DAG: buffer_load_dwordx2 [[A:v\[[0-9]+:[0-9]+\]]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64
; SI-DAG: buffer_load_dwordx2 [[B:v\[[0-9]+:[0-9]+\]]], {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0 addr64 offset:8
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_2(double addrspace(1)* %out, double addrspace(1)* %aptr, double addrspace(1)* %in) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.0 = getelementptr double addrspace(1)* %in, i32 %tid
  %gep.1 = getelementptr double addrspace(1)* %gep.0, i32 1

  %a = load double addrspace(1)* %gep.0, align 8
  %b = load double addrspace(1)* %gep.1, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 true) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f32_scalar_num_1:
; SI-DAG: buffer_load_dword [[B:v[0-9]+]]
; SI-DAG: s_load_dword [[A:s[0-9]+]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_scalar_num_1(float addrspace(1)* %out, float addrspace(1)* %in, float %a) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr float addrspace(1)* %in, i32 %tid

  %b = load float addrspace(1)* %gep, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 false) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f32_scalar_num_2:
; SI-DAG: buffer_load_dword [[B:v[0-9]+]]
; SI-DAG: s_load_dword [[A:s[0-9]+]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_scalar_num_2(float addrspace(1)* %out, float addrspace(1)* %in, float %a) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr float addrspace(1)* %in, i32 %tid

  %b = load float addrspace(1)* %gep, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 true) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f32_scalar_den_1:
; SI-DAG: buffer_load_dword [[A:v[0-9]+]]
; SI-DAG: s_load_dword [[B:s[0-9]+]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_scalar_den_1(float addrspace(1)* %out, float addrspace(1)* %in, float %b) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr float addrspace(1)* %in, i32 %tid

  %a = load float addrspace(1)* %gep, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 false) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f32_scalar_den_2:
; SI-DAG: buffer_load_dword [[A:v[0-9]+]]
; SI-DAG: s_load_dword [[B:s[0-9]+]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_scalar_den_2(float addrspace(1)* %out, float addrspace(1)* %in, float %b) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr float addrspace(1)* %in, i32 %tid

  %a = load float addrspace(1)* %gep, align 4

  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 true) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f64_scalar_num_1:
; SI-DAG: buffer_load_dwordx2 [[B:v\[[0-9]+:[0-9]+\]]]
; SI-DAG: s_load_dwordx2 [[A:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_scalar_num_1(double addrspace(1)* %out, double addrspace(1)* %in, double %a) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr double addrspace(1)* %in, i32 %tid

  %b = load double addrspace(1)* %gep, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 false) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f64_scalar_num_2:
; SI-DAG: s_load_dwordx2 [[A:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI-DAG: buffer_load_dwordx2 [[B:v\[[0-9]+:[0-9]+\]]]
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_scalar_num_2(double addrspace(1)* %out, double addrspace(1)* %in, double %a) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr double addrspace(1)* %in, i32 %tid

  %b = load double addrspace(1)* %gep, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 true) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f64_scalar_den_1:
; SI-DAG: buffer_load_dwordx2 [[A:v\[[0-9]+:[0-9]+\]]]
; SI-DAG: s_load_dwordx2 [[B:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_scalar_den_1(double addrspace(1)* %out, double addrspace(1)* %in, double %b) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr double addrspace(1)* %in, i32 %tid

  %a = load double addrspace(1)* %gep, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 false) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f64_scalar_den_2:
; SI-DAG: buffer_load_dwordx2 [[A:v\[[0-9]+:[0-9]+\]]]
; SI-DAG: s_load_dwordx2 [[B:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[B]], [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_scalar_den_2(double addrspace(1)* %out, double addrspace(1)* %in, double %b) nounwind {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep = getelementptr double addrspace(1)* %in, i32 %tid

  %a = load double addrspace(1)* %gep, align 8

  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 true) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f32_all_scalar_1:
; SI-DAG: s_load_dword [[A:s[0-9]+]], {{s\[[0-9]+:[0-9]+\]}}, 0xb
; SI-DAG: s_load_dword [[B:s[0-9]+]], {{s\[[0-9]+:[0-9]+\]}}, 0xc
; SI: v_mov_b32_e32 [[VA:v[0-9]+]], [[A]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], [[VA]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_all_scalar_1(float addrspace(1)* %out, float %a, float %b) nounwind {
  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 false) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f32_all_scalar_2:
; SI-DAG: s_load_dword [[A:s[0-9]+]], {{s\[[0-9]+:[0-9]+\]}}, 0xb
; SI-DAG: s_load_dword [[B:s[0-9]+]], {{s\[[0-9]+:[0-9]+\]}}, 0xc
; SI: v_mov_b32_e32 [[VB:v[0-9]+]], [[B]]
; SI: v_div_scale_f32 [[RESULT0:v[0-9]+]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], [[VB]], [[A]]
; SI: buffer_store_dword [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f32_all_scalar_2(float addrspace(1)* %out, float %a, float %b) nounwind {
  %result = call { float, i1 } @llvm.AMDGPU.div.scale.f32(float %a, float %b, i1 true) nounwind readnone
  %result0 = extractvalue { float, i1 } %result, 0
  store float %result0, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL @test_div_scale_f64_all_scalar_1:
; SI-DAG: s_load_dwordx2 s{{\[}}[[A_LO:[0-9]+]]:[[A_HI:[0-9]+]]{{\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0xb
; SI-DAG: s_load_dwordx2 [[B:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI-DAG: v_mov_b32_e32 v[[VA_LO:[0-9]+]], s[[A_LO]]
; SI-DAG: v_mov_b32_e32 v[[VA_HI:[0-9]+]], s[[A_HI]]
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[B]], [[B]], v{{\[}}[[VA_LO]]:[[VA_HI]]{{\]}}
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_all_scalar_1(double addrspace(1)* %out, double %a, double %b) nounwind {
  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 false) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}

; SI-LABEL @test_div_scale_f64_all_scalar_2:
; SI-DAG: s_load_dwordx2 [[A:s\[[0-9]+:[0-9]+\]]], {{s\[[0-9]+:[0-9]+\]}}, 0xb
; SI-DAG: s_load_dwordx2 s{{\[}}[[B_LO:[0-9]+]]:[[B_HI:[0-9]+]]{{\]}}, {{s\[[0-9]+:[0-9]+\]}}, 0xd
; SI-DAG: v_mov_b32_e32 v[[VB_LO:[0-9]+]], s[[B_LO]]
; SI-DAG: v_mov_b32_e32 v[[VB_HI:[0-9]+]], s[[B_HI]]
; SI: v_div_scale_f64 [[RESULT0:v\[[0-9]+:[0-9]+\]]], [[RESULT1:s\[[0-9]+:[0-9]+\]]], [[A]], v{{\[}}[[VB_LO]]:[[VB_HI]]{{\]}}, [[A]]
; SI: buffer_store_dwordx2 [[RESULT0]]
; SI: s_endpgm
define void @test_div_scale_f64_all_scalar_2(double addrspace(1)* %out, double %a, double %b) nounwind {
  %result = call { double, i1 } @llvm.AMDGPU.div.scale.f64(double %a, double %b, i1 true) nounwind readnone
  %result0 = extractvalue { double, i1 } %result, 0
  store double %result0, double addrspace(1)* %out, align 8
  ret void
}
