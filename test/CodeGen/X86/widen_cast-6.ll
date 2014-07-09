; RUN: llc < %s -march=x86 -mattr=+sse4.1 | FileCheck %s

; Test bit convert that requires widening in the operand.

define i32 @return_v2hi() nounwind {
; CHECK-LABEL: @return_v2hi
; CHECK:      pushl
; CHECK-NEXT: xorl %eax, %eax
; CHECK-NEXT: popl
; CHECK-NEXT: ret
entry:
	%retval12 = bitcast <2 x i16> zeroinitializer to i32		; <i32> [#uses=1]
	ret i32 %retval12
}
