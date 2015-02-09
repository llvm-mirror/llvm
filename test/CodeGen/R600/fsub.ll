; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck -check-prefix=R600 -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s


; FUNC-LABEL: {{^}}v_fsub_f32:
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
define void @v_fsub_f32(float addrspace(1)* %out, float addrspace(1)* %in) {
  %b_ptr = getelementptr float addrspace(1)* %in, i32 1
  %a = load float addrspace(1)* %in, align 4
  %b = load float addrspace(1)* %b_ptr, align 4
  %result = fsub float %a, %b
  store float %result, float addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}s_fsub_f32:
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW]}}, KC0[2].Z, -KC0[2].W

; SI: v_sub_f32_e32 {{v[0-9]+}}, {{s[0-9]+}}, {{v[0-9]+}}
define void @s_fsub_f32(float addrspace(1)* %out, float %a, float %b) {
  %sub = fsub float %a, %b
  store float %sub, float addrspace(1)* %out, align 4
  ret void
}

declare float @llvm.R600.load.input(i32) readnone

declare void @llvm.AMDGPU.store.output(float, i32)

; FUNC-LABEL: {{^}}fsub_v2f32:
; R600-DAG: ADD {{\** *}}T{{[0-9]+\.[XYZW]}}, KC0[3].X, -KC0[3].Z
; R600-DAG: ADD {{\** *}}T{{[0-9]+\.[XYZW]}}, KC0[2].W, -KC0[3].Y

; FIXME: Should be using SGPR directly for first operand
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
define void @fsub_v2f32(<2 x float> addrspace(1)* %out, <2 x float> %a, <2 x float> %b) {
  %sub = fsub <2 x float> %a, %b
  store <2 x float> %sub, <2 x float> addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}v_fsub_v4f32:
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], -T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], -T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], -T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], -T[0-9]+\.[XYZW]}}

; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
define void @v_fsub_v4f32(<4 x float> addrspace(1)* %out, <4 x float> addrspace(1)* %in) {
  %b_ptr = getelementptr <4 x float> addrspace(1)* %in, i32 1
  %a = load <4 x float> addrspace(1)* %in, align 16
  %b = load <4 x float> addrspace(1)* %b_ptr, align 16
  %result = fsub <4 x float> %a, %b
  store <4 x float> %result, <4 x float> addrspace(1)* %out, align 16
  ret void
}

; FIXME: Should be using SGPR directly for first operand

; FUNC-LABEL: {{^}}s_fsub_v4f32:
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: v_subrev_f32_e32 {{v[0-9]+}}, {{v[0-9]+}}, {{v[0-9]+}}
; SI: s_endpgm
define void @s_fsub_v4f32(<4 x float> addrspace(1)* %out, <4 x float> %a, <4 x float> %b) {
  %result = fsub <4 x float> %a, %b
  store <4 x float> %result, <4 x float> addrspace(1)* %out, align 16
  ret void
}
