; RUN: llc -mtriple=powerpc64le-unknown-linux-gnu -mcpu=pwr8 < %s | FileCheck %s -check-prefix=PPC64-P8
; RUN: llc -mtriple=powerpc64le-unknown-linux-gnu -mcpu=pwr7 < %s | FileCheck %s -check-prefix=PPC64
; RUN: llc -mtriple=powerpc64-unknown-linux-gnu -mcpu=pwr8 < %s | FileCheck %s -check-prefix=PPC64-P8
; RUN: llc -mtriple=powerpc64-unknown-linux-gnu -mcpu=pwr7 < %s | FileCheck %s -check-prefix=PPC64
; RUN: llc -mtriple=powerpc-unknown-linux-gnu < %s | FileCheck %s -check-prefix=PPC32

define i128 @test_abs(ppc_fp128 %x) nounwind  {
entry:
; PPC64-LABEL: test_abs:
; PPC64-DAG: stxsdx 2, 0, [[ADDR_HI:[0-9]+]]
; PPC64-DAG: stxsdx 1, 0, [[ADDR_LO:[0-9]+]]
; PPC64-DAG: addi [[ADDR_HI]], [[SP:[0-9]+]], [[OFFSET_HI:-?[0-9]+]]
; PPC64-DAG: addi [[ADDR_LO]], [[SP]], [[OFFSET_LO:-?[0-9]+]]
; PPC64-DAG: li [[MASK_REG:[0-9]+]], 1
; PPC64: sldi [[MASK_REG]], [[MASK_REG]], 63
; PPC64-DAG: ld [[HI:[0-9]+]], [[OFFSET_LO]]([[SP]])
; PPC64-DAG: ld [[LO:[0-9]+]], [[OFFSET_HI]]([[SP]])
; PPC64: and [[FLIP_BIT:[0-9]+]], [[HI]], [[MASK_REG]]
; PPC64-DAG: xor 3, [[HI]], [[FLIP_BIT]]
; PPC64-DAG: xor 4, [[LO]], [[FLIP_BIT]]
; PPC64: blr

; PPC64-P8-LABEL: test_abs:
; PPC64-P8-DAG: mfvsrd [[LO:[0-9]+]], 2
; PPC64-P8-DAG: mfvsrd [[HI:[0-9]+]], 1
; PPC64-P8-DAG: li [[MASK_REG:[0-9]+]], 1
; PPC64-P8-DAG: sldi [[SHIFT_REG:[0-9]+]], [[MASK_REG]], 63
; PPC64-P8: and [[FLIP_BIT:[0-9]+]], [[HI]], [[SHIFT_REG]]
; PPC64-P8-DAG: xor 3, [[HI]], [[FLIP_BIT]]
; PPC64-P8-DAG: xor 4, [[LO]], [[FLIP_BIT]]
; PPC64-P8: blr

; PPC32-DAG: stfd 1, 24(1)
; PPC32-DAG: stfd 2, 16(1)
; PPC32: nop
; PPC32-DAG: lwz [[HI0:[0-9]+]], 24(1)
; PPC32-DAG: lwz [[LO0:[0-9]+]], 16(1)
; PPC32-DAG: lwz [[HI1:[0-9]+]], 28(1)
; PPC32-DAG: lwz [[LO1:[0-9]+]], 20(1)
; PPC32: rlwinm [[FLIP_BIT:[0-9]+]], [[HI0]], 0, 0, 0
; PPC32-DAG: xor [[HI0]], [[HI0]], [[FLIP_BIT]]
; PPC32-DAG: xor [[LO0]], [[LO0]], [[FLIP_BIT]]
; PPC32: blr
	%0 = tail call ppc_fp128 @llvm.fabs.ppcf128(ppc_fp128 %x)
	%1 = bitcast ppc_fp128 %0 to i128
	ret i128 %1
}

define i128 @test_neg(ppc_fp128 %x) nounwind  {
entry:
; PPC64-LABEL: test_neg:
; PPC64-DAG: stxsdx 2, 0, [[ADDR_HI:[0-9]+]]
; PPC64-DAG: stxsdx 1, 0, [[ADDR_LO:[0-9]+]]
; PPC64-DAG: addi [[ADDR_HI]], [[SP:[0-9]+]], [[OFFSET_HI:-?[0-9]+]]
; PPC64-DAG: addi [[ADDR_LO]], [[SP]], [[OFFSET_LO:-?[0-9]+]]
; PPC64-DAG: li [[FLIP_BIT:[0-9]+]], 1
; PPC64-DAG: sldi [[FLIP_BIT]], [[FLIP_BIT]], 63
; PPC64-DAG: ld [[HI:[0-9]+]], [[OFFSET_LO]]([[SP]])
; PPC64-DAG: ld [[LO:[0-9]+]], [[OFFSET_HI]]([[SP]])
; PPC64-NOT: BARRIER
; PPC64-DAG: xor 3, [[HI]], [[FLIP_BIT]]
; PPC64-DAG: xor 4, [[LO]], [[FLIP_BIT]]
; PPC64: blr

; PPC64-P8-LABEL: test_neg:
; PPC64-P8-DAG: mfvsrd [[LO:[0-9]+]], 2
; PPC64-P8-DAG: mfvsrd [[HI:[0-9]+]], 1
; PPC64-P8-DAG: li [[IMM1:[0-9]+]], 1
; PPC64-P8-DAG: sldi [[FLIP_BIT]], [[IMM1]], 63
; PPC64-P8-NOT: BARRIER
; PPC64-P8-DAG: xor 3, [[HI]], [[FLIP_BIT]]
; PPC64-P8-DAG: xor 4, [[LO]], [[FLIP_BIT]]
; PPC64-P8: blr

; PPC32-DAG: stfd 1, 24(1)
; PPC32-DAG: stfd 2, 16(1)
; PPC32: nop
; PPC32-DAG: lwz [[HI0:[0-9]+]], 24(1)
; PPC32-DAG: lwz [[LO0:[0-9]+]], 16(1)
; PPC32-DAG: lwz [[HI1:[0-9]+]], 28(1)
; PPC32-DAG: lwz [[LO1:[0-9]+]], 20(1)
; PPC32-NOT: BARRIER
; PPC32-DAG: xoris [[HI0]], [[HI0]], 32768
; PPC32-DAG: xoris [[LO0]], [[LO0]], 32768
; PPC32: blr
	%0 = fsub ppc_fp128 0xM80000000000000000000000000000000, %x
	%1 = bitcast ppc_fp128 %0 to i128
	ret i128 %1
}

define i128 @test_copysign(ppc_fp128 %x) nounwind  {
entry:
; PPC64-LABEL: test_copysign:
; PPC64-DAG: stxsdx 1, 0, [[ADDR_REG:[0-9]+]]
; PPC64-DAG: addi [[ADDR_REG]], 1, [[OFFSET:-?[0-9]+]]
; PPC64-DAG: li [[SIGN:[0-9]+]], 1
; PPC64-DAG: sldi [[SIGN]], [[SIGN]], 63
; PPC64-DAG: li [[HI_TMP:[0-9]+]], 16399
; PPC64-DAG: sldi [[CST_HI:[0-9]+]], [[HI_TMP]], 48
; PPC64-DAG: li [[LO_TMP:[0-9]+]], 3019
; PPC64-DAG: sldi [[CST_LO:[0-9]+]], [[LO_TMP]], 52
; PPC64-NOT: BARRIER
; PPC64-DAG: ld [[X_HI:[0-9]+]], [[OFFSET]](1)
; PPC64-DAG: and [[NEW_HI_TMP:[0-9]+]], [[X_HI]], [[SIGN]]
; PPC64-DAG: or 3, [[NEW_HI_TMP]], [[CST_HI]]
; PPC64-DAG: xor 4, [[SIGN]], [[CST_LO]]
; PPC64: blr

; PPC64-P8-LABEL: test_copysign:
; PPC64-P8-DAG: mfvsrd [[X_HI:[0-9]+]], 1
; PPC64-P8-DAG: li [[SIGN:[0-9]+]], 1
; PPC64-P8-DAG: sldi [[SIGN]], [[SIGN]], 63
; PPC64-P8-DAG: li [[HI_TMP:[0-9]+]], 16399
; PPC64-P8-DAG: sldi [[CST_HI:[0-9]+]], [[HI_TMP]], 48
; PPC64-P8-DAG: li [[LO_TMP:[0-9]+]], 3019
; PPC64-P8-DAG: sldi [[CST_LO:[0-9]+]], [[LO_TMP]], 52
; PPC64-P8-NOT: BARRIER
; PPC64-P8-DAG: and [[NEW_HI_TMP:[0-9]+]], [[X_HI]], [[SIGN]]
; PPC64-P8-DAG: or 3, [[NEW_HI_TMP]], [[CST_HI]]
; PPC64-P8-DAG: xor 4, [[NEW_HI_TMP]], [[CST_LO]]
; PPC64-P8: blr

; PPC32: stfd 1, [[STACK:[0-9]+]](1)
; PPC32: nop
; PPC32: lwz [[HI:[0-9]+]], [[STACK]](1)
; PPC32: rlwinm [[FLIP_BIT:[0-9]+]], [[HI]], 0, 0, 0
; PPC32-NOT: BARRIER
; PPC32-DAG: oris {{[0-9]+}}, [[FLIP_BIT]], 16399
; PPC32-DAG: xoris {{[0-9]+}}, [[FLIP_BIT]], 48304
; PPC32: blr
	%0 = tail call ppc_fp128 @llvm.copysign.ppcf128(ppc_fp128 0xMBCB0000000000000400F000000000000, ppc_fp128 %x)
	%1 = bitcast ppc_fp128 %0 to i128
	ret i128 %1
}

declare ppc_fp128 @llvm.fabs.ppcf128(ppc_fp128)
declare ppc_fp128 @llvm.copysign.ppcf128(ppc_fp128, ppc_fp128)
