; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @icmpeqop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpeqop__:
  %1 = icmp eq i256 %a, %b
; CHECK: EQ
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpneop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpneop__:
  %1 = icmp ne i256 %a, %b
; CHECK: EQ
; CHECK-NEXT: ISZERO
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpugtop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpugtop__:
  %1 = icmp ugt i256 %a, %b
; CHECK: GT
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpugeop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpugeop__:
  %1 = icmp uge i256 %a, %b
; CHECK: LT
; CHECK-NEXT: ISZERO
  %2 = zext i1 %1 to i256
  ret i256 %2
}


define i256 @icmpultop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpultop__:
  %1 = icmp ult i256 %a, %b
; CHECK: LT
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpuleop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpuleop__:
  %1 = icmp ule i256 %a, %b
; CHECK: GT
; CHECK-NEXT: ISZERO
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpsgtop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsgtop__:
  %1 = icmp sgt i256 %a, %b
; CHECK: SGT
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpsgeop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsgeop__:
  %1 = icmp sge i256 %a, %b
; CHECK: SLT
; CHECK-NEXT: ISZERO
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpsltop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsltop__:
  %1 = icmp slt i256 %a, %b
; CHECK: SLT
  %2 = zext i1 %1 to i256
  ret i256 %2
}

define i256 @icmpsleop(i256 %a, i256 %b) nounwind {
; CHECK-LABEL: icmpsleop__:
  %1 = icmp sle i256 %a, %b
; CHECK: SGT
; CHECK-NEXT: ISZERO
  %2 = zext i1 %1 to i256
  ret i256 %2
}
