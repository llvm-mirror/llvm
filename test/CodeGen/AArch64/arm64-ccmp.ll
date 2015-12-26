; RUN: llc < %s -mcpu=cyclone -verify-machineinstrs -aarch64-ccmp -aarch64-stress-ccmp | FileCheck %s
target triple = "arm64-apple-ios"

; CHECK: single_same
; CHECK: cmp w0, #5
; CHECK-NEXT: ccmp w1, #17, #4, ne
; CHECK-NEXT: b.ne
; CHECK: %if.then
; CHECK: bl _foo
; CHECK: %if.end
define i32 @single_same(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 5
  %cmp1 = icmp eq i32 %b, 17
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Different condition codes for the two compares.
; CHECK: single_different
; CHECK: cmp w0, #6
; CHECK-NEXT: ccmp w1, #17, #0, ge
; CHECK-NEXT: b.eq
; CHECK: %if.then
; CHECK: bl _foo
; CHECK: %if.end
define i32 @single_different(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp sle i32 %a, 5
  %cmp1 = icmp ne i32 %b, 17
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Second block clobbers the flags, can't convert (easily).
; CHECK: single_flagclobber
; CHECK: cmp
; CHECK: b.eq
; CHECK: cmp
; CHECK: b.gt
define i32 @single_flagclobber(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 5
  br i1 %cmp, label %if.then, label %lor.lhs.false

lor.lhs.false:                                    ; preds = %entry
  %cmp1 = icmp slt i32 %b, 7
  %mul = shl nsw i32 %b, 1
  %add = add nsw i32 %b, 1
  %cond = select i1 %cmp1, i32 %mul, i32 %add
  %cmp2 = icmp slt i32 %cond, 17
  br i1 %cmp2, label %if.then, label %if.end

if.then:                                          ; preds = %lor.lhs.false, %entry
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:                                           ; preds = %if.then, %lor.lhs.false
  ret i32 7
}

; Second block clobbers the flags and ends with a tbz terminator.
; CHECK: single_flagclobber_tbz
; CHECK: cmp
; CHECK: b.eq
; CHECK: cmp
; CHECK: tbz
define i32 @single_flagclobber_tbz(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 5
  br i1 %cmp, label %if.then, label %lor.lhs.false

lor.lhs.false:                                    ; preds = %entry
  %cmp1 = icmp slt i32 %b, 7
  %mul = shl nsw i32 %b, 1
  %add = add nsw i32 %b, 1
  %cond = select i1 %cmp1, i32 %mul, i32 %add
  %and = and i32 %cond, 8
  %cmp2 = icmp ne i32 %and, 0
  br i1 %cmp2, label %if.then, label %if.end

if.then:                                          ; preds = %lor.lhs.false, %entry
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:                                           ; preds = %if.then, %lor.lhs.false
  ret i32 7
}

; Speculatively execute division by zero.
; The sdiv/udiv instructions do not trap when the divisor is zero, so they are
; safe to speculate.
; CHECK-LABEL: speculate_division:
; CHECK: cmp w0, #1
; CHECK: sdiv [[DIVRES:w[0-9]+]], w1, w0
; CHECK: ccmp [[DIVRES]], #16, #0, ge
; CHECK: b.gt [[BLOCK:LBB[0-9_]+]]
; CHECK: bl _foo
; CHECK: [[BLOCK]]:
; CHECK: orr w0, wzr, #0x7
define i32 @speculate_division(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %land.lhs.true, label %if.end

land.lhs.true:
  %div = sdiv i32 %b, %a
  %cmp1 = icmp slt i32 %div, 17
  br i1 %cmp1, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Floating point compare.
; CHECK: single_fcmp
; CHECK: cmp
; CHECK-NOT: b.
; CHECK: fccmp {{.*}}, #8, ge
; CHECK: b.lt
define i32 @single_fcmp(i32 %a, float %b) nounwind ssp {
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %land.lhs.true, label %if.end

land.lhs.true:
  %conv = sitofp i32 %a to float
  %div = fdiv float %b, %conv
  %cmp1 = fcmp oge float %div, 1.700000e+01
  br i1 %cmp1, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Chain multiple compares.
; CHECK: multi_different
; CHECK: cmp
; CHECK: ccmp
; CHECK: ccmp
; CHECK: b.
define void @multi_different(i32 %a, i32 %b, i32 %c) nounwind ssp {
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %land.lhs.true, label %if.end

land.lhs.true:
  %div = sdiv i32 %b, %a
  %cmp1 = icmp eq i32 %div, 5
  %cmp4 = icmp sgt i32 %div, %c
  %or.cond = and i1 %cmp1, %cmp4
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret void
}

; Convert a cbz in the head block.
; CHECK: cbz_head
; CHECK: cmp w0, #0
; CHECK: ccmp
define i32 @cbz_head(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 0
  %cmp1 = icmp ne i32 %b, 17
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Check that the immediate operand is in range. The ccmp instruction encodes a
; smaller range of immediates than subs/adds.
; The ccmp immediates must be in the range 0-31.
; CHECK: immediate_range
; CHECK-NOT: ccmp
define i32 @immediate_range(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 5
  %cmp1 = icmp eq i32 %b, 32
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Convert a cbz in the second block.
; CHECK: cbz_second
; CHECK: cmp w0, #0
; CHECK: ccmp w1, #0, #0, ne
; CHECK: b.eq
define i32 @cbz_second(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 0
  %cmp1 = icmp ne i32 %b, 0
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}

; Convert a cbnz in the second block.
; CHECK: cbnz_second
; CHECK: cmp w0, #0
; CHECK: ccmp w1, #0, #4, ne
; CHECK: b.ne
define i32 @cbnz_second(i32 %a, i32 %b) nounwind ssp {
entry:
  %cmp = icmp eq i32 %a, 0
  %cmp1 = icmp eq i32 %b, 0
  %or.cond = or i1 %cmp, %cmp1
  br i1 %or.cond, label %if.then, label %if.end

if.then:
  %call = tail call i32 @foo() nounwind
  br label %if.end

if.end:
  ret i32 7
}
declare i32 @foo()

%str1 = type { %str2 }
%str2 = type { [24 x i8], i8*, i32, %str1*, i32, [4 x i8], %str1*, %str1*, %str1*, %str1*, %str1*, %str1*, %str1*, %str1*, %str1*, i8*, i8, i8*, %str1*, i8* }

; Test case distilled from 126.gcc.
; The phi in sw.bb.i.i gets multiple operands for the %entry predecessor.
; CHECK: build_modify_expr
define void @build_modify_expr() nounwind ssp {
entry:
  switch i32 undef, label %sw.bb.i.i [
    i32 69, label %if.end85
    i32 70, label %if.end85
    i32 71, label %if.end85
    i32 72, label %if.end85
    i32 73, label %if.end85
    i32 105, label %if.end85
    i32 106, label %if.end85
  ]

if.end85:
  ret void

sw.bb.i.i:
  %ref.tr.i.i = phi %str1* [ %0, %sw.bb.i.i ], [ undef, %entry ]
  %operands.i.i = getelementptr inbounds %str1, %str1* %ref.tr.i.i, i64 0, i32 0, i32 2
  %arrayidx.i.i = bitcast i32* %operands.i.i to %str1**
  %0 = load %str1*, %str1** %arrayidx.i.i, align 8
  %code1.i.i.phi.trans.insert = getelementptr inbounds %str1, %str1* %0, i64 0, i32 0, i32 0, i64 16
  br label %sw.bb.i.i
}

; CHECK-LABEL: select_and
define i64 @select_and(i32 %w0, i32 %w1, i64 %x2, i64 %x3) {
; CHECK: cmp w1, #5
; CHECK-NEXT: ccmp w0, w1, #0, ne
; CHECK-NEXT: csel x0, x2, x3, lt
; CHECK-NEXT: ret
  %1 = icmp slt i32 %w0, %w1
  %2 = icmp ne i32 5, %w1
  %3 = and i1 %1, %2
  %sel = select i1 %3, i64 %x2, i64 %x3
  ret i64 %sel
}

; CHECK-LABEL: select_or
define i64 @select_or(i32 %w0, i32 %w1, i64 %x2, i64 %x3) {
; CHECK: cmp w1, #5
; CHECK-NEXT: ccmp w0, w1, #8, eq
; CHECK-NEXT: csel x0, x2, x3, lt
; CHECK-NEXT: ret
  %1 = icmp slt i32 %w0, %w1
  %2 = icmp ne i32 5, %w1
  %3 = or i1 %1, %2
  %sel = select i1 %3, i64 %x2, i64 %x3
  ret i64 %sel
}

; CHECK-LABEL: select_complicated
define i16 @select_complicated(double %v1, double %v2, i16 %a, i16 %b) {
; CHECK: ldr [[REG:d[0-9]+]],
; CHECK: fcmp d0, d2
; CHECK-NEXT: fmov d2, #13.00000000
; CHECK-NEXT: fccmp d1, d2, #4, ne
; CHECK-NEXT: fccmp d0, d1, #1, ne
; CHECK-NEXT: fccmp d0, d1, #4, vc
; CEHCK-NEXT: csel w0, w0, w1, eq
  %1 = fcmp one double %v1, %v2
  %2 = fcmp oeq double %v2, 13.0
  %3 = fcmp oeq double %v1, 42.0
  %or0 = or i1 %2, %3
  %or1 = or i1 %1, %or0
  %sel = select i1 %or1, i16 %a, i16 %b
  ret i16 %sel
}

; CHECK-LABEL: gccbug
define i64 @gccbug(i64 %x0, i64 %x1) {
; CHECK: cmp x0, #2
; CHECK-NEXT: ccmp x0, #4, #4, ne
; CHECK-NEXT: ccmp x1, #0, #0, eq
; CHECK-NEXT: orr w[[REGNUM:[0-9]+]], wzr, #0x1
; CHECK-NEXT: cinc x0, x[[REGNUM]], eq
; CHECK-NEXT: ret
  %cmp0 = icmp eq i64 %x1, 0
  %cmp1 = icmp eq i64 %x0, 2
  %cmp2 = icmp eq i64 %x0, 4

  %or = or i1 %cmp2, %cmp1
  %and = and i1 %or, %cmp0

  %sel = select i1 %and, i64 2, i64 1
  ret i64 %sel
}

; CHECK-LABEL: select_ororand
define i32 @select_ororand(i32 %w0, i32 %w1, i32 %w2, i32 %w3) {
; CHECK: cmp w3, #4
; CHECK-NEXT: ccmp w2, #2, #0, gt
; CHECK-NEXT: ccmp w1, #13, #2, ge
; CHECK-NEXT: ccmp w0, #0, #4, ls
; CHECK-NEXT: csel w0, w3, wzr, eq
; CHECK-NEXT: ret
  %c0 = icmp eq i32 %w0, 0
  %c1 = icmp ugt i32 %w1, 13
  %c2 = icmp slt i32 %w2, 2
  %c4 = icmp sgt i32 %w3, 4
  %or = or i1 %c0, %c1
  %and = and i1 %c2, %c4
  %or1 = or i1 %or, %and
  %sel = select i1 %or1, i32 %w3, i32 0
  ret i32 %sel
}

; CHECK-LABEL: select_andor
define i32 @select_andor(i32 %v1, i32 %v2, i32 %v3) {
; CHECK: cmp w1, w2
; CHECK-NEXT: ccmp w0, #0, #4, lt
; CHECK-NEXT: ccmp w0, w1, #0, eq
; CHECK-NEXT: csel w0, w0, w1, eq
; CHECK-NEXT: ret
  %c0 = icmp eq i32 %v1, %v2
  %c1 = icmp sge i32 %v2, %v3
  %c2 = icmp eq i32 %v1, 0
  %or = or i1 %c2, %c1
  %and = and i1 %or, %c0
  %sel = select i1 %and, i32 %v1, i32 %v2
  ret i32 %sel
}

; CHECK-LABEL: select_noccmp1
define i64 @select_noccmp1(i64 %v1, i64 %v2, i64 %v3, i64 %r) {
; CHECK: cmp x0, #0
; CHECK-NEXT: cset [[REG0:w[0-9]+]], lt
; CHECK-NEXT: cmp x0, #13
; CHECK-NOT: ccmp
; CHECK-NEXT: cset [[REG1:w[0-9]+]], gt
; CHECK-NEXT: cmp x2, #2
; CHECK-NEXT: cset [[REG2:w[0-9]+]], lt
; CHECK-NEXT: cmp x2, #4
; CHECK-NEXT: cset [[REG3:w[0-9]+]], gt
; CHECK-NEXT: and [[REG4:w[0-9]+]], [[REG0]], [[REG1]]
; CHECK-NEXT: and [[REG5:w[0-9]+]], [[REG2]], [[REG3]]
; CHECK-NEXT: orr [[REG6:w[0-9]+]], [[REG4]], [[REG5]]
; CHECK-NEXT: cmp [[REG6]], #0
; CHECK-NEXT: csel x0, xzr, x3, ne
; CHECK-NEXT: ret
  %c0 = icmp slt i64 %v1, 0
  %c1 = icmp sgt i64 %v1, 13
  %c2 = icmp slt i64 %v3, 2
  %c4 = icmp sgt i64 %v3, 4
  %and0 = and i1 %c0, %c1
  %and1 = and i1 %c2, %c4
  %or = or i1 %and0, %and1
  %sel = select i1 %or, i64 0, i64 %r
  ret i64 %sel
}

@g = global i32 0

; Should not use ccmp if we have to compute the or expression in an integer
; register anyway because of other users.
; CHECK-LABEL: select_noccmp2
define i64 @select_noccmp2(i64 %v1, i64 %v2, i64 %v3, i64 %r) {
; CHECK: cmp x0, #0
; CHECK-NEXT: cset [[REG0:w[0-9]+]], lt
; CHECK-NOT: ccmp
; CHECK-NEXT: cmp x0, #13
; CHECK-NEXT: cset [[REG1:w[0-9]+]], gt
; CHECK-NEXT: orr [[REG2:w[0-9]+]], [[REG0]], [[REG1]]
; CHECK-NEXT: cmp [[REG2]], #0
; CHECK-NEXT: csel x0, xzr, x3, ne
; CHECK-NEXT: sbfx [[REG3:w[0-9]+]], [[REG2]], #0, #1
; CHECK-NEXT: adrp x[[REGN4:[0-9]+]], _g@PAGE
; CHECK-NEXT: str [[REG3]], [x[[REGN4]], _g@PAGEOFF]
; CHECK-NEXT: ret
  %c0 = icmp slt i64 %v1, 0
  %c1 = icmp sgt i64 %v1, 13
  %or = or i1 %c0, %c1
  %sel = select i1 %or, i64 0, i64 %r
  %ext = sext i1 %or to i32
  store volatile i32 %ext, i32* @g
  ret i64 %sel
}
