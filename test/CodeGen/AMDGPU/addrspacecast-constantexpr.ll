; RUN: opt -mtriple=amdgcn-unknown-amdhsa -S -amdgpu-annotate-kernel-features < %s | FileCheck -check-prefix=HSA %s
; RUN: llc -mtriple=amdgcn-unknown-amdhsa < %s | FileCheck -check-prefix=FUNC -check-prefix=GCN %s
; RUN: llc -mtriple=amdgcn-unknown-amdhsa -mcpu=fiji < %s | FileCheck -check-prefix=FUNC -check-prefix=GCN %s

declare void @llvm.memcpy.p1i32.p4i32.i32(i32 addrspace(1)* nocapture, i32 addrspace(4)* nocapture, i32, i32, i1) #0

@lds.i32 = unnamed_addr addrspace(3) global i32 undef, align 4
@lds.arr = unnamed_addr addrspace(3) global [256 x i32] undef, align 4

@global.i32 = unnamed_addr addrspace(1) global i32 undef, align 4
@global.arr = unnamed_addr addrspace(1) global [256 x i32] undef, align 4

; HSA: @store_cast_0_flat_to_group_addrspacecast() #1
define void @store_cast_0_flat_to_group_addrspacecast() #1 {
  store i32 7, i32 addrspace(3)* addrspacecast (i32 addrspace(4)* null to i32 addrspace(3)*)
  ret void
}

; HSA: @store_cast_0_group_to_flat_addrspacecast() #2
define void @store_cast_0_group_to_flat_addrspacecast() #1 {
  store i32 7, i32 addrspace(4)* addrspacecast (i32 addrspace(3)* null to i32 addrspace(4)*)
  ret void
}

; FIXME: Offsets for group segment aperture are different for SI/VI: 0x10/0x40?

; HSA: define void @store_constant_cast_group_gv_to_flat() #2

; FUNC-LABEL: {{^}}store_constant_cast_group_gv_to_flat
; GCN: s_mov_b32 m0, -1
; GCN: s_load_dword s[[SGPR_GS_AP:[0-9]+]]
; GCN: v_mov_b32_e32 v[[OFFSET:[0-9]+]], 0
; GCN: v_mov_b32_e32 v[[VALUE:[0-9]+]], 7
; GCN: s_waitcnt lgkmcnt(0)
; GCN: v_mov_b32_e32 v[[VGPR_GS_AP:[0-9]+]], s[[SGPR_GS_AP]]
; GCN: flat_store_dword v{{\[}}[[OFFSET]]:[[VGPR_GS_AP]]{{\]}}, v[[VALUE]]
define void @store_constant_cast_group_gv_to_flat() #1 {
  store volatile i32 7, i32 addrspace(4)* addrspacecast (i32 addrspace(3)* @lds.i32 to i32 addrspace(4)*)
  ret void
}

; HSA: @store_constant_cast_group_gv_gep_to_flat() #2

; FUNC-LABEL: {{^}}store_constant_cast_group_gv_gep_to_flat
; GCN: s_mov_b32 m0, -1
define void @store_constant_cast_group_gv_gep_to_flat() #1 {
  store i32 7, i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8)
  ret void
}

; HSA: @store_constant_cast_global_gv_to_flat() #1
define void @store_constant_cast_global_gv_to_flat() #1 {
  store i32 7, i32 addrspace(4)* addrspacecast (i32 addrspace(1)* @global.i32 to i32 addrspace(4)*)
  ret void
}

; HSA: @store_constant_cast_global_gv_gep_to_flat() #1
define void @store_constant_cast_global_gv_gep_to_flat() #1 {
  store i32 7, i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(1)* @global.arr to [256 x i32] addrspace(4)*), i64 0, i64 8)
  ret void
}

; HSA: @load_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #2

; FUNC-LABEL: {{^}}load_constant_cast_group_gv_gep_to_flat
; GCN: s_mov_b32 m0, -1
define void @load_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #1 {
  %val = load i32, i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8)
  store i32 %val, i32 addrspace(1)* %out
  ret void
}

; HSA: @atomicrmw_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #2
define void @atomicrmw_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #1 {
  %val = atomicrmw add i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8), i32 1 seq_cst
  store i32 %val, i32 addrspace(1)* %out
  ret void
}

; HSA: @cmpxchg_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #2
define void @cmpxchg_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #1 {
  %val = cmpxchg i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8), i32 0, i32 1 seq_cst seq_cst
  %val0 = extractvalue { i32, i1 } %val, 0
  store i32 %val0, i32 addrspace(1)* %out
  ret void
}

; HSA: @memcpy_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #2
define void @memcpy_constant_cast_group_gv_gep_to_flat(i32 addrspace(1)* %out) #1 {
  call void @llvm.memcpy.p1i32.p4i32.i32(i32 addrspace(1)* %out, i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8), i32 32, i32 4, i1 false)
  ret void
}

; Can't just search the pointer value
; HSA: @store_value_constant_cast_lds_gv_gep_to_flat(i32 addrspace(4)* addrspace(1)* %out) #2
define void @store_value_constant_cast_lds_gv_gep_to_flat(i32 addrspace(4)* addrspace(1)* %out) #1 {
  store i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8), i32 addrspace(4)* addrspace(1)* %out
  ret void
}

; Can't just search pointer types
; HSA: @store_ptrtoint_value_constant_cast_lds_gv_gep_to_flat(i64 addrspace(1)* %out) #2
define void @store_ptrtoint_value_constant_cast_lds_gv_gep_to_flat(i64 addrspace(1)* %out) #1 {
  store i64 ptrtoint (i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8) to i64), i64 addrspace(1)* %out
  ret void
}

; Cast group to flat, do GEP, cast back to group
; HSA: @store_constant_cast_group_gv_gep_to_flat_to_group() #2
define void @store_constant_cast_group_gv_gep_to_flat_to_group() #1 {
  store i32 7, i32 addrspace(3)* addrspacecast (i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8) to i32 addrspace(3)*)
  ret void
}

; HSA: @ret_constant_cast_group_gv_gep_to_flat_to_group() #2
define i32 addrspace(3)* @ret_constant_cast_group_gv_gep_to_flat_to_group() #1 {
  ret i32 addrspace(3)* addrspacecast (i32 addrspace(4)* getelementptr ([256 x i32], [256 x i32] addrspace(4)* addrspacecast ([256 x i32] addrspace(3)* @lds.arr to [256 x i32] addrspace(4)*), i64 0, i64 8) to i32 addrspace(3)*)
}

; HSA: attributes #0 = { argmemonly nounwind }
; HSA: attributes #1 = { nounwind }
; HSA: attributes #2 = { nounwind "amdgpu-queue-ptr" }

attributes #0 = { argmemonly nounwind }
attributes #1 = { nounwind }
