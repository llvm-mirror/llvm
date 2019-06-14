; RUN: llc < %s -march=evm -filetype=asm | FileCheck %s

define zeroext i8 @lshr8(i8 zeroext %a, i8 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: lshr8__:
  %shr = lshr i8 %a, %cnt
  ret i8 %shr
}

define signext i8 @ashr8(i8 signext %a, i8 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: ashr8:
  %shr = ashr i8 %a, %cnt
  ret i8 %shr
}

define zeroext i8 @shl8(i8 zeroext %a, i8 zeroext %cnt) nounwind readnone {
entry:
; CHECK: shl8
  %shl = shl i8 %a, %cnt
  ret i8 %shl
}

define zeroext i16 @lshr16(i16 zeroext %a, i16 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: lshr16:
  %shr = lshr i16 %a, %cnt
  ret i16 %shr
}

define signext i16 @ashr16(i16 signext %a, i16 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: ashr16:
  %shr = ashr i16 %a, %cnt
  ret i16 %shr
}

define zeroext i16 @shl16(i16 zeroext %a, i16 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: shl16:
  %shl = shl i16 %a, %cnt
  ret i16 %shl
}

define zeroext i32 @lshr32(i32 zeroext %a, i32 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: lshr32:
  %shr = lshr i32 %a, %cnt
  ret i32 %shr
}

define signext i32 @ashr32(i32 signext %a, i32 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: ashr32:
  %shr = ashr i32 %a, %cnt
  ret i32 %shr
}

define zeroext i32 @shl32(i32 zeroext %a, i32 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: shl32:
  %shl = shl i32 %a, %cnt
  ret i32 %shl
}

define zeroext i64 @lshr64(i64 zeroext %a, i64 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: lshr64:
  %shr = lshr i64 %a, %cnt
  ret i64 %shr
}

define signext i64 @ashr64(i64 signext %a, i64 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: ashr64:
  %shr = ashr i64 %a, %cnt
  ret i64 %shr
}

define zeroext i64 @shl64(i64 zeroext %a, i64 zeroext %cnt) nounwind readnone {
entry:
; CHECK-LABEL: shl64:
  %shl = shl i64 %a, %cnt
  ret i64 %shl
}
