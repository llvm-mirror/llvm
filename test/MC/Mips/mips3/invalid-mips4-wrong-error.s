# Instructions that are invalid and are correctly rejected but use the wrong
# error message at the moment.
#
# RUN: not llvm-mc %s -triple=mips-unknown-linux -show-encoding -mcpu=mips3 \
# RUN:     2>%t1
# RUN: FileCheck %s < %t1

	.set noat
        bc1fl     $fcc7,27          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
        bc1tl     $fcc7,27          # CHECK: :[[@LINE]]:{{[0-9]+}}: error: invalid operand for instruction
