; RUN: llc < %s -march=evm -filetype=asm | FileCheck %s

define i256 @am1(i256* %a) nounwind {
; CHECK-LABEL: am1:
  %1 = load i256, i256* %a
  ret i256 %1
}

@foo = external global i16

define i256 @am2() nounwind {
; CHECK-LABEL: am2:
  %1 = load i256, i256* @foo
  ret i256 %1
}

define i256 @am4() nounwind {
; CHECK-LABEL: am4:
  %1 = load volatile i256, i256* inttoptr(i256 32 to i256*)
  ret i256 %1
}

define i256 @am5(i256* %a) nounwind {
; CHECK-LABEL: am5:
  %1 = getelementptr i256, i256* %a, i256 2
  %2 = load i256, i256* %1
  ret i256 %2
}

%S = type { i256, i256 }
@baz = common global %S zeroinitializer, align 1

define i256 @am6() nounwind {
; CHECK-LABEL: am6:
  %1 = load i256, i256* getelementptr (%S, %S* @baz, i32 0, i32 1)
  ret i256 %1
}
