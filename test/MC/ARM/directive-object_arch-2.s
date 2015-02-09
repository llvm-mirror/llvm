@ RUN: llvm-mc -triple armv7-eabi -filetype obj -o - %s \
@ RUN:   | llvm-readobj -arm-attributes | FileCheck %s

	.syntax unified

	.object_arch armv4
	.arch armv7

@ CHECK: FileAttributes {
@ CHECK:   Attribute {
@ CHECK:     Tag: 5
@ CHECK:     TagName: CPU_name
@ CHECK:     Value: 7
@ CHECK:   }
@ CHECK:   Attribute {
@ CHECK:     Tag: 6
@ CHEKC:     Value: 1
@ CHECK:     TagName: CPU_arch
@ CHECK:     Description: ARM v4
@ CHECK:   }
@ CHECK: }

