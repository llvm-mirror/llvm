; RUN: opt -instsimplify -S < %s | FileCheck %s

; CHECK-LABEL: @test
define i1 @test(i8 %p, i8* %pq) {
  %q = load i8, i8* %pq, !range !0 ; %q is known nonzero; no known bits
  %1 = or i8 %p, 2                 ; %1[1] = 1
  %2 = and i8 %1, 254              ; %2[0] = 0, %2[1] = 1
  %A = lshr i8 %2, 1               ; We should know that %A is nonzero.
  %x = icmp eq i8 %A, 0
  ; CHECK: ret i1 false
  ret i1 %x
}

!0 = !{ i8 1, i8 5 }
