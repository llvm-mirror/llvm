; RUN: llc < %s -march=r600 -mcpu=redwood | FileCheck --check-prefix=R600-CHECK %s
; RUN: llc < %s -march=r600 -mcpu=SI | FileCheck --check-prefix=SI-CHECK %s

; BFI_INT Definition pattern from ISA docs
; (y & x) | (z & ~x)
;
; R600-CHECK: @bfi_def
; R600-CHECK: BFI_INT
; SI-CHECK:   @bfi_def
; SI-CHECK:   V_BFI_B32
define void @bfi_def(i32 addrspace(1)* %out, i32 %x, i32 %y, i32 %z) {
entry:
  %0 = xor i32 %x, -1
  %1 = and i32 %z, %0
  %2 = and i32 %y, %x
  %3 = or i32 %1, %2
  store i32 %3, i32 addrspace(1)* %out
  ret void
}

; SHA-256 Ch function
; z ^ (x & (y ^ z))
; R600-CHECK: @bfi_sha256_ch
; R600-CHECK: BFI_INT
; SI-CHECK:   @bfi_sha256_ch
; SI-CHECK:   V_BFI_B32
define void @bfi_sha256_ch(i32 addrspace(1)* %out, i32 %x, i32 %y, i32 %z) {
entry:
  %0 = xor i32 %y, %z
  %1 = and i32 %x, %0
  %2 = xor i32 %z, %1
  store i32 %2, i32 addrspace(1)* %out
  ret void
}

; SHA-256 Ma function
; ((x & z) | (y & (x | z)))
; R600-CHECK: @bfi_sha256_ma
; R600-CHECK: XOR_INT * [[DST:T[0-9]+\.[XYZW]]], {{T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; R600-CHECK: BFI_INT * {{T[0-9]+\.[XYZW]}}, {{[[DST]]|PV\.[XYZW]}}, {{T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; SI-CHECK: V_XOR_B32_e64 [[DST:VGPR[0-9]+]], {{[SV]GPR[0-9]+, [SV]GPR[0-9]+}}
; SI-CHECK: V_BFI_B32 {{VGPR[0-9]+}}, [[DST]], {{[SV]GPR[0-9]+, [SV]GPR[0-9]+}}

define void @bfi_sha256_ma(i32 addrspace(1)* %out, i32 %x, i32 %y, i32 %z) {
entry:
  %0 = and i32 %x, %z
  %1 = or i32 %x, %z
  %2 = and i32 %y, %1
  %3 = or i32 %0, %2
  store i32 %3, i32 addrspace(1)* %out
  ret void
}
