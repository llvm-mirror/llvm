; RUN: llc < %s -march=mips -mcpu=mips2 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=M2 -check-prefix=GP32
; RUN: llc < %s -march=mips -mcpu=mips32 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=32R1-R2 -check-prefix=GP32
; RUN: llc < %s -march=mips -mcpu=mips32r2 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=32R1-R2 -check-prefix=32R2 -check-prefix=GP32
; RUN: llc < %s -march=mips -mcpu=mips32r6 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=32R6 -check-prefix=GP32
; RUN: llc < %s -march=mips64 -mcpu=mips4 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=M4 -check-prefix=GP64-NOT-R6
; RUN: llc < %s -march=mips64 -mcpu=mips64 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=64R1-R2 -check-prefix=GP64-NOT-R6
; RUN: llc < %s -march=mips64 -mcpu=mips64r2 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=64R1-R2 -check-prefix=GP64 -check-prefix=GP64-NOT-R6
; RUN: llc < %s -march=mips64 -mcpu=mips64r6 | FileCheck %s -check-prefix=ALL \
; RUN:    -check-prefix=64R6

define signext i1 @mul_i1(i1 signext %a, i1 signext %b) {
entry:
; ALL-LABEL: mul_i1:

  ; M2:         mult    $4, $5
  ; M2:         mflo    $[[T0:[0-9]+]]
  ; M2:         sll     $[[T0]], $[[T0]], 31
  ; M2:         sra     $2, $[[T0]], 31

  ; 32R1-R2:    mul     $[[T0:[0-9]+]], $4, $5
  ; 32R1-R2:    sll     $[[T0]], $[[T0]], 31
  ; 32R1-R2:    sra     $2, $[[T0]], 31

  ; 32R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R6:       sll     $[[T0]], $[[T0]], 31
  ; 32R6:       sra     $2, $[[T0]], 31

  ; M4:         mult    $4, $5
  ; M4:         mflo    $[[T0:[0-9]+]]
  ; M4:         sll     $[[T0]], $[[T0]], 31
  ; M4:         sra     $2, $[[T0]], 31

  ; 64R1-R2:    mul     $[[T0:[0-9]+]], $4, $5
  ; 64R1-R2:    sll     $[[T0]], $[[T0]], 31
  ; 64R1-R2:    sra     $2, $[[T0]], 31

  ; 64R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R6:       sll     $[[T0]], $[[T0]], 31
  ; 64R6:       sra     $2, $[[T0]], 31

  %r = mul i1 %a, %b
  ret i1 %r
}

define signext i8 @mul_i8(i8 signext %a, i8 signext %b) {
entry:
; ALL-LABEL: mul_i8:

  ; M2:         mult    $4, $5
  ; M2:         mflo    $[[T0:[0-9]+]]
  ; M2:         sll     $[[T0]], $[[T0]], 24
  ; M2:         sra     $2, $[[T0]], 24

  ; 32R1:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R1:       sll     $[[T0]], $[[T0]], 24
  ; 32R1:       sra     $2, $[[T0]], 24

  ; 32R2:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R2:       seb     $2, $[[T0]]

  ; 32R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R6:       seb     $2, $[[T0]]

  ; M4:         mult    $4, $5
  ; M4:         mflo    $[[T0:[0-9]+]]
  ; M4:         sll     $[[T0]], $[[T0]], 24
  ; M4:         sra     $2, $[[T0]], 24

  ; 64R1:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R1:       sll     $[[T0]], $[[T0]], 24
  ; 64R1:       sra     $2, $[[T0]], 24

  ; 64R2:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R2:       seb     $2, $[[T0]]

  ; 64R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R6:       seb     $2, $[[T0]]
  %r = mul i8 %a, %b
  ret i8 %r
}

define signext i16 @mul_i16(i16 signext %a, i16 signext %b) {
entry:
; ALL-LABEL: mul_i16:

  ; M2:         mult    $4, $5
  ; M2:         mflo    $[[T0:[0-9]+]]
  ; M2:         sll     $[[T0]], $[[T0]], 16
  ; M2:         sra     $2, $[[T0]], 16

  ; 32R1:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R1:       sll     $[[T0]], $[[T0]], 16
  ; 32R1:       sra     $2, $[[T0]], 16

  ; 32R2:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R2:       seh     $2, $[[T0]]

  ; 32R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 32R6:       seh     $2, $[[T0]]

  ; M4:         mult    $4, $5
  ; M4:         mflo    $[[T0:[0-9]+]]
  ; M4:         sll     $[[T0]], $[[T0]], 16
  ; M4:         sra     $2, $[[T0]], 16

  ; 64R1:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R1:       sll     $[[T0]], $[[T0]], 16
  ; 64R1:       sra     $2, $[[T0]], 16

  ; 64R2:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R2:       seh     $2, $[[T0]]

  ; 64R6:       mul     $[[T0:[0-9]+]], $4, $5
  ; 64R6:       seh     $2, $[[T0]]
  %r = mul i16 %a, %b
  ret i16 %r
}

define signext i32 @mul_i32(i32 signext %a, i32 signext %b) {
entry:
; ALL-LABEL: mul_i32:

  ; M2:         mult    $4, $5
  ; M2:         mflo    $2

  ; 32R1-R2:    mul     $2, $4, $5
  ; 32R6:       mul     $2, $4, $5

  ; 64R1-R2:    mul     $2, $4, $5
  ; 64R6:       mul     $2, $4, $5
  %r = mul i32 %a, %b
  ret i32 %r
}

define signext i64 @mul_i64(i64 signext %a, i64 signext %b) {
entry:
; ALL-LABEL: mul_i64:

  ; M2:         mult    $4, $7
  ; M2:         mflo    $[[T0:[0-9]+]]
  ; M2:         mult    $5, $6
  ; M2:         mflo    $[[T1:[0-9]+]]
  ; M2:         multu   $5, $7
  ; M2:         mflo    $3
  ; M2:         mfhi    $4
  ; M2:         addu    $[[T2:[0-9]+]], $4, $[[T1]]
  ; M2:         addu    $2, $[[T2]], $[[T0]]

  ; 32R1-R2:    multu   $5, $7
  ; 32R1-R2:    mflo    $3
  ; 32R1-R2:    mfhi    $[[T0:[0-9]+]]
  ; 32R1-R2:    mul     $[[T1:[0-9]+]], $4, $7
  ; 32R1-R2:    mul     $[[T2:[0-9]+]], $5, $6
  ; 32R1-R2:    addu    $[[T0]], $[[T0]], $[[T2:[0-9]+]]
  ; 32R1-R2:    addu    $2, $[[T0]], $[[T1]]

  ; 32R6:       mul     $[[T0:[0-9]+]], $5, $6
  ; 32R6:       muhu    $[[T1:[0-9]+]], $5, $7
  ; 32R6:       addu    $[[T0]], $[[T1]], $[[T0]]
  ; 32R6:       mul     $[[T2:[0-9]+]], $4, $7
  ; 32R6:       addu    $2, $[[T0]], $[[T2]]
  ; 32R6:       mul     $3, $5, $7

  ; M4:         dmult   $4, $5
  ; M4:         mflo    $2

  ; 64R1-R2:    dmult   $4, $5
  ; 64R1-R2:    mflo    $2

  ; 64R6:       dmul    $2, $4, $5

  %r = mul i64 %a, %b
  ret i64 %r
}

define signext i128 @mul_i128(i128 signext %a, i128 signext %b) {
entry:
; ALL-LABEL: mul_i128:

  ; GP32:           lw      $25, %call16(__multi3)($gp)

  ; GP64-NOT-R6:    dmult   $4, $7
  ; GP64-NOT-R6:    mflo    $[[T0:[0-9]+]]
  ; GP64-NOT-R6:    dmult   $5, $6
  ; GP64-NOT-R6:    mflo    $[[T1:[0-9]+]]
  ; GP64-NOT-R6:    dmultu  $5, $7
  ; GP64-NOT-R6:    mflo    $3
  ; GP64-NOT-R6:    mfhi    $[[T2:[0-9]+]]
  ; GP64-NOT-R6:    daddu   $[[T3:[0-9]+]], $[[T2]], $[[T1]]
  ; GP64-NOT-R6:    daddu   $2, $[[T3:[0-9]+]], $[[T0]]

  ; 64R6:           dmul    $[[T0:[0-9]+]], $5, $6
  ; 64R6:           dmuhu   $[[T1:[0-9]+]], $5, $7
  ; 64R6:           daddu   $[[T2:[0-9]+]], $[[T1]], $[[T0]]
  ; 64R6:           dmul    $[[T3:[0-9]+]], $4, $7
  ; 64R6:           daddu   $2, $[[T2]], $[[T3]]
  ; 64R6:           dmul    $3, $5, $7

  %r = mul i128 %a, %b
  ret i128 %r
}
