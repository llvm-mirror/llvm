; RUN: opt -instcombine -S < %s | FileCheck %s
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%foo = type { i64, i32, i32 }

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #0
declare void @llvm.dbg.value(metadata, i64, metadata, metadata) #0

; Function Attrs: sspreq
define void @julia_fastshortest_6256() #1 {
top:
  %cp = alloca %foo, align 8
  call void @llvm.dbg.declare(metadata %foo* %cp, metadata !1, metadata !16), !dbg !17
  br i1 undef, label %idxend, label %fail

fail:                                             ; preds = %top
  unreachable

idxend:                                           ; preds = %top
; CHECK-NOT call void @llvm.dbg.value(metadata %foo* %cp, i64 0, metadata !1, metadata !16), !dbg !17
  %0 = load volatile %foo, %foo* %cp, align 8
; CHECK: call void @llvm.dbg.value(metadata %foo %0, i64 0, metadata !1, metadata !16), !dbg !17
  store volatile %foo %0, %foo* undef, align 8
  ret void
}

attributes #0 = { nounwind readnone }
attributes #1 = { sspreq }

!llvm.module.flags = !{!0}
!llvm.dbg.cu = !{}

!0 = !{i32 1, !"Debug Info Version", i32 3}
!1 = !DILocalVariable(name: "cp", scope: !2, file: !3, line: 106, type: !12)
!2 = distinct !DISubprogram(name: "fastshortest", linkageName: "julia_fastshortest_6256", scope: null, file: !3, type: !4, isLocal: false, isDefinition: true, isOptimized: true, variables: !11)
!3 = !DIFile(filename: "grisu/fastshortest.jl", directory: ".")
!4 = !DISubroutineType(types: !5)
!5 = !{!6, !7}
!6 = !DIBasicType(name: "Float64", size: 64, align: 64, encoding: DW_ATE_unsigned)
!7 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 64, align: 64)
!8 = !DICompositeType(tag: DW_TAG_structure_type, name: "jl_value_t", file: !9, line: 71, align: 64, elements: !10)
!9 = !DIFile(filename: "julia.h", directory: "")
!10 = !{!7}
!11 = !{}
!12 = !DICompositeType(tag: DW_TAG_structure_type, name: "Float", size: 128, align: 64, elements: !13, runtimeLang: DW_LANG_Julia)
!13 = !{!14, !15, !15}
!14 = !DIBasicType(name: "UInt64", size: 64, align: 64, encoding: DW_ATE_unsigned)
!15 = !DIBasicType(name: "Int32", size: 32, align: 32, encoding: DW_ATE_unsigned)
!16 = !DIExpression()
!17 = !DILocation(line: 106, scope: !2)
