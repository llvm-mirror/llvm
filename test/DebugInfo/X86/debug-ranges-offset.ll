; RUN: llc -filetype=obj -mtriple=x86_64-pc-linux-gnu %s -o %t
; RUN: llvm-readobj --relocations %t | FileCheck %s

; Check that we don't have any relocations in the ranges section - 
; to show that we're producing this as a relative offset to the
; low_pc for the compile unit.
; CHECK-NOT: .rela.debug_ranges

@llvm.global_ctors = appending global [1 x { i32, void ()* }] [{ i32, void ()* } { i32 0, void ()* @__msan_init }]
@str = private unnamed_addr constant [4 x i8] c"zzz\00"
@__msan_retval_tls = external thread_local(initialexec) global [8 x i64]
@__msan_retval_origin_tls = external thread_local(initialexec) global i32
@__msan_param_tls = external thread_local(initialexec) global [1000 x i64]
@__msan_param_origin_tls = external thread_local(initialexec) global [1000 x i32]
@__msan_va_arg_tls = external thread_local(initialexec) global [1000 x i64]
@__msan_va_arg_overflow_size_tls = external thread_local(initialexec) global i64
@__msan_origin_tls = external thread_local(initialexec) global i32
@__executable_start = external hidden global i32
@_end = external hidden global i32

; Function Attrs: sanitize_memory uwtable
define void @_Z1fv() #0 {
entry:
  %p = alloca i32*, align 8
  %0 = ptrtoint i32** %p to i64, !dbg !19
  %1 = and i64 %0, -70368744177672, !dbg !19
  %2 = inttoptr i64 %1 to i64*, !dbg !19
  store i64 -1, i64* %2, align 8, !dbg !19
  store i64 0, i64* getelementptr inbounds ([1000 x i64]* @__msan_param_tls, i64 0, i64 0), align 8, !dbg !19
  store i64 0, i64* getelementptr inbounds ([8 x i64]* @__msan_retval_tls, i64 0, i64 0), align 8, !dbg !19
  %call = call i8* @_Znwm(i64 4) #4, !dbg !19
  %_msret = load i64* getelementptr inbounds ([8 x i64]* @__msan_retval_tls, i64 0, i64 0), align 8, !dbg !19
  %3 = bitcast i8* %call to i32*, !dbg !19
  tail call void @llvm.dbg.value(metadata i32* %3, i64 0, metadata !9, metadata !{!"0x102"}), !dbg !19
  %4 = inttoptr i64 %1 to i64*, !dbg !19
  store i64 %_msret, i64* %4, align 8, !dbg !19
  store volatile i32* %3, i32** %p, align 8, !dbg !19
  tail call void @llvm.dbg.value(metadata i32** %p, i64 0, metadata !9, metadata !{!"0x102"}), !dbg !19
  %p.0.p.0. = load volatile i32** %p, align 8, !dbg !20
  %_msld = load i64* %4, align 8, !dbg !20
  %_mscmp = icmp eq i64 %_msld, 0, !dbg !20
  br i1 %_mscmp, label %6, label %5, !dbg !20, !prof !22

; <label>:5                                       ; preds = %entry
  call void @__msan_warning_noreturn(), !dbg !20
  call void asm sideeffect "", ""() #3, !dbg !20
  unreachable, !dbg !20

; <label>:6                                       ; preds = %entry
  %7 = load i32* %p.0.p.0., align 4, !dbg !20, !tbaa !23
  %8 = ptrtoint i32* %p.0.p.0. to i64, !dbg !20
  %9 = and i64 %8, -70368744177665, !dbg !20
  %10 = inttoptr i64 %9 to i32*, !dbg !20
  %_msld2 = load i32* %10, align 4, !dbg !20
  %11 = icmp ne i32 %_msld2, 0, !dbg !20
  %12 = xor i32 %_msld2, -1, !dbg !20
  %13 = and i32 %7, %12, !dbg !20
  %14 = icmp eq i32 %13, 0, !dbg !20
  %_msprop_icmp = and i1 %11, %14, !dbg !20
  br i1 %_msprop_icmp, label %15, label %16, !dbg !20, !prof !27

; <label>:15                                      ; preds = %6
  call void @__msan_warning_noreturn(), !dbg !20
  call void asm sideeffect "", ""() #3, !dbg !20
  unreachable, !dbg !20

; <label>:16                                      ; preds = %6
  %tobool = icmp eq i32 %7, 0, !dbg !20
  br i1 %tobool, label %if.end, label %if.then, !dbg !20

if.then:                                          ; preds = %16
  store i64 0, i64* getelementptr inbounds ([1000 x i64]* @__msan_param_tls, i64 0, i64 0), align 8, !dbg !28
  store i32 0, i32* bitcast ([8 x i64]* @__msan_retval_tls to i32*), align 8, !dbg !28
  %puts = call i32 @puts(i8* getelementptr inbounds ([4 x i8]* @str, i64 0, i64 0)), !dbg !28
  br label %if.end, !dbg !28

if.end:                                           ; preds = %16, %if.then
  ret void, !dbg !29
}

; Function Attrs: nobuiltin
declare i8* @_Znwm(i64) #1

; Function Attrs: sanitize_memory uwtable
define i32 @main() #0 {
entry:
  %p.i = alloca i32*, align 8
  %0 = ptrtoint i32** %p.i to i64, !dbg !30
  %1 = and i64 %0, -70368744177672, !dbg !30
  %2 = inttoptr i64 %1 to i64*, !dbg !30
  store i64 -1, i64* %2, align 8, !dbg !30
  %p.i.0..sroa_cast = bitcast i32** %p.i to i8*, !dbg !30
  call void @llvm.lifetime.start(i64 8, i8* %p.i.0..sroa_cast), !dbg !30
  store i64 0, i64* getelementptr inbounds ([1000 x i64]* @__msan_param_tls, i64 0, i64 0), align 8, !dbg !30
  store i64 0, i64* getelementptr inbounds ([8 x i64]* @__msan_retval_tls, i64 0, i64 0), align 8, !dbg !30
  %call.i = call i8* @_Znwm(i64 4) #4, !dbg !30
  %_msret = load i64* getelementptr inbounds ([8 x i64]* @__msan_retval_tls, i64 0, i64 0), align 8, !dbg !30
  %3 = bitcast i8* %call.i to i32*, !dbg !30
  tail call void @llvm.dbg.value(metadata i32* %3, i64 0, metadata !32, metadata !{!"0x102"}), !dbg !30
  %4 = inttoptr i64 %1 to i64*, !dbg !30
  store i64 %_msret, i64* %4, align 8, !dbg !30
  store volatile i32* %3, i32** %p.i, align 8, !dbg !30
  tail call void @llvm.dbg.value(metadata i32** %p.i, i64 0, metadata !32, metadata !{!"0x102"}), !dbg !30
  %p.i.0.p.0.p.0..i = load volatile i32** %p.i, align 8, !dbg !33
  %_msld = load i64* %4, align 8, !dbg !33
  %_mscmp = icmp eq i64 %_msld, 0, !dbg !33
  br i1 %_mscmp, label %6, label %5, !dbg !33, !prof !22

; <label>:5                                       ; preds = %entry
  call void @__msan_warning_noreturn(), !dbg !33
  call void asm sideeffect "", ""() #3, !dbg !33
  unreachable, !dbg !33

; <label>:6                                       ; preds = %entry
  %7 = load i32* %p.i.0.p.0.p.0..i, align 4, !dbg !33, !tbaa !23
  %8 = ptrtoint i32* %p.i.0.p.0.p.0..i to i64, !dbg !33
  %9 = and i64 %8, -70368744177665, !dbg !33
  %10 = inttoptr i64 %9 to i32*, !dbg !33
  %_msld2 = load i32* %10, align 4, !dbg !33
  %11 = icmp ne i32 %_msld2, 0, !dbg !33
  %12 = xor i32 %_msld2, -1, !dbg !33
  %13 = and i32 %7, %12, !dbg !33
  %14 = icmp eq i32 %13, 0, !dbg !33
  %_msprop_icmp = and i1 %11, %14, !dbg !33
  br i1 %_msprop_icmp, label %15, label %16, !dbg !33, !prof !27

; <label>:15                                      ; preds = %6
  call void @__msan_warning_noreturn(), !dbg !33
  call void asm sideeffect "", ""() #3, !dbg !33
  unreachable, !dbg !33

; <label>:16                                      ; preds = %6
  %tobool.i = icmp eq i32 %7, 0, !dbg !33
  br i1 %tobool.i, label %_Z1fv.exit, label %if.then.i, !dbg !33

if.then.i:                                        ; preds = %16
  store i64 0, i64* getelementptr inbounds ([1000 x i64]* @__msan_param_tls, i64 0, i64 0), align 8, !dbg !34
  store i32 0, i32* bitcast ([8 x i64]* @__msan_retval_tls to i32*), align 8, !dbg !34
  %puts.i = call i32 @puts(i8* getelementptr inbounds ([4 x i8]* @str, i64 0, i64 0)), !dbg !34
  br label %_Z1fv.exit, !dbg !34

_Z1fv.exit:                                       ; preds = %16, %if.then.i
  call void @llvm.lifetime.end(i64 8, i8* %p.i.0..sroa_cast), !dbg !35
  store i32 0, i32* bitcast ([8 x i64]* @__msan_retval_tls to i32*), align 8, !dbg !36
  ret i32 0, !dbg !36
}

declare void @__msan_init()

; Function Attrs: nounwind readnone
declare void @llvm.dbg.value(metadata, i64, metadata, metadata) #2

; Function Attrs: nounwind
declare i32 @puts(i8* nocapture readonly) #3

; Function Attrs: nounwind
declare void @llvm.lifetime.start(i64, i8* nocapture) #3

; Function Attrs: nounwind
declare void @llvm.lifetime.end(i64, i8* nocapture) #3

declare void @__msan_warning_noreturn()

declare void @__msan_maybe_warning_1(i8, i32)

declare void @__msan_maybe_store_origin_1(i8, i8*, i32)

declare void @__msan_maybe_warning_2(i16, i32)

declare void @__msan_maybe_store_origin_2(i16, i8*, i32)

declare void @__msan_maybe_warning_4(i32, i32)

declare void @__msan_maybe_store_origin_4(i32, i8*, i32)

declare void @__msan_maybe_warning_8(i64, i32)

declare void @__msan_maybe_store_origin_8(i64, i8*, i32)

declare void @__msan_set_alloca_origin4(i8*, i64, i8*, i64)

declare void @__msan_poison_stack(i8*, i64)

declare i32 @__msan_chain_origin(i32)

declare i8* @__msan_memmove(i8*, i8*, i64)

declare i8* @__msan_memcpy(i8*, i8*, i64)

declare i8* @__msan_memset(i8*, i32, i64)

; Function Attrs: nounwind
declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) #3

attributes #0 = { sanitize_memory uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nobuiltin "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind readnone }
attributes #3 = { nounwind }
attributes #4 = { builtin }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!16, !17}
!llvm.ident = !{!18}

!0 = !{!"0x11\004\00clang version 3.5.0 (trunk 207243) (llvm/trunk 207259)\001\00\000\00\001", !1, !2, !2, !3, !2, !2} ; [ DW_TAG_compile_unit ] [/usr/local/google/home/echristo/tmp/foo.cpp] [DW_LANG_C_plus_plus]
!1 = !{!"foo.cpp", !"/usr/local/google/home/echristo/tmp"}
!2 = !{}
!3 = !{!4, !13}
!4 = !{!"0x2e\00f\00f\00_Z1fv\003\000\001\000\006\00256\001\003", !1, !5, !6, null, void ()* @_Z1fv, null, null, !8} ; [ DW_TAG_subprogram ] [line 3] [def] [f]
!5 = !{!"0x29", !1}          ; [ DW_TAG_file_type ] [/usr/local/google/home/echristo/tmp/foo.cpp]
!6 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !7, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!7 = !{null}
!8 = !{!9}
!9 = !{!"0x100\00p\004\000", !4, !5, !10} ; [ DW_TAG_auto_variable ] [p] [line 4]
!10 = !{!"0x35\00\000\000\000\000\000", null, null, !11} ; [ DW_TAG_volatile_type ] [line 0, size 0, align 0, offset 0] [from ]
!11 = !{!"0xf\00\000\0064\0064\000\000", null, null, !12} ; [ DW_TAG_pointer_type ] [line 0, size 64, align 64, offset 0] [from int]
!12 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!13 = !{!"0x2e\00main\00main\00\009\000\001\000\006\00256\001\009", !1, !5, !14, null, i32 ()* @main, null, null, !2} ; [ DW_TAG_subprogram ] [line 9] [def] [main]
!14 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !15, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!15 = !{!12}
!16 = !{i32 2, !"Dwarf Version", i32 4}
!17 = !{i32 1, !"Debug Info Version", i32 2}
!18 = !{!"clang version 3.5.0 (trunk 207243) (llvm/trunk 207259)"}
!19 = !MDLocation(line: 4, scope: !4)
!20 = !MDLocation(line: 5, scope: !21)
!21 = !{!"0xb\005\000\000", !1, !4} ; [ DW_TAG_lexical_block ] [/usr/local/google/home/echristo/tmp/foo.cpp]
!22 = !{!"branch_weights", i32 1000, i32 1}
!23 = !{!24, !24, i64 0}
!24 = !{!"int", !25, i64 0}
!25 = !{!"omnipotent char", !26, i64 0}
!26 = !{!"Simple C/C++ TBAA"}
!27 = !{!"branch_weights", i32 1, i32 1000}
!28 = !MDLocation(line: 6, scope: !21)
!29 = !MDLocation(line: 7, scope: !4)
!30 = !MDLocation(line: 4, scope: !4, inlinedAt: !31)
!31 = !MDLocation(line: 10, scope: !13)
!32 = !{!"0x100\00p\004\000", !4, !5, !10, !31} ; [ DW_TAG_auto_variable ] [p] [line 4]
!33 = !MDLocation(line: 5, scope: !21, inlinedAt: !31)
!34 = !MDLocation(line: 6, scope: !21, inlinedAt: !31)
!35 = !MDLocation(line: 7, scope: !4, inlinedAt: !31)
!36 = !MDLocation(line: 11, scope: !13)
