; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -instcombine -S | FileCheck %s

; With left shift, the comparison should not be modified.
define i1 @test_shift_and_cmp_not_changed1(i8 %p) {
; CHECK-LABEL: @test_shift_and_cmp_not_changed1(
; CHECK-NEXT:    [[SHLP:%.*]] = shl i8 [[P:%.*]], 5
; CHECK-NEXT:    [[ANDP:%.*]] = and i8 [[SHLP]], -64
; CHECK-NEXT:    [[CMP:%.*]] = icmp slt i8 [[ANDP]], 32
; CHECK-NEXT:    ret i1 [[CMP]]
;
  %shlp = shl i8 %p, 5
  %andp = and i8 %shlp, -64
  %cmp = icmp slt i8 %andp, 32
  ret i1 %cmp
}

; With arithmetic right shift, the comparison should not be modified.
define i1 @test_shift_and_cmp_not_changed2(i8 %p) {
; CHECK-LABEL: @test_shift_and_cmp_not_changed2(
; CHECK-NEXT:    [[SHLP:%.*]] = ashr i8 [[P:%.*]], 5
; CHECK-NEXT:    [[ANDP:%.*]] = and i8 [[SHLP]], -64
; CHECK-NEXT:    [[CMP:%.*]] = icmp slt i8 [[ANDP]], 32
; CHECK-NEXT:    ret i1 [[CMP]]
;
  %shlp = ashr i8 %p, 5
  %andp = and i8 %shlp, -64
  %cmp = icmp slt i8 %andp, 32
  ret i1 %cmp
}

; This should simplify functionally to the left shift case.
; The extra input parameter should be optimized away.
define i1 @test_shift_and_cmp_changed1(i8 %p, i8 %q) {
; CHECK-LABEL: @test_shift_and_cmp_changed1(
; CHECK-NEXT:    [[ANDP:%.*]] = shl i8 [[P:%.*]], 5
; CHECK-NEXT:    [[SHL:%.*]] = and i8 [[ANDP]], -64
; CHECK-NEXT:    [[CMP:%.*]] = icmp slt i8 [[SHL]], 32
; CHECK-NEXT:    ret i1 [[CMP]]
;
  %andp = and i8 %p, 6
  %andq = and i8 %q, 8
  %or = or i8 %andq, %andp
  %shl = shl i8 %or, 5
  %ashr = ashr i8 %shl, 5
  %cmp = icmp slt i8 %ashr, 1
  ret i1 %cmp
}

define <2 x i1> @test_shift_and_cmp_changed1_vec(<2 x i8> %p, <2 x i8> %q) {
; CHECK-LABEL: @test_shift_and_cmp_changed1_vec(
; CHECK-NEXT:    [[ANDP:%.*]] = shl <2 x i8> [[P:%.*]], <i8 5, i8 5>
; CHECK-NEXT:    [[SHL:%.*]] = and <2 x i8> [[ANDP]], <i8 -64, i8 -64>
; CHECK-NEXT:    [[CMP:%.*]] = icmp slt <2 x i8> [[SHL]], <i8 32, i8 32>
; CHECK-NEXT:    ret <2 x i1> [[CMP]]
;
  %andp = and <2 x i8> %p, <i8 6, i8 6>
  %andq = and <2 x i8> %q, <i8 8, i8 8>
  %or = or <2 x i8> %andq, %andp
  %shl = shl <2 x i8> %or, <i8 5, i8 5>
  %ashr = ashr <2 x i8> %shl, <i8 5, i8 5>
  %cmp = icmp slt <2 x i8> %ashr, <i8 1, i8 1>
  ret <2 x i1> %cmp
}

; Unsigned compare allows a transformation to compare against 0.
define i1 @test_shift_and_cmp_changed2(i8 %p) {
; CHECK-LABEL: @test_shift_and_cmp_changed2(
; CHECK-NEXT:    [[ANDP:%.*]] = and i8 [[P:%.*]], 6
; CHECK-NEXT:    [[CMP:%.*]] = icmp eq i8 [[ANDP]], 0
; CHECK-NEXT:    ret i1 [[CMP]]
;
  %shlp = shl i8 %p, 5
  %andp = and i8 %shlp, -64
  %cmp = icmp ult i8 %andp, 32
  ret i1 %cmp
}

define <2 x i1> @test_shift_and_cmp_changed2_vec(<2 x i8> %p) {
; CHECK-LABEL: @test_shift_and_cmp_changed2_vec(
; CHECK-NEXT:    [[ANDP:%.*]] = and <2 x i8> [[P:%.*]], <i8 6, i8 6>
; CHECK-NEXT:    [[CMP:%.*]] = icmp eq <2 x i8> [[ANDP]], zeroinitializer
; CHECK-NEXT:    ret <2 x i1> [[CMP]]
;
  %shlp = shl <2 x i8> %p, <i8 5, i8 5>
  %andp = and <2 x i8> %shlp, <i8 -64, i8 -64>
  %cmp = icmp ult <2 x i8> %andp, <i8 32, i8 32>
  ret <2 x i1> %cmp
}

; nsw on the shift should not affect the comparison.
define i1 @test_shift_and_cmp_changed3(i8 %p) {
; CHECK-LABEL: @test_shift_and_cmp_changed3(
; CHECK-NEXT:    [[SHLP:%.*]] = shl nsw i8 [[P:%.*]], 5
; CHECK-NEXT:    [[ANDP:%.*]] = and i8 [[SHLP]], -64
; CHECK-NEXT:    [[CMP:%.*]] = icmp slt i8 [[ANDP]], 32
; CHECK-NEXT:    ret i1 [[CMP]]
;
  %shlp = shl nsw i8 %p, 5
  %andp = and i8 %shlp, -64
  %cmp = icmp slt i8 %andp, 32
  ret i1 %cmp
}

; Logical shift right allows a return true because the 'and' guarantees no bits are set.
define i1 @test_shift_and_cmp_changed4(i8 %p) {
; CHECK-LABEL: @test_shift_and_cmp_changed4(
; CHECK-NEXT:    ret i1 true
;
  %shlp = lshr i8 %p, 5
  %andp = and i8 %shlp, -64
  %cmp = icmp slt i8 %andp, 32
  ret i1 %cmp
}

