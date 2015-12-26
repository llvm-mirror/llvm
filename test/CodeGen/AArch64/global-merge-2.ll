; RUN: llc %s -mtriple=aarch64-none-linux-gnu -aarch64-global-merge -global-merge-on-external -o - | FileCheck %s
; RUN: llc %s -mtriple=aarch64-linux-gnuabi -aarch64-global-merge -global-merge-on-external -o - | FileCheck %s
; RUN: llc %s -mtriple=aarch64-apple-ios -aarch64-global-merge -global-merge-on-external -o - | FileCheck %s --check-prefix=CHECK-APPLE-IOS

@x = global i32 0, align 4
@y = global i32 0, align 4
@z = global i32 0, align 4

define void @f1(i32 %a1, i32 %a2) {
;CHECK-APPLE-IOS-LABEL: _f1:
;CHECK-APPLE-IOS-NOT: adrp
;CHECK-APPLE-IOS: adrp	x8, l__MergedGlobals@PAGE
;CHECK-APPLE-IOS: add	x8, x8, l__MergedGlobals@PAGEOFF
;CHECK-APPLE-IOS-NOT: adrp
  store i32 %a1, i32* @x, align 4
  store i32 %a2, i32* @y, align 4
  ret void
}

define void @g1(i32 %a1, i32 %a2) {
;CHECK-APPLE-IOS-LABEL: _g1:
;CHECK-APPLE-IOS: adrp	x8, l__MergedGlobals@PAGE
;CHECK-APPLE-IOS: add	x8, x8, l__MergedGlobals@PAGEOFF
;CHECK-APPLE-IOS-NOT: adrp
  store i32 %a1, i32* @y, align 4
  store i32 %a2, i32* @z, align 4
  ret void
}

;CHECK:	.type	.L_MergedGlobals,@object // @_MergedGlobals
;CHECK:	.local	.L_MergedGlobals
;CHECK:	.comm	.L_MergedGlobals,12,8

;CHECK:	.globl	x
;CHECK: x = .L_MergedGlobals
;CHECK: .size x, 4
;CHECK:	.globl	y
;CHECK: y = .L_MergedGlobals+4
;CHECK: .size y, 4
;CHECK:	.globl	z
;CHECK: z = .L_MergedGlobals+8
;CHECK: .size z, 4

;CHECK-APPLE-IOS: .zerofill __DATA,__bss,l__MergedGlobals,12,3

;CHECK-APPLE-IOS: .globl	_x
;CHECK-APPLE-IOS:  = l__MergedGlobals
;CHECK-APPLE-IOS: .globl	_y
;CHECK-APPLE-IOS: _y = l__MergedGlobals+4
;CHECK-APPLE-IOS: .globl	_z
;CHECK-APPLE-IOS: _z = l__MergedGlobals+8
;CHECK-APPLE-IOS: .subsections_via_symbols
