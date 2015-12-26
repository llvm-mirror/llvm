; RUN: llc < %s -march=amdgcn -mcpu=SI -verify-machineinstrs | FileCheck %s

; CHECK-LABEL: {{^}}fold_sgpr:
; CHECK: v_add_i32_e32 v{{[0-9]+}}, vcc, s
define void @fold_sgpr(i32 addrspace(1)* %out, i32 %fold) {
entry:
  %tmp0 = icmp ne i32 %fold, 0
  br i1 %tmp0, label %if, label %endif

if:
  %id = call i32 @llvm.r600.read.tidig.x()
  %offset = add i32 %fold, %id
  %tmp1 = getelementptr i32, i32 addrspace(1)* %out, i32 %offset
  store i32 0, i32 addrspace(1)* %tmp1
  br label %endif

endif:
  ret void
}

; CHECK-LABEL: {{^}}fold_imm:
; CHECK: v_or_b32_e32 v{{[0-9]+}}, 5
define void @fold_imm(i32 addrspace(1)* %out, i32 %cmp) {
entry:
  %fold = add i32 3, 2
  %tmp0 = icmp ne i32 %cmp, 0
  br i1 %tmp0, label %if, label %endif

if:
  %id = call i32 @llvm.r600.read.tidig.x()
  %val = or i32 %id, %fold
  store i32 %val, i32 addrspace(1)* %out
  br label %endif

endif:
  ret void
}

; CHECK-LABEL: {{^}}fold_64bit_constant_add:
; CHECK-NOT: s_mov_b64
; FIXME: It would be better if we could use v_add here and drop the extra
; v_mov_b32 instructions.
; CHECK-DAG: s_add_u32 [[LO:s[0-9]+]], s{{[0-9]+}}, 1
; CHECK-DAG: s_addc_u32 [[HI:s[0-9]+]], s{{[0-9]+}}, 0
; CHECK-DAG: v_mov_b32_e32 v[[VLO:[0-9]+]], [[LO]]
; CHECK-DAG: v_mov_b32_e32 v[[VHI:[0-9]+]], [[HI]]
; CHECK: buffer_store_dwordx2 v{{\[}}[[VLO]]:[[VHI]]{{\]}},

define void @fold_64bit_constant_add(i64 addrspace(1)* %out, i32 %cmp, i64 %val) {
entry:
  %tmp0 = add i64 %val, 1
  store i64 %tmp0, i64 addrspace(1)* %out
  ret void
}

; Inline constants should always be folded.

; CHECK-LABEL: {{^}}vector_inline:
; CHECK: v_xor_b32_e32 v{{[0-9]+}}, 5, v{{[0-9]+}}
; CHECK: v_xor_b32_e32 v{{[0-9]+}}, 5, v{{[0-9]+}}
; CHECK: v_xor_b32_e32 v{{[0-9]+}}, 5, v{{[0-9]+}}
; CHECK: v_xor_b32_e32 v{{[0-9]+}}, 5, v{{[0-9]+}}

define void @vector_inline(<4 x i32> addrspace(1)* %out) {
entry:
  %tmp0 = call i32 @llvm.r600.read.tidig.x()
  %tmp1 = add i32 %tmp0, 1
  %tmp2 = add i32 %tmp0, 2
  %tmp3 = add i32 %tmp0, 3
  %vec0 = insertelement <4 x i32> undef, i32 %tmp0, i32 0
  %vec1 = insertelement <4 x i32> %vec0, i32 %tmp1, i32 1
  %vec2 = insertelement <4 x i32> %vec1, i32 %tmp2, i32 2
  %vec3 = insertelement <4 x i32> %vec2, i32 %tmp3, i32 3
  %tmp4 = xor <4 x i32> <i32 5, i32 5, i32 5, i32 5>, %vec3
  store <4 x i32> %tmp4, <4 x i32> addrspace(1)* %out
  ret void
}

; Immediates with one use should be folded
; CHECK-LABEL: {{^}}imm_one_use:
; CHECK: v_xor_b32_e32 v{{[0-9]+}}, 0x64, v{{[0-9]+}}

define void @imm_one_use(i32 addrspace(1)* %out) {
entry:
  %tmp0 = call i32 @llvm.r600.read.tidig.x()
  %tmp1 = xor i32 %tmp0, 100
  store i32 %tmp1, i32 addrspace(1)* %out
  ret void
}
; CHECK-LABEL: {{^}}vector_imm:
; CHECK: s_movk_i32 [[IMM:s[0-9]+]], 0x64
; CHECK: v_xor_b32_e32 v{{[0-9]}}, [[IMM]], v{{[0-9]}}
; CHECK: v_xor_b32_e32 v{{[0-9]}}, [[IMM]], v{{[0-9]}}
; CHECK: v_xor_b32_e32 v{{[0-9]}}, [[IMM]], v{{[0-9]}}
; CHECK: v_xor_b32_e32 v{{[0-9]}}, [[IMM]], v{{[0-9]}}

define void @vector_imm(<4 x i32> addrspace(1)* %out) {
entry:
  %tmp0 = call i32 @llvm.r600.read.tidig.x()
  %tmp1 = add i32 %tmp0, 1
  %tmp2 = add i32 %tmp0, 2
  %tmp3 = add i32 %tmp0, 3
  %vec0 = insertelement <4 x i32> undef, i32 %tmp0, i32 0
  %vec1 = insertelement <4 x i32> %vec0, i32 %tmp1, i32 1
  %vec2 = insertelement <4 x i32> %vec1, i32 %tmp2, i32 2
  %vec3 = insertelement <4 x i32> %vec2, i32 %tmp3, i32 3
  %tmp4 = xor <4 x i32> <i32 100, i32 100, i32 100, i32 100>, %vec3
  store <4 x i32> %tmp4, <4 x i32> addrspace(1)* %out
  ret void
}

declare i32 @llvm.r600.read.tidig.x() #0
attributes #0 = { readnone }
