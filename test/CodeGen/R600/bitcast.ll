; RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s
; RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck -check-prefix=SI -check-prefix=FUNC %s

; This test just checks that the compiler doesn't crash.

declare void @llvm.SI.export(i32, i32, i32, i32, i32, float, float, float, float)

; FUNC-LABEL: {{^}}v32i8_to_v8i32:
; SI: s_endpgm
define void @v32i8_to_v8i32(<32 x i8> addrspace(2)* inreg) #0 {
entry:
  %1 = load <32 x i8> addrspace(2)* %0
  %2 = bitcast <32 x i8> %1 to <8 x i32>
  %3 = extractelement <8 x i32> %2, i32 1
  %4 = icmp ne i32 %3, 0
  %5 = select i1 %4, float 0.0, float 1.0
  call void @llvm.SI.export(i32 15, i32 1, i32 1, i32 0, i32 1, float %5, float %5, float %5, float %5)
  ret void
}

; FUNC-LABEL: {{^}}i8ptr_v16i8ptr:
; SI: s_endpgm
define void @i8ptr_v16i8ptr(<16 x i8> addrspace(1)* %out, i8 addrspace(1)* %in) {
entry:
  %0 = bitcast i8 addrspace(1)* %in to <16 x i8> addrspace(1)*
  %1 = load <16 x i8> addrspace(1)* %0
  store <16 x i8> %1, <16 x i8> addrspace(1)* %out
  ret void
}

define void @f32_to_v2i16(<2 x i16> addrspace(1)* %out, float addrspace(1)* %in) nounwind {
  %load = load float addrspace(1)* %in, align 4
  %bc = bitcast float %load to <2 x i16>
  store <2 x i16> %bc, <2 x i16> addrspace(1)* %out, align 4
  ret void
}

define void @v2i16_to_f32(float addrspace(1)* %out, <2 x i16> addrspace(1)* %in) nounwind {
  %load = load <2 x i16> addrspace(1)* %in, align 4
  %bc = bitcast <2 x i16> %load to float
  store float %bc, float addrspace(1)* %out, align 4
  ret void
}

define void @v4i8_to_i32(i32 addrspace(1)* %out, <4 x i8> addrspace(1)* %in) nounwind {
  %load = load <4 x i8> addrspace(1)* %in, align 4
  %bc = bitcast <4 x i8> %load to i32
  store i32 %bc, i32 addrspace(1)* %out, align 4
  ret void
}

define void @i32_to_v4i8(<4 x i8> addrspace(1)* %out, i32 addrspace(1)* %in) nounwind {
  %load = load i32 addrspace(1)* %in, align 4
  %bc = bitcast i32 %load to <4 x i8>
  store <4 x i8> %bc, <4 x i8> addrspace(1)* %out, align 4
  ret void
}

; FUNC-LABEL: {{^}}bitcast_v2i32_to_f64:
; SI: s_endpgm
define void @bitcast_v2i32_to_f64(double addrspace(1)* %out, <2 x i32> addrspace(1)* %in) {
  %val = load <2 x i32> addrspace(1)* %in, align 8
  %add = add <2 x i32> %val, <i32 4, i32 9>
  %bc = bitcast <2 x i32> %add to double
  store double %bc, double addrspace(1)* %out, align 8
  ret void
}

; FUNC-LABEL: {{^}}bitcast_f64_to_v2i32:
; SI: s_endpgm
define void @bitcast_f64_to_v2i32(<2 x i32> addrspace(1)* %out, double addrspace(1)* %in) {
  %val = load double addrspace(1)* %in, align 8
  %add = fadd double %val, 4.0
  %bc = bitcast double %add to <2 x i32>
  store <2 x i32> %bc, <2 x i32> addrspace(1)* %out, align 8
  ret void
}

attributes #0 = { "ShaderType"="0" }
