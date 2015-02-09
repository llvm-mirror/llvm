; REQUIRES: object-emission

; RUN: llc -split-dwarf=Enable -filetype=obj -O0 -generate-type-units -mtriple=x86_64-unknown-linux-gnu < %s \
; RUN:     | llvm-dwarfdump - | FileCheck %s

; RUN: llc -split-dwarf=Disable -filetype=obj -O0 -generate-type-units -mtriple=x86_64-unknown-linux-gnu < %s \
; RUN:     | llvm-dwarfdump - | FileCheck --check-prefix=SINGLE %s

; Test case built from:
;int i;
;
;template <int *I>
;struct S1 {};
;
;S1<&i> s1;
;
;template <int *I>
;struct S2_1 {};
;
;struct S2 {
;  S2_1<&i> s2_1;
;};
;
;S2 s2;
;
;template <int *I>
;struct S3_1 {};
;
;struct S3_2 {};
;
;struct S3 {
;  S3_1<&i> s3_1;
;  S3_2 s3_2;
;};
;
;S3 s3;
;
;struct S4_1 {};
;
;template <int *T>
;struct S4_2 {};
;
;struct S4 {
;  S4_1 s4_1;
;  S4_2<&::i> s4_2;
;};
;
;S4 s4;


; CHECK: .debug_info.dwo contents:

; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S1<&i>"

; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S2"
; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S2_1<&i>"

; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S3"
; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S3_1<&i>"
; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_declaration
; CHECK-NEXT: DW_AT_signature

; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S4"
; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_declaration
; CHECK-NEXT: DW_AT_signature
; CHECK: DW_TAG_structure_type
; CHECK-NEXT: DW_AT_name {{.*}}"S4_2<&i>"

; SINGLE: .debug_info contents:

; SINGLE: DW_TAG_structure_type
; SINGLE-NEXT: DW_AT_declaration
; SINGLE-NEXT: DW_AT_signature

; SINGLE: DW_TAG_structure_type
; SINGLE-NEXT: DW_AT_declaration
; SINGLE-NEXT: DW_AT_signature

; SINGLE: DW_TAG_structure_type
; SINGLE-NEXT: DW_AT_declaration
; SINGLE-NEXT: DW_AT_signature

; SINGLE: DW_TAG_structure_type
; SINGLE-NEXT: DW_AT_declaration
; SINGLE-NEXT: DW_AT_signature

%struct.S1 = type { i8 }
%struct.S2 = type { %struct.S2_1 }
%struct.S2_1 = type { i8 }
%struct.S3 = type { %struct.S3_1, %struct.S3_2 }
%struct.S3_1 = type { i8 }
%struct.S3_2 = type { i8 }
%struct.S4 = type { %struct.S4_1, %struct.S4_2 }
%struct.S4_1 = type { i8 }
%struct.S4_2 = type { i8 }

@i = global i32 0, align 4
@a = global %struct.S1 zeroinitializer, align 1
@s2 = global %struct.S2 zeroinitializer, align 1
@s3 = global %struct.S3 zeroinitializer, align 1
@s4 = global %struct.S4 zeroinitializer, align 1

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!34, !35}
!llvm.ident = !{!36}

!0 = !{!"0x11\004\00clang version 3.5.0 \000\00\000\00tu.dwo\001", !1, !2, !3, !2, !27, !2} ; [ DW_TAG_compile_unit ] [/tmp/dbginfo/tu.cpp] [DW_LANG_C_plus_plus]
!1 = !{!"tu.cpp", !"/tmp/dbginfo"}
!2 = !{}
!3 = !{!4, !9, !12, !13, !17, !18, !19, !23, !24}
!4 = !{!"0x13\00S1<&i>\004\008\008\000\000\000", !1, null, null, !2, null, !5, !"_ZTS2S1IXadL_Z1iEEE"} ; [ DW_TAG_structure_type ] [S1<&i>] [line 4, size 8, align 8, offset 0] [def] [from ]
!5 = !{!6}
!6 = !{!"0x30\00I\000\000", null, !7, i32* @i, null} ; [ DW_TAG_template_value_parameter ]
!7 = !{!"0xf\00\000\0064\0064\000\000", null, null, !8} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from int]
!8 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!9 = !{!"0x13\00S2\0011\008\008\000\000\000", !1, null, null, !10, null, null, !"_ZTS2S2"} ; [ DW_TAG_structure_type ] [S2] [line 11, size 8, align 8, offset 0] [def] [from ]
!10 = !{!11}
!11 = !{!"0xd\00s2_1\0012\008\008\000\000", !1, !"_ZTS2S2", !"_ZTS4S2_1IXadL_Z1iEEE"} ; [ DW_TAG_member ] [s2_1] [line 12, size 8, align 8, offset 0] [from _ZTS4S2_1IXadL_Z1iEEE]
!12 = !{!"0x13\00S2_1<&i>\009\008\008\000\000\000", !1, null, null, !2, null, !5, !"_ZTS4S2_1IXadL_Z1iEEE"} ; [ DW_TAG_structure_type ] [S2_1<&i>] [line 9, size 8, align 8, offset 0] [def] [from ]
!13 = !{!"0x13\00S3\0022\0016\008\000\000\000", !1, null, null, !14, null, null, !"_ZTS2S3"} ; [ DW_TAG_structure_type ] [S3] [line 22, size 16, align 8, offset 0] [def] [from ]
!14 = !{!15, !16}
!15 = !{!"0xd\00s3_1\0023\008\008\000\000", !1, !"_ZTS2S3", !"_ZTS4S3_1IXadL_Z1iEEE"} ; [ DW_TAG_member ] [s3_1] [line 23, size 8, align 8, offset 0] [from _ZTS4S3_1IXadL_Z1iEEE]
!16 = !{!"0xd\00s3_2\0024\008\008\008\000", !1, !"_ZTS2S3", !"_ZTS4S3_2"} ; [ DW_TAG_member ] [s3_2] [line 24, size 8, align 8, offset 8] [from _ZTS4S3_2]
!17 = !{!"0x13\00S3_1<&i>\0018\008\008\000\000\000", !1, null, null, !2, null, !5, !"_ZTS4S3_1IXadL_Z1iEEE"} ; [ DW_TAG_structure_type ] [S3_1<&i>] [line 18, size 8, align 8, offset 0] [def] [from ]
!18 = !{!"0x13\00S3_2\0020\008\008\000\000\000", !1, null, null, !2, null, null, !"_ZTS4S3_2"} ; [ DW_TAG_structure_type ] [S3_2] [line 20, size 8, align 8, offset 0] [def] [from ]
!19 = !{!"0x13\00S4\0034\0016\008\000\000\000", !1, null, null, !20, null, null, !"_ZTS2S4"} ; [ DW_TAG_structure_type ] [S4] [line 34, size 16, align 8, offset 0] [def] [from ]
!20 = !{!21, !22}
!21 = !{!"0xd\00s4_1\0035\008\008\000\000", !1, !"_ZTS2S4", !"_ZTS4S4_1"} ; [ DW_TAG_member ] [s4_1] [line 35, size 8, align 8, offset 0] [from _ZTS4S4_1]
!22 = !{!"0xd\00s4_2\0036\008\008\008\000", !1, !"_ZTS2S4", !"_ZTS4S4_2IXadL_Z1iEEE"} ; [ DW_TAG_member ] [s4_2] [line 36, size 8, align 8, offset 8] [from _ZTS4S4_2IXadL_Z1iEEE]
!23 = !{!"0x13\00S4_1\0029\008\008\000\000\000", !1, null, null, !2, null, null, !"_ZTS4S4_1"} ; [ DW_TAG_structure_type ] [S4_1] [line 29, size 8, align 8, offset 0] [def] [from ]
!24 = !{!"0x13\00S4_2<&i>\0032\008\008\000\000\000", !1, null, null, !2, null, !25, !"_ZTS4S4_2IXadL_Z1iEEE"} ; [ DW_TAG_structure_type ] [S4_2<&i>] [line 32, size 8, align 8, offset 0] [def] [from ]
!25 = !{!26}
!26 = !{!"0x30\00T\000\000", null, !7, i32* @i, null} ; [ DW_TAG_template_value_parameter ]
!27 = !{!28, !30, !31, !32, !33}
!28 = !{!"0x34\00i\00i\00\001\000\001", null, !29, !8, i32* @i, null} ; [ DW_TAG_variable ] [i] [line 1] [def]
!29 = !{!"0x29", !1}         ; [ DW_TAG_file_type ] [/tmp/dbginfo/tu.cpp]
!30 = !{!"0x34\00a\00a\00\006\000\001", null, !29, !"_ZTS2S1IXadL_Z1iEEE", %struct.S1* @a, null} ; [ DW_TAG_variable ] [a] [line 6] [def]
!31 = !{!"0x34\00s2\00s2\00\0015\000\001", null, !29, !"_ZTS2S2", %struct.S2* @s2, null} ; [ DW_TAG_variable ] [s2] [line 15] [def]
!32 = !{!"0x34\00s3\00s3\00\0027\000\001", null, !29, !"_ZTS2S3", %struct.S3* @s3, null} ; [ DW_TAG_variable ] [s3] [line 27] [def]
!33 = !{!"0x34\00s4\00s4\00\0039\000\001", null, !29, !"_ZTS2S4", %struct.S4* @s4, null} ; [ DW_TAG_variable ] [s4] [line 39] [def]
!34 = !{i32 2, !"Dwarf Version", i32 4}
!35 = !{i32 1, !"Debug Info Version", i32 2}
!36 = !{!"clang version 3.5.0 "}
