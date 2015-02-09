; RUN: llc -march=r600 -mcpu=redwood < %s | FileCheck %s -check-prefix=R600 -check-prefix=FUNC
; RUN: llc -show-mc-encoding -mattr=+promote-alloca -verify-machineinstrs -march=amdgcn -mcpu=SI < %s | FileCheck %s -check-prefix=SI-PROMOTE -check-prefix=SI -check-prefix=FUNC
; RUN: llc -show-mc-encoding -mattr=-promote-alloca -verify-machineinstrs -march=amdgcn -mcpu=SI < %s | FileCheck %s -check-prefix=SI-ALLOCA -check-prefix=SI -check-prefix=FUNC

declare i32 @llvm.r600.read.tidig.x() nounwind readnone

; FUNC-LABEL: {{^}}mova_same_clause:

; R600: LDS_WRITE
; R600: LDS_WRITE
; R600: LDS_READ
; R600: LDS_READ

; SI-PROMOTE: ds_write_b32
; SI-PROMOTE: ds_write_b32
; SI-PROMOTE: ds_read_b32
; SI-PROMOTE: ds_read_b32

; SI-ALLOCA: buffer_store_dword v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen ; encoding: [0x00,0x10,0x70,0xe0
; SI-ALLOCA: buffer_store_dword v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen ; encoding: [0x00,0x10,0x70,0xe0
define void @mova_same_clause(i32 addrspace(1)* nocapture %out, i32 addrspace(1)* nocapture %in) {
entry:
  %stack = alloca [5 x i32], align 4
  %0 = load i32 addrspace(1)* %in, align 4
  %arrayidx1 = getelementptr inbounds [5 x i32]* %stack, i32 0, i32 %0
  store i32 4, i32* %arrayidx1, align 4
  %arrayidx2 = getelementptr inbounds i32 addrspace(1)* %in, i32 1
  %1 = load i32 addrspace(1)* %arrayidx2, align 4
  %arrayidx3 = getelementptr inbounds [5 x i32]* %stack, i32 0, i32 %1
  store i32 5, i32* %arrayidx3, align 4
  %arrayidx10 = getelementptr inbounds [5 x i32]* %stack, i32 0, i32 0
  %2 = load i32* %arrayidx10, align 4
  store i32 %2, i32 addrspace(1)* %out, align 4
  %arrayidx12 = getelementptr inbounds [5 x i32]* %stack, i32 0, i32 1
  %3 = load i32* %arrayidx12
  %arrayidx13 = getelementptr inbounds i32 addrspace(1)* %out, i32 1
  store i32 %3, i32 addrspace(1)* %arrayidx13
  ret void
}

; This test checks that the stack offset is calculated correctly for structs.
; All register loads/stores should be optimized away, so there shouldn't be
; any MOVA instructions.
;
; XXX: This generated code has unnecessary MOVs, we should be able to optimize
; this.

; FUNC-LABEL: {{^}}multiple_structs:
; R600-NOT: MOVA_INT
; SI-NOT: v_movrel
; SI-NOT: v_movrel
%struct.point = type { i32, i32 }

define void @multiple_structs(i32 addrspace(1)* %out) {
entry:
  %a = alloca %struct.point
  %b = alloca %struct.point
  %a.x.ptr = getelementptr %struct.point* %a, i32 0, i32 0
  %a.y.ptr = getelementptr %struct.point* %a, i32 0, i32 1
  %b.x.ptr = getelementptr %struct.point* %b, i32 0, i32 0
  %b.y.ptr = getelementptr %struct.point* %b, i32 0, i32 1
  store i32 0, i32* %a.x.ptr
  store i32 1, i32* %a.y.ptr
  store i32 2, i32* %b.x.ptr
  store i32 3, i32* %b.y.ptr
  %a.indirect.ptr = getelementptr %struct.point* %a, i32 0, i32 0
  %b.indirect.ptr = getelementptr %struct.point* %b, i32 0, i32 0
  %a.indirect = load i32* %a.indirect.ptr
  %b.indirect = load i32* %b.indirect.ptr
  %0 = add i32 %a.indirect, %b.indirect
  store i32 %0, i32 addrspace(1)* %out
  ret void
}

; Test direct access of a private array inside a loop.  The private array
; loads and stores should be lowered to copies, so there shouldn't be any
; MOVA instructions.

; FUNC-LABEL: {{^}}direct_loop:
; R600-NOT: MOVA_INT
; SI-NOT: v_movrel

define void @direct_loop(i32 addrspace(1)* %out, i32 addrspace(1)* %in) {
entry:
  %prv_array_const = alloca [2 x i32]
  %prv_array = alloca [2 x i32]
  %a = load i32 addrspace(1)* %in
  %b_src_ptr = getelementptr i32 addrspace(1)* %in, i32 1
  %b = load i32 addrspace(1)* %b_src_ptr
  %a_dst_ptr = getelementptr [2 x i32]* %prv_array_const, i32 0, i32 0
  store i32 %a, i32* %a_dst_ptr
  %b_dst_ptr = getelementptr [2 x i32]* %prv_array_const, i32 0, i32 1
  store i32 %b, i32* %b_dst_ptr
  br label %for.body

for.body:
  %inc = phi i32 [0, %entry], [%count, %for.body]
  %x_ptr = getelementptr [2 x i32]* %prv_array_const, i32 0, i32 0
  %x = load i32* %x_ptr
  %y_ptr = getelementptr [2 x i32]* %prv_array, i32 0, i32 0
  %y = load i32* %y_ptr
  %xy = add i32 %x, %y
  store i32 %xy, i32* %y_ptr
  %count = add i32 %inc, 1
  %done = icmp eq i32 %count, 4095
  br i1 %done, label %for.end, label %for.body

for.end:
  %value_ptr = getelementptr [2 x i32]* %prv_array, i32 0, i32 0
  %value = load i32* %value_ptr
  store i32 %value, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}short_array:

; R600: MOVA_INT

; SI-PROMOTE-DAG: buffer_store_short v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen ; encoding: [0x00,0x10,0x68,0xe0
; SI-PROMOTE-DAG: buffer_store_short v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen offset:2 ; encoding: [0x02,0x10,0x68,0xe0
; SI-PROMOTE: buffer_load_sshort v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}}
define void @short_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %0 = alloca [2 x i16]
  %1 = getelementptr [2 x i16]* %0, i32 0, i32 0
  %2 = getelementptr [2 x i16]* %0, i32 0, i32 1
  store i16 0, i16* %1
  store i16 1, i16* %2
  %3 = getelementptr [2 x i16]* %0, i32 0, i32 %index
  %4 = load i16* %3
  %5 = sext i16 %4 to i32
  store i32 %5, i32 addrspace(1)* %out
  ret void
}

; FUNC-LABEL: {{^}}char_array:

; R600: MOVA_INT

; SI-DAG: buffer_store_byte v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen ; encoding: [0x00,0x10,0x60,0xe0
; SI-DAG: buffer_store_byte v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen offset:1 ; encoding: [0x01,0x10,0x60,0xe0
define void @char_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %0 = alloca [2 x i8]
  %1 = getelementptr [2 x i8]* %0, i32 0, i32 0
  %2 = getelementptr [2 x i8]* %0, i32 0, i32 1
  store i8 0, i8* %1
  store i8 1, i8* %2
  %3 = getelementptr [2 x i8]* %0, i32 0, i32 %index
  %4 = load i8* %3
  %5 = sext i8 %4 to i32
  store i32 %5, i32 addrspace(1)* %out
  ret void

}

; Make sure we don't overwrite workitem information with private memory

; FUNC-LABEL: {{^}}work_item_info:
; R600-NOT: MOV T0.X
; Additional check in case the move ends up in the last slot
; R600-NOT: MOV * TO.X

; SI-NOT: v_mov_b32_e{{(32|64)}} v0
define void @work_item_info(i32 addrspace(1)* %out, i32 %in) {
entry:
  %0 = alloca [2 x i32]
  %1 = getelementptr [2 x i32]* %0, i32 0, i32 0
  %2 = getelementptr [2 x i32]* %0, i32 0, i32 1
  store i32 0, i32* %1
  store i32 1, i32* %2
  %3 = getelementptr [2 x i32]* %0, i32 0, i32 %in
  %4 = load i32* %3
  %5 = call i32 @llvm.r600.read.tidig.x()
  %6 = add i32 %4, %5
  store i32 %6, i32 addrspace(1)* %out
  ret void
}

; Test that two stack objects are not stored in the same register
; The second stack object should be in T3.X
; FUNC-LABEL: {{^}}no_overlap:
; R600_CHECK: MOV
; R600_CHECK: [[CHAN:[XYZW]]]+
; R600-NOT: [[CHAN]]+
; SI: v_mov_b32_e32 v3
define void @no_overlap(i32 addrspace(1)* %out, i32 %in) {
entry:
  %0 = alloca [3 x i8], align 1
  %1 = alloca [2 x i8], align 1
  %2 = getelementptr [3 x i8]* %0, i32 0, i32 0
  %3 = getelementptr [3 x i8]* %0, i32 0, i32 1
  %4 = getelementptr [3 x i8]* %0, i32 0, i32 2
  %5 = getelementptr [2 x i8]* %1, i32 0, i32 0
  %6 = getelementptr [2 x i8]* %1, i32 0, i32 1
  store i8 0, i8* %2
  store i8 1, i8* %3
  store i8 2, i8* %4
  store i8 1, i8* %5
  store i8 0, i8* %6
  %7 = getelementptr [3 x i8]* %0, i32 0, i32 %in
  %8 = getelementptr [2 x i8]* %1, i32 0, i32 %in
  %9 = load i8* %7
  %10 = load i8* %8
  %11 = add i8 %9, %10
  %12 = sext i8 %11 to i32
  store i32 %12, i32 addrspace(1)* %out
  ret void
}

define void @char_array_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %alloca = alloca [2 x [2 x i8]]
  %gep0 = getelementptr [2 x [2 x i8]]* %alloca, i32 0, i32 0, i32 0
  %gep1 = getelementptr [2 x [2 x i8]]* %alloca, i32 0, i32 0, i32 1
  store i8 0, i8* %gep0
  store i8 1, i8* %gep1
  %gep2 = getelementptr [2 x [2 x i8]]* %alloca, i32 0, i32 0, i32 %index
  %load = load i8* %gep2
  %sext = sext i8 %load to i32
  store i32 %sext, i32 addrspace(1)* %out
  ret void
}

define void @i32_array_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %alloca = alloca [2 x [2 x i32]]
  %gep0 = getelementptr [2 x [2 x i32]]* %alloca, i32 0, i32 0, i32 0
  %gep1 = getelementptr [2 x [2 x i32]]* %alloca, i32 0, i32 0, i32 1
  store i32 0, i32* %gep0
  store i32 1, i32* %gep1
  %gep2 = getelementptr [2 x [2 x i32]]* %alloca, i32 0, i32 0, i32 %index
  %load = load i32* %gep2
  store i32 %load, i32 addrspace(1)* %out
  ret void
}

define void @i64_array_array(i64 addrspace(1)* %out, i32 %index) {
entry:
  %alloca = alloca [2 x [2 x i64]]
  %gep0 = getelementptr [2 x [2 x i64]]* %alloca, i32 0, i32 0, i32 0
  %gep1 = getelementptr [2 x [2 x i64]]* %alloca, i32 0, i32 0, i32 1
  store i64 0, i64* %gep0
  store i64 1, i64* %gep1
  %gep2 = getelementptr [2 x [2 x i64]]* %alloca, i32 0, i32 0, i32 %index
  %load = load i64* %gep2
  store i64 %load, i64 addrspace(1)* %out
  ret void
}

%struct.pair32 = type { i32, i32 }

define void @struct_array_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %alloca = alloca [2 x [2 x %struct.pair32]]
  %gep0 = getelementptr [2 x [2 x %struct.pair32]]* %alloca, i32 0, i32 0, i32 0, i32 1
  %gep1 = getelementptr [2 x [2 x %struct.pair32]]* %alloca, i32 0, i32 0, i32 1, i32 1
  store i32 0, i32* %gep0
  store i32 1, i32* %gep1
  %gep2 = getelementptr [2 x [2 x %struct.pair32]]* %alloca, i32 0, i32 0, i32 %index, i32 0
  %load = load i32* %gep2
  store i32 %load, i32 addrspace(1)* %out
  ret void
}

define void @struct_pair32_array(i32 addrspace(1)* %out, i32 %index) {
entry:
  %alloca = alloca [2 x %struct.pair32]
  %gep0 = getelementptr [2 x %struct.pair32]* %alloca, i32 0, i32 0, i32 1
  %gep1 = getelementptr [2 x %struct.pair32]* %alloca, i32 0, i32 1, i32 0
  store i32 0, i32* %gep0
  store i32 1, i32* %gep1
  %gep2 = getelementptr [2 x %struct.pair32]* %alloca, i32 0, i32 %index, i32 0
  %load = load i32* %gep2
  store i32 %load, i32 addrspace(1)* %out
  ret void
}

define void @select_private(i32 addrspace(1)* %out, i32 %in) nounwind {
entry:
  %tmp = alloca [2 x i32]
  %tmp1 = getelementptr [2 x i32]* %tmp, i32 0, i32 0
  %tmp2 = getelementptr [2 x i32]* %tmp, i32 0, i32 1
  store i32 0, i32* %tmp1
  store i32 1, i32* %tmp2
  %cmp = icmp eq i32 %in, 0
  %sel = select i1 %cmp, i32* %tmp1, i32* %tmp2
  %load = load i32* %sel
  store i32 %load, i32 addrspace(1)* %out
  ret void
}

; AMDGPUPromoteAlloca does not know how to handle ptrtoint.  When it
; finds one, it should stop trying to promote.

; FUNC-LABEL: ptrtoint:
; SI-NOT: ds_write
; SI: buffer_store_dword v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen
; SI: buffer_load_dword v{{[0-9]+}}, v{{[0-9]+}}, s[{{[0-9]+:[0-9]+}}], s{{[0-9]+}} offen offset:5
define void @ptrtoint(i32 addrspace(1)* %out, i32 %a, i32 %b) {
  %alloca = alloca [16 x i32]
  %tmp0 = getelementptr [16 x i32]* %alloca, i32 0, i32 %a
  store i32 5, i32* %tmp0
  %tmp1 = ptrtoint [16 x i32]* %alloca to i32
  %tmp2 = add i32 %tmp1, 5
  %tmp3 = inttoptr i32 %tmp2 to i32*
  %tmp4 = getelementptr i32* %tmp3, i32 %b
  %tmp5 = load i32* %tmp4
  store i32 %tmp5, i32 addrspace(1)* %out
  ret void
}
