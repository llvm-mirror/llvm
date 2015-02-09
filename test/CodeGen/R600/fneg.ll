; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck -check-prefix=R600 -check-prefix=FUNC %s

; FUNC-LABEL: {{^}}fneg_f32:
; R600: -PV

; SI: v_xor_b32
define void @fneg_f32(float addrspace(1)* %out, float %in) {
  %fneg = fsub float -0.000000e+00, %in
  store float %fneg, float addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_v2f32:
; R600: -PV
; R600: -PV

; SI: v_xor_b32
; SI: v_xor_b32
define void @fneg_v2f32(<2 x float> addrspace(1)* nocapture %out, <2 x float> %in) {
  %fneg = fsub <2 x float> <float -0.000000e+00, float -0.000000e+00>, %in
  store <2 x float> %fneg, <2 x float> addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_v4f32:
; R600: -PV
; R600: -T
; R600: -PV
; R600: -PV

; SI: v_xor_b32
; SI: v_xor_b32
; SI: v_xor_b32
; SI: v_xor_b32
define void @fneg_v4f32(<4 x float> addrspace(1)* nocapture %out, <4 x float> %in) {
  %fneg = fsub <4 x float> <float -0.000000e+00, float -0.000000e+00, float -0.000000e+00, float -0.000000e+00>, %in
  store <4 x float> %fneg, <4 x float> addrspace(1)* %out
  ret void
}

; DAGCombiner will transform:
; (fneg (f32 bitcast (i32 a))) => (f32 bitcast (xor (i32 a), 0x80000000))
; unless the target returns true for isNegFree()

; FUNC-LABEL: {{^}}fneg_free_f32:
; R600-NOT: XOR
; R600: -KC0[2].Z

; XXX: We could use v_add_f32_e64 with the negate bit here instead.
; SI: v_sub_f32_e64 v{{[0-9]}}, 0, s{{[0-9]+$}}
define void @fneg_free_f32(float addrspace(1)* %out, i32 %in) {
  %bc = bitcast i32 %in to float
  %fsub = fsub float 0.0, %bc
  store float %fsub, float addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_fold_f32:
; SI: s_load_dword [[NEG_VALUE:s[0-9]+]], s[{{[0-9]+:[0-9]+}}], 0xb
; SI-NOT: xor
; SI: v_mul_f32_e64 v{{[0-9]+}}, -[[NEG_VALUE]], [[NEG_VALUE]]
define void @fneg_fold_f32(float addrspace(1)* %out, float %in) {
  %fsub = fsub float -0.0, %in
  %fmul = fmul float %fsub, %in
  store float %fmul, float addrspace(1)* %out
  ret void
}
