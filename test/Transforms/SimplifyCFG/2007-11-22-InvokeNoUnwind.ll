; RUN: opt < %s -simplifycfg -S | FileCheck %s

; CHECK-NOT: invoke

declare i32 @func(i8*) nounwind

define i32 @test() {
	invoke i32 @func( i8* null )
			to label %Cont unwind label %Other		; <i32>:1 [#uses=0]

Cont:		; preds = %0
	ret i32 0

Other:		; preds = %0
	ret i32 1
}
