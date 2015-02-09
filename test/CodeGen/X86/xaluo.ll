; RUN: llc -mtriple=x86_64-darwin-unknown                             < %s | FileCheck %s --check-prefix=CHECK --check-prefix=SDAG
; RUN: llc -mtriple=x86_64-darwin-unknown -fast-isel -fast-isel-abort < %s | FileCheck %s --check-prefix=CHECK --check-prefix=FAST

;
; Get the actual value of the overflow bit.
;
; SADDO reg, reg
define zeroext i1 @saddo.i8(i8 signext %v1, i8 signext %v2, i8* %res) {
entry:
; CHECK-LABEL: saddo.i8
; CHECK:       addb %sil, %dil
; CHECK-NEXT:  seto %al
  %t = call {i8, i1} @llvm.sadd.with.overflow.i8(i8 %v1, i8 %v2)
  %val = extractvalue {i8, i1} %t, 0
  %obit = extractvalue {i8, i1} %t, 1
  store i8 %val, i8* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i16(i16 %v1, i16 %v2, i16* %res) {
entry:
; CHECK-LABEL: saddo.i16
; CHECK:       addw %si, %di
; CHECK-NEXT:  seto %al
  %t = call {i16, i1} @llvm.sadd.with.overflow.i16(i16 %v1, i16 %v2)
  %val = extractvalue {i16, i1} %t, 0
  %obit = extractvalue {i16, i1} %t, 1
  store i16 %val, i16* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: saddo.i32
; CHECK:       addl %esi, %edi
; CHECK-NEXT:  seto %al
  %t = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: saddo.i64
; CHECK:       addq %rsi, %rdi
; CHECK-NEXT:  seto %al
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; SADDO reg, 1 | INC
define zeroext i1 @saddo.inc.i8(i8 %v1, i8* %res) {
entry:
; CHECK-LABEL: saddo.inc.i8
; CHECK:       incb %dil
; CHECK-NEXT:  seto %al
  %t = call {i8, i1} @llvm.sadd.with.overflow.i8(i8 %v1, i8 1)
  %val = extractvalue {i8, i1} %t, 0
  %obit = extractvalue {i8, i1} %t, 1
  store i8 %val, i8* %res
  ret i1 %obit
}

define zeroext i1 @saddo.inc.i16(i16 %v1, i16* %res) {
entry:
; CHECK-LABEL: saddo.inc.i16
; CHECK:       incw %di
; CHECK-NEXT:  seto %al
  %t = call {i16, i1} @llvm.sadd.with.overflow.i16(i16 %v1, i16 1)
  %val = extractvalue {i16, i1} %t, 0
  %obit = extractvalue {i16, i1} %t, 1
  store i16 %val, i16* %res
  ret i1 %obit
}

define zeroext i1 @saddo.inc.i32(i32 %v1, i32* %res) {
entry:
; CHECK-LABEL: saddo.inc.i32
; CHECK:       incl %edi
; CHECK-NEXT:  seto %al
  %t = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %v1, i32 1)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @saddo.inc.i64(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: saddo.inc.i64
; CHECK:       incq %rdi
; CHECK-NEXT:  seto %al
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 1)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; SADDO reg, imm | imm, reg
; FIXME: DAG doesn't optimize immediates on the LHS.
define zeroext i1 @saddo.i64imm1(i64 %v1, i64* %res) {
entry:
; SDAG-LABEL: saddo.i64imm1
; SDAG:       mov
; SDAG-NEXT:  addq
; SDAG-NEXT:  seto
; FAST-LABEL: saddo.i64imm1
; FAST:       addq $2, %rdi
; FAST-NEXT:  seto %al
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 2, i64 %v1)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; Check boundary conditions for large immediates.
define zeroext i1 @saddo.i64imm2(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: saddo.i64imm2
; CHECK:       addq $-2147483648, %rdi
; CHECK-NEXT:  seto %al
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 -2147483648)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i64imm3(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: saddo.i64imm3
; CHECK:       movabsq $-21474836489, %[[REG:[a-z]+]]
; CHECK-NEXT:  addq %rdi, %[[REG]]
; CHECK-NEXT:  seto
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 -21474836489)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i64imm4(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: saddo.i64imm4
; CHECK:       addq $2147483647, %rdi
; CHECK-NEXT:  seto
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 2147483647)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

define zeroext i1 @saddo.i64imm5(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: saddo.i64imm5
; CHECK:       movl $2147483648
; CHECK:       addq %rdi
; CHECK-NEXT:  seto
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 2147483648)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; UADDO
define zeroext i1 @uaddo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: uaddo.i32
; CHECK:       addl %esi, %edi
; CHECK-NEXT:  setb %al
  %t = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @uaddo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: uaddo.i64
; CHECK:       addq %rsi, %rdi
; CHECK-NEXT:  setb %al
  %t = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; UADDO reg, 1 | NOT INC
define zeroext i1 @uaddo.inc.i8(i8 %v1, i8* %res) {
entry:
; CHECK-LABEL: uaddo.inc.i8
; CHECK-NOT:   incb %dil
  %t = call {i8, i1} @llvm.uadd.with.overflow.i8(i8 %v1, i8 1)
  %val = extractvalue {i8, i1} %t, 0
  %obit = extractvalue {i8, i1} %t, 1
  store i8 %val, i8* %res
  ret i1 %obit
}

define zeroext i1 @uaddo.inc.i16(i16 %v1, i16* %res) {
entry:
; CHECK-LABEL: uaddo.inc.i16
; CHECK-NOT:   incw %di
  %t = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %v1, i16 1)
  %val = extractvalue {i16, i1} %t, 0
  %obit = extractvalue {i16, i1} %t, 1
  store i16 %val, i16* %res
  ret i1 %obit
}

define zeroext i1 @uaddo.inc.i32(i32 %v1, i32* %res) {
entry:
; CHECK-LABEL: uaddo.inc.i32
; CHECK-NOT:   incl %edi
  %t = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %v1, i32 1)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @uaddo.inc.i64(i64 %v1, i64* %res) {
entry:
; CHECK-LABEL: uaddo.inc.i64
; CHECK-NOT:   incq %rdi
  %t = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %v1, i64 1)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; SSUBO
define zeroext i1 @ssubo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: ssubo.i32
; CHECK:       subl %esi, %edi
; CHECK-NEXT:  seto %al
  %t = call {i32, i1} @llvm.ssub.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @ssubo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: ssubo.i64
; CHECK:       subq %rsi, %rdi
; CHECK-NEXT:  seto %al
  %t = call {i64, i1} @llvm.ssub.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; USUBO
define zeroext i1 @usubo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: usubo.i32
; CHECK:       subl %esi, %edi
; CHECK-NEXT:  setb %al
  %t = call {i32, i1} @llvm.usub.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @usubo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: usubo.i64
; CHECK:       subq %rsi, %rdi
; CHECK-NEXT:  setb %al
  %t = call {i64, i1} @llvm.usub.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; SMULO
define zeroext i1 @smulo.i8(i8 %v1, i8 %v2, i8* %res) {
entry:
; CHECK-LABEL:   smulo.i8
; CHECK:         movb %dil, %al
; CHECK-NEXT:    imulb %sil
; CHECK-NEXT:    seto %cl
  %t = call {i8, i1} @llvm.smul.with.overflow.i8(i8 %v1, i8 %v2)
  %val = extractvalue {i8, i1} %t, 0
  %obit = extractvalue {i8, i1} %t, 1
  store i8 %val, i8* %res
  ret i1 %obit
}

define zeroext i1 @smulo.i16(i16 %v1, i16 %v2, i16* %res) {
entry:
; CHECK-LABEL: smulo.i16
; CHECK:       imulw %si, %di
; CHECK-NEXT:  seto %al
  %t = call {i16, i1} @llvm.smul.with.overflow.i16(i16 %v1, i16 %v2)
  %val = extractvalue {i16, i1} %t, 0
  %obit = extractvalue {i16, i1} %t, 1
  store i16 %val, i16* %res
  ret i1 %obit
}

define zeroext i1 @smulo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: smulo.i32
; CHECK:       imull %esi, %edi
; CHECK-NEXT:  seto %al
  %t = call {i32, i1} @llvm.smul.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @smulo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: smulo.i64
; CHECK:       imulq %rsi, %rdi
; CHECK-NEXT:  seto %al
  %t = call {i64, i1} @llvm.smul.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

; UMULO
define zeroext i1 @umulo.i8(i8 %v1, i8 %v2, i8* %res) {
entry:
; CHECK-LABEL:   umulo.i8
; CHECK:         movb %dil, %al
; CHECK-NEXT:    mulb %sil
; CHECK-NEXT:    seto %cl
  %t = call {i8, i1} @llvm.umul.with.overflow.i8(i8 %v1, i8 %v2)
  %val = extractvalue {i8, i1} %t, 0
  %obit = extractvalue {i8, i1} %t, 1
  store i8 %val, i8* %res
  ret i1 %obit
}

define zeroext i1 @umulo.i16(i16 %v1, i16 %v2, i16* %res) {
entry:
; CHECK-LABEL: umulo.i16
; CHECK:       mulw %si
; CHECK-NEXT:  seto
  %t = call {i16, i1} @llvm.umul.with.overflow.i16(i16 %v1, i16 %v2)
  %val = extractvalue {i16, i1} %t, 0
  %obit = extractvalue {i16, i1} %t, 1
  store i16 %val, i16* %res
  ret i1 %obit
}

define zeroext i1 @umulo.i32(i32 %v1, i32 %v2, i32* %res) {
entry:
; CHECK-LABEL: umulo.i32
; CHECK:       mull %esi
; CHECK-NEXT:  seto
  %t = call {i32, i1} @llvm.umul.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  store i32 %val, i32* %res
  ret i1 %obit
}

define zeroext i1 @umulo.i64(i64 %v1, i64 %v2, i64* %res) {
entry:
; CHECK-LABEL: umulo.i64
; CHECK:       mulq %rsi
; CHECK-NEXT:  seto
  %t = call {i64, i1} @llvm.umul.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  store i64 %val, i64* %res
  ret i1 %obit
}

;
; Check the use of the overflow bit in combination with a select instruction.
;
define i32 @saddo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: saddo.select.i32
; CHECK:       addl   %esi, %eax
; CHECK-NEXT:  cmovol %edi, %esi
  %t = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @saddo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: saddo.select.i64
; CHECK:       addq   %rsi, %rax
; CHECK-NEXT:  cmovoq %rdi, %rsi
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}

define i32 @uaddo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: uaddo.select.i32
; CHECK:       addl   %esi, %eax
; CHECK-NEXT:  cmovbl %edi, %esi
  %t = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @uaddo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: uaddo.select.i64
; CHECK:       addq   %rsi, %rax
; CHECK-NEXT:  cmovbq %rdi, %rsi
  %t = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}

define i32 @ssubo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: ssubo.select.i32
; CHECK:       cmpl   %esi, %edi
; CHECK-NEXT:  cmovol %edi, %esi
  %t = call {i32, i1} @llvm.ssub.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @ssubo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: ssubo.select.i64
; CHECK:       cmpq   %rsi, %rdi
; CHECK-NEXT:  cmovoq %rdi, %rsi
  %t = call {i64, i1} @llvm.ssub.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}

define i32 @usubo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: usubo.select.i32
; CHECK:       cmpl   %esi, %edi
; CHECK-NEXT:  cmovbl %edi, %esi
  %t = call {i32, i1} @llvm.usub.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @usubo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: usubo.select.i64
; CHECK:       cmpq   %rsi, %rdi
; CHECK-NEXT:  cmovbq %rdi, %rsi
  %t = call {i64, i1} @llvm.usub.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}

define i32 @smulo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: smulo.select.i32
; CHECK:       imull  %esi, %eax
; CHECK-NEXT:  cmovol %edi, %esi
  %t = call {i32, i1} @llvm.smul.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @smulo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: smulo.select.i64
; CHECK:       imulq  %rsi, %rax
; CHECK-NEXT:  cmovoq %rdi, %rsi
  %t = call {i64, i1} @llvm.smul.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}

define i32 @umulo.select.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: umulo.select.i32
; CHECK:       mull   %esi
; CHECK-NEXT:  cmovol %edi, %esi
  %t = call {i32, i1} @llvm.umul.with.overflow.i32(i32 %v1, i32 %v2)
  %obit = extractvalue {i32, i1} %t, 1
  %ret = select i1 %obit, i32 %v1, i32 %v2
  ret i32 %ret
}

define i64 @umulo.select.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: umulo.select.i64
; CHECK:       mulq   %rsi
; CHECK-NEXT:  cmovoq %rdi, %rsi
  %t = call {i64, i1} @llvm.umul.with.overflow.i64(i64 %v1, i64 %v2)
  %obit = extractvalue {i64, i1} %t, 1
  %ret = select i1 %obit, i64 %v1, i64 %v2
  ret i64 %ret
}


;
; Check the use of the overflow bit in combination with a branch instruction.
;
define zeroext i1 @saddo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: saddo.br.i32
; CHECK:       addl   %esi, %edi
; CHECK-NEXT:  jo
  %t = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @saddo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: saddo.br.i64
; CHECK:       addq   %rsi, %rdi
; CHECK-NEXT:  jo
  %t = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @uaddo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: uaddo.br.i32
; CHECK:       addl   %esi, %edi
; CHECK-NEXT:  jb
  %t = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @uaddo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: uaddo.br.i64
; CHECK:       addq   %rsi, %rdi
; CHECK-NEXT:  jb
  %t = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @ssubo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: ssubo.br.i32
; CHECK:       cmpl   %esi, %edi
; CHECK-NEXT:  jo
  %t = call {i32, i1} @llvm.ssub.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @ssubo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: ssubo.br.i64
; CHECK:       cmpq   %rsi, %rdi
; CHECK-NEXT:  jo
  %t = call {i64, i1} @llvm.ssub.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @usubo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: usubo.br.i32
; CHECK:       cmpl   %esi, %edi
; CHECK-NEXT:  jb
  %t = call {i32, i1} @llvm.usub.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @usubo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: usubo.br.i64
; CHECK:       cmpq   %rsi, %rdi
; CHECK-NEXT:  jb
  %t = call {i64, i1} @llvm.usub.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @smulo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: smulo.br.i32
; CHECK:       imull  %esi, %edi
; CHECK-NEXT:  jo
  %t = call {i32, i1} @llvm.smul.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @smulo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: smulo.br.i64
; CHECK:       imulq  %rsi, %rdi
; CHECK-NEXT:  jo
  %t = call {i64, i1} @llvm.smul.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @umulo.br.i32(i32 %v1, i32 %v2) {
entry:
; CHECK-LABEL: umulo.br.i32
; CHECK:       mull  %esi
; CHECK-NEXT:  jo
  %t = call {i32, i1} @llvm.umul.with.overflow.i32(i32 %v1, i32 %v2)
  %val = extractvalue {i32, i1} %t, 0
  %obit = extractvalue {i32, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

define zeroext i1 @umulo.br.i64(i64 %v1, i64 %v2) {
entry:
; CHECK-LABEL: umulo.br.i64
; CHECK:       mulq  %rsi
; CHECK-NEXT:  jo
  %t = call {i64, i1} @llvm.umul.with.overflow.i64(i64 %v1, i64 %v2)
  %val = extractvalue {i64, i1} %t, 0
  %obit = extractvalue {i64, i1} %t, 1
  br i1 %obit, label %overflow, label %continue, !prof !0

overflow:
  ret i1 false

continue:
  ret i1 true
}

declare {i8,  i1} @llvm.sadd.with.overflow.i8 (i8,  i8 ) nounwind readnone
declare {i16, i1} @llvm.sadd.with.overflow.i16(i16, i16) nounwind readnone
declare {i32, i1} @llvm.sadd.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.sadd.with.overflow.i64(i64, i64) nounwind readnone
declare {i8,  i1} @llvm.uadd.with.overflow.i8 (i8,  i8 ) nounwind readnone
declare {i16, i1} @llvm.uadd.with.overflow.i16(i16, i16) nounwind readnone
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.uadd.with.overflow.i64(i64, i64) nounwind readnone
declare {i32, i1} @llvm.ssub.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.ssub.with.overflow.i64(i64, i64) nounwind readnone
declare {i32, i1} @llvm.usub.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.usub.with.overflow.i64(i64, i64) nounwind readnone
declare {i8,  i1} @llvm.smul.with.overflow.i8 (i8,  i8 ) nounwind readnone
declare {i16, i1} @llvm.smul.with.overflow.i16(i16, i16) nounwind readnone
declare {i32, i1} @llvm.smul.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.smul.with.overflow.i64(i64, i64) nounwind readnone
declare {i8,  i1} @llvm.umul.with.overflow.i8 (i8,  i8 ) nounwind readnone
declare {i16, i1} @llvm.umul.with.overflow.i16(i16, i16) nounwind readnone
declare {i32, i1} @llvm.umul.with.overflow.i32(i32, i32) nounwind readnone
declare {i64, i1} @llvm.umul.with.overflow.i64(i64, i64) nounwind readnone

!0 = !{!"branch_weights", i32 0, i32 2147483647}
