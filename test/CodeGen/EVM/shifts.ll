; RUN: llc < %s -march=evm -filetype=asm | FileCheck %s

define i256 @lshr(i256 %a, i256 %cnt) {
entry:
; CHECK-LABEL: lshr__:
  %shr = lshr i256 %a, %cnt
; CHECK: SHR
  ret i256 %shr
}

define i256 @ashr(i256 %a, i256 %cnt)   {
entry:
; CHECK-LABEL: ashr__:
  %shr = ashr i256 %a, %cnt
; CHECK: SAR
  ret i256 %shr
}

define i256 @shl(i256 %a, i256 %cnt)   {
; CHECK: shl__
entry:
  %shl = shl i256 %a, %cnt
; CHECK: SHL
  ret i256 %shl
}

