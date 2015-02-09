; RUN: llc < %s -march=amdgcn -mcpu=SI -verify-machineinstrs | FileCheck %s

; This tests that the llvm.SI.end.cf intrinsic is not inserted into the
; loop block.  This intrinsic will be lowered to s_or_b64 by the code
; generator.

; CHECK-LABEL: {{^}}test:

; This is was lowered from the llvm.SI.end.cf intrinsic:
; CHECK: s_or_b64 exec, exec

; CHECK: [[LOOP_LABEL:[0-9A-Za-z_]+]]: ; %loop{{$}}
; CHECK-NOT: s_or_b64 exec, exec
; CHECK: s_cbranch_execnz [[LOOP_LABEL]]
define void @test(i32 addrspace(1)* %out, i32 %cond) {
entry:
  %tmp0 = icmp eq i32 %cond, 0
  br i1 %tmp0, label %if, label %loop

if:
  store i32 0, i32 addrspace(1)* %out
  br label %loop

loop:
  %tmp1 = phi i32 [0, %entry], [0, %if], [%inc, %loop]
  %inc = add i32 %tmp1, %cond
  %tmp2 = icmp ugt i32 %inc, 10
  br i1 %tmp2, label %done, label %loop

done:
  %tmp3 = getelementptr i32 addrspace(1)* %out, i64 1
  store i32 %inc, i32 addrspace(1)* %tmp3
  ret void
}
