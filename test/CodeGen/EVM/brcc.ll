; RUN: llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @brcc1(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: brcc1
entry:
  %wb = icmp eq i256 %a, %b
  br i1 %wb, label %t1, label %t2
t1:
  %t1v = add i256 %a, 4
  br label %exit
t2:
  %t2v = add i256 %b, 8
  br label %exit
exit:
  %v = phi i256 [ %t1v, %t1 ], [ %t2v, %t2 ]
  ret i256 %v
}

; CHECK-LABEL: brcc2
; CHECK: breq %r0, %r1
define i256 @brcc2(i256 %a, i256 %b) nounwind {
entry:
  %wb = icmp ne i256 %a, %b
  br i1 %wb, label %t1, label %t2
t1:
  %t1v = add i256 %a, 4
  br label %exit
t2:
  %t2v = add i256 %b, 8
  br label %exit
exit:
  %v = phi i256 [ %t1v, %t1 ], [ %t2v, %t2 ]
  ret i256 %v
}


