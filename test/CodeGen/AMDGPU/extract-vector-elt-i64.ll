; RUN: llc -march=amdgcn -verify-machineinstrs < %s | FileCheck -check-prefix=GCN %s

; How the replacement of i64 stores with v2i32 stores resulted in
; breaking other users of the bitcast if they already existed

; GCN-LABEL: {{^}}extract_vector_elt_select_error:
; GCN: buffer_store_dword
; GCN: buffer_store_dword
; GCN: buffer_store_dwordx2
define void @extract_vector_elt_select_error(i32 addrspace(1)* %out, i64 addrspace(1)* %in, i64 %val) nounwind {
  %vec = bitcast i64 %val to <2 x i32>
  %elt0 = extractelement <2 x i32> %vec, i32 0
  %elt1 = extractelement <2 x i32> %vec, i32 1

  store volatile i32 %elt0, i32 addrspace(1)* %out
  store volatile i32 %elt1, i32 addrspace(1)* %out
  store volatile i64 %val, i64 addrspace(1)* %in
  ret void
}


define void @extract_vector_elt_v2i64(i64 addrspace(1)* %out, <2 x i64> %foo) nounwind {
  %p0 = extractelement <2 x i64> %foo, i32 0
  %p1 = extractelement <2 x i64> %foo, i32 1
  %out1 = getelementptr i64, i64 addrspace(1)* %out, i32 1
  store volatile i64 %p1, i64 addrspace(1)* %out
  store volatile i64 %p0, i64 addrspace(1)* %out1
  ret void
}

define void @dyn_extract_vector_elt_v2i64(i64 addrspace(1)* %out, <2 x i64> %foo, i32 %elt) nounwind {
  %dynelt = extractelement <2 x i64> %foo, i32 %elt
  store volatile i64 %dynelt, i64 addrspace(1)* %out
  ret void
}

define void @dyn_extract_vector_elt_v2i64_2(i64 addrspace(1)* %out, <2 x i64> addrspace(1)* %foo, i32 %elt, <2 x i64> %arst) nounwind {
  %load = load volatile <2 x i64>, <2 x i64> addrspace(1)* %foo
  %or = or <2 x i64> %load, %arst
  %dynelt = extractelement <2 x i64> %or, i32 %elt
  store volatile i64 %dynelt, i64 addrspace(1)* %out
  ret void
}
