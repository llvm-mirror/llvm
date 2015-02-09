; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs< %s | FileCheck -check-prefix=SI %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs< %s | FileCheck -check-prefix=SI %s


; SI-LABEL: {{^}}global_truncstore_i32_to_i1:
; SI: s_load_dword [[LOAD:s[0-9]+]],
; SI: s_and_b32 [[SREG:s[0-9]+]], [[LOAD]], 1
; SI: v_mov_b32_e32 [[VREG:v[0-9]+]], [[SREG]]
; SI: buffer_store_byte [[VREG]],
define void @global_truncstore_i32_to_i1(i1 addrspace(1)* %out, i32 %val) nounwind {
  %trunc = trunc i32 %val to i1
  store i1 %trunc, i1 addrspace(1)* %out, align 1
  ret void
}

; SI-LABEL: {{^}}global_truncstore_i64_to_i1:
; SI: buffer_store_byte
define void @global_truncstore_i64_to_i1(i1 addrspace(1)* %out, i64 %val) nounwind {
  %trunc = trunc i64 %val to i1
  store i1 %trunc, i1 addrspace(1)* %out, align 1
  ret void
}

; SI-LABEL: {{^}}global_truncstore_i16_to_i1:
; SI: s_load_dword [[LOAD:s[0-9]+]],
; SI: s_and_b32 [[SREG:s[0-9]+]], [[LOAD]], 1
; SI: v_mov_b32_e32 [[VREG:v[0-9]+]], [[SREG]]
; SI: buffer_store_byte [[VREG]],
define void @global_truncstore_i16_to_i1(i1 addrspace(1)* %out, i16 %val) nounwind {
  %trunc = trunc i16 %val to i1
  store i1 %trunc, i1 addrspace(1)* %out, align 1
  ret void
}
