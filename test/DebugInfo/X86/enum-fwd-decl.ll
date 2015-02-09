; RUN: llc -O0 -mtriple=x86_64-apple-darwin %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-dump=info %t | FileCheck %s

@e = global i16 0, align 2

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!9}

!0 = !{!"0x11\004\00clang version 3.2 (trunk 165274) (llvm/trunk 165272)\000\00\000\00\000", !8, !1, !1, !1, !3,  !1} ; [ DW_TAG_compile_unit ] [/tmp/foo.cpp] [DW_LANG_C_plus_plus]
!1 = !{}
!3 = !{!5}
!5 = !{!"0x34\00e\00e\00\002\000\001", null, !6, !7, i16* @e, null} ; [ DW_TAG_variable ] [e] [line 2] [def]
!6 = !{!"0x29", !8} ; [ DW_TAG_file_type ]
!7 = !{!"0x4\00E\001\0016\0016\000\004\000", !8, null, null, null, null, null, null} ; [ DW_TAG_enumeration_type ] [E] [line 1, size 16, align 16, offset 0] [decl] [from ]
!8 = !{!"foo.cpp", !"/tmp"}

; CHECK: DW_TAG_enumeration_type
; CHECK-NEXT: DW_AT_name
; CHECK-NEXT: DW_AT_byte_size
; CHECK-NEXT: DW_AT_declaration
!9 = !{i32 1, !"Debug Info Version", i32 2}
