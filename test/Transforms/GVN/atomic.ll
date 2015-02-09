; RUN: opt -basicaa -gvn -S < %s | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-apple-macosx10.7.0"

@x = common global i32 0, align 4
@y = common global i32 0, align 4

; GVN across unordered store (allowed)
define i32 @test1() nounwind uwtable ssp {
; CHECK-LABEL: test1
; CHECK: add i32 %x, %x
entry:
  %x = load i32* @y
  store atomic i32 %x, i32* @x unordered, align 4
  %y = load i32* @y
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN across seq_cst store (allowed)
define i32 @test2() nounwind uwtable ssp {
; CHECK-LABEL: test2
; CHECK: add i32 %x, %x
entry:
  %x = load i32* @y
  store atomic i32 %x, i32* @x seq_cst, align 4
  %y = load i32* @y
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN across unordered load (allowed)
define i32 @test3() nounwind uwtable ssp {
; CHECK-LABEL: test3
; CHECK: add i32 %x, %x
entry:
  %x = load i32* @y
  %y = load atomic i32* @x unordered, align 4
  %z = load i32* @y
  %a = add i32 %x, %z
  %b = add i32 %y, %a
  ret i32 %b
}

; GVN across acquire load (allowed as the original load was not atomic)
define i32 @test4() nounwind uwtable ssp {
; CHECK-LABEL: test4
; CHECK: load atomic i32* @x
; CHECK-NOT: load i32* @y
entry:
  %x = load i32* @y
  %y = load atomic i32* @x seq_cst, align 4
  %x2 = load i32* @y
  %x3 = add i32 %x, %x2
  %y2 = add i32 %y, %x3
  ret i32 %y2
}

; GVN load to unordered load (allowed)
define i32 @test5() nounwind uwtable ssp {
; CHECK-LABEL: test5
; CHECK: add i32 %x, %x
entry:
  %x = load atomic i32* @x unordered, align 4
  %y = load i32* @x
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN unordered load to load (unordered load must not be removed)
define i32 @test6() nounwind uwtable ssp {
; CHECK-LABEL: test6
; CHECK: load atomic i32* @x unordered
entry:
  %x = load i32* @x
  %x2 = load atomic i32* @x unordered, align 4
  %x3 = add i32 %x, %x2
  ret i32 %x3
}

; GVN across release-acquire pair (forbidden)
define i32 @test7() nounwind uwtable ssp {
; CHECK-LABEL: test7
; CHECK: add i32 %x, %y
entry:
  %x = load i32* @y
  store atomic i32 %x, i32* @x release, align 4
  %w = load atomic i32* @x acquire, align 4
  %y = load i32* @y
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN across acquire-release pair (allowed)
define i32 @test8() nounwind uwtable ssp {
; CHECK-LABEL: test8
; CHECK: add i32 %x, %x
entry:
  %x = load i32* @y
  %w = load atomic i32* @x acquire, align 4
  store atomic i32 %x, i32* @x release, align 4
  %y = load i32* @y
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN across monotonic store (allowed)
define i32 @test9() nounwind uwtable ssp {
; CHECK-LABEL: test9
; CHECK: add i32 %x, %x
entry:
  %x = load i32* @y
  store atomic i32 %x, i32* @x monotonic, align 4
  %y = load i32* @y
  %z = add i32 %x, %y
  ret i32 %z
}

; GVN of an unordered across monotonic load (not allowed)
define i32 @test10() nounwind uwtable ssp {
; CHECK-LABEL: test10
; CHECK: add i32 %x, %y
entry:
  %x = load atomic i32* @y unordered, align 4
  %clobber = load atomic i32* @x monotonic, align 4
  %y = load atomic i32* @y monotonic, align 4
  %z = add i32 %x, %y
  ret i32 %z
}

