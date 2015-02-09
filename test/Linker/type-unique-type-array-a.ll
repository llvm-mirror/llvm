; REQUIRES: object-emission
;
; RUN: llvm-link %s %p/type-unique-type-array-b.ll -S -o - | %llc_dwarf -filetype=obj -O0 | llvm-dwarfdump -debug-dump=info - | FileCheck %s
;
; rdar://problem/17628609
;
; cat -n a.cpp
;     1	struct SA {
;     2	  int a;
;     3	};
;     4	
;     5	class A {
;     6	public:
;     7	  void testA(SA a) {
;     8	  }
;     9	};
;    10	
;    11	void topA(A *a, SA sa) {
;    12	  a->testA(sa);
;    13	}
;
; CHECK: DW_TAG_compile_unit
; CHECK: DW_TAG_class_type
; CHECK-NEXT:   DW_AT_name {{.*}} "A"
; CHECK: DW_TAG_subprogram
; CHECK: DW_AT_MIPS_linkage_name {{.*}} "_ZN1A5testAE2SA"
; CHECK: DW_TAG_formal_parameter
; CHECK: DW_TAG_formal_parameter
; CHECK-NEXT: DW_AT_type [DW_FORM_ref4] (cu + 0x{{.*}} => {0x[[STRUCT:.*]]})
; CHECK: 0x[[STRUCT]]: DW_TAG_structure_type
; CHECK-NEXT:   DW_AT_name {{.*}} "SA"

; CHECK: DW_TAG_compile_unit
; CHECK: DW_TAG_class_type
; CHECK-NEXT:   DW_AT_name {{.*}} "B"
; CHECK: DW_TAG_subprogram
; CHECK:   DW_AT_MIPS_linkage_name {{.*}} "_ZN1B5testBE2SA"
; CHECK: DW_TAG_formal_parameter
; CHECK: DW_TAG_formal_parameter
; CHECK-NEXT: DW_AT_type [DW_FORM_ref_addr] {{.*}}[[STRUCT]]

%class.A = type { i8 }
%struct.SA = type { i32 }

; Function Attrs: ssp uwtable
define void @_Z4topAP1A2SA(%class.A* %a, i32 %sa.coerce) #0 {
entry:
  %sa = alloca %struct.SA, align 4
  %a.addr = alloca %class.A*, align 8
  %agg.tmp = alloca %struct.SA, align 4
  %coerce.dive = getelementptr %struct.SA* %sa, i32 0, i32 0
  store i32 %sa.coerce, i32* %coerce.dive
  store %class.A* %a, %class.A** %a.addr, align 8
  call void @llvm.dbg.declare(metadata %class.A** %a.addr, metadata !24, metadata !{!"0x102"}), !dbg !25
  call void @llvm.dbg.declare(metadata %struct.SA* %sa, metadata !26, metadata !{!"0x102"}), !dbg !27
  %0 = load %class.A** %a.addr, align 8, !dbg !28
  %1 = bitcast %struct.SA* %agg.tmp to i8*, !dbg !28
  %2 = bitcast %struct.SA* %sa to i8*, !dbg !28
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %1, i8* %2, i64 4, i32 4, i1 false), !dbg !28
  %coerce.dive1 = getelementptr %struct.SA* %agg.tmp, i32 0, i32 0, !dbg !28
  %3 = load i32* %coerce.dive1, !dbg !28
  call void @_ZN1A5testAE2SA(%class.A* %0, i32 %3), !dbg !28
  ret void, !dbg !29
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: nounwind ssp uwtable
define linkonce_odr void @_ZN1A5testAE2SA(%class.A* %this, i32 %a.coerce) #2 align 2 {
entry:
  %a = alloca %struct.SA, align 4
  %this.addr = alloca %class.A*, align 8
  %coerce.dive = getelementptr %struct.SA* %a, i32 0, i32 0
  store i32 %a.coerce, i32* %coerce.dive
  store %class.A* %this, %class.A** %this.addr, align 8
  call void @llvm.dbg.declare(metadata %class.A** %this.addr, metadata !30, metadata !{!"0x102"}), !dbg !31
  call void @llvm.dbg.declare(metadata %struct.SA* %a, metadata !32, metadata !{!"0x102"}), !dbg !33
  %this1 = load %class.A** %this.addr
  ret void, !dbg !34
}

; Function Attrs: nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) #3

attributes #0 = { ssp uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind ssp uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!21, !22}
!llvm.ident = !{!23}

!0 = !{!"0x11\004\00clang version 3.5.0 (trunk 214102:214113M) (llvm/trunk 214102:214115M)\000\00\000\00\001", !1, !2, !3, !14, !2, !2} ; [ DW_TAG_compile_unit ] [a.cpp] [DW_LANG_C_plus_plus]
!1 = !{!"a.cpp", !"/Users/manmanren/test-Nov/type_unique/rdar_di_array"}
!2 = !{}
!3 = !{!4, !10}
!4 = !{!"0x2\00A\005\008\008\000\000\000", !1, null, null, !5, null, null, !"_ZTS1A"} ; [ DW_TAG_class_type ] [A] [line 5, size 8, align 8, offset 0] [def] [from ]
!5 = !{!6}
!6 = !{!"0x2e\00testA\00testA\00_ZN1A5testAE2SA\007\000\000\000\006\00256\000\007", !1, !"_ZTS1A", !7, null, null, null, i32 0, null} ; [ DW_TAG_subprogram ] [line 7] [testA]
!7 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !8, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!8 = !{null, !9, !"_ZTS2SA"}
!9 = !{!"0xf\00\000\0064\0064\000\001088", null, null, !"_ZTS1A"} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [artificial] [from _ZTS1A]
!10 = !{!"0x13\00SA\001\0032\0032\000\000\000", !1, null, null, !11, null, null, !"_ZTS2SA"} ; [ DW_TAG_structure_type ] [SA] [line 1, size 32, align 32, offset 0] [def] [from ]
!11 = !{!12}
!12 = !{!"0xd\00a\002\0032\0032\000\000", !1, !"_ZTS2SA", !13} ; [ DW_TAG_member ] [a] [line 2, size 32, align 32, offset 0] [from int]
!13 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!14 = !{!15, !20}
!15 = !{!"0x2e\00topA\00topA\00_Z4topAP1A2SA\0011\000\001\000\006\00256\000\0011", !1, !16, !17, null, void (%class.A*, i32)* @_Z4topAP1A2SA, null, null, !2} ; [ DW_TAG_subprogram ] [line 11] [def] [topA]
!16 = !{!"0x29", !1}         ; [ DW_TAG_file_type ] [a.cpp]
!17 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !18, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!18 = !{null, !19, !"_ZTS2SA"}
!19 = !{!"0xf\00\000\0064\0064\000\000", null, null, !"_ZTS1A"} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from _ZTS1A]
!20 = !{!"0x2e\00testA\00testA\00_ZN1A5testAE2SA\007\000\001\000\006\00256\000\007", !1, !"_ZTS1A", !7, null, void (%class.A*, i32)* @_ZN1A5testAE2SA, null, !6, !2} ; [ DW_TAG_subprogram ] [line 7] [def] [testA]
!21 = !{i32 2, !"Dwarf Version", i32 2}
!22 = !{i32 2, !"Debug Info Version", i32 2}
!23 = !{!"clang version 3.5.0 (trunk 214102:214113M) (llvm/trunk 214102:214115M)"}
!24 = !{!"0x101\00a\0016777227\000", !15, !16, !19} ; [ DW_TAG_arg_variable ] [a] [line 11]
!25 = !MDLocation(line: 11, column: 14, scope: !15)
!26 = !{!"0x101\00sa\0033554443\000", !15, !16, !"_ZTS2SA"} ; [ DW_TAG_arg_variable ] [sa] [line 11]
!27 = !MDLocation(line: 11, column: 20, scope: !15)
!28 = !MDLocation(line: 12, column: 3, scope: !15)
!29 = !MDLocation(line: 13, column: 1, scope: !15)
!30 = !{!"0x101\00this\0016777216\001088", !20, null, !19} ; [ DW_TAG_arg_variable ] [this] [line 0]
!31 = !MDLocation(line: 0, scope: !20)
!32 = !{!"0x101\00a\0033554439\000", !20, !16, !"_ZTS2SA"} ; [ DW_TAG_arg_variable ] [a] [line 7]
!33 = !MDLocation(line: 7, column: 17, scope: !20)
!34 = !MDLocation(line: 8, column: 3, scope: !20)
