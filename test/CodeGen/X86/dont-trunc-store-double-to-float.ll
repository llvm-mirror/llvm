; RUN: llc -march=x86 < %s | FileCheck %s

; CHECK-LABEL: @bar
; CHECK: movl $1074339512,
; CHECK: movl $1374389535,
; CHECK: movl $1078523331,
define void @bar() unnamed_addr {
entry-block:
  %a = alloca double
  %b = alloca float

  store double 3.140000e+00, double* %a
  %0 = load double* %a

  %1 = fptrunc double %0 to float

  store float %1, float* %b

  ret void
}
