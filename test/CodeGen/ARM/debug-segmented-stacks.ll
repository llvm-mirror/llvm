; RUN: llc < %s -mtriple=arm-linux-unknown-gnueabi -verify-machineinstrs -filetype=asm | FileCheck %s -check-prefix=ARM-linux
; RUN: llc < %s -mtriple=arm-linux-unknown-gnueabi -filetype=obj

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!9, !10}
!llvm.ident = !{!11}

define void @test_basic() #0 {
        %mem = alloca i32, i32 10
        call void @dummy_use (i32* %mem, i32 10)
	ret void

; ARM-linux:      test_basic:

; ARM-linux:      push    {r4, r5}
; ARM-linux:      .cfi_def_cfa_offset 8
; ARM-linux:      .cfi_offset r5, -4
; ARM-linux:      .cfi_offset r4, -8
; ARM-linux-NEXT: mrc     p15, #0, r4, c13, c0, #3
; ARM-linux-NEXT: mov     r5, sp
; ARM-linux-NEXT: ldr     r4, [r4, #4]
; ARM-linux-NEXT: cmp     r4, r5
; ARM-linux-NEXT: blo     .LBB0_2

; ARM-linux:      mov     r4, #48
; ARM-linux-NEXT: mov     r5, #0
; ARM-linux-NEXT: stmdb   sp!, {lr}
; ARM-linux:      .cfi_def_cfa_offset 12
; ARM-linux:      .cfi_offset lr, -12
; ARM-linux-NEXT: bl      __morestack
; ARM-linux-NEXT: ldm     sp!, {lr}
; ARM-linux-NEXT: pop     {r4, r5}
; ARM-linux:      .cfi_def_cfa_offset 0
; ARM-linux-NEXT: bx      lr

; ARM-linux:      pop     {r4, r5}
; ARM-linux:      .cfi_def_cfa_offset 0
; ARM-linux       .cfi_same_value r4
; ARM-linux       .cfi_same_value r5
}

!0 = !{!"0x11\0012\00clang version 3.5 \000\00\000\00\000", !1, !2, !2, !3, !2, !2} ; [ DW_TAG_compile_unit ] [/tmp/var.c] [DW_LANG_C99]
!1 = !{!"var.c", !"/tmp"}
!2 = !{}
!3 = !{!4}
!4 = !{!"0x2e\00test_basic\00test_basic\00\005\000\001\000\006\00256\000\005", !1, !5, !6, null, void ()* @test_basic, null, null, !2} ; [ DW_TAG_subprogram ] [line 5] [def] [sum]
!5 = !{!"0x29", !1}          ; [ DW_TAG_file_type ] [/tmp/var.c]
!6 = !{!"0x15\00\000\000\000\000\000\000", i32 0, null, null, !7, null, null, null} ; [ DW_TAG_subroutine_type ] [line 0, size 0, align 0, offset 0] [from ]
!7 = !{!8, !8}
!8 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!9 = !{i32 2, !"Dwarf Version", i32 4}
!10 = !{i32 1, !"Debug Info Version", i32 2}
!11 = !{!"clang version 3.5 "}
!12 = !{!"0x101\00count\0016777221\000", !4, !5, !8} ; [ DW_TAG_arg_variable ] [count] [line 5]
!13 = !MDLocation(line: 5, scope: !4)
!14 = !{!"0x100\00vl\006\000", !4, !5, !15} ; [ DW_TAG_auto_variable ] [vl] [line 6]
!15 = !{!"0x16\00va_list\0030\000\000\000\000", !16, null, !17} ; [ DW_TAG_typedef ] [va_list] [line 30, size 0, align 0, offset 0] [from __builtin_va_list]
!16 = !{!"/linux-x86_64-high/gcc_4.7.2/dbg/llvm/bin/../lib/clang/3.5/include/stdarg.h", !"/tmp"}
!17 = !{!"0x16\00__builtin_va_list\006\000\000\000\000", !1, null, !18} ; [ DW_TAG_typedef ] [__builtin_va_list] [line 6, size 0, align 0, offset 0] [from __va_list]
!18 = !{!"0x13\00__va_list\006\0032\0032\000\000\000", !1, null, null, !19, null, null, null} ; [ DW_TAG_structure_type ] [__va_list] [line 6, size 32, align 32, offset 0] [def] [from ]
!19 = !{!20}
!20 = !{!"0xd\00__ap\006\0032\0032\000\000", !1, !18, !21} ; [ DW_TAG_member ] [__ap] [line 6, size 32, align 32, offset 0] [from ]
!21 = !{!"0xf\00\000\0032\0032\000\000", null, null, null} ; [ DW_TAG_pointer_type ] [line 0, size 32, align 32, offset 0] [from ]
!22 = !MDLocation(line: 6, scope: !4)
!23 = !MDLocation(line: 7, scope: !4)
!24 = !{!"0x100\00test_basic\008\000", !4, !5, !8} ; [ DW_TAG_auto_variable ] [sum] [line 8]
!25 = !MDLocation(line: 8, scope: !4)
!26 = !{!"0x100\00i\009\000", !27, !5, !8} ; [ DW_TAG_auto_variable ] [i] [line 9]
!27 = !{!"0xb\009\000\000", !1, !4} ; [ DW_TAG_lexical_block ] [/tmp/var.c]
!28 = !MDLocation(line: 9, scope: !27)
!29 = !MDLocation(line: 10, scope: !30)
!30 = !{!"0xb\009\000\001", !1, !27} ; [ DW_TAG_lexical_block ] [/tmp/var.c]
!31 = !MDLocation(line: 11, scope: !30)
!32 = !MDLocation(line: 12, scope: !4)
!33 = !MDLocation(line: 13, scope: !4)

; Just to prevent the alloca from being optimized away
declare void @dummy_use(i32*, i32)

attributes #0 = { "split-stack" }
