; RUN: llc -march=sparc <%s | FileCheck %s

; CHECK-LABEL: test_constraint_r
; CHECK:       add %o1, %o0, %o0
define i32 @test_constraint_r(i32 %a, i32 %b) {
entry:
  %0 = tail call i32 asm sideeffect "add $2, $1, $0", "=r,r,r"(i32 %a, i32 %b)
  ret i32 %0
}

; CHECK-LABEL: test_constraint_I:
; CHECK:       add %o0, 1023, %o0
define i32 @test_constraint_I(i32 %a) {
entry:
  %0 = tail call i32 asm sideeffect "add $1, $2, $0", "=r,r,rI"(i32 %a, i32 1023)
  ret i32 %0
}

; CHECK-LABEL: test_constraint_I_neg:
; CHECK:       add %o0, -4096, %o0
define i32 @test_constraint_I_neg(i32 %a) {
entry:
  %0 = tail call i32 asm sideeffect "add $1, $2, $0", "=r,r,rI"(i32 %a, i32 -4096)
  ret i32 %0
}

; CHECK-LABEL: test_constraint_I_largeimm:
; CHECK:       sethi 9, [[R0:%[gilo][0-7]]]
; CHECK:       or [[R0]], 784, [[R1:%[gilo][0-7]]]
; CHECK:       add %o0, [[R1]], %o0
define i32 @test_constraint_I_largeimm(i32 %a) {
entry:
  %0 = tail call i32 asm sideeffect "add $1, $2, $0", "=r,r,rI"(i32 %a, i32 10000)
  ret i32 %0
}

; CHECK-LABEL: test_constraint_reg:
; CHECK:       ldda [%o1] 43, %g2
; CHECK:       ldda [%o1] 43, %g4
define void @test_constraint_reg(i32 %s, i32* %ptr) {
entry:
  %0 = tail call i64 asm sideeffect "ldda [$1] $2, $0", "={r2},r,n"(i32* %ptr, i32 43)
  %1 = tail call i64 asm sideeffect "ldda [$1] $2, $0", "={g4},r,n"(i32* %ptr, i32 43)
  ret void
}

;; Ensure that i64 args to asm are allocated to the IntPair register class.
;; Also checks that register renaming for leaf proc works.
; CHECK-LABEL: test_constraint_r_i64:
; CHECK: mov %o0, %o5
; CHECK: sra %o5, 31, %o4
; CHECK: std %o4, [%o1]
define i32 @test_constraint_r_i64(i32 %foo, i64* %out, i32 %o) {
entry:
  %conv = sext i32 %foo to i64
  tail call void asm sideeffect "std $0, [$1]", "r,r,~{memory}"(i64 %conv, i64* %out)
  ret i32 %o
}

;; Same test without leaf-proc opt
; CHECK-LABEL: test_constraint_r_i64_noleaf:
; CHECK: mov %i0, %i5
; CHECK: sra %i5, 31, %i4
; CHECK: std %i4, [%i1]
define i32 @test_constraint_r_i64_noleaf(i32 %foo, i64* %out, i32 %o) #0 {
entry:
  %conv = sext i32 %foo to i64
  tail call void asm sideeffect "std $0, [$1]", "r,r,~{memory}"(i64 %conv, i64* %out)
  ret i32 %o
}
attributes #0 = { "no-frame-pointer-elim"="true" }

;; Ensures that tied in and out gets allocated properly.
; CHECK-LABEL: test_i64_inout:
; CHECK: sethi 0, %o2
; CHECK: mov 5, %o3
; CHECK: xor %o2, %g0, %o2
; CHECK: mov %o2, %o0
; CHECK: ret
define i64 @test_i64_inout() {
entry:
  %0 = call i64 asm sideeffect "xor $1, %g0, $0", "=r,0,~{i1}"(i64 5);
  ret i64 %0
}
