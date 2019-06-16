; RUN: llc-mtriple=evm -filetype=asm | FileCheck %s

define i256 @foo__(i256 %b, i256 %c) nounwind readnone ssp {
; CHECK-LABEL: foo__
entry:
  %mul = sub i256 0, %b
  %tobool = icmp eq i256 %c, 0
  %b.mul = select i1 %tobool, i256 %b, i256 %mul
  %add = add nsw i256 %b.mul, %c
  ret i256 %add
}

define i256 @foo3__(i256 %b, i256 %c) nounwind readnone ssp {
; CHECK-LABEL: foo3__
entry:
  %not.tobool = icmp ne i256 %c, 0
  %xor = sext i1 %not.tobool to i256
  %b.xor = xor i256 %xor, %b
  %add = add nsw i256 %b.xor, %c
  ret i256 %add
}

define i256 @foo4__(i256 %a) nounwind ssp {
; CHECK-LABEL: foo4__
  %cmp = icmp sgt i256 %a, -1
  %neg = sub nsw i256 0, %a
  %cond = select i1 %cmp, i256 %a, i256 %neg
  ret i256 %cond
}

define i256 @foo5(i256 %a, i256 %b) nounwind ssp {
entry:
; CHECK-LABEL: foo5
  %sub = sub nsw i256 %a, %b
  %cmp = icmp sgt i256 %sub, -1
  %sub3 = sub nsw i256 0, %sub
  %cond = select i1 %cmp, i256 %sub, i256 %sub3
  ret i256 %cond
}

; make sure we can handle branch instruction in optimizeCompare.
define i256 @foo6(i256 %a, i256 %b) nounwind ssp {
; CHECK-LABEL: foo6__
  %sub = sub nsw i256 %a, %b
  %cmp = icmp sgt i256 %sub, 0
  br i1 %cmp, label %l.if, label %l.else

l.if:
  ret i256 1

l.else:
  ret i256 %sub
}

define i256 @foo7(i256 %a, i256 %b) nounwind {
entry:
; CHECK-LABEL: foo7__:
  %sub = sub nsw i256 %a, %b
  %cmp = icmp sgt i256 %sub, -1
  %sub3 = sub nsw i256 0, %sub
  %cond = select i1 %cmp, i256 %sub, i256 %sub3
  br i1 %cmp, label %if.then, label %if.else

if.then:
  %cmp2 = icmp slt i256 %sub, -1
  %sel = select i1 %cmp2, i256 %cond, i256 %a
  ret i256 %sel

if.else:
  ret i256 %cond
}

define i256 @foo8(i256 %v, i256 %a, i256 %b) nounwind readnone ssp {
entry:
; CHECK-LABEL: foo8__:
  %tobool = icmp eq i256 %v, 0
  %neg = xor i256 -1, %b
  %cond = select i1 %tobool, i256 %neg, i256 %a
  ret i256 %cond
}

define i256 @foo9(i256 %v) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo9__:
  %tobool = icmp ne i256 %v, 0
  %cond = select i1 %tobool, i256 4, i256 -5
  ret i256 %cond
}

define i256 @foo11(i256 %v) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo11__:
  %tobool = icmp ne i256 %v, 0
  %cond = select i1 %tobool, i256 4, i256 -4
  ret i256 %cond
}

define i256 @foo13(i256 %v, i256 %a, i256 %b) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo13__:
  %tobool = icmp eq i256 %v, 0
  %sub = sub i256 0, %b
  %cond = select i1 %tobool, i256 %sub, i256 %a
  ret i256 %cond
}

define i64 @foo14(i64 %v, i64 %a, i64 %b) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo14__:
; CHECK: cmp x0, #0
; CHECK: csneg x0, x1, x2, ne
  %tobool = icmp eq i64 %v, 0
  %sub = sub i64 0, %b
  %cond = select i1 %tobool, i64 %sub, i64 %a
  ret i64 %cond
}

define i256 @foo15__(i256 %a, i256 %b) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo15:
  %cmp = icmp sgt i256 %a, %b
  %. = select i1 %cmp, i256 2, i256 1
  ret i256 %.
}

define i256 @foo16(i256 %a, i256 %b) nounwind readnone optsize ssp {
entry:
; CHECK-LABEL: foo16__:
  %cmp = icmp sgt i256 %a, %b
  %. = select i1 %cmp, i256 1, i256 2
  ret i256 %.
}

define i256 @foo20(i256 %x) {
; CHECK-LABEL: foo20__:
  %cmp = icmp eq i256 %x, 5
  %res = select i1 %cmp, i256 6, i256 1
  ret i256 %res
}

define i256 @foo22(i256 %x) {
; CHECK-LABEL: foo22__:
  %cmp = icmp eq i256 %x, 5
  %res = select i1 %cmp, i256 1, i256 6
  ret i256 %res
}

