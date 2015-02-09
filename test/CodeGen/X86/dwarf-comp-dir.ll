; RUN: llc %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-dump=line %t | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!5}

!0 = !{!"0x11\0012\00clang version 3.1 (trunk 143523)\001\00\000\00\000", !4, !2, !7, !2, !2, null} ; [ DW_TAG_compile_unit ]
!2 = !{}
!3 = !{!"0x29", !4} ; [ DW_TAG_file_type ]
!4 = !{!"empty.c", !"/home/nlewycky"}
!6 = !{!"0x13\00foo\001\008\008\000\000\000", !4, null, null, !2, null, null, !"_ZTS3foo"} ; [ DW_TAG_structure_type ] [foo] [line 1, size 8, align 8, offset 0] [def] [from ]
!7 = !{!6}

; The important part of the following check is that dir = #0.
;                        Dir  Mod Time   File Len   File Name
;                        ---- ---------- ---------- ---------------------------
; CHECK: file_names[  1]    0 0x00000000 0x00000000 empty.c
!5 = !{i32 1, !"Debug Info Version", i32 2}
