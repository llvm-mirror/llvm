; RUN: llc %s -o %t.o -filetype=obj -relocation-model=pic
; RUN: llvm-as %p/Inputs/pr19901-1.ll -o %t2.o
; RUN: %gold -plugin %llvmshlibdir/LLVMgold.so \
; RUN:     -shared -m elf_x86_64 -o %t.so %t2.o %t.o
; RUN: llvm-readobj -t %t.so | FileCheck %s

; CHECK:       Symbol {
; CHECK:         Name: f
; CHECK-NEXT:    Value:
; CHECK-NEXT:    Size:
; CHECK-NEXT:    Binding: Local
; CHECK-NEXT:    Type: Function
; CHECK-NEXT:    Other: {{2|0}}
; CHECK-NEXT:    Section: .text
; CHECK-NEXT:  }

target triple = "x86_64-unknown-linux-gnu"
define i32 @g() {
  call void @f()
  ret i32 0
}
define linkonce_odr hidden void @f() {
  ret void
}
