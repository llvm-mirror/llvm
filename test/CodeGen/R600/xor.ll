;RUN: llc < %s -march=r600 -mcpu=redwood | FileCheck --check-prefix=EG-CHECK %s
;RUN: llc < %s -march=r600 -mcpu=verde | FileCheck --check-prefix=SI-CHECK %s

;EG-CHECK: @xor_v2i32
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

;SI-CHECK: @xor_v2i32
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}


define void @xor_v2i32(<2 x i32> addrspace(1)* %out, <2 x i32> addrspace(1)* %in0, <2 x i32> addrspace(1)* %in1) {
  %a = load <2 x i32> addrspace(1) * %in0
  %b = load <2 x i32> addrspace(1) * %in1
  %result = xor <2 x i32> %a, %b
  store <2 x i32> %result, <2 x i32> addrspace(1)* %out
  ret void
}

;EG-CHECK: @xor_v4i32
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}
;EG-CHECK: XOR_INT {{\*? *}}T{{[0-9]+\.[XYZW], T[0-9]+\.[XYZW], T[0-9]+\.[XYZW]}}

;SI-CHECK: @xor_v4i32
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}
;SI-CHECK: V_XOR_B32_e32 VGPR{{[0-9]+, VGPR[0-9]+, VGPR[0-9]+}}

define void @xor_v4i32(<4 x i32> addrspace(1)* %out, <4 x i32> addrspace(1)* %in0, <4 x i32> addrspace(1)* %in1) {
  %a = load <4 x i32> addrspace(1) * %in0
  %b = load <4 x i32> addrspace(1) * %in1
  %result = xor <4 x i32> %a, %b
  store <4 x i32> %result, <4 x i32> addrspace(1)* %out
  ret void
}
