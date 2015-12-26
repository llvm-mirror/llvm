; RUN: llc -march=amdgcn -mcpu=fiji -show-mc-encoding < %s | FileCheck -check-prefix=VI %s

declare void @llvm.amdgcn.s.dcache.wb() #0

; VI-LABEL: {{^}}test_s_dcache_wb:
; VI-NEXT: ; BB#0:
; VI-NEXT: s_dcache_wb ; encoding: [0x00,0x00,0x84,0xc0,0x00,0x00,0x00,0x00]
; VI-NEXT: s_endpgm
define void @test_s_dcache_wb() #0 {
  call void @llvm.amdgcn.s.dcache.wb()
  ret void
}

; VI-LABEL: {{^}}test_s_dcache_wb_insert_wait:
; VI-NEXT: ; BB#0:
; VI-NEXT: s_dcache_wb
; VI-NEXT: s_waitcnt lgkmcnt(0) ; encoding
define void @test_s_dcache_wb_insert_wait() #0 {
  call void @llvm.amdgcn.s.dcache.wb()
  br label %end

end:
  store volatile i32 3, i32 addrspace(1)* undef
  ret void
}

attributes #0 = { nounwind }
