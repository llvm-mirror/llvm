; RUN: llc -mtriple=x86_64-apple-darwin %s -o %t -filetype=obj
; RUN: llvm-dwarfdump -debug-dump=info %t | FileCheck %s

; CHECK: DW_TAG_formal_parameter [
; CHECK: DW_TAG_class_type
; CHECK: DW_AT_object_pointer [DW_FORM_ref4]     (cu + 0x00fd => {0x000000fd})
; CHECK: 0x000000fd:     DW_TAG_formal_parameter [13]
; CHECK-NEXT: DW_AT_name [DW_FORM_strp]     ( .debug_str[0x00000086] = "this")

%class.A = type { i32 }

define i32 @_Z3fooi(i32) nounwind uwtable ssp {
entry:
  %.addr = alloca i32, align 4
  %a = alloca %class.A, align 4
  store i32 %0, i32* %.addr, align 4
  call void @llvm.dbg.declare(metadata !{i32* %.addr}, metadata !36), !dbg !35
  call void @llvm.dbg.declare(metadata !{%class.A* %a}, metadata !21), !dbg !23
  call void @_ZN1AC1Ev(%class.A* %a), !dbg !24
  %m_a = getelementptr inbounds %class.A* %a, i32 0, i32 0, !dbg !25
  %1 = load i32* %m_a, align 4, !dbg !25
  ret i32 %1, !dbg !25
}

declare void @llvm.dbg.declare(metadata, metadata) nounwind readnone

define linkonce_odr void @_ZN1AC1Ev(%class.A* %this) unnamed_addr nounwind uwtable ssp align 2 {
entry:
  %this.addr = alloca %class.A*, align 8
  store %class.A* %this, %class.A** %this.addr, align 8
  call void @llvm.dbg.declare(metadata !{%class.A** %this.addr}, metadata !26), !dbg !28
  %this1 = load %class.A** %this.addr
  call void @_ZN1AC2Ev(%class.A* %this1), !dbg !29
  ret void, !dbg !29
}

define linkonce_odr void @_ZN1AC2Ev(%class.A* %this) unnamed_addr nounwind uwtable ssp align 2 {
entry:
  %this.addr = alloca %class.A*, align 8
  store %class.A* %this, %class.A** %this.addr, align 8
  call void @llvm.dbg.declare(metadata !{%class.A** %this.addr}, metadata !30), !dbg !31
  %this1 = load %class.A** %this.addr
  %m_a = getelementptr inbounds %class.A* %this1, i32 0, i32 0, !dbg !32
  store i32 0, i32* %m_a, align 4, !dbg !32
  ret void, !dbg !34
}

!llvm.dbg.cu = !{!0}

!0 = metadata !{i32 786449, metadata !37, i32 4, metadata !"clang version 3.2 (trunk 163586) (llvm/trunk 163570)", i1 false, metadata !"", i32 0, metadata !1, metadata !1, metadata !3, metadata !1,  metadata !1, metadata !""} ; [ DW_TAG_compile_unit ] [/Users/echristo/debug-tests/bar.cpp] [DW_LANG_C_plus_plus]
!1 = metadata !{i32 0}
!3 = metadata !{metadata !5, metadata !10, metadata !20}
!5 = metadata !{i32 786478, metadata !6, metadata !6, metadata !"foo", metadata !"foo", metadata !"_Z3fooi", i32 7, metadata !7, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, i32 (i32)* @_Z3fooi, null, null, metadata !1, i32 7} ; [ DW_TAG_subprogram ] [line 7] [def] [foo]
!6 = metadata !{i32 786473, metadata !37} ; [ DW_TAG_file_type ]
!7 = metadata !{i32 786453, i32 0, metadata !"", i32 0, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !8, i32 0, i32 0} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!8 = metadata !{metadata !9}
!9 = metadata !{i32 786468, null, null, metadata !"int", i32 0, i64 32, i64 32, i64 0, i32 0, i32 5} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!10 = metadata !{i32 786478, metadata !6, null, metadata !"A", metadata !"A", metadata !"_ZN1AC1Ev", i32 3, metadata !11, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, void (%class.A*)* @_ZN1AC1Ev, null, metadata !17, metadata !1, i32 3} ; [ DW_TAG_subprogram ] [line 3] [def] [A]
!11 = metadata !{i32 786453, i32 0, metadata !"", i32 0, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !12, i32 0, i32 0} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!12 = metadata !{null, metadata !13}
!13 = metadata !{i32 786447, i32 0, metadata !"", i32 0, i32 0, i64 64, i64 64, i64 0, i32 1088, metadata !14} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from A]
!14 = metadata !{i32 786434, metadata !37, null, metadata !"A", i32 1, i64 32, i64 32, i32 0, i32 0, null, metadata !15, i32 0, null, null} ; [ DW_TAG_class_type ] [A] [line 1, size 32, align 32, offset 0] [from ]
!15 = metadata !{metadata !16, metadata !17}
!16 = metadata !{i32 786445, metadata !37, metadata !14, metadata !"m_a", i32 4, i64 32, i64 32, i64 0, i32 0, metadata !9} ; [ DW_TAG_member ] [m_a] [line 4, size 32, align 32, offset 0] [from int]
!17 = metadata !{i32 786478, metadata !6, metadata !14, metadata !"A", metadata !"A", metadata !"", i32 3, metadata !11, i1 false, i1 false, i32 0, i32 0, null, i32 256, i1 false, null, null, i32 0, metadata !18, i32 3} ; [ DW_TAG_subprogram ] [line 3] [A]
!18 = metadata !{metadata !19}
!19 = metadata !{i32 786468}                      ; [ DW_TAG_base_type ] [line 0, size 0, align 0, offset 0]
!20 = metadata !{i32 786478, metadata !6, null, metadata !"A", metadata !"A", metadata !"_ZN1AC2Ev", i32 3, metadata !11, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 false, void (%class.A*)* @_ZN1AC2Ev, null, metadata !17, metadata !1, i32 3} ; [ DW_TAG_subprogram ] [line 3] [def] [A]
!21 = metadata !{i32 786688, metadata !22, metadata !"a", metadata !6, i32 8, metadata !14, i32 0, i32 0} ; [ DW_TAG_auto_variable ] [a] [line 8]
!22 = metadata !{i32 786443, metadata !6, metadata !5, i32 7, i32 11, i32 0} ; [ DW_TAG_lexical_block ] [/Users/echristo/debug-tests/bar.cpp]
!23 = metadata !{i32 8, i32 5, metadata !22, null}
!24 = metadata !{i32 8, i32 6, metadata !22, null}
!25 = metadata !{i32 9, i32 3, metadata !22, null}
!26 = metadata !{i32 786689, metadata !10, metadata !"this", metadata !6, i32 16777219, metadata !27, i32 1088, i32 0} ; [ DW_TAG_arg_variable ] [this] [line 3]
!27 = metadata !{i32 786447, null, null, metadata !"", i32 0, i64 64, i64 64, i64 0, i32 0, metadata !14} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from A]
!28 = metadata !{i32 3, i32 3, metadata !10, null}
!29 = metadata !{i32 3, i32 18, metadata !10, null}
!30 = metadata !{i32 786689, metadata !20, metadata !"this", metadata !6, i32 16777219, metadata !27, i32 1088, i32 0} ; [ DW_TAG_arg_variable ] [this] [line 3]
!31 = metadata !{i32 3, i32 3, metadata !20, null}
!32 = metadata !{i32 3, i32 9, metadata !33, null}
!33 = metadata !{i32 786443, metadata !6, metadata !20, i32 3, i32 7, i32 1} ; [ DW_TAG_lexical_block ] [/Users/echristo/debug-tests/bar.cpp]
!34 = metadata !{i32 3, i32 18, metadata !33, null}
!35 = metadata !{i32 7, i32 0, metadata !5, null}
!36 = metadata !{i32 786689, metadata !5, metadata !"", metadata !6, i32 16777223, metadata !9, i32 0, i32 0} ; [ DW_TAG_arg_variable ] [line 7]
!37 = metadata !{metadata !"bar.cpp", metadata !"/Users/echristo/debug-tests"}
