; RUN: llc -mtriple=x86_64-apple-darwin %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-dump=info %t | FileCheck %s

; Check that the friend tag is there and is followed by a DW_AT_friend that has a reference back.

; CHECK: [[BACK:0x[0-9a-f]*]]:   DW_TAG_class_type
; CHECK-NEXT: DW_AT_name [DW_FORM_strp]       ( .debug_str[{{.*}}] = "A")
; CHECK: DW_TAG_friend
; CHECK-NEXT: DW_AT_friend [DW_FORM_ref4]   (cu + 0x{{[0-9a-f]*}} => {[[BACK]]})


%class.A = type { i32 }
%class.B = type { i32 }

@a = global %class.A zeroinitializer, align 4
@b = global %class.B zeroinitializer, align 4

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!29}

!0 = !{!"0x11\004\00clang version 3.1 (trunk 153413) (llvm/trunk 153428)\000\00\000\00\000", !28, !1, !1, !1, !3,  !1} ; [ DW_TAG_compile_unit ]
!1 = !{}
!3 = !{!5, !17}
!5 = !{!"0x34\00a\00a\00\0010\000\001", null, !6, !7, %class.A* @a, null} ; [ DW_TAG_variable ]
!6 = !{!"0x29", !28} ; [ DW_TAG_file_type ]
!7 = !{!"0x2\00A\001\0032\0032\000\000\000", !28, null, null, !8, null, null, null} ; [ DW_TAG_class_type ] [A] [line 1, size 32, align 32, offset 0] [def] [from ]
!8 = !{!9, !11}
!9 = !{!"0xd\00a\002\0032\0032\000\001", !28, !7, !10} ; [ DW_TAG_member ]
!10 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ]
!11 = !{!"0x2e\00A\00A\00\001\000\000\000\006\00320\000\001", !6, !7, !12, null, null, null, i32 0, !15} ; [ DW_TAG_subprogram ]
!12 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !13, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!13 = !{null, !14}
!14 = !{!"0xf\00\000\0064\0064\000\0064", i32 0, null, !7} ; [ DW_TAG_pointer_type ]
!15 = !{!16}
!16 = !{!"0x24"}                      ; [ DW_TAG_base_type ]
!17 = !{!"0x34\00b\00b\00\0011\000\001", null, !6, !18, %class.B* @b, null} ; [ DW_TAG_variable ]
!18 = !{!"0x2\00B\005\0032\0032\000\000\000", !28, null, null, !19, null, null, null} ; [ DW_TAG_class_type ] [B] [line 5, size 32, align 32, offset 0] [def] [from ]
!19 = !{!20, !21, !27}
!20 = !{!"0xd\00b\007\0032\0032\000\001", !28, !18, !10} ; [ DW_TAG_member ]
!21 = !{!"0x2e\00B\00B\00\005\000\000\000\006\00320\000\005", !6, !18, !22, null, null, null, i32 0, !25} ; [ DW_TAG_subprogram ]
!22 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !23, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!23 = !{null, !24}
!24 = !{!"0xf\00\000\0064\0064\000\0064", i32 0, null, !18} ; [ DW_TAG_pointer_type ]
!25 = !{!26}
!26 = !{!"0x24"}                      ; [ DW_TAG_base_type ]
!27 = !{!"0x2a\00\000\000\000\000\000", !18, null, !7} ; [ DW_TAG_friend ]
!28 = !{!"foo.cpp", !"/Users/echristo/tmp"}
!29 = !{i32 1, !"Debug Info Version", i32 2}
