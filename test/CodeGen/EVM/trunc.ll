; RUN: llc < %s -mtriple=evm -filetype=asm | FileCheck %s

define i8 @trunc8_loreg(i256 %x, i256 %y) {
; CHECK-LABEL: trunc8_loreg__:
  %conv = trunc i256 %y to i8
  ret i8 %conv
}

define i8 @trunc8_hireg(i256 %x, i256 %y) {
; CHECK-LABEL: trunc8_hireg__:
  %shr1 = lshr i256 %y, 8
  %conv = trunc i256 %shr1 to i8
  ret i8 %conv
}
