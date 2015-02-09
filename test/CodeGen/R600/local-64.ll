; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs< %s | FileCheck --check-prefix=SI --check-prefix=BOTH %s
; RUN: llc -march=amdgcn -mcpu=bonaire -verify-machineinstrs< %s | FileCheck --check-prefix=CI --check-prefix=BOTH %s

; BOTH-LABEL: {{^}}local_i32_load
; BOTH: ds_read_b32 [[REG:v[0-9]+]], v{{[0-9]+}} offset:28 [M0]
; BOTH: buffer_store_dword [[REG]],
define void @local_i32_load(i32 addrspace(1)* %out, i32 addrspace(3)* %in) nounwind {
  %gep = getelementptr i32 addrspace(3)* %in, i32 7
  %val = load i32 addrspace(3)* %gep, align 4
  store i32 %val, i32 addrspace(1)* %out, align 4
  ret void
}

; BOTH-LABEL: {{^}}local_i32_load_0_offset
; BOTH: ds_read_b32 [[REG:v[0-9]+]], v{{[0-9]+}} [M0]
; BOTH: buffer_store_dword [[REG]],
define void @local_i32_load_0_offset(i32 addrspace(1)* %out, i32 addrspace(3)* %in) nounwind {
  %val = load i32 addrspace(3)* %in, align 4
  store i32 %val, i32 addrspace(1)* %out, align 4
  ret void
}

; BOTH-LABEL: {{^}}local_i8_load_i16_max_offset:
; BOTH-NOT: ADD
; BOTH: ds_read_u8 [[REG:v[0-9]+]], {{v[0-9]+}} offset:65535 [M0]
; BOTH: buffer_store_byte [[REG]],
define void @local_i8_load_i16_max_offset(i8 addrspace(1)* %out, i8 addrspace(3)* %in) nounwind {
  %gep = getelementptr i8 addrspace(3)* %in, i32 65535
  %val = load i8 addrspace(3)* %gep, align 4
  store i8 %val, i8 addrspace(1)* %out, align 4
  ret void
}

; BOTH-LABEL: {{^}}local_i8_load_over_i16_max_offset:
; The LDS offset will be 65536 bytes, which is larger than the size of LDS on
; SI, which is why it is being OR'd with the base pointer.
; SI: s_or_b32 [[ADDR:s[0-9]+]], s{{[0-9]+}}, 0x10000
; CI: s_add_i32 [[ADDR:s[0-9]+]], s{{[0-9]+}}, 0x10000
; BOTH: v_mov_b32_e32 [[VREGADDR:v[0-9]+]], [[ADDR]]
; BOTH: ds_read_u8 [[REG:v[0-9]+]], [[VREGADDR]] [M0]
; BOTH: buffer_store_byte [[REG]],
define void @local_i8_load_over_i16_max_offset(i8 addrspace(1)* %out, i8 addrspace(3)* %in) nounwind {
  %gep = getelementptr i8 addrspace(3)* %in, i32 65536
  %val = load i8 addrspace(3)* %gep, align 4
  store i8 %val, i8 addrspace(1)* %out, align 4
  ret void
}

; BOTH-LABEL: {{^}}local_i64_load:
; BOTH-NOT: ADD
; BOTH: ds_read_b64 [[REG:v[[0-9]+:[0-9]+]]], v{{[0-9]+}} offset:56 [M0]
; BOTH: buffer_store_dwordx2 [[REG]],
define void @local_i64_load(i64 addrspace(1)* %out, i64 addrspace(3)* %in) nounwind {
  %gep = getelementptr i64 addrspace(3)* %in, i32 7
  %val = load i64 addrspace(3)* %gep, align 8
  store i64 %val, i64 addrspace(1)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_i64_load_0_offset
; BOTH: ds_read_b64 [[REG:v\[[0-9]+:[0-9]+\]]], v{{[0-9]+}} [M0]
; BOTH: buffer_store_dwordx2 [[REG]],
define void @local_i64_load_0_offset(i64 addrspace(1)* %out, i64 addrspace(3)* %in) nounwind {
  %val = load i64 addrspace(3)* %in, align 8
  store i64 %val, i64 addrspace(1)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_f64_load:
; BOTH-NOT: ADD
; BOTH: ds_read_b64 [[REG:v[[0-9]+:[0-9]+]]], v{{[0-9]+}} offset:56 [M0]
; BOTH: buffer_store_dwordx2 [[REG]],
define void @local_f64_load(double addrspace(1)* %out, double addrspace(3)* %in) nounwind {
  %gep = getelementptr double addrspace(3)* %in, i32 7
  %val = load double addrspace(3)* %gep, align 8
  store double %val, double addrspace(1)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_f64_load_0_offset
; BOTH: ds_read_b64 [[REG:v\[[0-9]+:[0-9]+\]]], v{{[0-9]+}} [M0]
; BOTH: buffer_store_dwordx2 [[REG]],
define void @local_f64_load_0_offset(double addrspace(1)* %out, double addrspace(3)* %in) nounwind {
  %val = load double addrspace(3)* %in, align 8
  store double %val, double addrspace(1)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_i64_store:
; BOTH-NOT: ADD
; BOTH: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:56 [M0]
define void @local_i64_store(i64 addrspace(3)* %out) nounwind {
  %gep = getelementptr i64 addrspace(3)* %out, i32 7
  store i64 5678, i64 addrspace(3)* %gep, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_i64_store_0_offset:
; BOTH-NOT: ADD
; BOTH: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} [M0]
define void @local_i64_store_0_offset(i64 addrspace(3)* %out) nounwind {
  store i64 1234, i64 addrspace(3)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_f64_store:
; BOTH-NOT: ADD
; BOTH: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:56 [M0]
define void @local_f64_store(double addrspace(3)* %out) nounwind {
  %gep = getelementptr double addrspace(3)* %out, i32 7
  store double 16.0, double addrspace(3)* %gep, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_f64_store_0_offset
; BOTH: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} [M0]
define void @local_f64_store_0_offset(double addrspace(3)* %out) nounwind {
  store double 20.0, double addrspace(3)* %out, align 8
  ret void
}

; BOTH-LABEL: {{^}}local_v2i64_store:
; BOTH-NOT: ADD
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:112 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:120 [M0]
; BOTH: s_endpgm
define void @local_v2i64_store(<2 x i64> addrspace(3)* %out) nounwind {
  %gep = getelementptr <2 x i64> addrspace(3)* %out, i32 7
  store <2 x i64> <i64 5678, i64 5678>, <2 x i64> addrspace(3)* %gep, align 16
  ret void
}

; BOTH-LABEL: {{^}}local_v2i64_store_0_offset:
; BOTH-NOT: ADD
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:8 [M0]
; BOTH: s_endpgm
define void @local_v2i64_store_0_offset(<2 x i64> addrspace(3)* %out) nounwind {
  store <2 x i64> <i64 1234, i64 1234>, <2 x i64> addrspace(3)* %out, align 16
  ret void
}

; BOTH-LABEL: {{^}}local_v4i64_store:
; BOTH-NOT: ADD
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:224 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:232 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:240 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:248 [M0]
; BOTH: s_endpgm
define void @local_v4i64_store(<4 x i64> addrspace(3)* %out) nounwind {
  %gep = getelementptr <4 x i64> addrspace(3)* %out, i32 7
  store <4 x i64> <i64 5678, i64 5678, i64 5678, i64 5678>, <4 x i64> addrspace(3)* %gep, align 16
  ret void
}

; BOTH-LABEL: {{^}}local_v4i64_store_0_offset:
; BOTH-NOT: ADD
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:8 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:16 [M0]
; BOTH-DAG: ds_write_b64 v{{[0-9]+}}, {{v\[[0-9]+:[0-9]+\]}} offset:24 [M0]
; BOTH: s_endpgm
define void @local_v4i64_store_0_offset(<4 x i64> addrspace(3)* %out) nounwind {
  store <4 x i64> <i64 1234, i64 1234, i64 1234, i64 1234>, <4 x i64> addrspace(3)* %out, align 16
  ret void
}
