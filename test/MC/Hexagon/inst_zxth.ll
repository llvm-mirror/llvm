;; RUN: llc -mtriple=hexagon-unknown-elf -filetype=obj %s -o - \
;; RUN: | llvm-objdump -s - | FileCheck %s

define i32 @foo (i16 %a)
{
  %1 = zext i16 %a to i32
  ret i32 %1
}

; CHECK:   0000 0040c070 00c09f52
