; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI %s

declare i32 @llvm.SI.tid() readnone

; SI-LABEL: {{^}}test_array_ptr_calc:
; SI: v_mul_lo_i32
; SI: v_mul_hi_i32
define void @test_array_ptr_calc(i32 addrspace(1)* noalias %out, [1025 x i32] addrspace(1)* noalias %inA, i32 addrspace(1)* noalias %inB) {
  %tid = call i32 @llvm.SI.tid() readnone
  %a_ptr = getelementptr [1025 x i32] addrspace(1)* %inA, i32 %tid, i32 0
  %b_ptr = getelementptr i32 addrspace(1)* %inB, i32 %tid
  %a = load i32 addrspace(1)* %a_ptr
  %b = load i32 addrspace(1)* %b_ptr
  %result = add i32 %a, %b
  store i32 %result, i32 addrspace(1)* %out
  ret void
}
