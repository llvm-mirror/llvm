; RUN: llc < %s -march=r600 -mcpu=redwood | FileCheck --check-prefix=R600 --check-prefix=FUNC %s
; RUN: llc < %s -march=amdgcn -mcpu=SI -verify-machineinstrs | FileCheck -check-prefix=SI -check-prefix=FUNC %s

declare i32 @llvm.r600.read.tidig.x() nounwind readnone

; FUNC-LABEL: {{^}}setcc_v2i32:
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW]}}, KC0[3].X, KC0[3].Z
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW]}}, KC0[2].W, KC0[3].Y

define void @setcc_v2i32(<2 x i32> addrspace(1)* %out, <2 x i32> %a, <2 x i32> %b) {
  %result = icmp eq <2 x i32> %a, %b
  %sext = sext <2 x i1> %result to <2 x i32>
  store <2 x i32> %sext, <2 x i32> addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}setcc_v4i32:
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600-DAG: SETE_INT * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

define void @setcc_v4i32(<4 x i32> addrspace(1)* %out, <4 x i32> addrspace(1)* %in) {
  %b_ptr = getelementptr <4 x i32> addrspace(1)* %in, i32 1
  %a = load <4 x i32> addrspace(1) * %in
  %b = load <4 x i32> addrspace(1) * %b_ptr
  %result = icmp eq <4 x i32> %a, %b
  %sext = sext <4 x i1> %result to <4 x i32>
  store <4 x i32> %sext, <4 x i32> addrspace(1)* %out
  ret void
}

;;;==========================================================================;;;
;; Float comparisons
;;;==========================================================================;;;

; FUNC-LABEL: {{^}}f32_oeq:
; R600: SETE_DX10
; SI: v_cmp_eq_f32
define void @f32_oeq(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp oeq float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ogt:
; R600: SETGT_DX10
; SI: v_cmp_gt_f32
define void @f32_ogt(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ogt float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_oge:
; R600: SETGE_DX10
; SI: v_cmp_ge_f32
define void @f32_oge(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp oge float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_olt:
; R600: SETGT_DX10
; SI: v_cmp_lt_f32
define void @f32_olt(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp olt float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ole:
; R600: SETGE_DX10
; SI: v_cmp_le_f32
define void @f32_ole(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ole float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_one:
; R600-DAG: SETE_DX10
; R600-DAG: SETE_DX10
; R600-DAG: AND_INT
; R600-DAG: SETNE_DX10
; R600-DAG: AND_INT
; R600-DAG: SETNE_INT

; SI: v_cmp_lg_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_one(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp one float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ord:
; R600-DAG: SETE_DX10
; R600-DAG: SETE_DX10
; R600-DAG: AND_INT
; R600-DAG: SETNE_INT
; SI: v_cmp_o_f32
define void @f32_ord(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ord float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ueq:
; R600-DAG: SETNE_DX10
; R600-DAG: SETNE_DX10
; R600-DAG: OR_INT
; R600-DAG: SETE_DX10
; R600-DAG: OR_INT
; R600-DAG: SETNE_INT

; SI: v_cmp_nlg_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_ueq(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ueq float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ugt:
; R600: SETGE
; R600: SETE_DX10
; SI: v_cmp_nle_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_ugt(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ugt float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_uge:
; R600: SETGT
; R600: SETE_DX10

; SI: v_cmp_nlt_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_uge(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp uge float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ult:
; R600: SETGE
; R600: SETE_DX10

; SI: v_cmp_nge_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_ult(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ult float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_ule:
; R600: SETGT
; R600: SETE_DX10

; SI: v_cmp_ngt_f32_e32 vcc
; SI-NEXT: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1, vcc
define void @f32_ule(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp ule float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_une:
; R600: SETNE_DX10
; SI: v_cmp_neq_f32
define void @f32_une(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp une float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}f32_uno:
; R600: SETNE_DX10
; R600: SETNE_DX10
; R600: OR_INT
; R600: SETNE_INT
; SI: v_cmp_u_f32
define void @f32_uno(i32 addrspace(1)* %out, float %a, float %b) {
entry:
  %0 = fcmp uno float %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

;;;==========================================================================;;;
;; 32-bit integer comparisons
;;;==========================================================================;;;

; FUNC-LABEL: {{^}}i32_eq:
; R600: SETE_INT
; SI: v_cmp_eq_i32
define void @i32_eq(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp eq i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_ne:
; R600: SETNE_INT
; SI: v_cmp_ne_i32
define void @i32_ne(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp ne i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_ugt:
; R600: SETGT_UINT
; SI: v_cmp_gt_u32
define void @i32_ugt(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp ugt i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_uge:
; R600: SETGE_UINT
; SI: v_cmp_ge_u32
define void @i32_uge(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp uge i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_ult:
; R600: SETGT_UINT
; SI: v_cmp_lt_u32
define void @i32_ult(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp ult i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_ule:
; R600: SETGE_UINT
; SI: v_cmp_le_u32
define void @i32_ule(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp ule i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_sgt:
; R600: SETGT_INT
; SI: v_cmp_gt_i32
define void @i32_sgt(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp sgt i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_sge:
; R600: SETGE_INT
; SI: v_cmp_ge_i32
define void @i32_sge(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp sge i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_slt:
; R600: SETGT_INT
; SI: v_cmp_lt_i32
define void @i32_slt(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp slt i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}i32_sle:
; R600: SETGE_INT
; SI: v_cmp_le_i32
define void @i32_sle(i32 addrspace(1)* %out, i32 %a, i32 %b) {
entry:
  %0 = icmp sle i32 %a, %b
  %1 = sext i1 %0 to i32
  store i32 %1, i32 addrspace(1)* %out
  ret void
}

; FIXME: This does 4 compares
; FUNC-LABEL: {{^}}v3i32_eq:
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI: s_endpgm
define void @v3i32_eq(<3 x i32> addrspace(1)* %out, <3 x i32> addrspace(1)* %ptra, <3 x i32> addrspace(1)* %ptrb) {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.a = getelementptr <3 x i32> addrspace(1)* %ptra, i32 %tid
  %gep.b = getelementptr <3 x i32> addrspace(1)* %ptrb, i32 %tid
  %gep.out = getelementptr <3 x i32> addrspace(1)* %out, i32 %tid
  %a = load <3 x i32> addrspace(1)* %gep.a
  %b = load <3 x i32> addrspace(1)* %gep.b
  %cmp = icmp eq <3 x i32> %a, %b
  %ext = sext <3 x i1> %cmp to <3 x i32>
  store <3 x i32> %ext, <3 x i32> addrspace(1)* %gep.out
  ret void
}

; FUNC-LABEL: {{^}}v3i8_eq:
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI-DAG: v_cmp_eq_i32
; SI-DAG: v_cndmask_b32_e64 {{v[0-9]+}}, 0, -1,
; SI: s_endpgm
define void @v3i8_eq(<3 x i8> addrspace(1)* %out, <3 x i8> addrspace(1)* %ptra, <3 x i8> addrspace(1)* %ptrb) {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %gep.a = getelementptr <3 x i8> addrspace(1)* %ptra, i32 %tid
  %gep.b = getelementptr <3 x i8> addrspace(1)* %ptrb, i32 %tid
  %gep.out = getelementptr <3 x i8> addrspace(1)* %out, i32 %tid
  %a = load <3 x i8> addrspace(1)* %gep.a
  %b = load <3 x i8> addrspace(1)* %gep.b
  %cmp = icmp eq <3 x i8> %a, %b
  %ext = sext <3 x i1> %cmp to <3 x i8>
  store <3 x i8> %ext, <3 x i8> addrspace(1)* %gep.out
  ret void
}
