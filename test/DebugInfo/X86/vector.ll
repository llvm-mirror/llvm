; RUN: llc -mtriple=x86_64-linux-gnu -O0 -filetype=obj -o %t %s
; RUN: llvm-dwarfdump -debug-dump=info %t | FileCheck %s

; Generated from:
; clang -g -S -emit-llvm -o foo.ll foo.c
; typedef int v4si __attribute__((__vector_size__(16)));
;
; v4si a

@a = common global <4 x i32> zeroinitializer, align 16

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!13}

!0 = !{!"0x11\0012\00clang version 3.3 (trunk 171825) (llvm/trunk 171822)\000\00\000\00\000", !12, !1, !1, !1, !3,  !1} ; [ DW_TAG_compile_unit ] [/Users/echristo/foo.c] [DW_LANG_C99]
!1 = !{}
!3 = !{!5}
!5 = !{!"0x34\00a\00a\00\003\000\001", null, !6, !7, <4 x i32>* @a, null} ; [ DW_TAG_variable ] [a] [line 3] [def]
!6 = !{!"0x29", !12} ; [ DW_TAG_file_type ]
!7 = !{!"0x16\00v4si\001\000\000\000\000", !12, null, !8} ; [ DW_TAG_typedef ] [v4si] [line 1, size 0, align 0, offset 0] [from ]
!8 = !{!"0x1\00\000\00128\00128\000\002048", null, null, !9, !10, i32 0, null, null, null} ; [ DW_TAG_array_type ] [line 0, size 128, align 128, offset 0] [vector] [from int]
!9 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!10 = !{!11}
!11 = !{!"0x21\000\004"}        ; [ DW_TAG_subrange_type ] [0, 3]
!12 = !{!"foo.c", !"/Users/echristo"}

; Check that we get an array type with a vector attribute.
; CHECK: DW_TAG_array_type
; CHECK-NEXT: DW_AT_GNU_vector
!13 = !{i32 1, !"Debug Info Version", i32 2}
