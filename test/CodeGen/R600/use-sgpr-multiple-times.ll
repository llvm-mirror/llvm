; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI %s

declare float @llvm.fma.f32(float, float, float) #1
declare float @llvm.fmuladd.f32(float, float, float) #1
declare i32 @llvm.AMDGPU.imad24(i32, i32, i32) #1


; SI-LABEL: {{^}}test_sgpr_use_twice_binop:
; SI: s_load_dword [[SGPR:s[0-9]+]],
; SI: v_add_f32_e64 [[RESULT:v[0-9]+]], [[SGPR]], [[SGPR]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_binop(float addrspace(1)* %out, float %a) #0 {
  %dbl = fadd float %a, %a
  store float %dbl, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_three_ternary_op:
; SI: s_load_dword [[SGPR:s[0-9]+]],
; SI: v_fma_f32 [[RESULT:v[0-9]+]], [[SGPR]], [[SGPR]], [[SGPR]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_three_ternary_op(float addrspace(1)* %out, float %a) #0 {
  %fma = call float @llvm.fma.f32(float %a, float %a, float %a) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_a_a_b:
; SI: s_load_dword [[SGPR0:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xb
; SI: s_load_dword [[SGPR1:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xc
; SI: v_mov_b32_e32 [[VGPR1:v[0-9]+]], [[SGPR1]]
; SI: v_fma_f32 [[RESULT:v[0-9]+]], [[SGPR0]], [[SGPR0]], [[VGPR1]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_a_a_b(float addrspace(1)* %out, float %a, float %b) #0 {
  %fma = call float @llvm.fma.f32(float %a, float %a, float %b) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_a_b_a:
; SI: s_load_dword [[SGPR0:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xb
; SI: s_load_dword [[SGPR1:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xc
; SI: v_mov_b32_e32 [[VGPR1:v[0-9]+]], [[SGPR1]]
; SI: v_fma_f32 [[RESULT:v[0-9]+]], [[VGPR1]], [[SGPR0]], [[SGPR0]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_a_b_a(float addrspace(1)* %out, float %a, float %b) #0 {
  %fma = call float @llvm.fma.f32(float %a, float %b, float %a) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_b_a_a:
; SI: s_load_dword [[SGPR0:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xb
; SI: s_load_dword [[SGPR1:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xc
; SI: v_mov_b32_e32 [[VGPR1:v[0-9]+]], [[SGPR1]]
; SI: v_fma_f32 [[RESULT:v[0-9]+]], [[SGPR0]], [[VGPR1]], [[SGPR0]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_b_a_a(float addrspace(1)* %out, float %a, float %b) #0 {
  %fma = call float @llvm.fma.f32(float %b, float %a, float %a) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_a_a_imm:
; SI: s_load_dword [[SGPR:s[0-9]+]]
; SI: v_fma_f32 [[RESULT:v[0-9]+]], [[SGPR]], [[SGPR]], 2.0
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_a_a_imm(float addrspace(1)* %out, float %a) #0 {
  %fma = call float @llvm.fma.f32(float %a, float %a, float 2.0) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_a_imm_a:
; SI: s_load_dword [[SGPR:s[0-9]+]]
; SI: v_fma_f32 [[RESULT:v[0-9]+]], 2.0, [[SGPR]], [[SGPR]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_a_imm_a(float addrspace(1)* %out, float %a) #0 {
  %fma = call float @llvm.fma.f32(float %a, float 2.0, float %a) #1
  store float %fma, float addrspace(1)* %out, align 4
  ret void
}

; Don't use fma since fma c, x, y is canonicalized to fma x, c, y
; SI-LABEL: {{^}}test_sgpr_use_twice_ternary_op_imm_a_a:
; SI: s_load_dword [[SGPR:s[0-9]+]]
; SI: v_mad_i32_i24 [[RESULT:v[0-9]+]], 2, [[SGPR]], [[SGPR]]
; SI: buffer_store_dword [[RESULT]]
define void @test_sgpr_use_twice_ternary_op_imm_a_a(i32 addrspace(1)* %out, i32 %a) #0 {
  %fma = call i32 @llvm.AMDGPU.imad24(i32 2, i32 %a, i32 %a) #1
  store i32 %fma, i32 addrspace(1)* %out, align 4
  ret void
}

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }
