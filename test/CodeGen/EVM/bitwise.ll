; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @andop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: andop:
  %1 = and i256 %a, %b
; CHECK: and
  ret i256 %1
}

define i256 @orop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: orop:
  %1 = or i256 %a, %b
; CHECK: OR
  ret i256 %1
}

define i256 @xorop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: xorop:
  %1 = xor i256 %a, %b
; CHECK: XOR
  ret i256 %1
}


