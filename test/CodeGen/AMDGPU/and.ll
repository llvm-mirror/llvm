; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck -check-prefix=EG -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=verde -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s

declare i32 @llvm.r600.read.tidig.x() #0

; FUNC-LABEL: {{^}}test2:
; EG: AND_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; EG: AND_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}
; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}

define void @test2(<2 x i32> addrspace(1)* %out, <2 x i32> addrspace(1)* %in) {
  %b_ptr = getelementptr <2 x i32>, <2 x i32> addrspace(1)* %in, i32 1
  %a = load <2 x i32>, <2 x i32> addrspace(1) * %in
  %b = load <2 x i32>, <2 x i32> addrspace(1) * %b_ptr
  %result = and <2 x i32> %a, %b
  store <2 x i32> %result, <2 x i32> addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}test4:
; EG: AND_INT {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; EG: AND_INT {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; EG: AND_INT {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; EG: AND_INT {{\** *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}
; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}
; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}
; SI: v_and_b32_e32 v{{[0-9]+, v[0-9]+, v[0-9]+}}

define void @test4(<4 x i32> addrspace(1)* %out, <4 x i32> addrspace(1)* %in) {
  %b_ptr = getelementptr <4 x i32>, <4 x i32> addrspace(1)* %in, i32 1
  %a = load <4 x i32>, <4 x i32> addrspace(1) * %in
  %b = load <4 x i32>, <4 x i32> addrspace(1) * %b_ptr
  %result = and <4 x i32> %a, %b
  store <4 x i32> %result, <4 x i32> addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}s_and_i32:
; SI: s_and_b32
define void @s_and_i32(i32 addrspace(1)* %out, i32 %a, i32 %b) {
  %and = and i32 %a, %b
  store i32 %and, i32 addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}s_and_constant_i32:
; SI: s_and_b32 s{{[0-9]+}}, s{{[0-9]+}}, 0x12d687
define void @s_and_constant_i32(i32 addrspace(1)* %out, i32 %a) {
  %and = and i32 %a, 1234567
  store i32 %and, i32 addrspace(1)* %out, align 4
  ret void
}

; FIXME: We should really duplicate the constant so that the SALU use
; can fold into the s_and_b32 and the VALU one is materialized
; directly without copying from the SGPR.

; Second use is a VGPR use of the constant.
; FUNC-LABEL: {{^}}s_and_multi_use_constant_i32_0:
; SI: s_mov_b32 [[K:s[0-9]+]], 0x12d687
; SI-DAG: s_and_b32 [[AND:s[0-9]+]], s{{[0-9]+}}, [[K]]
; SI-DAG: v_mov_b32_e32 [[VK:v[0-9]+]], [[K]]
; SI: buffer_store_dword [[VK]]
define void @s_and_multi_use_constant_i32_0(i32 addrspace(1)* %out, i32 %a, i32 %b) {
  %and = and i32 %a, 1234567

  ; Just to stop future replacement of copy to vgpr + store with VALU op.
  %foo = add i32 %and, %b
  store volatile i32 %foo, i32 addrspace(1)* %out
  store volatile i32 1234567, i32 addrspace(1)* %out
  ret void
}

; Second use is another SGPR use of the constant.
; FUNC-LABEL: {{^}}s_and_multi_use_constant_i32_1:
; SI: s_mov_b32 [[K:s[0-9]+]], 0x12d687
; SI: s_and_b32 [[AND:s[0-9]+]], s{{[0-9]+}}, [[K]]
; SI: s_add_i32
; SI: s_add_i32 [[ADD:s[0-9]+]], s{{[0-9]+}}, [[K]]
; SI: buffer_store_dword [[VK]]
define void @s_and_multi_use_constant_i32_1(i32 addrspace(1)* %out, i32 %a, i32 %b) {
  %and = and i32 %a, 1234567
  %foo = add i32 %and, 1234567
  %bar = add i32 %foo, %b
  store volatile i32 %bar, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}v_and_i32_vgpr_vgpr:
; SI: v_and_b32_e32 v{{[0-9]+}}, v{{[0-9]+}}, v{{[0-9]+}}
define void @v_and_i32_vgpr_vgpr(i32 addrspace(1)* %out, i32 addrspace(1)* %aptr, i32 addrspace(1)* %bptr) {
  %tid = call i32 @llvm.r600.read.tidig.x() #0
  %gep.a = getelementptr i32, i32 addrspace(1)* %aptr, i32 %tid
  %gep.b = getelementptr i32, i32 addrspace(1)* %bptr, i32 %tid
  %gep.out = getelementptr i32, i32 addrspace(1)* %out, i32 %tid
  %a = load i32, i32 addrspace(1)* %gep.a
  %b = load i32, i32 addrspace(1)* %gep.b
  %and = and i32 %a, %b
  store i32 %and, i32 addrspace(1)* %gep.out
  ret void
}

; FUNC-LABEL: {{^}}v_and_i32_sgpr_vgpr:
; SI-DAG: s_load_dword [[SA:s[0-9]+]]
; SI-DAG: {{buffer|flat}}_load_dword [[VB:v[0-9]+]]
; SI: v_and_b32_e32 v{{[0-9]+}}, [[SA]], [[VB]]
define void @v_and_i32_sgpr_vgpr(i32 addrspace(1)* %out, i32 %a, i32 addrspace(1)* %bptr) {
  %tid = call i32 @llvm.r600.read.tidig.x() #0
  %gep.b = getelementptr i32, i32 addrspace(1)* %bptr, i32 %tid
  %gep.out = getelementptr i32, i32 addrspace(1)* %out, i32 %tid
  %b = load i32, i32 addrspace(1)* %gep.b
  %and = and i32 %a, %b
  store i32 %and, i32 addrspace(1)* %gep.out
  ret void
}

; FUNC-LABEL: {{^}}v_and_i32_vgpr_sgpr:
; SI-DAG: s_load_dword [[SA:s[0-9]+]]
; SI-DAG: {{buffer|flat}}_load_dword [[VB:v[0-9]+]]
; SI: v_and_b32_e32 v{{[0-9]+}}, [[SA]], [[VB]]
define void @v_and_i32_vgpr_sgpr(i32 addrspace(1)* %out, i32 addrspace(1)* %aptr, i32 %b) {
  %tid = call i32 @llvm.r600.read.tidig.x() #0
  %gep.a = getelementptr i32, i32 addrspace(1)* %aptr, i32 %tid
  %gep.out = getelementptr i32, i32 addrspace(1)* %out, i32 %tid
  %a = load i32, i32 addrspace(1)* %gep.a
  %and = and i32 %a, %b
  store i32 %and, i32 addrspace(1)* %gep.out
  ret void
}

; FUNC-LABEL: {{^}}v_and_constant_i32
; SI: v_and_b32_e32 v{{[0-9]+}}, 0x12d687, v{{[0-9]+}}
define void @v_and_constant_i32(i32 addrspace(1)* %out, i32 addrspace(1)* %aptr) {
  %a = load i32, i32 addrspace(1)* %aptr, align 4
  %and = and i32 %a, 1234567
  store i32 %and, i32 addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}v_and_inline_imm_64_i32
; SI: v_and_b32_e32 v{{[0-9]+}}, 64, v{{[0-9]+}}
define void @v_and_inline_imm_64_i32(i32 addrspace(1)* %out, i32 addrspace(1)* %aptr) {
  %a = load i32, i32 addrspace(1)* %aptr, align 4
  %and = and i32 %a, 64
  store i32 %and, i32 addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}v_and_inline_imm_neg_16_i32
; SI: v_and_b32_e32 v{{[0-9]+}}, -16, v{{[0-9]+}}
define void @v_and_inline_imm_neg_16_i32(i32 addrspace(1)* %out, i32 addrspace(1)* %aptr) {
  %a = load i32, i32 addrspace(1)* %aptr, align 4
  %and = and i32 %a, -16
  store i32 %and, i32 addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}s_and_i64
; SI: s_and_b64
define void @s_and_i64(i64 addrspace(1)* %out, i64 %a, i64 %b) {
  %and = and i64 %a, %b
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FIXME: Should use SGPRs
; FUNC-LABEL: {{^}}s_and_i1:
; SI: v_and_b32
define void @s_and_i1(i1 addrspace(1)* %out, i1 %a, i1 %b) {
  %and = and i1 %a, %b
  store i1 %and, i1 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}s_and_constant_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}
define void @s_and_constant_i64(i64 addrspace(1)* %out, i64 %a) {
  %and = and i64 %a, 281474976710655
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}v_and_i64:
; SI: v_and_b32
; SI: v_and_b32
define void @v_and_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 addrspace(1)* %bptr) {
  %a = load i64, i64 addrspace(1)* %aptr, align 8
  %b = load i64, i64 addrspace(1)* %bptr, align 8
  %and = and i64 %a, %b
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}v_and_i64_br:
; SI: v_and_b32
; SI: v_and_b32
define void @v_and_i64_br(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 addrspace(1)* %bptr, i32 %cond) {
entry:
  %tmp0 = icmp eq i32 %cond, 0
  br i1 %tmp0, label %if, label %endif

if:
  %a = load i64, i64 addrspace(1)* %aptr, align 8
  %b = load i64, i64 addrspace(1)* %bptr, align 8
  %and = and i64 %a, %b
  br label %endif

endif:
  %tmp1 = phi i64 [%and, %if], [0, %entry]
  store i64 %tmp1, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}v_and_constant_i64:
; SI-DAG: s_mov_b32 [[KLO:s[0-9]+]], 0xab19b207
; SI-DAG: s_movk_i32 [[KHI:s[0-9]+]], 0x11e{{$}}
; SI-DAG: v_and_b32_e32 {{v[0-9]+}}, [[KLO]], {{v[0-9]+}}
; SI-DAG: v_and_b32_e32 {{v[0-9]+}}, [[KHI]], {{v[0-9]+}}
; SI: buffer_store_dwordx2
define void @v_and_constant_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr) {
  %a = load i64, i64 addrspace(1)* %aptr, align 8
  %and = and i64 %a, 1231231234567
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FIXME: Should replace and 0
; FUNC-LABEL: {{^}}v_and_i64_32_bit_constant:
; SI: v_and_b32_e32 {{v[0-9]+}}, {{s[0-9]+}}, {{v[0-9]+}}
; SI: v_and_b32_e32 {{v[0-9]+}}, 0, {{v[0-9]+}}
define void @v_and_i64_32_bit_constant(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr) {
  %a = load i64, i64 addrspace(1)* %aptr, align 8
  %and = and i64 %a, 1234567
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FIXME: Replace and 0 with mov 0
; FUNC-LABEL: {{^}}v_and_inline_imm_i64:
; SI: v_and_b32_e32 {{v[0-9]+}}, 64, {{v[0-9]+}}
; SI: v_and_b32_e32 {{v[0-9]+}}, 0, {{v[0-9]+}}
define void @v_and_inline_imm_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr) {
  %a = load i64, i64 addrspace(1)* %aptr, align 8
  %and = and i64 %a, 64
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_64_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 64
define void @s_and_inline_imm_64_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 64
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_1_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 1
define void @s_and_inline_imm_1_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 1
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_1.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 1.0
define void @s_and_inline_imm_1.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 4607182418800017408
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_neg_1.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, -1.0
define void @s_and_inline_imm_neg_1.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 13830554455654793216
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_0.5_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 0.5
define void @s_and_inline_imm_0.5_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 4602678819172646912
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_neg_0.5_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, -0.5
define void @s_and_inline_imm_neg_0.5_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 13826050856027422720
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_2.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 2.0
define void @s_and_inline_imm_2.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 4611686018427387904
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_neg_2.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, -2.0
define void @s_and_inline_imm_neg_2.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 13835058055282163712
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_4.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 4.0
define void @s_and_inline_imm_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 4616189618054758400
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_imm_neg_4.0_i64
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, -4.0
define void @s_and_inline_imm_neg_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 13839561654909534208
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}


; Test with the 64-bit integer bitpattern for a 32-bit float in the
; low 32-bits, which is not a valid 64-bit inline immmediate.

; FUNC-LABEL: {{^}}s_and_inline_imm_f32_4.0_i64
; SI-DAG: s_mov_b32 s[[K_LO:[0-9]+]], 4.0
; SI-DAG: s_mov_b32 s[[K_HI:[0-9]+]], 0{{$}}
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, s{{\[}}[[K_LO]]:[[K_HI]]{{\]}}
define void @s_and_inline_imm_f32_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 1082130432
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FIXME: Copy of -1 register
; FUNC-LABEL: {{^}}s_and_inline_imm_f32_neg_4.0_i64
; SI-DAG: s_mov_b32 s[[K_LO:[0-9]+]], -4.0
; SI-DAG: s_mov_b32 s[[K_HI:[0-9]+]], -1{{$}}
; SI-DAG: s_mov_b32 s[[K_HI_COPY:[0-9]+]], s[[K_HI]]
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, s{{\[}}[[K_LO]]:[[K_HI_COPY]]{{\]}}
define void @s_and_inline_imm_f32_neg_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, -1065353216
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; Shift into upper 32-bits
; FUNC-LABEL: {{^}}s_and_inline_high_imm_f32_4.0_i64
; SI-DAG: s_mov_b32 s[[K_HI:[0-9]+]], 4.0
; SI-DAG: s_mov_b32 s[[K_LO:[0-9]+]], 0{{$}}
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, s{{\[}}[[K_LO]]:[[K_HI]]{{\]}}
define void @s_and_inline_high_imm_f32_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 4647714815446351872
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}s_and_inline_high_imm_f32_neg_4.0_i64
; SI-DAG: s_mov_b32 s[[K_HI:[0-9]+]], -4.0
; SI-DAG: s_mov_b32 s[[K_LO:[0-9]+]], 0{{$}}
; SI: s_and_b64 s{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, s{{\[}}[[K_LO]]:[[K_HI]]{{\]}}
define void @s_and_inline_high_imm_f32_neg_4.0_i64(i64 addrspace(1)* %out, i64 addrspace(1)* %aptr, i64 %a) {
  %and = and i64 %a, 13871086852301127680
  store i64 %and, i64 addrspace(1)* %out, align 8
  ret void
}

attributes #0 = { nounwind readnone }
