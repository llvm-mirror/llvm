; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=r600 -mcpu=cypress -verify-machineinstrs < %s | FileCheck -check-prefix=EG -check-prefix=FUNC %s


declare float @llvm.copysign.f32(float, float) nounwind readnone
declare <2 x float> @llvm.copysign.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <4 x float> @llvm.copysign.v4f32(<4 x float>, <4 x float>) nounwind readnone

; Try to identify arg based on higher address.
; FUNC-LABEL: {{^}}test_copysign_f32:
; SI: s_load_dword [[SMAG:s[0-9]+]], {{.*}} 0xb
; SI: s_load_dword [[SSIGN:s[0-9]+]], {{.*}} 0xc
; SI-DAG: v_mov_b32_e32 [[VSIGN:v[0-9]+]], [[SSIGN]]
; SI-DAG: v_mov_b32_e32 [[VMAG:v[0-9]+]], [[SMAG]]
; SI-DAG: s_mov_b32 [[SCONST:s[0-9]+]], 0x7fffffff
; SI: v_bfi_b32 [[RESULT:v[0-9]+]], [[SCONST]], [[VMAG]], [[VSIGN]]
; SI: buffer_store_dword [[RESULT]],
; SI: s_endpgm

; EG: BFI_INT
define void @test_copysign_f32(float addrspace(1)* %out, float %mag, float %sign) nounwind {
  %result = call float @llvm.copysign.f32(float %mag, float %sign)
  store float %result, float addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}test_copysign_v2f32:
; SI: s_endpgm

; EG: BFI_INT
; EG: BFI_INT
define void @test_copysign_v2f32(<2 x float> addrspace(1)* %out, <2 x float> %mag, <2 x float> %sign) nounwind {
  %result = call <2 x float> @llvm.copysign.v2f32(<2 x float> %mag, <2 x float> %sign)
  store <2 x float> %result, <2 x float> addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}test_copysign_v4f32:
; SI: s_endpgm

; EG: BFI_INT
; EG: BFI_INT
; EG: BFI_INT
; EG: BFI_INT
define void @test_copysign_v4f32(<4 x float> addrspace(1)* %out, <4 x float> %mag, <4 x float> %sign) nounwind {
  %result = call <4 x float> @llvm.copysign.v4f32(<4 x float> %mag, <4 x float> %sign)
  store <4 x float> %result, <4 x float> addrspace(1)* %out, align 16
  ret void
}

