@@ test st_value bit 0 of thumb function
@ RUN: llvm-mc %s -triple=thumbv7-linux-gnueabi -filetype=obj -o - | \
@ RUN: llvm-readobj -t | FileCheck %s
	.syntax unified
	.text
	.globl	foo
	.align	2
	.type	foo,%function
	.code	16
	.thumb_func
foo:
	bx	lr

@@ make sure foo is thumb function: bit 0 = 1 (st_value)
@CHECK:        Symbol {
@CHECK:          Name: foo
@CHECK-NEXT:     Value: 0x1
@CHECK-NEXT:     Size: 0
@CHECK-NEXT:     Binding: Global
@CHECK-NEXT:     Type: Function
