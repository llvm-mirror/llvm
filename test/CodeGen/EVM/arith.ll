; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @add(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: add:
  %1 = add i256 %a, %b
; CHECK: ADD
  ret i256 %1
}

define i256 @sub(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: sub:
  %1 = sub i256 %a, %b
; CHECK: SUB
  ret i256 %1
}


