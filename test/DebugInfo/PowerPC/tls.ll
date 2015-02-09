; RUN: llc -mtriple=powerpc64-unknown-linux-gnu -O0 -filetype=asm < %s | FileCheck %s

; FIXME: add relocation and DWARF expression support to llvm-dwarfdump & use
; that here instead of raw assembly printing

; 10 bytes of data in this DW_FORM_block1 representation of the location of 'tls'
; CHECK: .byte  10{{ *}}# DW_AT_location
; DW_OP_const8u
; CHECK: .byte  14
; The debug relocation of the address of the tls variable
; CHECK: .quad  tls@dtprel+32768
; DW_OP_GNU_push_tls_address
; CHECK: .byte  224

@tls = thread_local global i32 7, align 4

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7, !8}

!0 = !{!"0x11\004\00clang version 3.4 \000\00\000\00\000", !1, !2, !2, !2, !3, !2} ; [ DW_TAG_compile_unit ] [/tmp/tls.cpp] [DW_LANG_C_plus_plus]
!1 = !{!"tls.cpp", !"/tmp"}
!2 = !{}
!3 = !{!4}
!4 = !{!"0x34\00tls\00tls\00\001\000\001", null, !5, !6, i32* @tls, null} ; [ DW_TAG_variable ] [tls] [line 1] [def]
!5 = !{!"0x29", !1}          ; [ DW_TAG_file_type ] [/tmp/tls.cpp]
!6 = !{!"0x24\00int\000\0032\0032\000\000\005", null, null} ; [ DW_TAG_base_type ] [int] [line 0, size 32, align 32, offset 0, enc DW_ATE_signed]
!7 = !{i32 2, !"Dwarf Version", i32 3}

!8 = !{i32 1, !"Debug Info Version", i32 2}
