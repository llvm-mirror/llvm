; RUN: llc < %s -march=x86 | FileCheck %s 

@foo = global i8 127

define i32 @main() nounwind {
; CHECK-LABEL: main:
; CHECK-NOT: ret
; CHECK: sar{{.}} $5
; CHECK: ret

   %tmp = load i8* @foo
   %bf.lo = lshr i8 %tmp, 5
   %bf.lo.cleared = and i8 %bf.lo, 7
   %1 = shl i8 %bf.lo.cleared, 5
   %bf.val.sext = ashr i8 %1, 5
   %conv = sext i8 %bf.val.sext to i32
   ret i32 %conv
}
