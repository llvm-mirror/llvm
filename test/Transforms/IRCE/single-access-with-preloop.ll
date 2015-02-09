; RUN: opt -irce -S < %s | FileCheck %s

define void @single_access_with_preloop(i32 *%arr, i32 *%a_len_ptr, i32 %n, i32 %offset) {
 entry:
  %len = load i32* %a_len_ptr, !range !0
  %first.itr.check = icmp sgt i32 %n, 0
  br i1 %first.itr.check, label %loop, label %exit

 loop:
  %idx = phi i32 [ 0, %entry ] , [ %idx.next, %in.bounds ]
  %idx.next = add i32 %idx, 1
  %array.idx = add i32 %idx, %offset
  %abc.high = icmp slt i32 %array.idx, %len
  %abc.low = icmp sge i32 %array.idx, 0
  %abc = and i1 %abc.low, %abc.high
  br i1 %abc, label %in.bounds, label %out.of.bounds, !prof !1

 in.bounds:
  %addr = getelementptr i32* %arr, i32 %array.idx
  store i32 0, i32* %addr
  %next = icmp slt i32 %idx.next, %n
  br i1 %next, label %loop, label %exit

 out.of.bounds:
  ret void

 exit:
  ret void
}

; CHECK-LABEL: loop.preheader:
; CHECK: [[safe_start:[^ ]+]] = sub i32 0, %offset
; CHECK: [[safe_end:[^ ]+]] = sub i32 %len, %offset
; CHECK: [[exit_preloop_at_cond:[^ ]+]] = icmp slt i32 %n, [[safe_start]]
; CHECK: [[exit_preloop_at:[^ ]+]] = select i1 [[exit_preloop_at_cond]], i32 %n, i32 [[safe_start]]
; CHECK: [[exit_mainloop_at_cond:[^ ]+]] = icmp slt i32 %n, [[safe_end]]
; CHECK: [[exit_mainloop_at:[^ ]+]] = select i1 [[exit_mainloop_at_cond]], i32 %n, i32 [[safe_end]]

; CHECK-LABEL: in.bounds:
; CHECK: [[continue_mainloop_cond:[^ ]+]] = icmp slt i32 %idx.next, [[exit_mainloop_at]]
; CHECK: br i1 [[continue_mainloop_cond]], label %loop, label %main.exit.selector

; CHECK-LABEL: main.exit.selector:
; CHECK: [[mainloop_its_left:[^ ]+]] = icmp slt i32 %idx.next, %n
; CHECK: br i1 [[mainloop_its_left]], label %main.pseudo.exit, label %exit.loopexit

; CHECK-LABEL: in.bounds.preloop:
; CHECK: [[continue_preloop_cond:[^ ]+]] = icmp slt i32 %idx.next.preloop, [[exit_preloop_at]]
; CHECK: br i1 [[continue_preloop_cond]], label %loop.preloop, label %preloop.exit.selector

; CHECK-LABEL: preloop.exit.selector:
; CHECK: [[preloop_its_left:[^ ]+]] = icmp slt i32 %idx.next.preloop, %n
; CHECK: br i1 [[preloop_its_left]], label %preloop.pseudo.exit, label %exit.loopexit

; CHECK-LABEL: in.bounds.postloop:
; CHECK: %next.postloop = icmp slt i32 %idx.next.postloop, %n
; CHECK: br i1 %next.postloop, label %loop.postloop, label %exit.loopexit

!0 = !{i32 0, i32 2147483647}
!1 = !{!"branch_weights", i32 64, i32 4}
