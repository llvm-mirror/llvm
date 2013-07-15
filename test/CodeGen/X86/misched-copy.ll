; REQUIRES: asserts
; RUN: llc < %s -march=x86 -mcpu=core2 -pre-RA-sched=source -enable-misched -verify-misched -debug-only=misched -o - 2>&1 > /dev/null | FileCheck %s
;
; Test scheduling of copy instructions.
;
; Argument copies should be hoisted to the top of the block.
; Return copies should be sunk to the end.
; MUL_HiLo PhysReg use copies should be just above the mul.
; MUL_HiLo PhysReg def copies should be just below the mul.
;
; CHECK:      *** Final schedule for BB#1 ***
; CHECK-NEXT: %EAX<def> = COPY
; CHECK:      MUL32r %vreg{{[0-9]+}}, %EAX<imp-def>, %EDX<imp-def>, %EFLAGS<imp-def,dead>, %EAX<imp-use>;
; CHECK-NEXT: COPY %E{{[AD]}}X;
; CHECK-NEXT: COPY %E{{[AD]}}X;
; CHECK:      DIVSSrm
define i64 @mulhoist(i32 %a, i32 %b) #0 {
entry:
  br label %body

body:
  %convb = sitofp i32 %b to float
  ; Generates an iMUL64r to legalize types.
  %aa = zext i32 %a to i64
  %mul = mul i64 %aa, 74383
  ; Do some dependent long latency stuff.
  %trunc = trunc i64 %mul to i32
  %convm = sitofp i32 %trunc to float
  %divm = fdiv float %convm, 0.75
  ;%addmb = fadd float %divm, %convb
  ;%divmb = fdiv float %addmb, 0.125
  ; Do some independent long latency stuff.
  %conva = sitofp i32 %a to float
  %diva = fdiv float %conva, 0.75
  %addab = fadd float %diva, %convb
  %divab = fdiv float %addab, 0.125
  br label %end

end:
  %val = fptosi float %divab to i64
  %add = add i64 %mul, %val
  ret i64 %add
}

attributes #0 = { nounwind ssp uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }

!0 = metadata !{metadata !"float", metadata !1}
!1 = metadata !{metadata !"omnipotent char", metadata !2}
!2 = metadata !{metadata !"Simple C/C++ TBAA"}
