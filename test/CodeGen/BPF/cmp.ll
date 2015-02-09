; RUN: llc < %s -march=bpf | FileCheck %s

; Function Attrs: nounwind readnone uwtable
define signext i8 @foo_cmp1(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp sgt i8 %a, %b
  br i1 %1, label %2, label %4

; <label>:2                                       ; preds = %0
  %3 = mul i8 %b, %a
  br label %6

; <label>:4                                       ; preds = %0
  %5 = shl i8 %b, 3
  br label %6

; <label>:6                                       ; preds = %4, %2
  %.0 = phi i8 [ %3, %2 ], [ %5, %4 ]
  ret i8 %.0
; CHECK-LABEL:foo_cmp1:
; CHECK: jsge r2, r1
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @foo_cmp2(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp slt i8 %a, %b
  br i1 %1, label %4, label %2

; <label>:2                                       ; preds = %0
  %3 = mul i8 %b, %a
  br label %6

; <label>:4                                       ; preds = %0
  %5 = shl i8 %b, 3
  br label %6

; <label>:6                                       ; preds = %4, %2
  %.0 = phi i8 [ %3, %2 ], [ %5, %4 ]
  ret i8 %.0
; CHECK-LABEL:foo_cmp2:
; CHECK: jsgt r2, r1
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @foo_cmp3(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp slt i8 %a, %b
  br i1 %1, label %2, label %4

; <label>:2                                       ; preds = %0
  %3 = mul i8 %b, %a
  br label %6

; <label>:4                                       ; preds = %0
  %5 = shl i8 %b, 3
  br label %6

; <label>:6                                       ; preds = %4, %2
  %.0 = phi i8 [ %3, %2 ], [ %5, %4 ]
  ret i8 %.0
; CHECK-LABEL:foo_cmp3:
; CHECK: jsge r1, r2
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @foo_cmp4(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp sgt i8 %a, %b
  br i1 %1, label %4, label %2

; <label>:2                                       ; preds = %0
  %3 = mul i8 %b, %a
  br label %6

; <label>:4                                       ; preds = %0
  %5 = shl i8 %b, 3
  br label %6

; <label>:6                                       ; preds = %4, %2
  %.0 = phi i8 [ %3, %2 ], [ %5, %4 ]
  ret i8 %.0
; CHECK-LABEL:foo_cmp4:
; CHECK: jsgt r1, r2
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @min(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp slt i8 %a, %b
  %a.b = select i1 %1, i8 %a, i8 %b
  ret i8 %a.b
; CHECK-LABEL:min:
; CHECK: jsgt r2, r1
; CHECK: mov r1, r2
; CHECK: mov r0, r1
}

; Function Attrs: nounwind readnone uwtable
define zeroext i8 @minu(i8 zeroext %a, i8 zeroext %b) #0 {
  %1 = icmp ult i8 %a, 100
  %a.b = select i1 %1, i8 %a, i8 %b
  ret i8 %a.b
; CHECK-LABEL:minu:
; CHECK: jgt r3, r1
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @max(i8 signext %a, i8 signext %b) #0 {
  %1 = icmp sgt i8 %a, %b
  %a.b = select i1 %1, i8 %a, i8 %b
  ret i8 %a.b
; CHECK-LABEL:max:
; CHECK: jsgt r1, r2
}

; Function Attrs: nounwind readnone uwtable
define signext i8 @meq(i8 signext %a, i8 signext %b, i8 signext %c) #0 {
  %1 = icmp eq i8 %a, %b
  %c.a = select i1 %1, i8 %c, i8 %a
  ret i8 %c.a
; CHECK-LABEL:meq:
; CHECK: jeq r1, r2
}
