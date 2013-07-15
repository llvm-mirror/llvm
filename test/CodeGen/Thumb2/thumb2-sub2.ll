; RUN: llc < %s -march=thumb -mattr=+thumb2 | FileCheck %s

define i32 @f1(i32 %a) {
    %tmp = sub i32 %a, 4095
    ret i32 %tmp
}
; CHECK-LABEL: f1:
; CHECK: 	subw	r0, r0, #4095
