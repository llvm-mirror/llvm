; RUN: llc %s -mtriple=aarch64-none-linux-gnu -enable-global-merge -o - | FileCheck %s
; RUN: llc %s -mtriple=aarch64-none-linux-gnu -enable-global-merge -global-merge-on-external -o - | FileCheck %s

; RUN: llc %s -mtriple=aarch64-linux-gnuabi -enable-global-merge -o - | FileCheck %s
; RUN: llc %s -mtriple=aarch64-linux-gnuabi -enable-global-merge -global-merge-on-external -o - | FileCheck %s

; RUN: llc %s -mtriple=aarch64-apple-ios -enable-global-merge -o - | FileCheck %s --check-prefix=CHECK-APPLE-IOS
; RUN: llc %s -mtriple=aarch64-apple-ios -enable-global-merge -global-merge-on-external -o - | FileCheck %s --check-prefix=CHECK-APPLE-IOS

@m = internal global i32 0, align 4
@n = internal global i32 0, align 4

define void @f1(i32 %a1, i32 %a2) {
;CHECK-APPLE-IOS-NOT: adrp
;CHECK-APPLE-IOS: adrp	x8, __MergedGlobals@PAGE
;CHECK-APPLE-IOS-NOT: adrp
;CHECK-APPLE-IOS: add	x8, x8, __MergedGlobals@PAGEOFF
  store i32 %a1, i32* @m, align 4
  store i32 %a2, i32* @n, align 4
  ret void
}

;CHECK:	.type	_MergedGlobals,@object  // @_MergedGlobals
;CHECK:	.local	_MergedGlobals
;CHECK:	.comm	_MergedGlobals,8,8

;CHECK-APPLE-IOS: .zerofill __DATA,__bss,__MergedGlobals,8,3 ; @_MergedGlobals
