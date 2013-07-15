; RUN: llc -verify-machineinstrs -mtriple=aarch64-none-linux-gnu < %s | FileCheck %s

%va_list = type {i8*, i8*, i8*, i32, i32}

@var = global %va_list zeroinitializer

declare void @llvm.va_start(i8*)

define void @test_simple(i32 %n, ...) {
; CHECK-LABEL: test_simple:
; CHECK: sub sp, sp, #[[STACKSIZE:[0-9]+]]
; CHECK: mov x[[FPRBASE:[0-9]+]], sp
; CHECK: str q7, [x[[FPRBASE]], #112]
; CHECK: add x[[GPRBASE:[0-9]+]], sp, #[[GPRFROMSP:[0-9]+]]
; CHECK: str x7, [x[[GPRBASE]], #48]

; Omit the middle ones

; CHECK: str q0, [sp]
; CHECK: str x1, [sp, #[[GPRFROMSP]]]

  %addr = bitcast %va_list* @var to i8*
  call void @llvm.va_start(i8* %addr)
; CHECK: add x[[VA_LIST:[0-9]+]], {{x[0-9]+}}, #:lo12:var
; CHECK: movn [[VR_OFFS:w[0-9]+]], #127
; CHECK: str [[VR_OFFS]], [x[[VA_LIST]], #28]
; CHECK: movn [[GR_OFFS:w[0-9]+]], #55
; CHECK: str [[GR_OFFS]], [x[[VA_LIST]], #24]
; CHECK: add [[VR_TOP:x[0-9]+]], x[[FPRBASE]], #128
; CHECK: str [[VR_TOP]], [x[[VA_LIST]], #16]
; CHECK: add [[GR_TOP:x[0-9]+]], x[[GPRBASE]], #56
; CHECK: str [[GR_TOP]], [x[[VA_LIST]], #8]
; CHECK: add [[STACK:x[0-9]+]], sp, #[[STACKSIZE]]
; CHECK: str [[STACK]], [{{x[0-9]+}}, #:lo12:var]

  ret void
}

define void @test_fewargs(i32 %n, i32 %n1, i32 %n2, float %m, ...) {
; CHECK-LABEL: test_fewargs:
; CHECK: sub sp, sp, #[[STACKSIZE:[0-9]+]]
; CHECK: mov x[[FPRBASE:[0-9]+]], sp
; CHECK: str q7, [x[[FPRBASE]], #96]
; CHECK: add x[[GPRBASE:[0-9]+]], sp, #[[GPRFROMSP:[0-9]+]]
; CHECK: str x7, [x[[GPRBASE]], #32]

; Omit the middle ones

; CHECK: str q1, [sp]
; CHECK: str x3, [sp, #[[GPRFROMSP]]]

  %addr = bitcast %va_list* @var to i8*
  call void @llvm.va_start(i8* %addr)
; CHECK: add x[[VA_LIST:[0-9]+]], {{x[0-9]+}}, #:lo12:var
; CHECK: movn [[VR_OFFS:w[0-9]+]], #111
; CHECK: str [[VR_OFFS]], [x[[VA_LIST]], #28]
; CHECK: movn [[GR_OFFS:w[0-9]+]], #39
; CHECK: str [[GR_OFFS]], [x[[VA_LIST]], #24]
; CHECK: add [[VR_TOP:x[0-9]+]], x[[FPRBASE]], #112
; CHECK: str [[VR_TOP]], [x[[VA_LIST]], #16]
; CHECK: add [[GR_TOP:x[0-9]+]], x[[GPRBASE]], #40
; CHECK: str [[GR_TOP]], [x[[VA_LIST]], #8]
; CHECK: add [[STACK:x[0-9]+]], sp, #[[STACKSIZE]]
; CHECK: str [[STACK]], [{{x[0-9]+}}, #:lo12:var]

  ret void
}

define void @test_nospare([8 x i64], [8 x float], ...) {
; CHECK-LABEL: test_nospare:

  %addr = bitcast %va_list* @var to i8*
  call void @llvm.va_start(i8* %addr)
; CHECK-NOT: sub sp, sp
; CHECK: mov [[STACK:x[0-9]+]], sp
; CHECK: str [[STACK]], [{{x[0-9]+}}, #:lo12:var]

  ret void
}

; If there are non-variadic arguments on the stack (here two i64s) then the
; __stack field should point just past them.
define void @test_offsetstack([10 x i64], [3 x float], ...) {
; CHECK-LABEL: test_offsetstack:
; CHECK: sub sp, sp, #80
; CHECK: mov x[[FPRBASE:[0-9]+]], sp
; CHECK: str q7, [x[[FPRBASE]], #64]

; CHECK-NOT: str x{{[0-9]+}},
; Omit the middle ones

; CHECK: str q3, [sp]

  %addr = bitcast %va_list* @var to i8*
  call void @llvm.va_start(i8* %addr)
; CHECK: add x[[VA_LIST:[0-9]+]], {{x[0-9]+}}, #:lo12:var
; CHECK: movn [[VR_OFFS:w[0-9]+]], #79
; CHECK: str [[VR_OFFS]], [x[[VA_LIST]], #28]
; CHECK: str wzr, [x[[VA_LIST]], #24]
; CHECK: add [[VR_TOP:x[0-9]+]], x[[FPRBASE]], #80
; CHECK: str [[VR_TOP]], [x[[VA_LIST]], #16]
; CHECK: add [[STACK:x[0-9]+]], sp, #96
; CHECK: str [[STACK]], [{{x[0-9]+}}, #:lo12:var]

  ret void
}

declare void @llvm.va_end(i8*)

define void @test_va_end() nounwind {
; CHECK-LABEL: test_va_end:
; CHECK-NEXT: BB#0

  %addr = bitcast %va_list* @var to i8*
  call void @llvm.va_end(i8* %addr)

  ret void
; CHECK-NEXT: ret
}

declare void @llvm.va_copy(i8* %dest, i8* %src)

@second_list = global %va_list zeroinitializer

define void @test_va_copy() {
; CHECK-LABEL: test_va_copy:
  %srcaddr = bitcast %va_list* @var to i8*
  %dstaddr = bitcast %va_list* @second_list to i8*
  call void @llvm.va_copy(i8* %dstaddr, i8* %srcaddr)

; Check beginning and end again:

; CHECK: ldr [[BLOCK:x[0-9]+]], [{{x[0-9]+}}, #:lo12:var]
; CHECK: str [[BLOCK]], [{{x[0-9]+}}, #:lo12:second_list]

; CHECK: add x[[DEST_LIST:[0-9]+]], {{x[0-9]+}}, #:lo12:second_list
; CHECK: add x[[SRC_LIST:[0-9]+]], {{x[0-9]+}}, #:lo12:var

; CHECK: ldr [[BLOCK:x[0-9]+]], [x[[SRC_LIST]], #24]
; CHECK: str [[BLOCK]], [x[[DEST_LIST]], #24]

  ret void
; CHECK: ret
}
