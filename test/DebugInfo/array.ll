; RUN: llc -O0 < %s | FileCheck %s
; Do not emit AT_upper_bound for an unbounded array.
; radar 9241695
define i32 @main() nounwind ssp {
entry:
  %retval = alloca i32, align 4
  %a = alloca [0 x i32], align 4
  store i32 0, i32* %retval
  call void @llvm.dbg.declare(metadata !{[0 x i32]* %a}, metadata !6), !dbg !11
  ret i32 0, !dbg !12
}

declare void @llvm.dbg.declare(metadata, metadata) nounwind readnone

!llvm.dbg.cu = !{!2}

!0 = metadata !{i32 786478, metadata !1, metadata !"main", metadata !"main", metadata !"", metadata !1, i32 3, metadata !3, i1 false, i1 true, i32 0, i32 0, i32 0, i32 0, i1 false, i32 ()* @main, null, null, null, i32 3} ; [ DW_TAG_subprogram ]
!1 = metadata !{i32 786473, metadata !14} ; [ DW_TAG_file_type ]
!2 = metadata !{i32 786449, metadata !1, i32 12, metadata !"clang version 3.0 (trunk 129138)", i1 false, metadata !"", i32 0, null, null, metadata !13, null,  null, null} ; [ DW_TAG_compile_unit ]
!3 = metadata !{i32 786453, metadata !1, metadata !"", metadata !1, i32 0, i64 0, i64 0, i32 0, i32 0, i32 0, metadata !4, i32 0, i32 0} ; [ DW_TAG_subroutine_type ]
!4 = metadata !{metadata !5}
!5 = metadata !{i32 786468, metadata !2, metadata !"int", null, i32 0, i64 32, i64 32, i64 0, i32 0, i32 5} ; [ DW_TAG_base_type ]
!6 = metadata !{i32 786688, metadata !7, metadata !"a", metadata !1, i32 4, metadata !8, i32 0, null} ; [ DW_TAG_auto_variable ]
!7 = metadata !{i32 786443, metadata !1, metadata !0, i32 3, i32 12, i32 0} ; [ DW_TAG_lexical_block ]
!8 = metadata !{i32 786433, metadata !2, metadata !"", metadata !2, i32 0, i64 0, i64 32, i32 0, i32 0, metadata !5, metadata !9, i32 0, i32 0} ; [ DW_TAG_array_type ]
!9 = metadata !{metadata !10}
;CHECK: DW_TAG_subrange_type
;CHECK-NEXT: DW_AT_type
;CHECK-NOT: DW_AT_lower_bound
;CHECK-NOT: DW_AT_upper_bound
;CHECK-NEXT: End Of Children Mark
!10 = metadata !{i32 786465, i64 0, i64 -1}        ; [ DW_TAG_subrange_type ]
!11 = metadata !{i32 4, i32 7, metadata !7, null}
!12 = metadata !{i32 5, i32 3, metadata !7, null}
!13 = metadata !{metadata !0}
!14 = metadata !{metadata !"array.c", metadata !"/private/tmp"}
