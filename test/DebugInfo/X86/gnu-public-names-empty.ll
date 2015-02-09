; RUN: llc -mtriple=x86_64-pc-linux-gnu -generate-gnu-dwarf-pub-sections -filetype=obj < %s | llvm-dwarfdump - | FileCheck %s

; Generated from:

; static int a __attribute__((section("a")));

; Check that the attributes in the compile unit both point to a correct
; location, even when nothing is exported.
; CHECK: DW_AT_GNU_pubnames [DW_FORM_flag_present]   (true)
; CHECK-NOT: DW_AT_GNU_pubtypes [

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}

!0 = !{!"0x11\0012\00clang version 3.4 (trunk 191846) (llvm/trunk 191866)\000\00\000\00\000", !1, !2, !2, !2, !2, !2} ; [ DW_TAG_compile_unit ] [/usr/local/google/home/echristo/tmp/foo.c] [DW_LANG_C99]
!1 = !{!"foo.c", !"/usr/local/google/home/echristo/tmp"}
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 1, !"Debug Info Version", i32 2}
