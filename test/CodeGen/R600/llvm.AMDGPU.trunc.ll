; RUN: llc < %s -march=r600 -mcpu=redwood | FileCheck --check-prefix=R600-CHECK %s
; RUN: llc < %s -march=r600 -mcpu=verde | FileCheck --check-prefix=SI-CHECK %s

; R600-CHECK: @amdgpu_trunc
; R600-CHECK: TRUNC * T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
; SI-CHECK: @amdgpu_trunc
; SI-CHECK: V_TRUNC_F32

define void @amdgpu_trunc(float addrspace(1)* %out, float %x) {
entry:
  %0 = call float @llvm.AMDGPU.trunc(float %x)
  store float %0, float addrspace(1)* %out
  ret void
}

declare float @llvm.AMDGPU.trunc(float ) readnone
