; struct A {
;   A();
;   virtual ~A();
; };
; struct B : A {
;   B();
;   virtual ~B();
; };
; B::B() {}
; CHECK: __ZN1BC1Ev:
; CHECK:     .loc	1 [[@LINE-2]] 0 prologue_end
; CHECK-NOT: .loc	1 0 0 prologue_end

; The location of the prologue_end marker should not be affected by the presence
; of CFI instructions.

; RUN: llc -O0 -filetype=asm < %s | FileCheck %s

; ModuleID = 'test1.cpp'
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-apple-ios"

%struct.B = type { %struct.A }
%struct.A = type { i32 (...)** }

@_ZTV1B = external unnamed_addr constant [4 x i8*]

; Function Attrs: nounwind
define %struct.B* @_ZN1BC2Ev(%struct.B* %this) unnamed_addr #0 align 2 {
entry:
  tail call void @llvm.dbg.value(metadata %struct.B* %this, i64 0, metadata !30, metadata !38), !dbg !39
  %0 = getelementptr inbounds %struct.B* %this, i64 0, i32 0, !dbg !40
  %call = tail call %struct.A* @_ZN1AC2Ev(%struct.A* %0) #3, !dbg !40
  %1 = getelementptr inbounds %struct.B* %this, i64 0, i32 0, i32 0, !dbg !40
  store i32 (...)** bitcast (i8** getelementptr inbounds ([4 x i8*]* @_ZTV1B, i64 0, i64 2) to i32 (...)**), i32 (...)*** %1, align 8, !dbg !40, !tbaa !41
  ret %struct.B* %this, !dbg !40
}

declare %struct.A* @_ZN1AC2Ev(%struct.A*)

; Function Attrs: nounwind
define %struct.B* @_ZN1BC1Ev(%struct.B* %this) unnamed_addr #0 align 2 {
entry:
  tail call void @llvm.dbg.value(metadata %struct.B* %this, i64 0, metadata !34, metadata !38), !dbg !44
  tail call void @llvm.dbg.value(metadata %struct.B* %this, i64 0, metadata !45, metadata !38) #3, !dbg !47
  %0 = getelementptr inbounds %struct.B* %this, i64 0, i32 0, !dbg !48
  %call.i = tail call %struct.A* @_ZN1AC2Ev(%struct.A* %0) #3, !dbg !48
  %1 = getelementptr inbounds %struct.B* %this, i64 0, i32 0, i32 0, !dbg !48
  store i32 (...)** bitcast (i8** getelementptr inbounds ([4 x i8*]* @_ZTV1B, i64 0, i64 2) to i32 (...)**), i32 (...)*** %1, align 8, !dbg !48, !tbaa !41
  ret %struct.B* %this, !dbg !46
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.value(metadata, i64, metadata, metadata) #2

attributes #0 = { nounwind }
attributes #2 = { nounwind readnone }
attributes #3 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!35, !36}
!llvm.ident = !{!37}

!0 = !{!"0x11\004\00clang version 3.6.0 (trunk 224279) (llvm/trunk 224283)\001\00\000\00\001", !1, !2, !3, !27, !2, !2} ; [ DW_TAG_compile_unit ] [<stdin>] [DW_LANG_C_plus_plus]
!1 = !{!"<stdin>", !""}
!2 = !{}
!3 = !{!4, !13}
!4 = !{!"0x13\00B\005\0064\0064\000\000\000", !5, null, null, !6, !"_ZTS1A", null, !"_ZTS1B"} ; [ DW_TAG_structure_type ] [B] [line 5, size 64, align 64, offset 0] [def] [from ]
!5 = !{!"test1.cpp", !""}
!6 = !{!7, !8, !12}
!7 = !{!"0x1c\00\000\000\000\000\000", null, !"_ZTS1B", !"_ZTS1A"} ; [ DW_TAG_inheritance ] [line 0, size 0, align 0, offset 0] [from _ZTS1A]
!8 = !{!"0x2e\00B\00B\00\006\000\000\000\000\00256\001\006", !5, !"_ZTS1B", !9, null, null, null, null, null} ; [ DW_TAG_subprogram ] [line 6] [B]
!9 = !{!"0x15\00\000\000\000\000\000\000", null, null, null, !10, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!10 = !{null, !11}
!11 = !{!"0xf\00\000\0064\0064\000\001088\00", null, null, !"_ZTS1B"} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [artificial] [from _ZTS1B]
!12 = !{!"0x2e\00~B\00~B\00\007\000\000\001\000\00256\001\007", !5, !"_ZTS1B", !9, !"_ZTS1B", null, null, null, null} ; [ DW_TAG_subprogram ] [line 7] [~B]
!13 = !{!"0x13\00A\001\0064\0064\000\000\000", !5, null, null, !14, !"_ZTS1A", null, !"_ZTS1A"} ; [ DW_TAG_structure_type ] [A] [line 1, size 64, align 64, offset 0] [def] [from ]
!14 = !{!15, !22, !26}
!15 = !{!"0xd\00_vptr$A\000\0064\000\000\0064", !5, !16, !17} ; [ DW_TAG_member ] [_vptr$A] [line 0, size 64, align 0, offset 0] [artificial] [from ]
!16 = !{!"0x29", !5}                              ; [ DW_TAG_file_type ] [test1.cpp]
!17 = !{!"0xf\00\000\0064\000\000\000", null, null, !18} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 0, offset 0] [from __vtbl_ptr_type]
!18 = !{!"0xf\00__vtbl_ptr_type\000\0064\000\000\000", null, null, !19} ; [ DW_TAG_pointer_type ] [__vtbl_ptr_type] [line 0, size 64, align 0, offset 0] [from ]
!19 = !{!"0x15\00\000\000\000\000\000\000", null, null, null, !20, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!20 = !{!21}
!21 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!22 = !{!"0x2e\00A\00A\00\002\000\000\000\000\00256\001\002", !5, !"_ZTS1A", !23, null, null, null, null, null} ; [ DW_TAG_subprogram ] [line 2] [A]
!23 = !{!"0x15\00\000\000\000\000\000\000", null, null, null, !24, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!24 = !{null, !25}
!25 = !{!"0xf\00\000\0064\0064\000\001088\00", null, null, !"_ZTS1A"} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [artificial] [from _ZTS1A]
!26 = !{!"0x2e\00~A\00~A\00\003\000\000\001\000\00256\001\003", !5, !"_ZTS1A", !23, !"_ZTS1A", null, null, null, null} ; [ DW_TAG_subprogram ] [line 3] [~A]
!27 = !{!28, !32}
!28 = !{!"0x2e\00B\00B\00_ZN1BC2Ev\009\000\001\000\000\00256\001\009", !5, !"_ZTS1B", !9, null, %struct.B* (%struct.B*)* @_ZN1BC2Ev, null, !8, !29} ; [ DW_TAG_subprogram ] [line 9] [def] [B]
!29 = !{!30}
!30 = !{!"0x101\00this\0016777216\001088", !28, null, !31} ; [ DW_TAG_arg_variable ] [this] [line 0]
!31 = !{!"0xf\00\000\0064\0064\000\000", null, null, !"_ZTS1B"} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from _ZTS1B]
!32 = !{!"0x2e\00B\00B\00_ZN1BC1Ev\009\000\001\000\000\00256\001\009", !5, !"_ZTS1B", !9, null, %struct.B* (%struct.B*)* @_ZN1BC1Ev, null, !8, !33} ; [ DW_TAG_subprogram ] [line 9] [def] [B]
!33 = !{!34}
!34 = !{!"0x101\00this\0016777216\001088", !32, null, !31} ; [ DW_TAG_arg_variable ] [this] [line 0]
!35 = !{i32 2, !"Dwarf Version", i32 4}
!36 = !{i32 2, !"Debug Info Version", i32 2}
!37 = !{!"clang version 3.6.0 (trunk 224279) (llvm/trunk 224283)"}
!38 = !{!"0x102"}                                 ; [ DW_TAG_expression ]
!39 = !MDLocation(line: 0, scope: !28)
!40 = !MDLocation(line: 9, scope: !28)
!41 = !{!42, !42, i64 0}
!42 = !{!"vtable pointer", !43, i64 0}
!43 = !{!"Simple C/C++ TBAA"}
!44 = !MDLocation(line: 0, scope: !32)
!45 = !{!"0x101\00this\0016777216\001088", !28, null, !31, !46} ; [ DW_TAG_arg_variable ] [this] [line 0]
!46 = !MDLocation(line: 9, scope: !32)
!47 = !MDLocation(line: 0, scope: !28, inlinedAt: !46)
!48 = !MDLocation(line: 9, scope: !28, inlinedAt: !46)
