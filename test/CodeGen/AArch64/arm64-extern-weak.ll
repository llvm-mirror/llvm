; RUN: llc -mtriple=arm64-none-linux-gnu -relocation-model=pic -o - < %s | FileCheck %s
; RUN: llc -mtriple=arm64-none-linux-gnu -relocation-model=static -o - < %s | FileCheck --check-prefix=CHECK-STATIC %s
; RUN: llc -mtriple=arm64-none-linux-gnu -code-model=large -o - < %s | FileCheck --check-prefix=CHECK-LARGE %s

declare extern_weak i32 @var()

define i32()* @foo() {
; The usual ADRP/ADD pair can't be used for a weak reference because it must
; evaluate to 0 if the symbol is undefined. We use a GOT entry for PIC
; otherwise a litpool entry.
  ret i32()* @var

; CHECK: adrp x[[VAR:[0-9]+]], :got:var
; CHECK: ldr x0, [x[[VAR]], :got_lo12:var]

; CHECK-STATIC: .LCPI0_0:
; CHECK-STATIC-NEXT: .xword  var
; CHECK-STATIC: adrp x[[VAR:[0-9]+]], .LCPI0_0
; CHECK-STATIC: ldr x0, [x[[VAR]], :lo12:.LCPI0_0]

  ; In the large model, the usual relocations are absolute and can
  ; materialise 0.
; CHECK-LARGE: movz x0, #:abs_g3:var
; CHECK-LARGE: movk x0, #:abs_g2_nc:var
; CHECK-LARGE: movk x0, #:abs_g1_nc:var
; CHECK-LARGE: movk x0, #:abs_g0_nc:var
}


@arr_var = extern_weak global [10 x i32]

define i32* @bar() {
  %addr = getelementptr [10 x i32]* @arr_var, i32 0, i32 5
; CHECK: adrp x[[ARR_VAR_HI:[0-9]+]], :got:arr_var
; CHECK: ldr [[ARR_VAR:x[0-9]+]], [x[[ARR_VAR_HI]], :got_lo12:arr_var]
; CHECK: add x0, [[ARR_VAR]], #20
  ret i32* %addr

; CHECK-STATIC: .LCPI1_0:
; CHECK-STATIC-NEXT: .xword arr_var
; CHECK-STATIC: ldr [[BASE:x[0-9]+]], [{{x[0-9]+}}, :lo12:.LCPI1_0]
; CHECK-STATIC: add x0, [[BASE]], #20

  ; In the large model, the usual relocations are absolute and can
  ; materialise 0.
; CHECK-LARGE: movz [[ARR_VAR:x[0-9]+]], #:abs_g3:arr_var
; CHECK-LARGE: movk [[ARR_VAR]], #:abs_g2_nc:arr_var
; CHECK-LARGE: movk [[ARR_VAR]], #:abs_g1_nc:arr_var
; CHECK-LARGE: movk [[ARR_VAR]], #:abs_g0_nc:arr_var
}

@defined_weak_var = internal unnamed_addr global i32 0

define i32* @wibble() {
  ret i32* @defined_weak_var
; CHECK: adrp [[BASE:x[0-9]+]], defined_weak_var
; CHECK: add x0, [[BASE]], :lo12:defined_weak_var

; CHECK-STATIC: adrp [[BASE:x[0-9]+]], defined_weak_var
; CHECK-STATIC: add x0, [[BASE]], :lo12:defined_weak_var

; CHECK-LARGE: movz x0, #:abs_g3:defined_weak_var
; CHECK-LARGE: movk x0, #:abs_g2_nc:defined_weak_var
; CHECK-LARGE: movk x0, #:abs_g1_nc:defined_weak_var
; CHECK-LARGE: movk x0, #:abs_g0_nc:defined_weak_var
}
