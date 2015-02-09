; Test the 'call' instruction and the tailcall variant.

; FIXME: We should remove the need for -enable-mips-tail-calls
; RUN: llc -march=mips   -mcpu=mips32   -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=O32
; RUN: llc -march=mips   -mcpu=mips32r2 -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=O32
; RUN: llc -march=mips   -mcpu=mips32r6 -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=O32
; RUN: llc -march=mips64 -mcpu=mips4    -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=N64
; RUN: llc -march=mips64 -mcpu=mips64   -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=N64
; RUN: llc -march=mips64 -mcpu=mips64r2 -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=N64
; RUN: llc -march=mips64 -mcpu=mips64r6 -enable-mips-tail-calls < %s | FileCheck %s -check-prefix=ALL -check-prefix=N64

declare void @extern_void_void()
declare i32 @extern_i32_void()
declare float @extern_float_void()

define i32 @call_void_void() {
; ALL-LABEL: call_void_void:

; O32:           lw $[[TGT:[0-9]+]], %call16(extern_void_void)($gp)

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_void_void)($gp)

; ALL:           jalr $[[TGT]]

  call void @extern_void_void()
  ret i32 0
}

define i32 @call_i32_void() {
; ALL-LABEL: call_i32_void:

; O32:           lw $[[TGT:[0-9]+]], %call16(extern_i32_void)($gp)

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_i32_void)($gp)

; ALL:           jalr $[[TGT]]

  %1 = call i32 @extern_i32_void()
  %2 = add i32 %1, 1
  ret i32 %2
}

define float @call_float_void() {
; ALL-LABEL: call_float_void:

; FIXME: Not sure why we don't use $gp directly on such a simple test. We should
;        look into it at some point.
; O32:           addu $[[GP:[0-9]+]], ${{[0-9]+}}, $25
; O32:           lw $[[TGT:[0-9]+]], %call16(extern_float_void)($[[GP]])

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_float_void)($gp)

; ALL:           jalr $[[TGT]]

; O32:           move $gp, $[[GP]]

  %1 = call float @extern_float_void()
  %2 = fadd float %1, 1.0
  ret float %2
}

define void @musttail_call_void_void() {
; ALL-LABEL: musttail_call_void_void:

; O32:           lw $[[TGT:[0-9]+]], %call16(extern_void_void)($gp)

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_void_void)($gp)

; NOT-R6:        jr $[[TGT]]
; R6:            r6.jr $[[TGT]]

  musttail call void @extern_void_void()
  ret void
}

define i32 @musttail_call_i32_void() {
; ALL-LABEL: musttail_call_i32_void:

; O32:           lw $[[TGT:[0-9]+]], %call16(extern_i32_void)($gp)

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_i32_void)($gp)

; NOT-R6:        jr $[[TGT]]
; R6:            r6.jr $[[TGT]]

  %1 = musttail call i32 @extern_i32_void()
  ret i32 %1
}

define float @musttail_call_float_void() {
; ALL-LABEL: musttail_call_float_void:

; O32:           lw $[[TGT:[0-9]+]], %call16(extern_float_void)($gp)

; N64:           ld $[[TGT:[0-9]+]], %call16(extern_float_void)($gp)

; NOT-R6:        jr $[[TGT]]
; R6:            r6.jr $[[TGT]]

  %1 = musttail call float @extern_float_void()
  ret float %1
}

define i32 @indirect_call_void_void(void ()* %addr) {
; ALL-LABEL: indirect_call_void_void:

; ALL:           move $25, $4
; ALL:           jalr $25

  call void %addr()
  ret i32 0
}

define i32 @indirect_call_i32_void(i32 ()* %addr) {
; ALL-LABEL: indirect_call_i32_void:

; ALL:           move $25, $4
; ALL:           jalr $25

  %1 = call i32 %addr()
  %2 = add i32 %1, 1
  ret i32 %2
}

define float @indirect_call_float_void(float ()* %addr) {
; ALL-LABEL: indirect_call_float_void:

; ALL:           move $25, $4
; ALL:           jalr $25

  %1 = call float %addr()
  %2 = fadd float %1, 1.0
  ret float %2
}

; We can't use 'musttail' here because the verifier is too conservative and
; prohibits any prototype difference.
define void @tail_indirect_call_void_void(void ()* %addr) {
; ALL-LABEL: tail_indirect_call_void_void:

; ALL:           move $25, $4
; ALL:           jr $25

  tail call void %addr()
  ret void
}

define i32 @tail_indirect_call_i32_void(i32 ()* %addr) {
; ALL-LABEL: tail_indirect_call_i32_void:

; ALL:           move $25, $4
; ALL:           jr $25

  %1 = tail call i32 %addr()
  ret i32 %1
}

define float @tail_indirect_call_float_void(float ()* %addr) {
; ALL-LABEL: tail_indirect_call_float_void:

; ALL:           move $25, $4
; ALL:           jr $25

  %1 = tail call float %addr()
  ret float %1
}
