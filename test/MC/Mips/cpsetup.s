# RUN: llvm-mc -triple mips64-unknown-unknown -target-abi o32 -filetype=obj -o - %s | \
# RUN:   llvm-objdump -d -r -arch=mips64 - | \
# RUN:     FileCheck -check-prefix=ALL -check-prefix=O32 %s

# RUN: llvm-mc -triple mips64-unknown-unknown -target-abi o32 %s | \
# RUN:   FileCheck -check-prefix=ALL -check-prefix=ASM %s

# RUN: llvm-mc -triple mips64-unknown-unknown -target-abi n32 -filetype=obj -o - %s | \
# RUN:   llvm-objdump -d -r -t -arch=mips64 - | \
# RUN:     FileCheck -check-prefix=ALL -check-prefix=NXX -check-prefix=N32 %s

# RUN: llvm-mc -triple mips64-unknown-unknown -target-abi n32 %s | \
# RUN:   FileCheck -check-prefix=ALL -check-prefix=ASM %s

# RUN: llvm-mc -triple mips64-unknown-unknown %s -filetype=obj -o - | \
# RUN:   llvm-objdump -d -r -t -arch=mips64 - | \
# RUN:     FileCheck -check-prefix=ALL -check-prefix=NXX -check-prefix=N64 %s

# RUN: llvm-mc -triple mips64-unknown-unknown %s | \
# RUN:   FileCheck -check-prefix=ALL -check-prefix=ASM %s

        .text
        .option pic2
t1:
        .cpsetup $25, 8, __cerror
        nop
        .cpreturn
        nop

# ALL-LABEL: t1:

# O32-NOT: __cerror

# FIXME: Direct object emission for N32 is still under development.
# N32 doesn't allow 3 operations to be specified in the same relocation
# record like N64 does.

# NXX-NEXT: sd       $gp, 8($sp)
# NXX-NEXT: lui      $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_HI16  __cerror
# NXX-NEXT: addiu    $gp, $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_LO16  __cerror
# N32-NEXT: addu     $gp, $gp, $25
# N64-NEXT: daddu    $gp, $gp, $25

# ASM-NEXT: .cpsetup $25, 8, __cerror

# ALL-NEXT: nop

# ASM-NEXT: .cpreturn
# NXX-NEXT: ld $gp, 8($sp)

# ALL-NEXT: nop

t2:
        .cpsetup $25, $2, __cerror
        nop
        .cpreturn
        nop

# ALL-LABEL: t2:

# O32-NOT: __cerror

# FIXME: Direct object emission for N32 is still under development.
# N32 doesn't allow 3 operations to be specified in the same relocation
# record like N64 does.

# NXX-NEXT: move     $2, $gp
# NXX-NEXT: lui      $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_HI16  __cerror
# NXX-NEXT: addiu    $gp, $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_LO16  __cerror
# N32-NEXT: addu     $gp, $gp, $25
# N64-NEXT: daddu    $gp, $gp, $25

# ASM-NEXT: .cpsetup $25, $2, __cerror

# ALL-NEXT: nop

# ASM-NEXT: .cpreturn
# NXX-NEXT: move $gp, $2

# ALL-NEXT: nop

# .cpsetup with local labels (PR22518):

# The '1:' label isn't emitted in all cases but we still want a label to match
# so we force one here.

t3:
        nop
1:
        .cpsetup $25, $2, 1b
        nop
        sub $3, $3, $2

# ALL-LABEL: t3:
# ALL-NEXT:  nop

# O32-NEXT:   nop
# O32-NEXT:   sub $3, $3, $2

# FIXME: Direct object emission for N32 is still under development.
# N32 doesn't allow 3 operations to be specified in the same relocation
# record like N64 does.

# NXX: $tmp0:
# NXX-NEXT: move     $2, $gp
# NXX-NEXT: lui      $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_HI16  $tmp0
# NXX-NEXT: addiu    $gp, $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_LO16  $tmp0
# N32-NEXT: addu     $gp, $gp, $25
# N64-NEXT: daddu    $gp, $gp, $25
# NXX-NEXT: nop
# NXX-NEXT: sub $3, $3, $2

# ASM: $tmp0:
# ASM-NEXT: .cpsetup $25, $2, $tmp0

# Ensure we have at least one instruction between labels so that the labels
# we're matching aren't removed.
        nop
# ALL-NEXT: nop

        .option pic0
t4:
        nop
        .cpsetup $25, 8, __cerror
        nop
        .cpreturn
        nop

# Testing that .cpsetup expands to nothing in this case
# by checking that the next instruction after the first
# nop is also a 'nop'.

# ALL-LABEL: t4:

# NXX-NEXT: nop
# NXX-NEXT: nop
# NXX-NEXT: nop

# ASM-NEXT: nop
# ASM-NEXT: .cpsetup $25, 8, __cerror
# ASM-NEXT: nop
# ASM-NEXT: .cpreturn
# ASM-NEXT: nop

# Test that we accept constant expressions.
        .option pic2
t5:
        .cpsetup $25, ((8*4) - (3*8)), __cerror
        nop

# ALL-LABEL: t5:

# O32-NOT: __cerror

# FIXME: Direct object emission for N32 is still under development.
# N32 doesn't allow 3 operations to be specified in the same relocation
# record like N64 does.

# NXX-NEXT: sd       $gp, 8($sp)
# NXX-NEXT: lui      $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_HI16  __cerror
# NXX-NEXT: addiu    $gp, $gp, 0
# NXX-NEXT: R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_LO16  __cerror
# N32-NEXT: addu     $gp, $gp, $25
# N64-NEXT: daddu    $gp, $gp, $25

# ASM-NEXT: .cpsetup $25, 8, __cerror

# ALL-NEXT: nop

# NXX-LABEL: SYMBOL TABLE:

# For .cpsetup with local labels, we need to check if $tmp0 is in the symbol
# table:
# NXX: .text  00000000 $tmp0
