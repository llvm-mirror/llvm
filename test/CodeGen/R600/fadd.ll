; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck %s -check-prefix=R600 -check-prefix=FUNC
; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck %s -check-prefix=SI -check-prefix=FUNC
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck %s -check-prefix=SI -check-prefix=FUNC

; FUNC-LABEL: {{^}}fadd_f32:
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW]}}, KC0[2].Z, KC0[2].W
; SI: v_add_f32
define void @fadd_f32(float addrspace(1)* %out, float %a, float %b) {
   %add = fadd float %a, %b
   store float %add, float addrspace(1)* %out, align 4
   ret void
}

; FUNC-LABEL: {{^}}fadd_v2f32:
; R600-DAG: ADD {{\** *}}T{{[0-9]\.[XYZW]}}, KC0[3].X, KC0[3].Z
; R600-DAG: ADD {{\** *}}T{{[0-9]\.[XYZW]}}, KC0[2].W, KC0[3].Y
; SI: v_add_f32
; SI: v_add_f32
define void @fadd_v2f32(<2 x float> addrspace(1)* %out, <2 x float> %a, <2 x float> %b) {
  %add = fadd <2 x float> %a, %b
  store <2 x float> %add, <2 x float> addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}fadd_v4f32:
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600: ADD {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
define void @fadd_v4f32(<4 x float> addrspace(1)* %out, <4 x float> addrspace(1)* %in) {
  %b_ptr = getelementptr <4 x float> addrspace(1)* %in, i32 1
  %a = load <4 x float> addrspace(1)* %in, align 16
  %b = load <4 x float> addrspace(1)* %b_ptr, align 16
  %result = fadd <4 x float> %a, %b
  store <4 x float> %result, <4 x float> addrspace(1)* %out, align 16
  ret void
}

; FUNC-LABEL: {{^}}fadd_v8f32:
; R600: ADD
; R600: ADD
; R600: ADD
; R600: ADD
; R600: ADD
; R600: ADD
; R600: ADD
; R600: ADD
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
define void @fadd_v8f32(<8 x float> addrspace(1)* %out, <8 x float> %a, <8 x float> %b) {
  %add = fadd <8 x float> %a, %b
  store <8 x float> %add, <8 x float> addrspace(1)* %out, align 32
  ret void
}
