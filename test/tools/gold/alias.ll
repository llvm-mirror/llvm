; RUN: llvm-as %s -o %t.o
; RUN: llvm-as %p/Inputs/alias-1.ll -o %t2.o
; RUN: ld -shared -o %t3.o -plugin %llvmshlibdir/LLVMgold.so %t2.o %t.o \
; RUN:  -plugin-opt=emit-llvm
; RUN: llvm-dis < %t3.o -o - | FileCheck %s

; CHECK-NOT: alias
; CHECK: @a = global i32 42
; CHECK-NEXT: @b = global i32 1
; CHECK-NOT: alias

@a = weak alias i32* @b
@b = global i32 1
