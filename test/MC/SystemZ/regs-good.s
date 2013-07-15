# RUN: llvm-mc -triple s390x-linux-gnu -show-encoding %s | FileCheck %s

#CHECK: lr	%r0, %r1                # encoding: [0x18,0x01]
#CHECK: lr	%r2, %r3                # encoding: [0x18,0x23]
#CHECK: lr	%r4, %r5                # encoding: [0x18,0x45]
#CHECK: lr	%r6, %r7                # encoding: [0x18,0x67]
#CHECK: lr	%r8, %r9                # encoding: [0x18,0x89]
#CHECK: lr	%r10, %r11              # encoding: [0x18,0xab]
#CHECK: lr	%r12, %r13              # encoding: [0x18,0xcd]
#CHECK: lr	%r14, %r15              # encoding: [0x18,0xef]

	lr	%r0,%r1
	lr	%r2,%r3
	lr	%r4,%r5
	lr	%r6,%r7
	lr	%r8,%r9
	lr	%r10,%r11
	lr	%r12,%r13
	lr	%r14,%r15

#CHECK: lgr	%r0, %r1                # encoding: [0xb9,0x04,0x00,0x01]
#CHECK: lgr	%r2, %r3                # encoding: [0xb9,0x04,0x00,0x23]
#CHECK: lgr	%r4, %r5                # encoding: [0xb9,0x04,0x00,0x45]
#CHECK: lgr	%r6, %r7                # encoding: [0xb9,0x04,0x00,0x67]
#CHECK: lgr	%r8, %r9                # encoding: [0xb9,0x04,0x00,0x89]
#CHECK: lgr	%r10, %r11              # encoding: [0xb9,0x04,0x00,0xab]
#CHECK: lgr	%r12, %r13              # encoding: [0xb9,0x04,0x00,0xcd]
#CHECK: lgr	%r14, %r15              # encoding: [0xb9,0x04,0x00,0xef]

	lgr	%r0,%r1
	lgr	%r2,%r3
	lgr	%r4,%r5
	lgr	%r6,%r7
	lgr	%r8,%r9
	lgr	%r10,%r11
	lgr	%r12,%r13
	lgr	%r14,%r15

#CHECK: dlr	%r0, %r0                # encoding: [0xb9,0x97,0x00,0x00]
#CHECK: dlr	%r2, %r0                # encoding: [0xb9,0x97,0x00,0x20]
#CHECK: dlr	%r4, %r0                # encoding: [0xb9,0x97,0x00,0x40]
#CHECK: dlr	%r6, %r0                # encoding: [0xb9,0x97,0x00,0x60]
#CHECK: dlr	%r8, %r0                # encoding: [0xb9,0x97,0x00,0x80]
#CHECK: dlr	%r10, %r0               # encoding: [0xb9,0x97,0x00,0xa0]
#CHECK: dlr	%r12, %r0               # encoding: [0xb9,0x97,0x00,0xc0]
#CHECK: dlr	%r14, %r0               # encoding: [0xb9,0x97,0x00,0xe0]

	dlr	%r0,%r0
	dlr	%r2,%r0
	dlr	%r4,%r0
	dlr	%r6,%r0
	dlr	%r8,%r0
	dlr	%r10,%r0
	dlr	%r12,%r0
	dlr	%r14,%r0

#CHECK: ler	%f0, %f1                # encoding: [0x38,0x01]
#CHECK: ler	%f2, %f3                # encoding: [0x38,0x23]
#CHECK: ler	%f4, %f5                # encoding: [0x38,0x45]
#CHECK: ler	%f6, %f7                # encoding: [0x38,0x67]
#CHECK: ler	%f8, %f9                # encoding: [0x38,0x89]
#CHECK: ler	%f10, %f11              # encoding: [0x38,0xab]
#CHECK: ler	%f12, %f13              # encoding: [0x38,0xcd]
#CHECK: ler	%f14, %f15              # encoding: [0x38,0xef]

	ler	%f0,%f1
	ler	%f2,%f3
	ler	%f4,%f5
	ler	%f6,%f7
	ler	%f8,%f9
	ler	%f10,%f11
	ler	%f12,%f13
	ler	%f14,%f15

#CHECK: ldr	%f0, %f1                # encoding: [0x28,0x01]
#CHECK: ldr	%f2, %f3                # encoding: [0x28,0x23]
#CHECK: ldr	%f4, %f5                # encoding: [0x28,0x45]
#CHECK: ldr	%f6, %f7                # encoding: [0x28,0x67]
#CHECK: ldr	%f8, %f9                # encoding: [0x28,0x89]
#CHECK: ldr	%f10, %f11              # encoding: [0x28,0xab]
#CHECK: ldr	%f12, %f13              # encoding: [0x28,0xcd]
#CHECK: ldr	%f14, %f15              # encoding: [0x28,0xef]

	ldr	%f0,%f1
	ldr	%f2,%f3
	ldr	%f4,%f5
	ldr	%f6,%f7
	ldr	%f8,%f9
	ldr	%f10,%f11
	ldr	%f12,%f13
	ldr	%f14,%f15

#CHECK: lxr	%f0, %f1                # encoding: [0xb3,0x65,0x00,0x01]
#CHECK: lxr	%f4, %f5                # encoding: [0xb3,0x65,0x00,0x45]
#CHECK: lxr	%f8, %f9                # encoding: [0xb3,0x65,0x00,0x89]
#CHECK: lxr	%f12, %f13              # encoding: [0xb3,0x65,0x00,0xcd]

	lxr	%f0,%f1
	lxr	%f4,%f5
	lxr	%f8,%f9
	lxr	%f12,%f13

#CHECK: .cfi_offset %r0, 0
#CHECK: .cfi_offset %r1, 8
#CHECK: .cfi_offset %r2, 16
#CHECK: .cfi_offset %r3, 24
#CHECK: .cfi_offset %r4, 32
#CHECK: .cfi_offset %r5, 40
#CHECK: .cfi_offset %r6, 48
#CHECK: .cfi_offset %r7, 56
#CHECK: .cfi_offset %r8, 64
#CHECK: .cfi_offset %r9, 72
#CHECK: .cfi_offset %r10, 80
#CHECK: .cfi_offset %r11, 88
#CHECK: .cfi_offset %r12, 96
#CHECK: .cfi_offset %r13, 104
#CHECK: .cfi_offset %r14, 112
#CHECK: .cfi_offset %r15, 120
#CHECK: .cfi_offset %f0, 128
#CHECK: .cfi_offset %f1, 136
#CHECK: .cfi_offset %f2, 144
#CHECK: .cfi_offset %f3, 152
#CHECK: .cfi_offset %f4, 160
#CHECK: .cfi_offset %f5, 168
#CHECK: .cfi_offset %f6, 176
#CHECK: .cfi_offset %f7, 184
#CHECK: .cfi_offset %f8, 192
#CHECK: .cfi_offset %f9, 200
#CHECK: .cfi_offset %f10, 208
#CHECK: .cfi_offset %f11, 216
#CHECK: .cfi_offset %f12, 224
#CHECK: .cfi_offset %f13, 232
#CHECK: .cfi_offset %f14, 240
#CHECK: .cfi_offset %f15, 248

	.cfi_startproc
	.cfi_offset %r0,0
	.cfi_offset %r1,8
	.cfi_offset %r2,16
	.cfi_offset %r3,24
	.cfi_offset %r4,32
	.cfi_offset %r5,40
	.cfi_offset %r6,48
	.cfi_offset %r7,56
	.cfi_offset %r8,64
	.cfi_offset %r9,72
	.cfi_offset %r10,80
	.cfi_offset %r11,88
	.cfi_offset %r12,96
	.cfi_offset %r13,104
	.cfi_offset %r14,112
	.cfi_offset %r15,120
	.cfi_offset %f0,128
	.cfi_offset %f1,136
	.cfi_offset %f2,144
	.cfi_offset %f3,152
	.cfi_offset %f4,160
	.cfi_offset %f5,168
	.cfi_offset %f6,176
	.cfi_offset %f7,184
	.cfi_offset %f8,192
	.cfi_offset %f9,200
	.cfi_offset %f10,208
	.cfi_offset %f11,216
	.cfi_offset %f12,224
	.cfi_offset %f13,232
	.cfi_offset %f14,240
	.cfi_offset %f15,248
	.cfi_endproc
