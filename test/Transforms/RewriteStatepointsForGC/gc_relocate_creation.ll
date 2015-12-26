; RUN: opt %s -rewrite-statepoints-for-gc -S 2>&1 | FileCheck %s
; This test is to verify gc.relocate can handle pointer to vector of
; pointers (<2 x i32 addrspace(1)*> addrspace(1)* in this case).
; The old scheme to create a gc.relocate of <2 x i32 addrspace(1)*> addrspace(1)*
; type will fail because llvm does not support mangling vector of pointers.
; The new scheme will create all gc.relocate to i8 addrspace(1)* type and
; then bitcast to the correct type.

declare void @foo()
declare void @use(...)
declare token @llvm.experimental.gc.statepoint.p0f_isVoidf(i64, i32, void ()*, i32, i32, ...)

define void @test1(<2 x i32 addrspace(1)*> addrspace(1)* %obj) gc "statepoint-example" {
entry:
  %safepoint_token = call token (i64, i32, void ()*, i32, i32, ...) @llvm.experimental.gc.statepoint.p0f_isVoidf(i64 0, i32 0, void ()* @foo, i32 0, i32 0, i32 0, i32 0)
; CHECK: %obj.relocated = call coldcc i8 addrspace(1)* @llvm.experimental.gc.relocate.p1i8(token %safepoint_token, i32 7, i32 7)
; CHECK-NEXT:  %obj.relocated.casted = bitcast i8 addrspace(1)* %obj.relocated to <2 x i32 addrspace(1)*> addrspace(1)*
  call void (...) @use(<2 x i32 addrspace(1)*> addrspace(1)* %obj)
  ret void
}
