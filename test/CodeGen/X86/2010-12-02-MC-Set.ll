; RUN: llc < %s -disable-dot-loc -mtriple=x86_64-apple-darwin -O0 | FileCheck %s


define void @foo() nounwind ssp {
entry:
  ret void, !dbg !5
}

!llvm.dbg.cu = !{!2}
!7 = metadata !{metadata !0}

!0 = metadata !{i32 786478, metadata !1, metadata !"foo", metadata !"foo", metadata !"", metadata !1, i32 3, metadata !3, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, void ()* @foo, null, null, null, i32 0} ; [ DW_TAG_subprogram ]
!1 = metadata !{i32 786473, metadata !"e.c", metadata !"/private/tmp", metadata !2} ; [ DW_TAG_file_type ]
!2 = metadata !{i32 786449, i32 12, metadata !1, metadata !"clang version 2.9 (trunk 120563)", i1 false, metadata !"", i32 0, null, null, metadata !7, null, metadata !""} ; [ DW_TAG_compile_unit ]
!3 = metadata !{i32 786453, metadata !1, metadata !"", metadata !1, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !4, i32 0, null} ; [ DW_TAG_subroutine_type ]
!4 = metadata !{null}
!5 = metadata !{i32 5, i32 1, metadata !6, null}
!6 = metadata !{i32 786443, metadata !0, i32 3, i32 16, metadata !1, i32 0} ; [ DW_TAG_lexical_block ]

; CHECK: .subsections_via_symbols
; CHECK-NEXT: __debug_line
; CHECK-NEXT: Lline_table_start0
; CHECK-NEXT: Ltmp{{[0-9]}} = (Ltmp
