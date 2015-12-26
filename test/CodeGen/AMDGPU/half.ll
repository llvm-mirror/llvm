; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=GCN -check-prefix=SI %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck -check-prefix=GCN -check-prefix=VI %s

; half args should be promoted to float

; GCN-LABEL: {{^}}load_f16_arg:
; GCN: s_load_dword [[ARG:s[0-9]+]]
; GCN: v_cvt_f16_f32_e32 [[CVT:v[0-9]+]], [[ARG]]
; GCN: buffer_store_short [[CVT]]
define void @load_f16_arg(half addrspace(1)* %out, half %arg) #0 {
  store half %arg, half addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}load_v2f16_arg:
; GCN-DAG: buffer_load_ushort [[V0:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0 offset:44
; GCN-DAG: buffer_load_ushort [[V1:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0 offset:46
; GCN-DAG: buffer_store_short [[V0]], s{{\[[0-9]+:[0-9]+\]}}, 0{{$}}
; GCN-DAG: buffer_store_short [[V1]], s{{\[[0-9]+:[0-9]+\]}}, 0 offset:2{{$}}
; GCN: s_endpgm
define void @load_v2f16_arg(<2 x half> addrspace(1)* %out, <2 x half> %arg) #0 {
  store <2 x half> %arg, <2 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}load_v3f16_arg:
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN-NOT: buffer_load
; GCN-DAG: buffer_store_dword
; GCN-DAG: buffer_store_short
; GCN-NOT: buffer_store
; GCN: s_endpgm
define void @load_v3f16_arg(<3 x half> addrspace(1)* %out, <3 x half> %arg) #0 {
  store <3 x half> %arg, <3 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}load_v4f16_arg:
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: s_endpgm
define void @load_v4f16_arg(<4 x half> addrspace(1)* %out, <4 x half> %arg) #0 {
  store <4 x half> %arg, <4 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}load_v8f16_arg:
define void @load_v8f16_arg(<8 x half> addrspace(1)* %out, <8 x half> %arg) #0 {
  store <8 x half> %arg, <8 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v2f16_arg:
define void @extload_v2f16_arg(<2 x float> addrspace(1)* %out, <2 x half> %in) #0 {
  %fpext = fpext <2 x half> %in to <2 x float>
  store <2 x float> %fpext, <2 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_f16_to_f32_arg:
define void @extload_f16_to_f32_arg(float addrspace(1)* %out, half %arg) #0 {
  %ext = fpext half %arg to float
  store float %ext, float addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v2f16_to_v2f32_arg:
define void @extload_v2f16_to_v2f32_arg(<2 x float> addrspace(1)* %out, <2 x half> %arg) #0 {
  %ext = fpext <2 x half> %arg to <2 x float>
  store <2 x float> %ext, <2 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v3f16_to_v3f32_arg:
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN-NOT: buffer_load
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN-NOT: v_cvt_f32_f16
; GCN-DAG: buffer_store_dword
; GCN-DAG: buffer_store_dwordx2
; GCN: s_endpgm
define void @extload_v3f16_to_v3f32_arg(<3 x float> addrspace(1)* %out, <3 x half> %arg) #0 {
  %ext = fpext <3 x half> %arg to <3 x float>
  store <3 x float> %ext, <3 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v4f16_to_v4f32_arg:
define void @extload_v4f16_to_v4f32_arg(<4 x float> addrspace(1)* %out, <4 x half> %arg) #0 {
  %ext = fpext <4 x half> %arg to <4 x float>
  store <4 x float> %ext, <4 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v8f16_to_v8f32_arg:
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort

; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32

; GCN: buffer_store_dwordx4
; GCN: buffer_store_dwordx4
define void @extload_v8f16_to_v8f32_arg(<8 x float> addrspace(1)* %out, <8 x half> %arg) #0 {
  %ext = fpext <8 x half> %arg to <8 x float>
  store <8 x float> %ext, <8 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_f16_to_f64_arg:
; SI: s_load_dword [[ARG:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0xb{{$}}
; VI: s_load_dword [[ARG:s[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0x2c{{$}}
; GCN: v_cvt_f64_f32_e32 [[RESULT:v\[[0-9]+:[0-9]+\]]], [[ARG]]
; GCN: buffer_store_dwordx2 [[RESULT]]
define void @extload_f16_to_f64_arg(double addrspace(1)* %out, half %arg) #0 {
  %ext = fpext half %arg to double
  store double %ext, double addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v2f16_to_v2f64_arg:
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN: s_endpgm
define void @extload_v2f16_to_v2f64_arg(<2 x double> addrspace(1)* %out, <2 x half> %arg) #0 {
  %ext = fpext <2 x half> %arg to <2 x double>
  store <2 x double> %ext, <2 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v3f16_to_v3f64_arg:
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN: s_endpgm
define void @extload_v3f16_to_v3f64_arg(<3 x double> addrspace(1)* %out, <3 x half> %arg) #0 {
  %ext = fpext <3 x half> %arg to <3 x double>
  store <3 x double> %ext, <3 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v4f16_to_v4f64_arg:
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN: s_endpgm
define void @extload_v4f16_to_v4f64_arg(<4 x double> addrspace(1)* %out, <4 x half> %arg) #0 {
  %ext = fpext <4 x half> %arg to <4 x double>
  store <4 x double> %ext, <4 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}extload_v8f16_to_v8f64_arg:
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v

; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v
; GCN-DAG: buffer_load_ushort v

; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32

; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32
; GCN-DAG: v_cvt_f32_f16_e32

; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32

; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32
; GCN-DAG: v_cvt_f64_f32_e32

; GCN: s_endpgm
define void @extload_v8f16_to_v8f64_arg(<8 x double> addrspace(1)* %out, <8 x half> %arg) #0 {
  %ext = fpext <8 x half> %arg to <8 x double>
  store <8 x double> %ext, <8 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_load_store_f16:
; GCN: buffer_load_ushort [[TMP:v[0-9]+]]
; GCN: buffer_store_short [[TMP]]
define void @global_load_store_f16(half addrspace(1)* %out, half addrspace(1)* %in) #0 {
  %val = load half, half addrspace(1)* %in
  store half %val, half addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_load_store_v2f16:
; GCN: buffer_load_dword [[TMP:v[0-9]+]]
; GCN: buffer_store_dword [[TMP]]
define void @global_load_store_v2f16(<2 x half> addrspace(1)* %out, <2 x half> addrspace(1)* %in) #0 {
  %val = load <2 x half>, <2 x half> addrspace(1)* %in
  store <2 x half> %val, <2 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_load_store_v4f16:
; GCN: buffer_load_dwordx2 [[TMP:v\[[0-9]+:[0-9]+\]]]
; GCN: buffer_store_dwordx2 [[TMP]]
define void @global_load_store_v4f16(<4 x half> addrspace(1)* %in, <4 x half> addrspace(1)* %out) #0 {
  %val = load <4 x half>, <4 x half> addrspace(1)* %in
  store <4 x half> %val, <4 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_load_store_v8f16:
; GCN: buffer_load_dwordx4 [[TMP:v\[[0-9]+:[0-9]+\]]]
; GCN: buffer_store_dwordx4 [[TMP:v\[[0-9]+:[0-9]+\]]]
; GCN: s_endpgm
define void @global_load_store_v8f16(<8 x half> addrspace(1)* %out, <8 x half> addrspace(1)* %in) #0 {
  %val = load <8 x half>, <8 x half> addrspace(1)* %in
  store <8 x half> %val, <8 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_f16_to_f32:
; GCN: buffer_load_ushort [[LOAD:v[0-9]+]]
; GCN: v_cvt_f32_f16_e32 [[CVT:v[0-9]+]], [[LOAD]]
; GCN: buffer_store_dword [[CVT]]
define void @global_extload_f16_to_f32(float addrspace(1)* %out, half addrspace(1)* %in) #0 {
  %val = load half, half addrspace(1)* %in
  %cvt = fpext half %val to float
  store float %cvt, float addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v2f16_to_v2f32:
; GCN-DAG: buffer_load_ushort [[LOAD0:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0{{$}}
; GCN-DAG: buffer_load_ushort [[LOAD1:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0 offset:2{{$}}
; GCN-DAG: v_cvt_f32_f16_e32 v[[CVT0:[0-9]+]], [[LOAD0]]
; GCN-DAG: v_cvt_f32_f16_e32 v[[CVT1:[0-9]+]], [[LOAD1]]
; GCN-DAG: buffer_store_dwordx2 v{{\[}}[[CVT0]]:[[CVT1]]{{\]}}
; GCN: s_endpgm
define void @global_extload_v2f16_to_v2f32(<2 x float> addrspace(1)* %out, <2 x half> addrspace(1)* %in) #0 {
  %val = load <2 x half>, <2 x half> addrspace(1)* %in
  %cvt = fpext <2 x half> %val to <2 x float>
  store <2 x float> %cvt, <2 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v3f16_to_v3f32:
define void @global_extload_v3f16_to_v3f32(<3 x float> addrspace(1)* %out, <3 x half> addrspace(1)* %in) #0 {
  %val = load <3 x half>, <3 x half> addrspace(1)* %in
  %cvt = fpext <3 x half> %val to <3 x float>
  store <3 x float> %cvt, <3 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v4f16_to_v4f32:
define void @global_extload_v4f16_to_v4f32(<4 x float> addrspace(1)* %out, <4 x half> addrspace(1)* %in) #0 {
  %val = load <4 x half>, <4 x half> addrspace(1)* %in
  %cvt = fpext <4 x half> %val to <4 x float>
  store <4 x float> %cvt, <4 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v8f16_to_v8f32:
define void @global_extload_v8f16_to_v8f32(<8 x float> addrspace(1)* %out, <8 x half> addrspace(1)* %in) #0 {
  %val = load <8 x half>, <8 x half> addrspace(1)* %in
  %cvt = fpext <8 x half> %val to <8 x float>
  store <8 x float> %cvt, <8 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v16f16_to_v16f32:
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort
; GCN: buffer_load_ushort

; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32

; GCN: buffer_store_dwordx4
; GCN: buffer_store_dwordx4
; GCN: buffer_store_dwordx4
; GCN: buffer_store_dwordx4

; GCN: s_endpgm
define void @global_extload_v16f16_to_v16f32(<16 x float> addrspace(1)* %out, <16 x half> addrspace(1)* %in) #0 {
  %val = load <16 x half>, <16 x half> addrspace(1)* %in
  %cvt = fpext <16 x half> %val to <16 x float>
  store <16 x float> %cvt, <16 x float> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_f16_to_f64:
; GCN: buffer_load_ushort [[LOAD:v[0-9]+]]
; GCN: v_cvt_f32_f16_e32 [[CVT0:v[0-9]+]], [[LOAD]]
; GCN: v_cvt_f64_f32_e32 [[CVT1:v\[[0-9]+:[0-9]+\]]], [[CVT0]]
; GCN: buffer_store_dwordx2 [[CVT1]]
define void @global_extload_f16_to_f64(double addrspace(1)* %out, half addrspace(1)* %in) #0 {
  %val = load half, half addrspace(1)* %in
  %cvt = fpext half %val to double
  store double %cvt, double addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v2f16_to_v2f64:
; GCN-DAG: buffer_load_ushort [[LOAD0:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0{{$}}
; GCN-DAG: buffer_load_ushort [[LOAD1:v[0-9]+]], s{{\[[0-9]+:[0-9]+\]}}, 0 offset:2{{$}}
; GCN-DAG: v_cvt_f32_f16_e32 v[[CVT0:[0-9]+]], [[LOAD0]]
; GCN-DAG: v_cvt_f32_f16_e32 v[[CVT1:[0-9]+]], [[LOAD1]]
; GCN-DAG: v_cvt_f64_f32_e32 v{{\[}}[[CVT2_LO:[0-9]+]]:[[CVT2_HI:[0-9]+]]{{\]}}, v[[CVT0]]
; GCN-DAG: v_cvt_f64_f32_e32 v{{\[}}[[CVT3_LO:[0-9]+]]:[[CVT3_HI:[0-9]+]]{{\]}}, v[[CVT1]]
; GCN-DAG: buffer_store_dwordx4 v{{\[}}[[CVT2_LO]]:[[CVT3_HI]]{{\]}}
; GCN: s_endpgm
define void @global_extload_v2f16_to_v2f64(<2 x double> addrspace(1)* %out, <2 x half> addrspace(1)* %in) #0 {
  %val = load <2 x half>, <2 x half> addrspace(1)* %in
  %cvt = fpext <2 x half> %val to <2 x double>
  store <2 x double> %cvt, <2 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v3f16_to_v3f64:

; GCN: buffer_load_dwordx2 [[LOAD:v\[[0-9]+:[0-9]+\]]]
; SI: v_lshr_b64 v{{\[[0-9]+:[0-9]+\]}}, [[LOAD]], 32
; VI: v_lshrrev_b64 v{{\[[0-9]+:[0-9]+\]}}, 32, [[LOAD]]
; GCN: v_lshrrev_b32_e32 {{v[0-9]+}}, 16, {{v[0-9]+}}

; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN: v_cvt_f32_f16_e32
; GCN-NOT: v_cvt_f32_f16_e32

; GCN: v_cvt_f64_f32_e32
; GCN: v_cvt_f64_f32_e32
; GCN: v_cvt_f64_f32_e32
; GCN-NOT: v_cvt_f64_f32_e32

; GCN-DAG: buffer_store_dwordx4 v{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 0{{$}}
; GCN-DAG: buffer_store_dwordx2 v{{\[[0-9]+:[0-9]+\]}}, s{{\[[0-9]+:[0-9]+\]}}, 0 offset:16
; GCN: s_endpgm
define void @global_extload_v3f16_to_v3f64(<3 x double> addrspace(1)* %out, <3 x half> addrspace(1)* %in) #0 {
  %val = load <3 x half>, <3 x half> addrspace(1)* %in
  %cvt = fpext <3 x half> %val to <3 x double>
  store <3 x double> %cvt, <3 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v4f16_to_v4f64:
define void @global_extload_v4f16_to_v4f64(<4 x double> addrspace(1)* %out, <4 x half> addrspace(1)* %in) #0 {
  %val = load <4 x half>, <4 x half> addrspace(1)* %in
  %cvt = fpext <4 x half> %val to <4 x double>
  store <4 x double> %cvt, <4 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v8f16_to_v8f64:
define void @global_extload_v8f16_to_v8f64(<8 x double> addrspace(1)* %out, <8 x half> addrspace(1)* %in) #0 {
  %val = load <8 x half>, <8 x half> addrspace(1)* %in
  %cvt = fpext <8 x half> %val to <8 x double>
  store <8 x double> %cvt, <8 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_extload_v16f16_to_v16f64:
define void @global_extload_v16f16_to_v16f64(<16 x double> addrspace(1)* %out, <16 x half> addrspace(1)* %in) #0 {
  %val = load <16 x half>, <16 x half> addrspace(1)* %in
  %cvt = fpext <16 x half> %val to <16 x double>
  store <16 x double> %cvt, <16 x double> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_f32_to_f16:
; GCN: buffer_load_dword [[LOAD:v[0-9]+]]
; GCN: v_cvt_f16_f32_e32 [[CVT:v[0-9]+]], [[LOAD]]
; GCN: buffer_store_short [[CVT]]
define void @global_truncstore_f32_to_f16(half addrspace(1)* %out, float addrspace(1)* %in) #0 {
  %val = load float, float addrspace(1)* %in
  %cvt = fptrunc float %val to half
  store half %cvt, half addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v2f32_to_v2f16:
; GCN: buffer_load_dwordx2 v{{\[}}[[LO:[0-9]+]]:[[HI:[0-9]+]]{{\]}}
; GCN-DAG: v_cvt_f16_f32_e32 [[CVT0:v[0-9]+]], v[[LO]]
; GCN-DAG: v_cvt_f16_f32_e32 [[CVT1:v[0-9]+]], v[[HI]]
; GCN-DAG: buffer_store_short [[CVT0]]
; GCN-DAG: buffer_store_short [[CVT1]]
; GCN: s_endpgm
define void @global_truncstore_v2f32_to_v2f16(<2 x half> addrspace(1)* %out, <2 x float> addrspace(1)* %in) #0 {
  %val = load <2 x float>, <2 x float> addrspace(1)* %in
  %cvt = fptrunc <2 x float> %val to <2 x half>
  store <2 x half> %cvt, <2 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v3f32_to_v3f16:
; GCN: buffer_load_dwordx4
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN-NOT: v_cvt_f16_f32_e32
; GCN: buffer_store_short
; GCN: buffer_store_dword
; GCN: s_endpgm
define void @global_truncstore_v3f32_to_v3f16(<3 x half> addrspace(1)* %out, <3 x float> addrspace(1)* %in) #0 {
  %val = load <3 x float>, <3 x float> addrspace(1)* %in
  %cvt = fptrunc <3 x float> %val to <3 x half>
  store <3 x half> %cvt, <3 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v4f32_to_v4f16:
; GCN: buffer_load_dwordx4
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: s_endpgm
define void @global_truncstore_v4f32_to_v4f16(<4 x half> addrspace(1)* %out, <4 x float> addrspace(1)* %in) #0 {
  %val = load <4 x float>, <4 x float> addrspace(1)* %in
  %cvt = fptrunc <4 x float> %val to <4 x half>
  store <4 x half> %cvt, <4 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v8f32_to_v8f16:
; GCN: buffer_load_dwordx4
; GCN: buffer_load_dwordx4
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: v_cvt_f16_f32_e32
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: buffer_store_short
; GCN: s_endpgm
define void @global_truncstore_v8f32_to_v8f16(<8 x half> addrspace(1)* %out, <8 x float> addrspace(1)* %in) #0 {
  %val = load <8 x float>, <8 x float> addrspace(1)* %in
  %cvt = fptrunc <8 x float> %val to <8 x half>
  store <8 x half> %cvt, <8 x half> addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v16f32_to_v16f16:
; GCN: buffer_load_dwordx4
; GCN: buffer_load_dwordx4
; GCN: buffer_load_dwordx4
; GCN: buffer_load_dwordx4
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: v_cvt_f16_f32_e32
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN-DAG: buffer_store_short
; GCN: s_endpgm
define void @global_truncstore_v16f32_to_v16f16(<16 x half> addrspace(1)* %out, <16 x float> addrspace(1)* %in) #0 {
  %val = load <16 x float>, <16 x float> addrspace(1)* %in
  %cvt = fptrunc <16 x float> %val to <16 x half>
  store <16 x half> %cvt, <16 x half> addrspace(1)* %out
  ret void
}

; FIXME: Unsafe math should fold conversions away
; GCN-LABEL: {{^}}fadd_f16:
; SI-DAG: v_cvt_f32_f16_e32 v{{[0-9]+}},
; SI-DAG: v_cvt_f32_f16_e32 v{{[0-9]+}},
; SI-DAG: v_cvt_f32_f16_e32 v{{[0-9]+}},
; SI-DAG: v_cvt_f32_f16_e32 v{{[0-9]+}},
; SI: v_add_f32
; GCN: s_endpgm
define void @fadd_f16(half addrspace(1)* %out, half %a, half %b) #0 {
   %add = fadd half %a, %b
   store half %add, half addrspace(1)* %out, align 4
   ret void
}

; GCN-LABEL: {{^}}fadd_v2f16:
; SI: v_add_f32
; SI: v_add_f32
; GCN: s_endpgm
define void @fadd_v2f16(<2 x half> addrspace(1)* %out, <2 x half> %a, <2 x half> %b) #0 {
  %add = fadd <2 x half> %a, %b
  store <2 x half> %add, <2 x half> addrspace(1)* %out, align 8
  ret void
}

; GCN-LABEL: {{^}}fadd_v4f16:
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; GCN: s_endpgm
define void @fadd_v4f16(<4 x half> addrspace(1)* %out, <4 x half> addrspace(1)* %in) #0 {
  %b_ptr = getelementptr <4 x half>, <4 x half> addrspace(1)* %in, i32 1
  %a = load <4 x half>, <4 x half> addrspace(1)* %in, align 16
  %b = load <4 x half>, <4 x half> addrspace(1)* %b_ptr, align 16
  %result = fadd <4 x half> %a, %b
  store <4 x half> %result, <4 x half> addrspace(1)* %out, align 16
  ret void
}

; GCN-LABEL: {{^}}fadd_v8f16:
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; SI: v_add_f32
; GCN: s_endpgm
define void @fadd_v8f16(<8 x half> addrspace(1)* %out, <8 x half> %a, <8 x half> %b) #0 {
  %add = fadd <8 x half> %a, %b
  store <8 x half> %add, <8 x half> addrspace(1)* %out, align 32
  ret void
}

; GCN-LABEL: {{^}}fsub_f16:
; GCN: v_subrev_f32_e32
; GCN: s_endpgm
define void @fsub_f16(half addrspace(1)* %out, half addrspace(1)* %in) #0 {
  %b_ptr = getelementptr half, half addrspace(1)* %in, i32 1
  %a = load half, half addrspace(1)* %in
  %b = load half, half addrspace(1)* %b_ptr
  %sub = fsub half %a, %b
  store half %sub, half addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}test_bitcast_from_half:
; GCN: buffer_load_ushort [[TMP:v[0-9]+]]
; GCN: buffer_store_short [[TMP]]
define void @test_bitcast_from_half(half addrspace(1)* %in, i16 addrspace(1)* %out) #0 {
  %val = load half, half addrspace(1)* %in
  %val_int = bitcast half %val to i16
  store i16 %val_int, i16 addrspace(1)* %out
  ret void
}

; GCN-LABEL: {{^}}test_bitcast_to_half:
; GCN: buffer_load_ushort [[TMP:v[0-9]+]]
; GCN: buffer_store_short [[TMP]]
define void @test_bitcast_to_half(half addrspace(1)* %out, i16 addrspace(1)* %in) #0 {
  %val = load i16, i16 addrspace(1)* %in
  %val_fp = bitcast i16 %val to half
  store half %val_fp, half addrspace(1)* %out
  ret void
}

attributes #0 = { nounwind }
