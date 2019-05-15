; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @icmpeqop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpeqop:
  %1 = icmp eq i256 %a, %b
; CHECK: EQ
  ret i256 %1
}

define i256 @icmpneop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpneop:
  %1 = icmp ne i256 %a, %b
; CHECK: EQ
; CHECK-NEXT: ISZERO
  ret i256 %1
}

define i256 @icmpugtop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpugtop:
  %1 = icmp ugt i256 %a, %b
; CHECK: GT
  ret i256 %1
}

define i256 @icmpugeop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpugeop:
  %1 = icmp uge i256 %a, %b
; CHECK: LT
; CHECK-NEXT: ISZERO
  ret i256 %1
}


define i256 @icmpultop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpultop:
  %1 = icmp ult i256 %a, %b
; CHECK: LT
  ret i256 %1
}

define i256 @icmpuleop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpuleop:
  %1 = icmp ule i256 %a, %b
; CHECK: GT
; CHECK-NEXT: ISZERO
  ret i256 %1
}

define i256 @icmpsgtop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsgtop:
  %1 = icmp sgt i256 %a, %b
; CHECK: SGT
  ret i256 %1
}

define i256 @icmpsgeop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsgeop:
  %1 = icmp sge i256 %a, %b
; CHECK: SLT
; CHECK-NEXT: ISZERO
  ret i256 %1
}

define i256 @icmpsltop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsltop:
  %1 = icmp slt i256 %a, %b
; CHECK: SLT
  ret i256 %1
}

define i256 @icmpsleop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsleop:
  %1 = icmp sle i256 %a, %b
; CHECK: SGT
; CHECK-NEXT: ISZERO
  ret i256 %1
}
