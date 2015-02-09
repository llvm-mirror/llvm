; RUN: llc < %s -mtriple=x86_64-apple-darwin | FileCheck -check-prefix=CHECK-NO-FP %s
; RUN: llc < %s -mtriple=x86_64-apple-darwin -disable-fp-elim | FileCheck -check-prefix=CHECK-FP %s
; RUN: llc < %s -mtriple=x86_64-linux-gnu | FileCheck -check-prefix=LINUX-NO-FP %s
; RUN: llc < %s -mtriple=x86_64-linux-gnu -disable-fp-elim | FileCheck -check-prefix=LINUX-FP %s

define void @func() {
entry:
  unreachable
}

; MachO cannot handle an empty function.
; CHECK-NO-FP:     _func:
; CHECK-NO-FP-NEXT: .cfi_startproc
; CHECK-NO-FP:     nop
; CHECK-NO-FP-NEXT: .cfi_endproc

; CHECK-FP:      _func:
; CHECK-FP-NEXT: .cfi_startproc
; CHECK-FP-NEXT: :
; CHECK-FP-NEXT: pushq %rbp
; CHECK-FP-NEXT: :
; CHECK-FP-NEXT: .cfi_def_cfa_offset 16
; CHECK-FP-NEXT: :
; CHECK-FP-NEXT: .cfi_offset %rbp, -16
; CHECK-FP-NEXT: movq %rsp, %rbp
; CHECK-FP-NEXT: :
; CHECK-FP-NEXT: .cfi_def_cfa_register %rbp
; CHECK-FP-NEXT: .cfi_endproc

; An empty function is perfectly fine on ELF.
; LINUX-NO-FP: func:
; LINUX-NO-FP-NEXT: .cfi_startproc
; LINUX-NO-FP-NEXT: {{^}}#
; LINUX-NO-FP-NEXT: {{^}}.L{{.*}}:{{$}}
; LINUX-NO-FP-NEXT: .size   func, .L{{.*}}-func
; LINUX-NO-FP-NEXT: .cfi_endproc

; A cfi directive can point to the end of a function. It (and in fact the
; entire body) could be optimized out because of the unreachable, but we
; don't do it right now.
; LINUX-FP: func:
; LINUX-FP-NEXT: .cfi_startproc
; LINUX-FP-NEXT: {{^}}#
; LINUX-FP-NEXT: pushq %rbp
; LINUX-FP-NEXT: {{^}}.L{{.*}}:{{$}}
; LINUX-FP-NEXT:  .cfi_def_cfa_offset 16
; LINUX-FP-NEXT: {{^}}.L{{.*}}:{{$}}
; LINUX-FP-NEXT: .cfi_offset %rbp, -16
; LINUX-FP-NEXT: movq        %rsp, %rbp
; LINUX-FP-NEXT:{{^}}.L{{.*}}:{{$}}
; LINUX-FP-NEXT: .cfi_def_cfa_register %rbp
; LINUX-FP-NEXT:{{^}}.L{{.*}}:{{$}}
; LINUX-FP-NEXT: .size   func, .Ltmp3-func
; LINUX-FP-NEXT: .cfi_endproc
