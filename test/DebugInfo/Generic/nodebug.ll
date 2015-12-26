; REQUIRES: object-emission

; RUN: %llc_dwarf < %s -filetype=obj | llvm-dwarfdump -debug-dump=info - | FileCheck %s

; Test that a nodebug function (a function not appearing in the debug info IR
; metadata subprogram list) with DebugLocs on its IR doesn't cause crashes/does
; the right thing.

; Build with clang from the following:
; extern int i;
; inline __attribute__((always_inline)) void f1() {
;   i = 3;
; }
;
; __attribute__((nodebug)) void f2() {
;   f1();
; }

; Check that there's only one DW_TAG_subprogram, nothing for the 'f2' function.
; CHECK: DW_TAG_subprogram
; CHECK-NOT: DW_TAG
; CHECK:   DW_AT_name {{.*}} "f1"
; CHECK-NOT: DW_TAG_subprogram

@i = external global i32

; Function Attrs: uwtable
define void @_Z2f2v() #0 {
entry:
  store i32 3, i32* @i, align 4, !dbg !11
  ret void
}

attributes #0 = { uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!8, !9}
!llvm.ident = !{!10}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, producer: "clang version 3.5.0 ", isOptimized: false, emissionKind: 1, file: !1, enums: !2, retainedTypes: !2, subprograms: !3, globals: !2, imports: !2)
!1 = !DIFile(filename: "nodebug.cpp", directory: "/tmp/dbginfo")
!2 = !{}
!3 = !{!4}
!4 = distinct !DISubprogram(name: "f1", linkageName: "_Z2f1v", line: 2, isLocal: false, isDefinition: true, virtualIndex: 6, flags: DIFlagPrototyped, isOptimized: false, scopeLine: 2, file: !1, scope: !5, type: !6, variables: !2)
!5 = !DIFile(filename: "nodebug.cpp", directory: "/tmp/dbginfo")
!6 = !DISubroutineType(types: !7)
!7 = !{null}
!8 = !{i32 2, !"Dwarf Version", i32 4}
!9 = !{i32 2, !"Debug Info Version", i32 3}
!10 = !{!"clang version 3.5.0 "}
!11 = !DILocation(line: 3, scope: !4)
