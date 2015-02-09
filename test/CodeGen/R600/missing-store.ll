; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=FUNC -check-prefix=SI %s

@ptr_load = addrspace(3) global i32 addrspace(2)* undef, align 8

; Make sure when the load from %ptr2 is folded the chain isn't lost,
; resulting in losing the store to gptr

; FUNC-LABEL: {{^}}missing_store_reduced:
; SI: ds_read_b64
; SI: buffer_store_dword
; SI: buffer_load_dword
; SI: buffer_store_dword
; SI: s_endpgm
define void @missing_store_reduced(i32 addrspace(1)* %out, i32 addrspace(1)* %gptr) #0 {
  %ptr0 = load i32 addrspace(2)* addrspace(3)* @ptr_load, align 8
  %ptr2 = getelementptr inbounds i32 addrspace(2)* %ptr0, i64 2

  store i32 99, i32 addrspace(1)* %gptr, align 4
  %tmp2 = load i32 addrspace(2)* %ptr2, align 4

  store i32 %tmp2, i32 addrspace(1)* %out, align 4
  ret void
}

attributes #0 = { nounwind }

