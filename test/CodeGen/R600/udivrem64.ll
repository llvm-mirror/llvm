;RUN: llc -march=amdgcn -mcpu=SI -verify-machineinstrs < %s | FileCheck --check-prefix=SI --check-prefix=FUNC %s
;RUN: llc -march=amdgcn -mcpu=tonga -verify-machineinstrs < %s | FileCheck --check-prefix=SI --check-prefix=FUNC %s
;RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck --check-prefix=EG --check-prefix=FUNC %s

;FUNC-LABEL: {{^}}test_udiv:
;EG: RECIP_UINT
;EG: LSHL {{.*}}, 1,
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT

;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI-NOT: v_mad_f32
;SI-NOT: v_lshr_64
;SI: s_endpgm
define void @test_udiv(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %result = udiv i64 %x, %y
  store i64 %result, i64 addrspace(1)* %out
  ret void
}

;FUNC-LABEL: {{^}}test_urem:
;EG: RECIP_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: BFE_UINT
;EG: AND_INT {{.*}}, 1,

;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI: s_bfe_u32
;SI-NOT: v_mad_f32
;SI-NOT: v_lshr_64
;SI: s_endpgm
define void @test_urem(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %result = urem i64 %x, %y
  store i64 %result, i64 addrspace(1)* %out
  ret void
}

;FUNC-LABEL: {{^}}test_udiv3264:
;EG: RECIP_UINT
;EG-NOT: BFE_UINT

;SI-NOT: s_bfe_u32
;SI-NOT: v_mad_f32
;SI-NOT: v_lshr_64
;SI: s_endpgm
define void @test_udiv3264(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %1 = lshr i64 %x, 33
  %2 = lshr i64 %y, 33
  %result = udiv i64 %1, %2
  store i64 %result, i64 addrspace(1)* %out
  ret void
}

;FUNC-LABEL: {{^}}test_urem3264:
;EG: RECIP_UINT
;EG-NOT: BFE_UINT

;SI-NOT: s_bfe_u32
;SI-NOT: v_mad_f32
;SI-NOT: v_lshr_64
;SI: s_endpgm
define void @test_urem3264(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %1 = lshr i64 %x, 33
  %2 = lshr i64 %y, 33
  %result = urem i64 %1, %2
  store i64 %result, i64 addrspace(1)* %out
  ret void
}

;FUNC-LABEL: {{^}}test_udiv2464:
;EG: UINT_TO_FLT
;EG: UINT_TO_FLT
;EG: FLT_TO_UINT
;EG-NOT: RECIP_UINT
;EG-NOT: BFE_UINT

;SI-NOT: v_lshr_64
;SI: v_mad_f32
;SI: s_endpgm
define void @test_udiv2464(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %1 = lshr i64 %x, 40
  %2 = lshr i64 %y, 40
  %result = udiv i64 %1, %2
  store i64 %result, i64 addrspace(1)* %out
  ret void
}

;FUNC-LABEL: {{^}}test_urem2464:
;EG: UINT_TO_FLT
;EG: UINT_TO_FLT
;EG: FLT_TO_UINT
;EG-NOT: RECIP_UINT
;EG-NOT: BFE_UINT

;SI-NOT: v_lshr_64
;SI: v_mad_f32
;SI: s_endpgm
define void @test_urem2464(i64 addrspace(1)* %out, i64 %x, i64 %y) {
  %1 = lshr i64 %x, 40
  %2 = lshr i64 %y, 40
  %result = urem i64 %1, %2
  store i64 %result, i64 addrspace(1)* %out
  ret void
}
