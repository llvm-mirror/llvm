@ RUN: llvm-mc %s -triple=armv7-linux-gnueabi | FileCheck -check-prefix=ASM %s
@ RUN: llvm-mc %s -triple=armv7-linux-gnueabi -filetype=obj -o - | \
@ RUN:    llvm-readobj -s -sd -sr | FileCheck -check-prefix=OBJ %s
	.syntax unified
	.text
	.globl	barf
	.align	2
	.type	barf,%function
barf:                                   @ @barf
@ BB#0:                                 @ %entry
	movw	r0, :lower16:GOT-(.LPC0_2+8)
	movt	r0, :upper16:GOT-(.LPC0_2+8)
.LPC0_2:
@ ASM:          movw    r0, :lower16:(GOT-(.LPC0_2+8))
@ ASM-NEXT:     movt    r0, :upper16:(GOT-(.LPC0_2+8))

@@ make sure that the text section fixups are sane too
@ OBJ:        Section {
@ OBJ:          Name: .text
@ OBJ-NEXT:     Type: SHT_PROGBITS
@ OBJ-NEXT:     Flags [ (0x6)
@ OBJ-NEXT:       SHF_ALLOC
@ OBJ-NEXT:       SHF_EXECINSTR
@ OBJ-NEXT:     ]
@ OBJ-NEXT:     Address: 0x0
@ OBJ-NEXT:     Offset: 0x34
@ OBJ-NEXT:     Size: 8
@ OBJ-NEXT:     Link: 0
@ OBJ-NEXT:     Info: 0
@ OBJ-NEXT:     AddressAlignment: 4
@ OBJ-NEXT:     EntrySize: 0
@ OBJ-NEXT:     Relocations [
@ OBJ-NEXT:     ]
@ OBJ-NEXT:     SectionData (
@ OBJ-NEXT:       0000: F00F0FE3 F40F4FE3
@ OBJ-NEXT:     )
@ OBJ-NEXT:   }
@ OBJ-NEXT:   Section {
@ OBJ-NEXT:     Index: 2
@ OBJ-NEXT:     Name: .rel.text (1)
@ OBJ-NEXT:     Type: SHT_REL (0x9)
@ OBJ-NEXT:     Flags [ (0x0)
@ OBJ-NEXT:     ]
@ OBJ-NEXT:     Address: 0x0
@ OBJ-NEXT:     Offset: 0x22C
@ OBJ-NEXT:     Size: 16
@ OBJ-NEXT:     Link: 6
@ OBJ-NEXT:     Info: 1
@ OBJ-NEXT:     AddressAlignment: 4
@ OBJ-NEXT:     EntrySize: 8
@ OBJ-NEXT:     Relocations [
@ OBJ-NEXT:       0x0 R_ARM_MOVW_PREL_NC
@ OBJ-NEXT:       0x4 R_ARM_MOVT_PREL
@ OBJ-NEXT:   ]
@ OBJ-NEXT:     SectionData (
@ OBJ-NEXT:       0000: 00000000 2D060000 04000000 2E060000  |....-...........|
@ OBJ-NEXT:     )
@ OBJ-NEXT:   }
