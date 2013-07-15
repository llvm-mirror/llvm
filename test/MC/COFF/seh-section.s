// This test ensures that, if the section containing a function has a suffix
// (e.g. .text$foo), its unwind info section also has a suffix (.xdata$foo).
// RUN: llvm-mc -filetype=obj -triple x86_64-pc-win32 %s | llvm-readobj -s -sd | FileCheck %s

// CHECK:      Name: .xdata$foo
// CHECK-NEXT: VirtualSize
// CHECK-NEXT: VirtualAddress
// CHECK-NEXT: RawDataSize: 8
// CHECK-NEXT: PointerToRawData
// CHECK-NEXT: PointerToRelocations
// CHECK-NEXT: PointerToLineNumbers
// CHECK-NEXT: RelocationCount: 0
// CHECK-NEXT: LineNumberCount: 0
// CHECK-NEXT: Characteristics [
// CHECK-NEXT:   IMAGE_SCN_ALIGN_4BYTES
// CHECK-NEXT:   IMAGE_SCN_CNT_INITIALIZED_DATA
// CHECK-NEXT:   IMAGE_SCN_MEM_READ
// CHECK-NEXT: ]
// CHECK-NEXT: SectionData (
// CHECK-NEXT:   0000: 01050200 05500402
// CHECK-NEXT: )

    .section .text$foo,"x"
    .globl foo
    .def foo; .scl 2; .type 32; .endef
    .seh_proc foo
foo:
    subq $8, %rsp
    .seh_stackalloc 8
    pushq %rbp
    .seh_pushreg %rbp
    .seh_endprologue
    popq %rbp
    addq $8, %rsp
    ret
    .seh_endproc

