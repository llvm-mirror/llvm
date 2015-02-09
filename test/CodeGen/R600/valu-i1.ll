; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs -enable-misched -asm-verbose < %s | FileCheck -check-prefix=SI %s

declare i32 @llvm.r600.read.tidig.x() nounwind readnone

; SI-LABEL: @test_if
; Make sure the i1 values created by the cfg structurizer pass are
; moved using VALU instructions
; SI-NOT: s_mov_b64 s[{{[0-9]:[0-9]}}], -1
; SI: v_mov_b32_e32 v{{[0-9]}}, -1
define void @test_if(i32 %a, i32 %b, i32 addrspace(1)* %src, i32 addrspace(1)* %dst) #1 {
entry:
  switch i32 %a, label %default [
    i32 0, label %case0
    i32 1, label %case1
  ]

case0:
  %arrayidx1 = getelementptr i32 addrspace(1)* %dst, i32 %b
  store i32 0, i32 addrspace(1)* %arrayidx1, align 4
  br label %end

case1:
  %arrayidx5 = getelementptr i32 addrspace(1)* %dst, i32 %b
  store i32 1, i32 addrspace(1)* %arrayidx5, align 4
  br label %end

default:
  %cmp8 = icmp eq i32 %a, 2
  %arrayidx10 = getelementptr i32 addrspace(1)* %dst, i32 %b
  br i1 %cmp8, label %if, label %else

if:
  store i32 2, i32 addrspace(1)* %arrayidx10, align 4
  br label %end

else:
  store i32 3, i32 addrspace(1)* %arrayidx10, align 4
  br label %end

end:
  ret void
}

; SI-LABEL: @simple_test_v_if
; SI: v_cmp_ne_i32_e64 [[BR_SREG:s\[[0-9]+:[0-9]+\]]], v{{[0-9]+}}, 0
; SI: s_and_saveexec_b64 [[BR_SREG]], [[BR_SREG]]
; SI: s_xor_b64 [[BR_SREG]], exec, [[BR_SREG]]

; SI: ; BB#1
; SI: buffer_store_dword
; SI: s_endpgm

; SI: BB1_2:
; SI: s_or_b64 exec, exec, [[BR_SREG]]
; SI: s_endpgm
define void @simple_test_v_if(i32 addrspace(1)* %dst, i32 addrspace(1)* %src) #1 {
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %is.0 = icmp ne i32 %tid, 0
  br i1 %is.0, label %store, label %exit

store:
  %gep = getelementptr i32 addrspace(1)* %dst, i32 %tid
  store i32 999, i32 addrspace(1)* %gep
  ret void

exit:
  ret void
}

; SI-LABEL: @simple_test_v_loop
; SI: v_cmp_ne_i32_e64 [[BR_SREG:s\[[0-9]+:[0-9]+\]]], v{{[0-9]+}}, 0
; SI: s_and_saveexec_b64 [[BR_SREG]], [[BR_SREG]]
; SI: s_xor_b64 [[BR_SREG]], exec, [[BR_SREG]]
; SI: s_cbranch_execz BB2_2

; SI: ; BB#1:
; SI: s_mov_b64 {{s\[[0-9]+:[0-9]+\]}}, 0{{$}}

; SI: BB2_3:
; SI: buffer_load_dword
; SI: buffer_store_dword
; SI: v_cmp_eq_i32_e32 vcc,
; SI: s_or_b64 [[OR_SREG:s\[[0-9]+:[0-9]+\]]]
; SI: s_andn2_b64 exec, exec, [[OR_SREG]]
; SI: s_cbranch_execnz BB2_3

define void @simple_test_v_loop(i32 addrspace(1)* %dst, i32 addrspace(1)* %src) #1 {
entry:
  %tid = call i32 @llvm.r600.read.tidig.x() nounwind readnone
  %is.0 = icmp ne i32 %tid, 0
  %limit = add i32 %tid, 64
  br i1 %is.0, label %loop, label %exit

loop:
  %i = phi i32 [%tid, %entry], [%i.inc, %loop]
  %gep.src = getelementptr i32 addrspace(1)* %src, i32 %i
  %gep.dst = getelementptr i32 addrspace(1)* %dst, i32 %i
  %load = load i32 addrspace(1)* %src
  store i32 %load, i32 addrspace(1)* %gep.dst
  %i.inc = add nsw i32 %i, 1
  %cmp = icmp eq i32 %limit, %i.inc
  br i1 %cmp, label %exit, label %loop

exit:
  ret void
}

; SI-LABEL: @multi_vcond_loop

; Load loop limit from buffer
; Branch to exit if uniformly not taken
; SI: ; BB#0:
; SI: buffer_load_dword [[VBOUND:v[0-9]+]]
; SI: v_cmp_gt_i32_e64 [[OUTER_CMP_SREG:s\[[0-9]+:[0-9]+\]]]
; SI: s_and_saveexec_b64 [[OUTER_CMP_SREG]], [[OUTER_CMP_SREG]]
; SI: s_xor_b64 [[OUTER_CMP_SREG]], exec, [[OUTER_CMP_SREG]]
; SI: s_cbranch_execz BB3_2

; Initialize inner condition to false
; SI: ; BB#1:
; SI: s_mov_b64 [[ZERO:s\[[0-9]+:[0-9]+\]]], 0{{$}}
; SI: s_mov_b64 [[COND_STATE:s\[[0-9]+:[0-9]+\]]], [[ZERO]]

; Clear exec bits for workitems that load -1s
; SI: BB3_3:
; SI: buffer_load_dword [[B:v[0-9]+]]
; SI: buffer_load_dword [[A:v[0-9]+]]
; SI-DAG: v_cmp_ne_i32_e64 [[NEG1_CHECK_0:s\[[0-9]+:[0-9]+\]]], [[A]], -1
; SI-DAG: v_cmp_ne_i32_e64 [[NEG1_CHECK_1:s\[[0-9]+:[0-9]+\]]], [[B]], -1
; SI: s_and_b64 [[ORNEG1:s\[[0-9]+:[0-9]+\]]], [[NEG1_CHECK_1]], [[NEG1_CHECK_0]]
; SI: s_and_saveexec_b64 [[ORNEG1]], [[ORNEG1]]
; SI: s_xor_b64 [[ORNEG1]], exec, [[ORNEG1]]
; SI: s_cbranch_execz BB3_5

; SI: BB#4:
; SI: buffer_store_dword
; SI: v_cmp_ge_i64_e32 vcc
; SI: s_or_b64 [[COND_STATE]], vcc, [[COND_STATE]]

; SI: BB3_5:
; SI: s_or_b64 exec, exec, [[ORNEG1]]
; SI: s_or_b64 [[COND_STATE]], [[ORNEG1]], [[COND_STATE]]
; SI: s_andn2_b64 exec, exec, [[COND_STATE]]
; SI: s_cbranch_execnz BB3_3

; SI: BB#6
; SI: s_or_b64 exec, exec, [[COND_STATE]]

; SI: BB3_2:
; SI-NOT: [[COND_STATE]]
; SI: s_endpgm

define void @multi_vcond_loop(i32 addrspace(1)* noalias nocapture %arg, i32 addrspace(1)* noalias nocapture readonly %arg1, i32 addrspace(1)* noalias nocapture readonly %arg2, i32 addrspace(1)* noalias nocapture readonly %arg3) #1 {
bb:
  %tmp = tail call i32 @llvm.r600.read.tidig.x() #0
  %tmp4 = sext i32 %tmp to i64
  %tmp5 = getelementptr inbounds i32 addrspace(1)* %arg3, i64 %tmp4
  %tmp6 = load i32 addrspace(1)* %tmp5, align 4
  %tmp7 = icmp sgt i32 %tmp6, 0
  %tmp8 = sext i32 %tmp6 to i64
  br i1 %tmp7, label %bb10, label %bb26

bb10:                                             ; preds = %bb, %bb20
  %tmp11 = phi i64 [ %tmp23, %bb20 ], [ 0, %bb ]
  %tmp12 = add nsw i64 %tmp11, %tmp4
  %tmp13 = getelementptr inbounds i32 addrspace(1)* %arg1, i64 %tmp12
  %tmp14 = load i32 addrspace(1)* %tmp13, align 4
  %tmp15 = getelementptr inbounds i32 addrspace(1)* %arg2, i64 %tmp12
  %tmp16 = load i32 addrspace(1)* %tmp15, align 4
  %tmp17 = icmp ne i32 %tmp14, -1
  %tmp18 = icmp ne i32 %tmp16, -1
  %tmp19 = and i1 %tmp17, %tmp18
  br i1 %tmp19, label %bb20, label %bb26

bb20:                                             ; preds = %bb10
  %tmp21 = add nsw i32 %tmp16, %tmp14
  %tmp22 = getelementptr inbounds i32 addrspace(1)* %arg, i64 %tmp12
  store i32 %tmp21, i32 addrspace(1)* %tmp22, align 4
  %tmp23 = add nuw nsw i64 %tmp11, 1
  %tmp24 = icmp slt i64 %tmp23, %tmp8
  br i1 %tmp24, label %bb10, label %bb26

bb26:                                             ; preds = %bb10, %bb20, %bb
  ret void
}

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
