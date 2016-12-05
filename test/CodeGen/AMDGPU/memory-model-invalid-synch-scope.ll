; RUN: not llc -mtriple=amdgcn-- -mcpu=fiji -verify-machineinstrs < %s 2>&1 | FileCheck %s

; CHECK: error: <unknown>:0:0: in function invalid_fence void (): Unknown synchronization scope
define void @invalid_fence() {
  fence syncscope(6) seq_cst
  ret void
}

; CHECK: error: <unknown>:0:0: in function invalid_load void (i32 addrspace(4)*, i32 addrspace(4)*): Unknown synchronization scope
define void @invalid_load(i32 addrspace(4)* %in, i32 addrspace(4)* %out) {
  %val = load atomic i32, i32 addrspace(4)* %in syncscope(6) seq_cst, align 4
  store i32 %val, i32 addrspace(4)* %out
  ret void
}

; CHECK: error: <unknown>:0:0: in function invalid_store void (i32, i32 addrspace(4)*): Unknown synchronization scope
define void @invalid_store(i32 %in, i32 addrspace(4)* %out) {
  store atomic i32 %in, i32 addrspace(4)* %out syncscope(6) seq_cst, align 4
  ret void
}

; CHECK: error: <unknown>:0:0: in function invalid_cmpxchg void (i32 addrspace(4)*, i32, i32): Unknown synchronization scope
define void @invalid_cmpxchg(i32 addrspace(4)* %out, i32 %in, i32 %old) {
  %gep = getelementptr i32, i32 addrspace(4)* %out, i32 4
  %val = cmpxchg volatile i32 addrspace(4)* %gep, i32 %old, i32 %in syncscope(6) seq_cst seq_cst
  ret void
}

; CHECK: error: <unknown>:0:0: in function invalid_rmw void (i32 addrspace(4)*, i32): Unknown synchronization scope
define void @invalid_rmw(i32 addrspace(4)* %out, i32 %in) {
  %val = atomicrmw volatile xchg i32 addrspace(4)* %out, i32 %in syncscope(6) seq_cst
  ret void
}
