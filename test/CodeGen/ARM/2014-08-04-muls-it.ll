; RUN: llc -mtriple thumbv7-eabi -arm-restrict-it -filetype asm -o - %s \
; RUN:    | FileCheck %s

define arm_aapcscc i32 @function(i32 %i, i32 %j) {
entry:
  %cmp = icmp eq i32 %i, %j
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %mul = mul nsw i32 %i, %i
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %i.addr.0 = phi i32 [ %mul, %if.then ], [ %i, %entry ]
  ret i32 %i.addr.0
}

; CHECK-LABEL: function
; CHECK: cmp r0, r1
; CHECK: bne [[LABEL:[.*]]]
; CHECK-NOT: mulseq r0, r0, r0
; CHECK: [[LABEL]]
; CHECK: muls r0, r0, r0
; CHECK: bx lr

