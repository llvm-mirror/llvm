; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s

; FIXME: Check something here. Currently it seems fabs + fneg aren't
; into 2 modifiers, although theoretically that should work.

; FUNC-LABEL: {{^}}fneg_fabs_fadd_f64:
; SI: v_mov_b32_e32 [[IMMREG:v[0-9]+]], 0x7fffffff
; SI: v_and_b32_e32 v[[FABS:[0-9]+]], {{s[0-9]+}}, [[IMMREG]]
; SI: v_add_f64 {{v\[[0-9]+:[0-9]+\]}}, {{v\[[0-9]+:[0-9]+\]}}, -v{{\[[0-9]+}}:[[FABS]]{{\]}}
define void @fneg_fabs_fadd_f64(double addrspace(1)* %out, double %x, double %y) {
  %fabs = call double @llvm.fabs.f64(double %x)
  %fsub = fsub double -0.000000e+00, %fabs
  %fadd = fadd double %y, %fsub
  store double %fadd, double addrspace(1)* %out, align 8
  ret void
}

define void @v_fneg_fabs_fadd_f64(double addrspace(1)* %out, double addrspace(1)* %xptr, double addrspace(1)* %yptr) {
  %x = load double addrspace(1)* %xptr, align 8
  %y = load double addrspace(1)* %xptr, align 8
  %fabs = call double @llvm.fabs.f64(double %x)
  %fsub = fsub double -0.000000e+00, %fabs
  %fadd = fadd double %y, %fsub
  store double %fadd, double addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_fmul_f64:
; SI: v_mul_f64 {{v\[[0-9]+:[0-9]+\]}}, {{s\[[0-9]+:[0-9]+\]}}, -|{{v\[[0-9]+:[0-9]+\]}}|
define void @fneg_fabs_fmul_f64(double addrspace(1)* %out, double %x, double %y) {
  %fabs = call double @llvm.fabs.f64(double %x)
  %fsub = fsub double -0.000000e+00, %fabs
  %fmul = fmul double %y, %fsub
  store double %fmul, double addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_free_f64:
define void @fneg_fabs_free_f64(double addrspace(1)* %out, i64 %in) {
  %bc = bitcast i64 %in to double
  %fabs = call double @llvm.fabs.f64(double %bc)
  %fsub = fsub double -0.000000e+00, %fabs
  store double %fsub, double addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_fn_free_f64:
; SI: v_mov_b32_e32 [[IMMREG:v[0-9]+]], 0x80000000
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
define void @fneg_fabs_fn_free_f64(double addrspace(1)* %out, i64 %in) {
  %bc = bitcast i64 %in to double
  %fabs = call double @fabs(double %bc)
  %fsub = fsub double -0.000000e+00, %fabs
  store double %fsub, double addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_f64:
; SI: s_load_dwordx2 s{{\[}}[[LO_X:[0-9]+]]:[[HI_X:[0-9]+]]{{\]}}
; SI: s_load_dwordx2
; SI: v_mov_b32_e32 [[IMMREG:v[0-9]+]], 0x80000000
; SI-DAG: v_or_b32_e32 v[[HI_V:[0-9]+]], s[[HI_X]], [[IMMREG]]
; SI-DAG: v_mov_b32_e32 v[[LO_V:[0-9]+]], s[[LO_X]]
; SI: buffer_store_dwordx2 v{{\[}}[[LO_V]]:[[HI_V]]{{\]}}
define void @fneg_fabs_f64(double addrspace(1)* %out, double %in) {
  %fabs = call double @llvm.fabs.f64(double %in)
  %fsub = fsub double -0.000000e+00, %fabs
  store double %fsub, double addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_v2f64:
; SI: v_mov_b32_e32 [[IMMREG:v[0-9]+]], 0x80000000
; SI-NOT: 0x80000000
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
define void @fneg_fabs_v2f64(<2 x double> addrspace(1)* %out, <2 x double> %in) {
  %fabs = call <2 x double> @llvm.fabs.v2f64(<2 x double> %in)
  %fsub = fsub <2 x double> <double -0.000000e+00, double -0.000000e+00>, %fabs
  store <2 x double> %fsub, <2 x double> addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}fneg_fabs_v4f64:
; SI: v_mov_b32_e32 [[IMMREG:v[0-9]+]], 0x80000000
; SI-NOT: 0x80000000
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
; SI: v_or_b32_e32 v{{[0-9]+}}, s{{[0-9]+}}, [[IMMREG]]
define void @fneg_fabs_v4f64(<4 x double> addrspace(1)* %out, <4 x double> %in) {
  %fabs = call <4 x double> @llvm.fabs.v4f64(<4 x double> %in)
  %fsub = fsub <4 x double> <double -0.000000e+00, double -0.000000e+00, double -0.000000e+00, double -0.000000e+00>, %fabs
  store <4 x double> %fsub, <4 x double> addrspace(1)* %out
  ret void
}

declare double @fabs(double) readnone
declare double @llvm.fabs.f64(double) readnone
declare <2 x double> @llvm.fabs.v2f64(<2 x double>) readnone
declare <4 x double> @llvm.fabs.v4f64(<4 x double>) readnone
