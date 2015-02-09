; RUN: llc -verify-machineinstrs -march=amdgcn -mcpu=SI < %s | FileCheck %s

; When a frame index offset is more than 12-bits, make sure we don't store
; it in mubuf's offset field.

; Also, make sure we use the same register for storing the scratch buffer addresss
; for both stores. This register is allocated by the register scavenger, so we
; should be able to reuse the same regiser for each scratch buffer access.

; CHECK-LABEL: {{^}}legal_offset_fi:
; CHECK: v_mov_b32_e32 [[OFFSET:v[0-9]+]], 0{{$}}
; CHECK: buffer_store_dword v{{[0-9]+}}, [[OFFSET]], s[{{[0-9]+}}:{{[0-9]+}}], s{{[0-9]+}} offen
; CHECK: v_mov_b32_e32 [[OFFSET]], 0x8000
; CHECK: buffer_store_dword v{{[0-9]+}}, [[OFFSET]], s[{{[0-9]+}}:{{[0-9]+}}], s{{[0-9]+}} offen{{$}}

define void @legal_offset_fi(i32 addrspace(1)* %out, i32 %cond, i32 %if_offset, i32 %else_offset) {
entry:
  %scratch0 = alloca [8192 x i32]
  %scratch1 = alloca [8192 x i32]

  %scratchptr0 = getelementptr [8192 x i32]* %scratch0, i32 0, i32 0
  store i32 1, i32* %scratchptr0

  %scratchptr1 = getelementptr [8192 x i32]* %scratch1, i32 0, i32 0
  store i32 2, i32* %scratchptr1

  %cmp = icmp eq i32 %cond, 0
  br i1 %cmp, label %if, label %else

if:
  %if_ptr = getelementptr [8192 x i32]* %scratch0, i32 0, i32 %if_offset
  %if_value = load i32* %if_ptr
  br label %done

else:
  %else_ptr = getelementptr [8192 x i32]* %scratch1, i32 0, i32 %else_offset
  %else_value = load i32* %else_ptr
  br label %done

done:
  %value = phi i32 [%if_value, %if], [%else_value, %else]
  store i32 %value, i32 addrspace(1)* %out
  ret void

  ret void

}

; CHECK-LABEL: {{^}}legal_offset_fi_offset
; CHECK: buffer_store_dword v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+}}:{{[0-9]+}}], s{{[0-9]+}} offen
; CHECK: v_add_i32_e32 [[OFFSET:v[0-9]+]], 0x8000
; CHECK: buffer_store_dword v{{[0-9]+}}, [[OFFSET]], s[{{[0-9]+}}:{{[0-9]+}}], s{{[0-9]+}} offen{{$}}

define void @legal_offset_fi_offset(i32 addrspace(1)* %out, i32 %cond, i32 addrspace(1)* %offsets, i32 %if_offset, i32 %else_offset) {
entry:
  %scratch0 = alloca [8192 x i32]
  %scratch1 = alloca [8192 x i32]

  %offset0 = load i32 addrspace(1)* %offsets
  %scratchptr0 = getelementptr [8192 x i32]* %scratch0, i32 0, i32 %offset0
  store i32 %offset0, i32* %scratchptr0

  %offsetptr1 = getelementptr i32 addrspace(1)* %offsets, i32 1
  %offset1 = load i32 addrspace(1)* %offsetptr1
  %scratchptr1 = getelementptr [8192 x i32]* %scratch1, i32 0, i32 %offset1
  store i32 %offset1, i32* %scratchptr1

  %cmp = icmp eq i32 %cond, 0
  br i1 %cmp, label %if, label %else

if:
  %if_ptr = getelementptr [8192 x i32]* %scratch0, i32 0, i32 %if_offset
  %if_value = load i32* %if_ptr
  br label %done

else:
  %else_ptr = getelementptr [8192 x i32]* %scratch1, i32 0, i32 %else_offset
  %else_value = load i32* %else_ptr
  br label %done

done:
  %value = phi i32 [%if_value, %if], [%else_value, %else]
  store i32 %value, i32 addrspace(1)* %out
  ret void
}

