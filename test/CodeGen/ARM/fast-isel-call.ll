; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-apple-ios | FileCheck %s --check-prefix=ARM
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-linux-gnueabi | FileCheck %s --check-prefix=ARM
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=thumbv7-apple-ios | FileCheck %s --check-prefix=THUMB
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-apple-ios -arm-long-calls | FileCheck %s --check-prefix=ARM-LONG
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-linux-gnueabi -arm-long-calls | FileCheck %s --check-prefix=ARM-LONG
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=thumbv7-apple-ios -arm-long-calls | FileCheck %s --check-prefix=THUMB-LONG
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-apple-ios -mattr=-vfp2 | FileCheck %s --check-prefix=ARM-NOVFP
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=armv7-linux-gnueabi -mattr=-vfp2 | FileCheck %s --check-prefix=ARM-NOVFP
; RUN: llc < %s -O0 -fast-isel-abort -relocation-model=dynamic-no-pic -mtriple=thumbv7-apple-ios -mattr=-vfp2 | FileCheck %s --check-prefix=THUMB-NOVFP

; Note that some of these tests assume that relocations are either
; movw/movt or constant pool loads. Different platforms will select
; different approaches.

define i32 @t0(i1 zeroext %a) nounwind {
  %1 = zext i1 %a to i32
  ret i32 %1
}

define i32 @t1(i8 signext %a) nounwind {
  %1 = sext i8 %a to i32
  ret i32 %1
}

define i32 @t2(i8 zeroext %a) nounwind {
  %1 = zext i8 %a to i32
  ret i32 %1
}

define i32 @t3(i16 signext %a) nounwind {
  %1 = sext i16 %a to i32
  ret i32 %1
}

define i32 @t4(i16 zeroext %a) nounwind {
  %1 = zext i16 %a to i32
  ret i32 %1
}

define void @foo(i8 %a, i16 %b) nounwind {
; ARM: foo
; THUMB: foo
;; Materialize i1 1
; ARM: movw r2, #1
;; zero-ext
; ARM: and r2, r2, #1
; THUMB: and r2, r2, #1
  %1 = call i32 @t0(i1 zeroext 1)
; ARM: sxtb	r2, r1
; ARM: mov r0, r2
; THUMB: sxtb	r2, r1
; THUMB: mov r0, r2
  %2 = call i32 @t1(i8 signext %a)
; ARM: and	r2, r1, #255
; ARM: mov r0, r2
; THUMB: and	r2, r1, #255
; THUMB: mov r0, r2
  %3 = call i32 @t2(i8 zeroext %a)
; ARM: sxth	r2, r1
; ARM: mov r0, r2
; THUMB: sxth	r2, r1
; THUMB: mov r0, r2
  %4 = call i32 @t3(i16 signext %b)
; ARM: uxth	r2, r1
; ARM: mov r0, r2
; THUMB: uxth	r2, r1
; THUMB: mov r0, r2
  %5 = call i32 @t4(i16 zeroext %b)

;; A few test to check materialization
;; Note: i1 1 was materialized with t1 call
; ARM: movw r1, #255
%6 = call i32 @t2(i8 zeroext 255)
; ARM: movw r1, #65535
; THUMB: movw r1, #65535
%7 = call i32 @t4(i16 zeroext 65535)
  ret void
}

define void @foo2() nounwind {
  %1 = call signext i16 @t5()
  %2 = call zeroext i16 @t6()
  %3 = call signext i8 @t7()
  %4 = call zeroext i8 @t8()
  %5 = call zeroext i1 @t9()
  ret void
}

declare signext i16 @t5();
declare zeroext i16 @t6();
declare signext i8 @t7();
declare zeroext i8 @t8();
declare zeroext i1 @t9();

define i32 @t10() {
entry:
; ARM: @t10
; ARM: movw [[R0:l?r[0-9]*]], #0
; ARM: movw [[R1:l?r[0-9]*]], #248
; ARM: movw [[R2:l?r[0-9]*]], #187
; ARM: movw [[R3:l?r[0-9]*]], #28
; ARM: movw [[R4:l?r[0-9]*]], #40
; ARM: movw [[R5:l?r[0-9]*]], #186
; ARM: and [[R0]], [[R0]], #255
; ARM: and [[R1]], [[R1]], #255
; ARM: and [[R2]], [[R2]], #255
; ARM: and [[R3]], [[R3]], #255
; ARM: and [[R4]], [[R4]], #255
; ARM: str [[R4]], [sp]
; ARM: and [[R4]], [[R5]], #255
; ARM: str [[R4]], [sp, #4]
; ARM: bl {{_?}}bar
; ARM-LONG: @t10
; ARM-LONG: {{(movw)|(ldr)}} [[R:l?r[0-9]*]], {{(:lower16:L_bar\$non_lazy_ptr)|(.LCPI)}}
; ARM-LONG: {{(movt [[R]], :upper16:L_bar\$non_lazy_ptr)?}}
; ARM-LONG: ldr [[R]], {{\[}}[[R]]{{\]}}
; ARM-LONG: blx [[R]]
; THUMB: @t10
; THUMB: movs [[R0:l?r[0-9]*]], #0
; THUMB: movt [[R0]], #0
; THUMB: movs [[R1:l?r[0-9]*]], #248
; THUMB: movt [[R1]], #0
; THUMB: movs [[R2:l?r[0-9]*]], #187
; THUMB: movt [[R2]], #0
; THUMB: movs [[R3:l?r[0-9]*]], #28
; THUMB: movt [[R3]], #0
; THUMB: movw [[R4:l?r[0-9]*]], #40
; THUMB: movt [[R4]], #0
; THUMB: movw [[R5:l?r[0-9]*]], #186
; THUMB: movt [[R5]], #0
; THUMB: and [[R0]], [[R0]], #255
; THUMB: and [[R1]], [[R1]], #255
; THUMB: and [[R2]], [[R2]], #255
; THUMB: and [[R3]], [[R3]], #255
; THUMB: and [[R4]], [[R4]], #255
; THUMB: str.w [[R4]], [sp]
; THUMB: and [[R4]], [[R5]], #255
; THUMB: str.w [[R4]], [sp, #4]
; THUMB: bl {{_?}}bar
; THUMB-LONG: @t10
; THUMB-LONG: {{(movw)|(ldr.n)}} [[R:l?r[0-9]*]], {{(:lower16:L_bar\$non_lazy_ptr)|(.LCPI)}}
; THUMB-LONG: {{(movt [[R]], :upper16:L_bar\$non_lazy_ptr)?}}
; THUMB-LONG: ldr{{(.w)?}} [[R]], {{\[}}[[R]]{{\]}}
; THUMB-LONG: blx [[R]]
  %call = call i32 @bar(i8 zeroext 0, i8 zeroext -8, i8 zeroext -69, i8 zeroext 28, i8 zeroext 40, i8 zeroext -70)
  ret i32 0
}

declare i32 @bar(i8 zeroext, i8 zeroext, i8 zeroext, i8 zeroext, i8 zeroext, i8 zeroext)

define i32 @bar0(i32 %i) nounwind {
  ret i32 0
}

define void @foo3() uwtable {
; ARM: movw    r0, #0
; ARM: {{(movw r1, :lower16:_?bar0)|(ldr r1, .LCPI)}}
; ARM: {{(movt r1, :upper16:_?bar0)|(ldr r1, \[r1\])}}
; ARM: blx     r1
; THUMB: movs    r0, #0
; THUMB: {{(movw r1, :lower16:_?bar0)|(ldr.n r1, .LCPI)}}
; THUMB: {{(movt r1, :upper16:_?bar0)|(ldr r1, \[r1\])}}
; THUMB: blx     r1
  %fptr = alloca i32 (i32)*, align 8
  store i32 (i32)* @bar0, i32 (i32)** %fptr, align 8
  %1 = load i32 (i32)** %fptr, align 8
  %call = call i32 %1(i32 0)
  ret void
}

define i32 @LibCall(i32 %a, i32 %b) {
entry:
; ARM: LibCall
; ARM: bl {{___udivsi3|__aeabi_uidiv}}
; ARM-LONG: LibCall
; ARM-LONG: {{(movw r2, :lower16:L___udivsi3\$non_lazy_ptr)|(ldr r2, .LCPI)}}
; ARM-LONG: {{(movt r2, :upper16:L___udivsi3\$non_lazy_ptr)?}}
; ARM-LONG: ldr r2, [r2]
; ARM-LONG: blx r2
; THUMB: LibCall
; THUMB: bl {{___udivsi3|__aeabi_uidiv}}
; THUMB-LONG: LibCall
; THUMB-LONG: {{(movw r2, :lower16:L___udivsi3\$non_lazy_ptr)|(ldr.n r2, .LCPI)}}
; THUMB-LONG: {{(movt r2, :upper16:L___udivsi3\$non_lazy_ptr)?}}
; THUMB-LONG: ldr r2, [r2]
; THUMB-LONG: blx r2
        %tmp1 = udiv i32 %a, %b         ; <i32> [#uses=1]
        ret i32 %tmp1
}

; Test fastcc

define fastcc void @fast_callee(float %i) ssp {
entry:
; ARM: fast_callee
; ARM: vmov r0, s0
; THUMB: fast_callee
; THUMB: vmov r0, s0
; ARM-NOVFP: fast_callee
; ARM-NOVFP-NOT: s0
; THUMB-NOVFP: fast_callee
; THUMB-NOVFP-NOT: s0
  call void @print(float %i)
  ret void
}

define void @fast_caller() ssp {
entry:
; ARM: fast_caller
; ARM: vldr s0,
; THUMB: fast_caller
; THUMB: vldr s0,
; ARM-NOVFP: fast_caller
; ARM-NOVFP: movw r0, #13107
; ARM-NOVFP: movt r0, #16611
; THUMB-NOVFP: fast_caller
; THUMB-NOVFP: movw r0, #13107
; THUMB-NOVFP: movt r0, #16611
  call fastcc void @fast_callee(float 0x401C666660000000)
  ret void
}

define void @no_fast_callee(float %i) ssp {
entry:
; ARM: no_fast_callee
; ARM: vmov s0, r0
; THUMB: no_fast_callee
; THUMB: vmov s0, r0
; ARM-NOVFP: no_fast_callee
; ARM-NOVFP-NOT: s0
; THUMB-NOVFP: no_fast_callee
; THUMB-NOVFP-NOT: s0
  call void @print(float %i)
  ret void
}

define void @no_fast_caller() ssp {
entry:
; ARM: no_fast_caller
; ARM: vmov r0, s0
; THUMB: no_fast_caller
; THUMB: vmov r0, s0
; ARM-NOVFP: no_fast_caller
; ARM-NOVFP: movw r0, #13107
; ARM-NOVFP: movt r0, #16611
; THUMB-NOVFP: no_fast_caller
; THUMB-NOVFP: movw r0, #13107
; THUMB-NOVFP: movt r0, #16611
  call void @no_fast_callee(float 0x401C666660000000)
  ret void
}

declare void @print(float)
