; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @addop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: addop__:
  %1 = add i256 %a, %b
; CHECK: ADD
  ret i256 %1
}

define i256 @subop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: subop__:
  %1 = sub i256 %a, %b
; CHECK: SUB
  ret i256 %1
}

define i256 @mulop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: mulop__:
  %1 = mul i256 %a, %b
; CHECK: MUL
  ret i256 %1
}


define i256 @udivop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: udivop__:
  %1 = udiv i256 %a, %b
; CHECK: DIV
  ret i256 %1
}

define i256 @sdivop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: sdivop__:
  %1 = sdiv i256 %a, %b
; CHECK: DIV
  ret i256 %1
}

define i256 @uremop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: uremop__:
  %1 = srem i256 %a, %b
; CHECK: MOD
  ret i256 %1
}

define i256 @sremop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: sremop__:
  %1 = srem i256 %a, %b
; CHECK: SMOD
  ret i256 %1
}


