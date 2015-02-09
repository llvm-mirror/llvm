; RUN: llc < %s -march=x86-64 -verify-machineinstrs | FileCheck %s --check-prefix X64
; RUN: llc < %s -march=x86 -verify-machineinstrs | FileCheck %s --check-prefix X32
; RUN: llc < %s -march=x86-64 -mattr=slow-incdec -verify-machineinstrs | FileCheck %s --check-prefix SLOW_INC

; This file checks that atomic (non-seq_cst) stores of immediate values are
; done in one mov instruction and not 2. More precisely, it makes sure that the
; immediate is not first copied uselessly into a register.

; Similarily, it checks that a binary operation of an immediate with an atomic
; variable that is stored back in that variable is done as a single instruction.
; For example: x.store(42 + x.load(memory_order_acquire), memory_order_release)
; should be just an add instruction, instead of loading x into a register, doing
; an add and storing the result back.
; The binary operations supported are currently add, and, or, xor.
; sub is not supported because they are translated by an addition of the
; negated immediate.
; Finally, we also check the same kind of pattern for inc/dec

; seq_cst stores are left as (lock) xchgl, but we try to check every other
; attribute at least once.

; Please note that these operations do not require the lock prefix: only
; sequentially consistent stores require this kind of protection on X86.
; And even for seq_cst operations, llvm uses the xchg instruction which has
; an implicit lock prefix, so making it explicit is not required.

define void @store_atomic_imm_8(i8* %p) {
; X64-LABEL: store_atomic_imm_8
; X64: movb
; X64-NOT: movb
; X32-LABEL: store_atomic_imm_8
; X32: movb
; X32-NOT: movb
  store atomic i8 42, i8* %p release, align 1
  ret void
}

define void @store_atomic_imm_16(i16* %p) {
; X64-LABEL: store_atomic_imm_16
; X64: movw
; X64-NOT: movw
; X32-LABEL: store_atomic_imm_16
; X32: movw
; X32-NOT: movw
  store atomic i16 42, i16* %p monotonic, align 2
  ret void
}

define void @store_atomic_imm_32(i32* %p) {
; X64-LABEL: store_atomic_imm_32
; X64: movl
; X64-NOT: movl
;   On 32 bits, there is an extra movl for each of those functions
;   (probably for alignment reasons).
; X32-LABEL: store_atomic_imm_32
; X32: movl 4(%esp), %eax
; X32: movl
; X32-NOT: movl
  store atomic i32 42, i32* %p release, align 4
  ret void
}

define void @store_atomic_imm_64(i64* %p) {
; X64-LABEL: store_atomic_imm_64
; X64: movq
; X64-NOT: movq
;   These are implemented with a CAS loop on 32 bit architectures, and thus
;   cannot be optimized in the same way as the others.
; X32-LABEL: store_atomic_imm_64
; X32: cmpxchg8b
  store atomic i64 42, i64* %p release, align 8
  ret void
}

; If an immediate is too big to fit in 32 bits, it cannot be store in one mov,
; even on X64, one must use movabsq that can only target a register.
define void @store_atomic_imm_64_big(i64* %p) {
; X64-LABEL: store_atomic_imm_64_big
; X64: movabsq
; X64: movq
  store atomic i64 100000000000, i64* %p monotonic, align 8
  ret void
}

; It would be incorrect to replace a lock xchgl by a movl
define void @store_atomic_imm_32_seq_cst(i32* %p) {
; X64-LABEL: store_atomic_imm_32_seq_cst
; X64: xchgl
; X32-LABEL: store_atomic_imm_32_seq_cst
; X32: xchgl
  store atomic i32 42, i32* %p seq_cst, align 4
  ret void
}

; ----- ADD -----

define void @add_8(i8* %p) {
; X64-LABEL: add_8
; X64-NOT: lock
; X64: addb
; X64-NOT: movb
; X32-LABEL: add_8
; X32-NOT: lock
; X32: addb
; X32-NOT: movb
  %1 = load atomic i8* %p seq_cst, align 1
  %2 = add i8 %1, 2
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @add_16(i16* %p) {
;   Currently the transformation is not done on 16 bit accesses, as the backend
;   treat 16 bit arithmetic as expensive on X86/X86_64.
; X64-LABEL: add_16
; X64-NOT: addw
; X32-LABEL: add_16
; X32-NOT: addw
  %1 = load atomic i16* %p acquire, align 2
  %2 = add i16 %1, 2
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @add_32(i32* %p) {
; X64-LABEL: add_32
; X64-NOT: lock
; X64: addl
; X64-NOT: movl
; X32-LABEL: add_32
; X32-NOT: lock
; X32: addl
; X32-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = add i32 %1, 2
  store atomic i32 %2, i32* %p monotonic, align 4
  ret void
}

define void @add_64(i64* %p) {
; X64-LABEL: add_64
; X64-NOT: lock
; X64: addq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'addq'.
; X32-LABEL: add_64
  %1 = load atomic i64* %p acquire, align 8
  %2 = add i64 %1, 2
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @add_32_seq_cst(i32* %p) {
; X64-LABEL: add_32_seq_cst
; X64: xchgl
; X32-LABEL: add_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = add i32 %1, 2
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}

; ----- AND -----

define void @and_8(i8* %p) {
; X64-LABEL: and_8
; X64-NOT: lock
; X64: andb
; X64-NOT: movb
; X32-LABEL: and_8
; X32-NOT: lock
; X32: andb
; X32-NOT: movb
  %1 = load atomic i8* %p monotonic, align 1
  %2 = and i8 %1, 2
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @and_16(i16* %p) {
;   Currently the transformation is not done on 16 bit accesses, as the backend
;   treat 16 bit arithmetic as expensive on X86/X86_64.
; X64-LABEL: and_16
; X64-NOT: andw
; X32-LABEL: and_16
; X32-NOT: andw
  %1 = load atomic i16* %p acquire, align 2
  %2 = and i16 %1, 2
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @and_32(i32* %p) {
; X64-LABEL: and_32
; X64-NOT: lock
; X64: andl
; X64-NOT: movl
; X32-LABEL: and_32
; X32-NOT: lock
; X32: andl
; X32-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = and i32 %1, 2
  store atomic i32 %2, i32* %p release, align 4
  ret void
}

define void @and_64(i64* %p) {
; X64-LABEL: and_64
; X64-NOT: lock
; X64: andq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'andq'.
; X32-LABEL: and_64
  %1 = load atomic i64* %p acquire, align 8
  %2 = and i64 %1, 2
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @and_32_seq_cst(i32* %p) {
; X64-LABEL: and_32_seq_cst
; X64: xchgl
; X32-LABEL: and_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = and i32 %1, 2
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}

; ----- OR -----

define void @or_8(i8* %p) {
; X64-LABEL: or_8
; X64-NOT: lock
; X64: orb
; X64-NOT: movb
; X32-LABEL: or_8
; X32-NOT: lock
; X32: orb
; X32-NOT: movb
  %1 = load atomic i8* %p acquire, align 1
  %2 = or i8 %1, 2
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @or_16(i16* %p) {
; X64-LABEL: or_16
; X64-NOT: orw
; X32-LABEL: or_16
; X32-NOT: orw
  %1 = load atomic i16* %p acquire, align 2
  %2 = or i16 %1, 2
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @or_32(i32* %p) {
; X64-LABEL: or_32
; X64-NOT: lock
; X64: orl
; X64-NOT: movl
; X32-LABEL: or_32
; X32-NOT: lock
; X32: orl
; X32-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = or i32 %1, 2
  store atomic i32 %2, i32* %p release, align 4
  ret void
}

define void @or_64(i64* %p) {
; X64-LABEL: or_64
; X64-NOT: lock
; X64: orq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'orq'.
; X32-LABEL: or_64
  %1 = load atomic i64* %p acquire, align 8
  %2 = or i64 %1, 2
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @or_32_seq_cst(i32* %p) {
; X64-LABEL: or_32_seq_cst
; X64: xchgl
; X32-LABEL: or_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = or i32 %1, 2
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}

; ----- XOR -----

define void @xor_8(i8* %p) {
; X64-LABEL: xor_8
; X64-NOT: lock
; X64: xorb
; X64-NOT: movb
; X32-LABEL: xor_8
; X32-NOT: lock
; X32: xorb
; X32-NOT: movb
  %1 = load atomic i8* %p acquire, align 1
  %2 = xor i8 %1, 2
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @xor_16(i16* %p) {
; X64-LABEL: xor_16
; X64-NOT: xorw
; X32-LABEL: xor_16
; X32-NOT: xorw
  %1 = load atomic i16* %p acquire, align 2
  %2 = xor i16 %1, 2
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @xor_32(i32* %p) {
; X64-LABEL: xor_32
; X64-NOT: lock
; X64: xorl
; X64-NOT: movl
; X32-LABEL: xor_32
; X32-NOT: lock
; X32: xorl
; X32-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = xor i32 %1, 2
  store atomic i32 %2, i32* %p release, align 4
  ret void
}

define void @xor_64(i64* %p) {
; X64-LABEL: xor_64
; X64-NOT: lock
; X64: xorq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'xorq'.
; X32-LABEL: xor_64
  %1 = load atomic i64* %p acquire, align 8
  %2 = xor i64 %1, 2
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @xor_32_seq_cst(i32* %p) {
; X64-LABEL: xor_32_seq_cst
; X64: xchgl
; X32-LABEL: xor_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = xor i32 %1, 2
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}

; ----- INC -----

define void @inc_8(i8* %p) {
; X64-LABEL: inc_8
; X64-NOT: lock
; X64: incb
; X64-NOT: movb
; X32-LABEL: inc_8
; X32-NOT: lock
; X32: incb
; X32-NOT: movb
; SLOW_INC-LABEL: inc_8
; SLOW_INC-NOT: incb
; SLOW_INC-NOT: movb
  %1 = load atomic i8* %p seq_cst, align 1
  %2 = add i8 %1, 1
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @inc_16(i16* %p) {
;   Currently the transformation is not done on 16 bit accesses, as the backend
;   treat 16 bit arithmetic as expensive on X86/X86_64.
; X64-LABEL: inc_16
; X64-NOT: incw
; X32-LABEL: inc_16
; X32-NOT: incw
; SLOW_INC-LABEL: inc_16
; SLOW_INC-NOT: incw
  %1 = load atomic i16* %p acquire, align 2
  %2 = add i16 %1, 1
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @inc_32(i32* %p) {
; X64-LABEL: inc_32
; X64-NOT: lock
; X64: incl
; X64-NOT: movl
; X32-LABEL: inc_32
; X32-NOT: lock
; X32: incl
; X32-NOT: movl
; SLOW_INC-LABEL: inc_32
; SLOW_INC-NOT: incl
; SLOW_INC-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = add i32 %1, 1
  store atomic i32 %2, i32* %p monotonic, align 4
  ret void
}

define void @inc_64(i64* %p) {
; X64-LABEL: inc_64
; X64-NOT: lock
; X64: incq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'incq'.
; X32-LABEL: inc_64
; SLOW_INC-LABEL: inc_64
; SLOW_INC-NOT: incq
; SLOW_INC-NOT: movq
  %1 = load atomic i64* %p acquire, align 8
  %2 = add i64 %1, 1
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @inc_32_seq_cst(i32* %p) {
; X64-LABEL: inc_32_seq_cst
; X64: xchgl
; X32-LABEL: inc_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = add i32 %1, 1
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}

; ----- DEC -----

define void @dec_8(i8* %p) {
; X64-LABEL: dec_8
; X64-NOT: lock
; X64: decb
; X64-NOT: movb
; X32-LABEL: dec_8
; X32-NOT: lock
; X32: decb
; X32-NOT: movb
; SLOW_INC-LABEL: dec_8
; SLOW_INC-NOT: decb
; SLOW_INC-NOT: movb
  %1 = load atomic i8* %p seq_cst, align 1
  %2 = sub i8 %1, 1
  store atomic i8 %2, i8* %p release, align 1
  ret void
}

define void @dec_16(i16* %p) {
;   Currently the transformation is not done on 16 bit accesses, as the backend
;   treat 16 bit arithmetic as expensive on X86/X86_64.
; X64-LABEL: dec_16
; X64-NOT: decw
; X32-LABEL: dec_16
; X32-NOT: decw
; SLOW_INC-LABEL: dec_16
; SLOW_INC-NOT: decw
  %1 = load atomic i16* %p acquire, align 2
  %2 = sub i16 %1, 1
  store atomic i16 %2, i16* %p release, align 2
  ret void
}

define void @dec_32(i32* %p) {
; X64-LABEL: dec_32
; X64-NOT: lock
; X64: decl
; X64-NOT: movl
; X32-LABEL: dec_32
; X32-NOT: lock
; X32: decl
; X32-NOT: movl
; SLOW_INC-LABEL: dec_32
; SLOW_INC-NOT: decl
; SLOW_INC-NOT: movl
  %1 = load atomic i32* %p acquire, align 4
  %2 = sub i32 %1, 1
  store atomic i32 %2, i32* %p monotonic, align 4
  ret void
}

define void @dec_64(i64* %p) {
; X64-LABEL: dec_64
; X64-NOT: lock
; X64: decq
; X64-NOT: movq
;   We do not check X86-32 as it cannot do 'decq'.
; X32-LABEL: dec_64
; SLOW_INC-LABEL: dec_64
; SLOW_INC-NOT: decq
; SLOW_INC-NOT: movq
  %1 = load atomic i64* %p acquire, align 8
  %2 = sub i64 %1, 1
  store atomic i64 %2, i64* %p release, align 8
  ret void
}

define void @dec_32_seq_cst(i32* %p) {
; X64-LABEL: dec_32_seq_cst
; X64: xchgl
; X32-LABEL: dec_32_seq_cst
; X32: xchgl
  %1 = load atomic i32* %p monotonic, align 4
  %2 = sub i32 %1, 1
  store atomic i32 %2, i32* %p seq_cst, align 4
  ret void
}
