@ RUN: not llvm-mc -triple=thumbv6-apple-darwin -o /dev/null < %s 2>&1 \
@ RUN:     | FileCheck --check-prefix=CHECK-ERRORS %s
@ RUN: not llvm-mc -triple=thumbv5-apple-darwin -o /dev/null < %s 2>&1 \
@ RUN:     | FileCheck --check-prefix=CHECK-ERRORS-V5 %s
@ RUN: not llvm-mc -triple=thumbv7m -o /dev/null < %s 2>&1 \
@ RUN:     | FileCheck --check-prefix=CHECK-ERRORS-V7M %s
@ RUN: not llvm-mc -triple=thumbv8 -o /dev/null < %s 2>&1 \
@ RUN:     | FileCheck --check-prefix=CHECK-ERRORS-V8 %s

@ Check for various assembly diagnostic messages on invalid input.

@ ADD instruction w/o 'S' suffix.
        add r1, r2, r3
@ CHECK-ERRORS: error: invalid instruction
@ CHECK-ERRORS:         add r1, r2, r3
@ CHECK-ERRORS:         ^

@ Instructions which require v6+ for both registers to be low regs.
        add r2, r3
        mov r2, r3
@ CHECK-ERRORS: error: instruction variant requires Thumb2
@ CHECK-ERRORS:         add r2, r3
@ CHECK-ERRORS:         ^
@ CHECK-ERRORS-V5: error: instruction variant requires ARMv6 or later
@ CHECK-ERRORS-V5:         mov r2, r3
@ CHECK-ERRORS-V5:         ^


@ Out of range immediates for ASR instruction.
        asrs r2, r3, #33
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS:         asrs r2, r3, #33
@ CHECK-ERRORS:                      ^

@ Out of range immediates for BKPT instruction.
        bkpt #256
        bkpt #-1
error: invalid operand for instruction
        bkpt #256
             ^
error: invalid operand for instruction
        bkpt #-1
             ^

@ Out of range immediates for v8 HLT instruction.
        hlt #64
        hlt #-1
@CHECK-ERRORS: error: instruction requires: armv8 arm-mode
@CHECK-ERRORS:        hlt #64
@CHECK-ERRORS:        ^
@CHECK-ERRORS-V8: error: instruction requires: arm-mode
@CHECK-ERRORS-V8:         hlt #64
@CHECK-ERRORS-V8:              ^
@CHECK-ERRORS: error: invalid operand for instruction
@CHECK-ERRORS:         hlt #-1
@CHECK-ERRORS:              ^

@ Invalid writeback and register lists for LDM
        ldm r2!, {r5, r8}
        ldm r2, {r5, r7}
        ldm r2!, {r2, r3, r4}
        ldm r2!, {r2, r3, r4, r10}
        ldmdb r2!, {r2, r3, r4}
        ldm r0, {r2, sp}
        ldmia r0, {r2-r3, sp}
        ldmia r0!, {r2-r3, sp}
        ldmfd r2, {r1, r3-r6, sp}
        ldmfd r2!, {r1, r3-r6, sp}
        ldmdb r1, {r2, r3, sp}
        ldmdb r1!, {r2, r3, sp} 
@ CHECK-ERRORS: error: registers must be in range r0-r7
@ CHECK-ERRORS:         ldm r2!, {r5, r8}
@ CHECK-ERRORS:                  ^
@ CHECK-ERRORS: error: writeback operator '!' expected
@ CHECK-ERRORS:         ldm r2, {r5, r7}
@ CHECK-ERRORS:             ^
@ CHECK-ERRORS: error: writeback operator '!' not allowed when base register in register list
@ CHECK-ERRORS:         ldm r2!, {r2, r3, r4}
@ CHECK-ERRORS:               ^
@ CHECK-ERRORS-V8: error: writeback operator '!' not allowed when base register in register list
@ CHECK-ERRORS-V8:         ldm r2!, {r2, r3, r4, r10}
@ CHECK-ERRORS-V8:               ^
@ CHECK-ERRORS-V8: error: writeback register not allowed in register list
@ CHECK-ERRORS-V8:         ldmdb r2!, {r2, r3, r4}
@ CHECK-ERRORS-V8:                 ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldm r0, {r2, sp}
@ CHECK-ERRORS-V7M:                 ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmia r0, {r2-r3, sp}
@ CHECK-ERRORS-V7M:                   ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmia r0!, {r2-r3, sp}
@ CHECK-ERRORS-V7M:                    ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmfd r2, {r1, r3-r6, sp}
@ CHECK-ERRORS-V7M:                   ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmfd r2!, {r1, r3-r6, sp}
@ CHECK-ERRORS-V7M:                    ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmdb r1, {r2, r3, sp}
@ CHECK-ERRORS-V7M:                   ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         ldmdb r1!, {r2, r3, sp}
@ CHECK-ERRORS-V7M:                    ^

@ Invalid writeback and register lists for PUSH/POP
        pop {r1, r2, r10}
        push {r8, r9}
@ CHECK-ERRORS: error: registers must be in range r0-r7 or pc
@ CHECK-ERRORS:         pop {r1, r2, r10}
@ CHECK-ERRORS:             ^
@ CHECK-ERRORS: error: registers must be in range r0-r7 or lr
@ CHECK-ERRORS:         push {r8, r9}
@ CHECK-ERRORS:              ^


@ Invalid writeback and register lists for STM
        stm r1, {r2, r6}
        stm r1!, {r2, r9}
        stm r2!, {r2, r9}
        stmdb r2!, {r0, r2}
        stm r1!, {r2, sp}
        stmia r4!, {r0-r3, sp}
        stmdb r1, {r2, r3, sp}
        stmdb r1!, {r2, r3, sp}
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         stm r1, {r2, r6}
@ CHECK-ERRORS:         ^
@ CHECK-ERRORS: error: registers must be in range r0-r7
@ CHECK-ERRORS:         stm r1!, {r2, r9}
@ CHECK-ERRORS:                  ^
@ CHECK-ERRORS-V8: error: writeback operator '!' not allowed when base register in register list
@ CHECK-ERRORS-V8:         stm r2!, {r2, r9}
@ CHECK-ERRORS-V8:                  ^
@ CHECK-ERRORS-V8: error: writeback register not allowed in register list
@ CHECK-ERRORS-V8:         stmdb r2!, {r0, r2}
@ CHECK-ERRORS-V8:                  ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         stm r1!, {r2, sp}
@ CHECK-ERRORS-V7M:                  ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         stmia r4!, {r0-r3, sp}
@ CHECK-ERRORS-V7M:                    ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         stmdb r1, {r2, r3, sp}
@ CHECK-ERRORS-V7M:                   ^
@ CHECK-ERRORS-V7M: error: SP may not be in the register list
@ CHECK-ERRORS-V7M:         stmdb r1!, {r2, r3, sp}
@ CHECK-ERRORS-V7M:                    ^

@ Out of range immediates for LSL instruction.
        lsls r4, r5, #-1
        lsls r4, r5, #32
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS:         lsls r4, r5, #-1
@ CHECK-ERRORS:                      ^
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS:         lsls r4, r5, #32
@ CHECK-ERRORS:                      ^

@ Mismatched source/destination operands for MUL instruction.
        muls r1, r2, r3
@ CHECK-ERRORS: error: destination register must match source register
@ CHECK-ERRORS:         muls r1, r2, r3
@ CHECK-ERRORS:              ^


@ Out of range immediates for STR instruction.
        str r2, [r7, #-1]
        str r5, [r1, #3]
        str r3, [r7, #128]
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         str r2, [r7, #-1]
@ CHECK-ERRORS:         ^
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         str r5, [r1, #3]
@ CHECK-ERRORS:         ^
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         str r3, [r7, #128]
@ CHECK-ERRORS:         ^

@ Out of range immediate for SVC instruction.
        svc #-1
        svc #256
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS:         svc #-1
@ CHECK-ERRORS:             ^
@ CHECK-ERRORS: error: instruction requires: arm-mode
@ CHECK-ERRORS:         svc #256
@ CHECK-ERRORS:         ^


@ Out of range immediate for ADD SP instructions
        add sp, #-1
        add sp, #3
        add sp, sp, #512
        add r2, sp, #1024
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         add sp, #-1
@ CHECK-ERRORS:                 ^
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         add sp, #3
@ CHECK-ERRORS:                 ^
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         add sp, sp, #512
@ CHECK-ERRORS:                     ^
@ CHECK-ERRORS: error: instruction requires: thumb2
@ CHECK-ERRORS:         add r2, sp, #1024
@ CHECK-ERRORS:         ^

        add r2, sp, ip
@ CHECK-ERRORS: error: source register must be the same as destination
@ CHECK-ERRORS:         add r2, sp, ip
@ CHECK-ERRORS:                     ^


@------------------------------------------------------------------------------
@ B/Bcc - out of range immediates for Thumb1 branches
@------------------------------------------------------------------------------

        beq    #-258
        bne    #256
        bgt    #13
        b      #-1048578
        b      #1048576
        b      #10323

@ CHECK-ERRORS: error: branch target out of range
@ CHECK-ERRORS: error: branch target out of range
@ CHECK-ERRORS: error: branch target out of range
@ CHECK-ERRORS: error: branch target out of range
@ CHECK-ERRORS: error: branch target out of range
@ CHECK-ERRORS: error: branch target out of range

@------------------------------------------------------------------------------
@ WFE/WFI/YIELD - are not supported pre v6T2
@------------------------------------------------------------------------------
        wfe
        wfi
        yield

@ CHECK-ERRORS: error: instruction requires: armv6m or armv6t2
@ CHECK-ERRORS: wfe
@ CHECK-ERRORS: ^
@ CHECK-ERRORS: error: instruction requires: armv6m or armv6t2
@ CHECK-ERRORS: wfi
@ CHECK-ERRORS: ^
@ CHECK-ERRORS: error: instruction requires: armv6m or armv6t2
@ CHECK-ERRORS: yield
@ CHECK-ERRORS: ^

@------------------------------------------------------------------------------
@ PLDW required mp-extensions
@------------------------------------------------------------------------------
        pldw [r0, #4]
@ CHECK-ERRORS: error: instruction requires: mp-extensions

@------------------------------------------------------------------------------
@ LDR(lit) - invalid offsets
@------------------------------------------------------------------------------

        ldr r4, [pc, #-12]
@ CHECK-ERRORS: error: instruction requires: thumb2

@------------------------------------------------------------------------------
@ STC2{L}/LDC2{L} - requires thumb2
@------------------------------------------------------------------------------
        stc2 p0, c8, [r1, #4]
        stc2l p6, c2, [r7, #4]
        ldc2 p0, c8, [r1, #4]
        ldc2l p6, c2, [r7, #4]
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS: error: invalid operand for instruction
@ CHECK-ERRORS: error: invalid operand for instruction
