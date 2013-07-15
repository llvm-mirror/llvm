# RUN: not llvm-mc -triple s390x-linux-gnu < %s 2> %t
# RUN: FileCheck < %t %s

#CHECK: error: invalid operand
#CHECK: a	%r0, -1
#CHECK: error: invalid operand
#CHECK: a	%r0, 4096

	a	%r0, -1
	a	%r0, 4096

#CHECK: error: invalid operand
#CHECK: adb	%f0, -1
#CHECK: error: invalid operand
#CHECK: adb	%f0, 4096

	adb	%f0, -1
	adb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: aeb	%f0, -1
#CHECK: error: invalid operand
#CHECK: aeb	%f0, 4096

	aeb	%f0, -1
	aeb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: afi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: afi	%r0, (1 << 31)

	afi	%r0, (-1 << 31) - 1
	afi	%r0, (1 << 31)

#CHECK: error: invalid operand
#CHECK: ag	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ag	%r0, 524288

	ag	%r0, -524289
	ag	%r0, 524288

#CHECK: error: invalid operand
#CHECK: agf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: agf	%r0, 524288

	agf	%r0, -524289
	agf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: agfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: agfi	%r0, (1 << 31)

	agfi	%r0, (-1 << 31) - 1
	agfi	%r0, (1 << 31)

#CHECK: error: invalid operand
#CHECK: aghi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: aghi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: aghi	%r0, foo

	aghi	%r0, -32769
	aghi	%r0, 32768
	aghi	%r0, foo

#CHECK: error: invalid operand
#CHECK: agsi	-524289, 0
#CHECK: error: invalid operand
#CHECK: agsi	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: agsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: agsi	0, -129
#CHECK: error: invalid operand
#CHECK: agsi	0, 128

	agsi	-524289, 0
	agsi	524288, 0
	agsi	0(%r1,%r2), 0
	agsi	0, -129
	agsi	0, 128

#CHECK: error: invalid operand
#CHECK: ah	%r0, -1
#CHECK: error: invalid operand
#CHECK: ah	%r0, 4096

	ah	%r0, -1
	ah	%r0, 4096

#CHECK: error: invalid operand
#CHECK: ahi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: ahi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: ahi	%r0, foo

	ahi	%r0, -32769
	ahi	%r0, 32768
	ahi	%r0, foo

#CHECK: error: invalid operand
#CHECK: ahy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ahy	%r0, 524288

	ahy	%r0, -524289
	ahy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: al	%r0, -1
#CHECK: error: invalid operand
#CHECK: al	%r0, 4096

	al	%r0, -1
	al	%r0, 4096

#CHECK: error: invalid operand
#CHECK: alc	%r0, -524289
#CHECK: error: invalid operand
#CHECK: alc	%r0, 524288

	alc	%r0, -524289
	alc	%r0, 524288

#CHECK: error: invalid operand
#CHECK: alcg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: alcg	%r0, 524288

	alcg	%r0, -524289
	alcg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: alfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: alfi	%r0, (1 << 32)

	alfi	%r0, -1
	alfi	%r0, (1 << 32)

#CHECK: error: invalid operand
#CHECK: alg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: alg	%r0, 524288

	alg	%r0, -524289
	alg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: algf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: algf	%r0, 524288

	algf	%r0, -524289
	algf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: algfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: algfi	%r0, (1 << 32)

	algfi	%r0, -1
	algfi	%r0, (1 << 32)

#CHECK: error: invalid operand
#CHECK: aly	%r0, -524289
#CHECK: error: invalid operand
#CHECK: aly	%r0, 524288

	aly	%r0, -524289
	aly	%r0, 524288

#CHECK: error: invalid operand
#CHECK: asi	-524289, 0
#CHECK: error: invalid operand
#CHECK: asi	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: asi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: asi	0, -129
#CHECK: error: invalid operand
#CHECK: asi	0, 128

	asi	-524289, 0
	asi	524288, 0
	asi	0(%r1,%r2), 0
	asi	0, -129
	asi	0, 128

#CHECK: error: invalid register pair
#CHECK: axbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: axbr	%f2, %f0

	axbr	%f0, %f2
	axbr	%f2, %f0


#CHECK: error: invalid operand
#CHECK: ay	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ay	%r0, 524288

	ay	%r0, -524289
	ay	%r0, 524288

#CHECK: error: offset out of range
#CHECK: bras	%r0, -0x100002
#CHECK: error: offset out of range
#CHECK: bras	%r0, -1
#CHECK: error: offset out of range
#CHECK: bras	%r0, 1
#CHECK: error: offset out of range
#CHECK: bras	%r0, 0x10000

	bras	%r0, -0x100002
	bras	%r0, -1
	bras	%r0, 1
	bras	%r0, 0x10000

#CHECK: error: offset out of range
#CHECK: brasl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: brasl	%r0, -1
#CHECK: error: offset out of range
#CHECK: brasl	%r0, 1
#CHECK: error: offset out of range
#CHECK: brasl	%r0, 0x100000000

	brasl	%r0, -0x1000000002
	brasl	%r0, -1
	brasl	%r0, 1
	brasl	%r0, 0x100000000

#CHECK: error: offset out of range
#CHECK: brc	0, -0x100002
#CHECK: error: offset out of range
#CHECK: brc	0, -1
#CHECK: error: offset out of range
#CHECK: brc	0, 1
#CHECK: error: offset out of range
#CHECK: brc	0, 0x10000

	brc	0, -0x100002
	brc	0, -1
	brc	0, 1
	brc	0, 0x10000

#CHECK: error: invalid operand
#CHECK: brc	foo, bar
#CHECK: error: invalid operand
#CHECK: brc	-1, bar
#CHECK: error: invalid operand
#CHECK: brc	16, bar

	brc	foo, bar
	brc	-1, bar
	brc	16, bar

#CHECK: error: offset out of range
#CHECK: brcl	0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: brcl	0, -1
#CHECK: error: offset out of range
#CHECK: brcl	0, 1
#CHECK: error: offset out of range
#CHECK: brcl	0, 0x100000000

	brcl	0, -0x1000000002
	brcl	0, -1
	brcl	0, 1
	brcl	0, 0x100000000

#CHECK: error: invalid operand
#CHECK: brcl	foo, bar
#CHECK: error: invalid operand
#CHECK: brcl	-1, bar
#CHECK: error: invalid operand
#CHECK: brcl	16, bar

	brcl	foo, bar
	brcl	-1, bar
	brcl	16, bar

#CHECK: error: invalid operand
#CHECK: c	%r0, -1
#CHECK: error: invalid operand
#CHECK: c	%r0, 4096

	c	%r0, -1
	c	%r0, 4096

#CHECK: error: invalid operand
#CHECK: cdb	%f0, -1
#CHECK: error: invalid operand
#CHECK: cdb	%f0, 4096

	cdb	%f0, -1
	cdb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: ceb	%f0, -1
#CHECK: error: invalid operand
#CHECK: ceb	%f0, 4096

	ceb	%f0, -1
	ceb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: cfdbr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cfdbr	%r0, 16, %f0

	cfdbr	%r0, -1, %f0
	cfdbr	%r0, 16, %f0

#CHECK: error: invalid operand
#CHECK: cfebr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cfebr	%r0, 16, %f0

	cfebr	%r0, -1, %f0
	cfebr	%r0, 16, %f0

#CHECK: error: invalid operand
#CHECK: cfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: cfi	%r0, (1 << 31)

	cfi	%r0, (-1 << 31) - 1
	cfi	%r0, (1 << 31)

#CHECK: error: invalid operand
#CHECK: cfxbr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cfxbr	%r0, 16, %f0
#CHECK: error: invalid register pair
#CHECK: cfxbr	%r0, 0, %f2

	cfxbr	%r0, -1, %f0
	cfxbr	%r0, 16, %f0
	cfxbr	%r0, 0, %f2


#CHECK: error: invalid operand
#CHECK: cg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: cg	%r0, 524288

	cg	%r0, -524289
	cg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: cgdbr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cgdbr	%r0, 16, %f0

	cgdbr	%r0, -1, %f0
	cgdbr	%r0, 16, %f0

#CHECK: error: invalid operand
#CHECK: cgebr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cgebr	%r0, 16, %f0

	cgebr	%r0, -1, %f0
	cgebr	%r0, 16, %f0

#CHECK: error: invalid operand
#CHECK: cgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: cgf	%r0, 524288

	cgf	%r0, -524289
	cgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: cgfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: cgfi	%r0, (1 << 31)

	cgfi	%r0, (-1 << 31) - 1
	cgfi	%r0, (1 << 31)

#CHECK: error: offset out of range
#CHECK: cgfrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: cgfrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: cgfrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: cgfrl	%r0, 0x100000000

	cgfrl	%r0, -0x1000000002
	cgfrl	%r0, -1
	cgfrl	%r0, 1
	cgfrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cgh	%r0, -524289
#CHECK: error: invalid operand
#CHECK: cgh	%r0, 524288

	cgh	%r0, -524289
	cgh	%r0, 524288

#CHECK: error: invalid operand
#CHECK: cghi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: cghi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: cghi	%r0, foo

	cghi	%r0, -32769
	cghi	%r0, 32768
	cghi	%r0, foo

#CHECK: error: offset out of range
#CHECK: cghrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: cghrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: cghrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: cghrl	%r0, 0x100000000

	cghrl	%r0, -0x1000000002
	cghrl	%r0, -1
	cghrl	%r0, 1
	cghrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cghsi	-1, 0
#CHECK: error: invalid operand
#CHECK: cghsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: cghsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: cghsi	0, -32769
#CHECK: error: invalid operand
#CHECK: cghsi	0, 32768

	cghsi	-1, 0
	cghsi	4096, 0
	cghsi	0(%r1,%r2), 0
	cghsi	0, -32769
	cghsi	0, 32768

#CHECK: error: invalid operand
#CHECK: cgij	%r0, -129, 0, 0
#CHECK: error: invalid operand
#CHECK: cgij	%r0, 128, 0, 0

	cgij	%r0, -129, 0, 0
	cgij	%r0, 128, 0, 0

#CHECK: error: offset out of range
#CHECK: cgij	%r0, 0, 0, -0x100002
#CHECK: error: offset out of range
#CHECK: cgij	%r0, 0, 0, -1
#CHECK: error: offset out of range
#CHECK: cgij	%r0, 0, 0, 1
#CHECK: error: offset out of range
#CHECK: cgij	%r0, 0, 0, 0x10000

	cgij	%r0, 0, 0, -0x100002
	cgij	%r0, 0, 0, -1
	cgij	%r0, 0, 0, 1
	cgij	%r0, 0, 0, 0x10000

#CHECK: error: invalid instruction
#CHECK:	cgijo	%r0, 0, 0, 0
#CHECK: error: invalid instruction
#CHECK:	cgijno	%r0, 0, 0, 0

	cgijo	%r0, 0, 0, 0
	cgijno	%r0, 0, 0, 0

#CHECK: error: offset out of range
#CHECK: cgrj	%r0, %r0, 0, -0x100002
#CHECK: error: offset out of range
#CHECK: cgrj	%r0, %r0, 0, -1
#CHECK: error: offset out of range
#CHECK: cgrj	%r0, %r0, 0, 1
#CHECK: error: offset out of range
#CHECK: cgrj	%r0, %r0, 0, 0x10000

	cgrj	%r0, %r0, 0, -0x100002
	cgrj	%r0, %r0, 0, -1
	cgrj	%r0, %r0, 0, 1
	cgrj	%r0, %r0, 0, 0x10000

#CHECK: error: invalid instruction
#CHECK:	cgrjo	%r0, %r0, 0, 0
#CHECK: error: invalid instruction
#CHECK:	cgrjno	%r0, %r0, 0, 0

	cgrjo	%r0, %r0, 0, 0
	cgrjno	%r0, %r0, 0, 0

#CHECK: error: offset out of range
#CHECK: cgrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: cgrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: cgrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: cgrl	%r0, 0x100000000

	cgrl	%r0, -0x1000000002
	cgrl	%r0, -1
	cgrl	%r0, 1
	cgrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cgxbr	%r0, -1, %f0
#CHECK: error: invalid operand
#CHECK: cgxbr	%r0, 16, %f0
#CHECK: error: invalid register pair
#CHECK: cgxbr	%r0, 0, %f2

	cgxbr	%r0, -1, %f0
	cgxbr	%r0, 16, %f0
	cgxbr	%r0, 0, %f2


#CHECK: error: invalid operand
#CHECK: ch	%r0, -1
#CHECK: error: invalid operand
#CHECK: ch	%r0, 4096

	ch	%r0, -1
	ch	%r0, 4096

#CHECK: error: invalid operand
#CHECK: chhsi	-1, 0
#CHECK: error: invalid operand
#CHECK: chhsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: chhsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: chhsi	0, -32769
#CHECK: error: invalid operand
#CHECK: chhsi	0, 32768

	chhsi	-1, 0
	chhsi	4096, 0
	chhsi	0(%r1,%r2), 0
	chhsi	0, -32769
	chhsi	0, 32768

#CHECK: error: invalid operand
#CHECK: chi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: chi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: chi	%r0, foo

	chi	%r0, -32769
	chi	%r0, 32768
	chi	%r0, foo

#CHECK: error: offset out of range
#CHECK: chrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: chrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: chrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: chrl	%r0, 0x100000000

	chrl	%r0, -0x1000000002
	chrl	%r0, -1
	chrl	%r0, 1
	chrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: chsi	-1, 0
#CHECK: error: invalid operand
#CHECK: chsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: chsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: chsi	0, -32769
#CHECK: error: invalid operand
#CHECK: chsi	0, 32768

	chsi	-1, 0
	chsi	4096, 0
	chsi	0(%r1,%r2), 0
	chsi	0, -32769
	chsi	0, 32768

#CHECK: error: invalid operand
#CHECK: chy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: chy	%r0, 524288

	chy	%r0, -524289
	chy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: cij	%r0, -129, 0, 0
#CHECK: error: invalid operand
#CHECK: cij	%r0, 128, 0, 0

	cij	%r0, -129, 0, 0
	cij	%r0, 128, 0, 0

#CHECK: error: offset out of range
#CHECK: cij	%r0, 0, 0, -0x100002
#CHECK: error: offset out of range
#CHECK: cij	%r0, 0, 0, -1
#CHECK: error: offset out of range
#CHECK: cij	%r0, 0, 0, 1
#CHECK: error: offset out of range
#CHECK: cij	%r0, 0, 0, 0x10000

	cij	%r0, 0, 0, -0x100002
	cij	%r0, 0, 0, -1
	cij	%r0, 0, 0, 1
	cij	%r0, 0, 0, 0x10000

#CHECK: error: invalid instruction
#CHECK:	cijo	%r0, 0, 0, 0
#CHECK: error: invalid instruction
#CHECK:	cijno	%r0, 0, 0, 0

	cijo	%r0, 0, 0, 0
	cijno	%r0, 0, 0, 0

#CHECK: error: invalid operand
#CHECK: cl	%r0, -1
#CHECK: error: invalid operand
#CHECK: cl	%r0, 4096

	cl	%r0, -1
	cl	%r0, 4096

#CHECK: error: invalid operand
#CHECK: clfhsi	-1, 0
#CHECK: error: invalid operand
#CHECK: clfhsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: clfhsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: clfhsi	0, -1
#CHECK: error: invalid operand
#CHECK: clfhsi	0, 65536

	clfhsi	-1, 0
	clfhsi	4096, 0
	clfhsi	0(%r1,%r2), 0
	clfhsi	0, -1
	clfhsi	0, 65536

#CHECK: error: invalid operand
#CHECK: clfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: clfi	%r0, (1 << 32)

	clfi	%r0, -1
	clfi	%r0, (1 << 32)

#CHECK: error: invalid operand
#CHECK: clg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: clg	%r0, 524288

	clg	%r0, -524289
	clg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: clgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: clgf	%r0, 524288

	clgf	%r0, -524289
	clgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: clgfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: clgfi	%r0, (1 << 32)

	clgfi	%r0, -1
	clgfi	%r0, (1 << 32)

#CHECK: error: offset out of range
#CHECK: clgfrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: clgfrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: clgfrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: clgfrl	%r0, 0x100000000

	clgfrl	%r0, -0x1000000002
	clgfrl	%r0, -1
	clgfrl	%r0, 1
	clgfrl	%r0, 0x100000000

#CHECK: error: offset out of range
#CHECK: clghrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: clghrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: clghrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: clghrl	%r0, 0x100000000

	clghrl	%r0, -0x1000000002
	clghrl	%r0, -1
	clghrl	%r0, 1
	clghrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: clghsi	-1, 0
#CHECK: error: invalid operand
#CHECK: clghsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: clghsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: clghsi	0, -1
#CHECK: error: invalid operand
#CHECK: clghsi	0, 65536

	clghsi	-1, 0
	clghsi	4096, 0
	clghsi	0(%r1,%r2), 0
	clghsi	0, -1
	clghsi	0, 65536

#CHECK: error: offset out of range
#CHECK: clgrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: clgrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: clgrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: clgrl	%r0, 0x100000000

	clgrl	%r0, -0x1000000002
	clgrl	%r0, -1
	clgrl	%r0, 1
	clgrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: clhhsi	-1, 0
#CHECK: error: invalid operand
#CHECK: clhhsi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: clhhsi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: clhhsi	0, -1
#CHECK: error: invalid operand
#CHECK: clhhsi	0, 65536

	clhhsi	-1, 0
	clhhsi	4096, 0
	clhhsi	0(%r1,%r2), 0
	clhhsi	0, -1
	clhhsi	0, 65536

#CHECK: error: offset out of range
#CHECK: clhrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: clhrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: clhrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: clhrl	%r0, 0x100000000

	clhrl	%r0, -0x1000000002
	clhrl	%r0, -1
	clhrl	%r0, 1
	clhrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cli	-1, 0
#CHECK: error: invalid operand
#CHECK: cli	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: cli	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: cli	0, -1
#CHECK: error: invalid operand
#CHECK: cli	0, 256

	cli	-1, 0
	cli	4096, 0
	cli	0(%r1,%r2), 0
	cli	0, -1
	cli	0, 256

#CHECK: error: invalid operand
#CHECK: cliy	-524289, 0
#CHECK: error: invalid operand
#CHECK: cliy	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: cliy	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: cliy	0, -1
#CHECK: error: invalid operand
#CHECK: cliy	0, 256

	cliy	-524289, 0
	cliy	524288, 0
	cliy	0(%r1,%r2), 0
	cliy	0, -1
	cliy	0, 256

#CHECK: error: offset out of range
#CHECK: clrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: clrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: clrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: clrl	%r0, 0x100000000

	clrl	%r0, -0x1000000002
	clrl	%r0, -1
	clrl	%r0, 1
	clrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cly	%r0, -524289
#CHECK: error: invalid operand
#CHECK: cly	%r0, 524288

	cly	%r0, -524289
	cly	%r0, 524288

#CHECK: error: offset out of range
#CHECK: crj	%r0, %r0, 0, -0x100002
#CHECK: error: offset out of range
#CHECK: crj	%r0, %r0, 0, -1
#CHECK: error: offset out of range
#CHECK: crj	%r0, %r0, 0, 1
#CHECK: error: offset out of range
#CHECK: crj	%r0, %r0, 0, 0x10000

	crj	%r0, %r0, 0, -0x100002
	crj	%r0, %r0, 0, -1
	crj	%r0, %r0, 0, 1
	crj	%r0, %r0, 0, 0x10000

#CHECK: error: invalid instruction
#CHECK:	crjo	%r0, %r0, 0, 0
#CHECK: error: invalid instruction
#CHECK:	crjno	%r0, %r0, 0, 0

	crjo	%r0, %r0, 0, 0
	crjno	%r0, %r0, 0, 0

#CHECK: error: offset out of range
#CHECK: crl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: crl	%r0, -1
#CHECK: error: offset out of range
#CHECK: crl	%r0, 1
#CHECK: error: offset out of range
#CHECK: crl	%r0, 0x100000000

	crl	%r0, -0x1000000002
	crl	%r0, -1
	crl	%r0, 1
	crl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: cs	%r0, %r0, -1
#CHECK: error: invalid operand
#CHECK: cs	%r0, %r0, 4096
#CHECK: error: invalid use of indexed addressing
#CHECK: cs	%r0, %r0, 0(%r1,%r2)

	cs	%r0, %r0, -1
	cs	%r0, %r0, 4096
	cs	%r0, %r0, 0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: csg	%r0, %r0, -524289
#CHECK: error: invalid operand
#CHECK: csg	%r0, %r0, 524288
#CHECK: error: invalid use of indexed addressing
#CHECK: csg	%r0, %r0, 0(%r1,%r2)

	csg	%r0, %r0, -524289
	csg	%r0, %r0, 524288
	csg	%r0, %r0, 0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: csy	%r0, %r0, -524289
#CHECK: error: invalid operand
#CHECK: csy	%r0, %r0, 524288
#CHECK: error: invalid use of indexed addressing
#CHECK: csy	%r0, %r0, 0(%r1,%r2)

	csy	%r0, %r0, -524289
	csy	%r0, %r0, 524288
	csy	%r0, %r0, 0(%r1,%r2)

#CHECK: error: invalid register pair
#CHECK: cxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: cxbr	%f2, %f0

	cxbr	%f0, %f2
	cxbr	%f2, %f0

#CHECK: error: invalid register pair
#CHECK: cxfbr	%f2, %r0

	cxfbr	%f2, %r0

#CHECK: error: invalid register pair
#CHECK: cxgbr	%f2, %r0

	cxgbr	%f2, %r0

#CHECK: error: invalid operand
#CHECK: cy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: cy	%r0, 524288

	cy	%r0, -524289
	cy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: ddb	%f0, -1
#CHECK: error: invalid operand
#CHECK: ddb	%f0, 4096

	ddb	%f0, -1
	ddb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: deb	%f0, -1
#CHECK: error: invalid operand
#CHECK: deb	%f0, 4096

	deb	%f0, -1
	deb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: dl	%r0, -524289
#CHECK: error: invalid operand
#CHECK: dl	%r0, 524288
#CHECK: error: invalid register pair
#CHECK: dl	%r1, 0

	dl	%r0, -524289
	dl	%r0, 524288
	dl	%r1, 0

#CHECK: error: invalid operand
#CHECK: dlg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: dlg	%r0, 524288
#CHECK: error: invalid register pair
#CHECK: dlg	%r1, 0

	dlg	%r0, -524289
	dlg	%r0, 524288
	dlg	%r1, 0

#CHECK: error: invalid register pair
#CHECK: dlgr	%r1, %r0

	dlgr	%r1, %r0

#CHECK: error: invalid register pair
#CHECK: dlr	%r1, %r0

	dlr	%r1, %r0

#CHECK: error: invalid operand
#CHECK: dsg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: dsg	%r0, 524288
#CHECK: error: invalid register pair
#CHECK: dsg	%r1, 0

	dsg	%r0, -524289
	dsg	%r0, 524288
	dsg	%r1, 0

#CHECK: error: invalid operand
#CHECK: dsgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: dsgf	%r0, 524288
#CHECK: error: invalid register pair
#CHECK: dsgf	%r1, 0

	dsgf	%r0, -524289
	dsgf	%r0, 524288
	dsgf	%r1, 0

#CHECK: error: invalid register pair
#CHECK: dsgfr	%r1, %r0

	dsgfr	%r1, %r0

#CHECK: error: invalid register pair
#CHECK: dsgr	%r1, %r0

	dsgr	%r1, %r0

#CHECK: error: invalid register pair
#CHECK: dxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: dxbr	%f2, %f0

	dxbr	%f0, %f2
	dxbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: fidbr	%f0, -1, %f0
#CHECK: error: invalid operand
#CHECK: fidbr	%f0, 16, %f0

	fidbr	%f0, -1, %f0
	fidbr	%f0, 16, %f0

#CHECK: error: invalid operand
#CHECK: fiebr	%f0, -1, %f0
#CHECK: error: invalid operand
#CHECK: fiebr	%f0, 16, %f0

	fiebr	%f0, -1, %f0
	fiebr	%f0, 16, %f0

#CHECK: error: invalid operand
#CHECK: fixbr	%f0, -1, %f0
#CHECK: error: invalid operand
#CHECK: fixbr	%f0, 16, %f0
#CHECK: error: invalid register pair
#CHECK: fixbr	%f0, 0, %f2
#CHECK: error: invalid register pair
#CHECK: fixbr	%f2, 0, %f0

	fixbr	%f0, -1, %f0
	fixbr	%f0, 16, %f0
	fixbr	%f0, 0, %f2
	fixbr	%f2, 0, %f0

#CHECK: error: invalid register pair
#CHECK: flogr	%r1, %r0

	flogr	%r1, %r0

#CHECK: error: invalid operand
#CHECK: ic	%r0, -1
#CHECK: error: invalid operand
#CHECK: ic	%r0, 4096

	ic	%r0, -1
	ic	%r0, 4096

#CHECK: error: invalid operand
#CHECK: icy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: icy	%r0, 524288

	icy	%r0, -524289
	icy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: iihf	%r0, -1
#CHECK: error: invalid operand
#CHECK: iihf	%r0, 1 << 32

	iihf	%r0, -1
	iihf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: iihh	%r0, -1
#CHECK: error: invalid operand
#CHECK: iihh	%r0, 0x10000

	iihh	%r0, -1
	iihh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: iihl	%r0, -1
#CHECK: error: invalid operand
#CHECK: iihl	%r0, 0x10000

	iihl	%r0, -1
	iihl	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: iilf	%r0, -1
#CHECK: error: invalid operand
#CHECK: iilf	%r0, 1 << 32

	iilf	%r0, -1
	iilf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: iilh	%r0, -1
#CHECK: error: invalid operand
#CHECK: iilh	%r0, 0x10000

	iilh	%r0, -1
	iilh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: iill	%r0, -1
#CHECK: error: invalid operand
#CHECK: iill	%r0, 0x10000

	iill	%r0, -1
	iill	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: l	%r0, -1
#CHECK: error: invalid operand
#CHECK: l	%r0, 4096

	l	%r0, -1
	l	%r0, 4096

#CHECK: error: invalid operand
#CHECK: la	%r0, -1
#CHECK: error: invalid operand
#CHECK: la	%r0, 4096

	la	%r0, -1
	la	%r0, 4096

#CHECK: error: offset out of range
#CHECK: larl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: larl	%r0, -1
#CHECK: error: offset out of range
#CHECK: larl	%r0, 1
#CHECK: error: offset out of range
#CHECK: larl	%r0, 0x100000000

	larl	%r0, -0x1000000002
	larl	%r0, -1
	larl	%r0, 1
	larl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: lay	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lay	%r0, 524288

	lay	%r0, -524289
	lay	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lb	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lb	%r0, 524288

	lb	%r0, -524289
	lb	%r0, 524288

#CHECK: error: invalid register pair
#CHECK: lcxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: lcxbr	%f2, %f0

	lcxbr	%f0, %f2
	lcxbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: ld	%f0, -1
#CHECK: error: invalid operand
#CHECK: ld	%f0, 4096

	ld	%f0, -1
	ld	%f0, 4096

#CHECK: error: invalid operand
#CHECK: ldeb	%f0, -1
#CHECK: error: invalid operand
#CHECK: ldeb	%f0, 4096

	ldeb	%f0, -1
	ldeb	%f0, 4096

#CHECK: error: invalid register pair
#CHECK: ldxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: ldxbr	%f2, %f0

	ldxbr	%f0, %f2
	ldxbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: ldy	%f0, -524289
#CHECK: error: invalid operand
#CHECK: ldy	%f0, 524288

	ldy	%f0, -524289
	ldy	%f0, 524288

#CHECK: error: invalid operand
#CHECK: le	%f0, -1
#CHECK: error: invalid operand
#CHECK: le	%f0, 4096

	le	%f0, -1
	le	%f0, 4096

#CHECK: error: invalid register pair
#CHECK: lexbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: lexbr	%f2, %f0

	lexbr	%f0, %f2
	lexbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: ley	%f0, -524289
#CHECK: error: invalid operand
#CHECK: ley	%f0, 524288

	ley	%f0, -524289
	ley	%f0, 524288

#CHECK: error: invalid operand
#CHECK: lg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lg	%r0, 524288

	lg	%r0, -524289
	lg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lgb	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lgb	%r0, 524288

	lgb	%r0, -524289
	lgb	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lgf	%r0, 524288

	lgf	%r0, -524289
	lgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lgfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: lgfi	%r0, (1 << 31)

	lgfi	%r0, (-1 << 31) - 1
	lgfi	%r0, (1 << 31)

#CHECK: error: offset out of range
#CHECK: lgfrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: lgfrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: lgfrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: lgfrl	%r0, 0x100000000

	lgfrl	%r0, -0x1000000002
	lgfrl	%r0, -1
	lgfrl	%r0, 1
	lgfrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: lgh	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lgh	%r0, 524288

	lgh	%r0, -524289
	lgh	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lghi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: lghi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: lghi	%r0, foo

	lghi	%r0, -32769
	lghi	%r0, 32768
	lghi	%r0, foo

#CHECK: error: offset out of range
#CHECK: lghrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: lghrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: lghrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: lghrl	%r0, 0x100000000

	lghrl	%r0, -0x1000000002
	lghrl	%r0, -1
	lghrl	%r0, 1
	lghrl	%r0, 0x100000000

#CHECK: error: offset out of range
#CHECK: lgrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: lgrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: lgrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: lgrl	%r0, 0x100000000

	lgrl	%r0, -0x1000000002
	lgrl	%r0, -1
	lgrl	%r0, 1
	lgrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: lh	%r0, -1
#CHECK: error: invalid operand
#CHECK: lh	%r0, 4096

	lh	%r0, -1
	lh	%r0, 4096

#CHECK: error: invalid operand
#CHECK: lhi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: lhi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: lhi	%r0, foo

	lhi	%r0, -32769
	lhi	%r0, 32768
	lhi	%r0, foo

#CHECK: error: offset out of range
#CHECK: lhrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: lhrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: lhrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: lhrl	%r0, 0x100000000

	lhrl	%r0, -0x1000000002
	lhrl	%r0, -1
	lhrl	%r0, 1
	lhrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: lhy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lhy	%r0, 524288

	lhy	%r0, -524289
	lhy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: llc	%r0, -524289
#CHECK: error: invalid operand
#CHECK: llc	%r0, 524288

	llc	%r0, -524289
	llc	%r0, 524288

#CHECK: error: invalid operand
#CHECK: llgc	%r0, -524289
#CHECK: error: invalid operand
#CHECK: llgc	%r0, 524288

	llgc	%r0, -524289
	llgc	%r0, 524288

#CHECK: error: invalid operand
#CHECK: llgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: llgf	%r0, 524288

	llgf	%r0, -524289
	llgf	%r0, 524288

#CHECK: error: offset out of range
#CHECK: llgfrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: llgfrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: llgfrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: llgfrl	%r0, 0x100000000

	llgfrl	%r0, -0x1000000002
	llgfrl	%r0, -1
	llgfrl	%r0, 1
	llgfrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: llgh	%r0, -524289
#CHECK: error: invalid operand
#CHECK: llgh	%r0, 524288

	llgh	%r0, -524289
	llgh	%r0, 524288

#CHECK: error: offset out of range
#CHECK: llghrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: llghrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: llghrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: llghrl	%r0, 0x100000000

	llghrl	%r0, -0x1000000002
	llghrl	%r0, -1
	llghrl	%r0, 1
	llghrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: llh	%r0, -524289
#CHECK: error: invalid operand
#CHECK: llh	%r0, 524288

	llh	%r0, -524289
	llh	%r0, 524288

#CHECK: error: offset out of range
#CHECK: llhrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: llhrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: llhrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: llhrl	%r0, 0x100000000

	llhrl	%r0, -0x1000000002
	llhrl	%r0, -1
	llhrl	%r0, 1
	llhrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: llihf	%r0, -1
#CHECK: error: invalid operand
#CHECK: llihf	%r0, 1 << 32

	llihf	%r0, -1
	llihf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: llihh	%r0, -1
#CHECK: error: invalid operand
#CHECK: llihh	%r0, 0x10000

	llihh	%r0, -1
	llihh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: llihl	%r0, -1
#CHECK: error: invalid operand
#CHECK: llihl	%r0, 0x10000

	llihl	%r0, -1
	llihl	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: llilf	%r0, -1
#CHECK: error: invalid operand
#CHECK: llilf	%r0, 1 << 32

	llilf	%r0, -1
	llilf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: llilh	%r0, -1
#CHECK: error: invalid operand
#CHECK: llilh	%r0, 0x10000

	llilh	%r0, -1
	llilh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: llill	%r0, -1
#CHECK: error: invalid operand
#CHECK: llill	%r0, 0x10000

	llill	%r0, -1
	llill	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: lmg	%r0, %r0, -524289
#CHECK: error: invalid operand
#CHECK: lmg	%r0, %r0, 524288
#CHECK: error: invalid use of indexed addressing
#CHECK: lmg	%r0, %r0, 0(%r1,%r2)

	lmg	%r0, %r0, -524289
	lmg	%r0, %r0, 524288
	lmg	%r0, %r0, 0(%r1,%r2)

#CHECK: error: invalid register pair
#CHECK: lnxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: lnxbr	%f2, %f0

	lnxbr	%f0, %f2
	lnxbr	%f2, %f0

#CHECK: error: invalid register pair
#CHECK: lpxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: lpxbr	%f2, %f0

	lpxbr	%f0, %f2
	lpxbr	%f2, %f0

#CHECK: error: offset out of range
#CHECK: lrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: lrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: lrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: lrl	%r0, 0x100000000

	lrl	%r0, -0x1000000002
	lrl	%r0, -1
	lrl	%r0, 1
	lrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: lrv	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lrv	%r0, 524288

	lrv	%r0, -524289
	lrv	%r0, 524288

#CHECK: error: invalid operand
#CHECK: lrvg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: lrvg	%r0, 524288

	lrvg	%r0, -524289
	lrvg	%r0, 524288

#CHECK: error: invalid register pair
#CHECK: lxr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: lxr	%f2, %f0

	lxr	%f0, %f2
	lxr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: ly	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ly	%r0, 524288

	ly	%r0, -524289
	ly	%r0, 524288

#CHECK: error: invalid register pair
#CHECK: lzxr	%f2

	lzxr	%f2

#CHECK: error: invalid operand
#CHECK: madb	%f0, %f0, -1
#CHECK: error: invalid operand
#CHECK: madb	%f0, %f0, 4096

	madb	%f0, %f0, -1
	madb	%f0, %f0, 4096

#CHECK: error: invalid operand
#CHECK: maeb	%f0, %f0, -1
#CHECK: error: invalid operand
#CHECK: maeb	%f0, %f0, 4096

	maeb	%f0, %f0, -1
	maeb	%f0, %f0, 4096

#CHECK: error: invalid operand
#CHECK: mdb	%f0, -1
#CHECK: error: invalid operand
#CHECK: mdb	%f0, 4096

	mdb	%f0, -1
	mdb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: mdeb	%f0, -1
#CHECK: error: invalid operand
#CHECK: mdeb	%f0, 4096

	mdeb	%f0, -1
	mdeb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: meeb	%f0, -1
#CHECK: error: invalid operand
#CHECK: meeb	%f0, 4096

	meeb	%f0, -1
	meeb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: mghi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: mghi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: mghi	%r0, foo

	mghi	%r0, -32769
	mghi	%r0, 32768
	mghi	%r0, foo

#CHECK: error: invalid operand
#CHECK: mh	%r0, -1
#CHECK: error: invalid operand
#CHECK: mh	%r0, 4096

	mh	%r0, -1
	mh	%r0, 4096

#CHECK: error: invalid operand
#CHECK: mhi	%r0, -32769
#CHECK: error: invalid operand
#CHECK: mhi	%r0, 32768
#CHECK: error: invalid operand
#CHECK: mhi	%r0, foo

	mhi	%r0, -32769
	mhi	%r0, 32768
	mhi	%r0, foo

#CHECK: error: invalid operand
#CHECK: mhy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: mhy	%r0, 524288

	mhy	%r0, -524289
	mhy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: mlg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: mlg	%r0, 524288
#CHECK: error: invalid register pair
#CHECK: mlg	%r1, 0

	mlg	%r0, -524289
	mlg	%r0, 524288
	mlg	%r1, 0

#CHECK: error: invalid register pair
#CHECK: mlgr	%r1, %r0

	mlgr	%r1, %r0

#CHECK: error: invalid operand
#CHECK: ms	%r0, -1
#CHECK: error: invalid operand
#CHECK: ms	%r0, 4096

	ms	%r0, -1
	ms	%r0, 4096

#CHECK: error: invalid operand
#CHECK: msdb	%f0, %f0, -1
#CHECK: error: invalid operand
#CHECK: msdb	%f0, %f0, 4096

	msdb	%f0, %f0, -1
	msdb	%f0, %f0, 4096

#CHECK: error: invalid operand
#CHECK: mseb	%f0, %f0, -1
#CHECK: error: invalid operand
#CHECK: mseb	%f0, %f0, 4096

	mseb	%f0, %f0, -1
	mseb	%f0, %f0, 4096

#CHECK: error: invalid operand
#CHECK: msfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: msfi	%r0, (1 << 31)

	msfi	%r0, (-1 << 31) - 1
	msfi	%r0, (1 << 31)

#CHECK: error: invalid operand
#CHECK: msg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: msg	%r0, 524288

	msg	%r0, -524289
	msg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: msgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: msgf	%r0, 524288

	msgf	%r0, -524289
	msgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: msgfi	%r0, (-1 << 31) - 1
#CHECK: error: invalid operand
#CHECK: msgfi	%r0, (1 << 31)

	msgfi	%r0, (-1 << 31) - 1
	msgfi	%r0, (1 << 31)

#CHECK: error: invalid operand
#CHECK: msy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: msy	%r0, 524288

	msy	%r0, -524289
	msy	%r0, 524288

#CHECK: error: missing length in address
#CHECK: mvc	0, 0
#CHECK: error: missing length in address
#CHECK: mvc	0(%r1), 0(%r1)
#CHECK: error: invalid use of length addressing
#CHECK: mvc	0(1,%r1), 0(2,%r1)
#CHECK: error: invalid operand
#CHECK: mvc	0(0,%r1), 0(%r1)
#CHECK: error: invalid operand
#CHECK: mvc	0(257,%r1), 0(%r1)
#CHECK: error: invalid operand
#CHECK: mvc	-1(1,%r1), 0(%r1)
#CHECK: error: invalid operand
#CHECK: mvc	4096(1,%r1), 0(%r1)
#CHECK: error: invalid operand
#CHECK: mvc	0(1,%r1), -1(%r1)
#CHECK: error: invalid operand
#CHECK: mvc	0(1,%r1), 4096(%r1)
#CHECK: error: %r0 used in an address
#CHECK: mvc	0(1,%r0), 0(%r1)
#CHECK: error: %r0 used in an address
#CHECK: mvc	0(1,%r1), 0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: mvc	0(%r1,%r2), 0(%r1)
#CHECK: error: invalid use of indexed addressing
#CHECK: mvc	0(1,%r2), 0(%r1,%r2)
#CHECK: error: unknown token in expression
#CHECK: mvc	0(-), 0

	mvc	0, 0
	mvc	0(%r1), 0(%r1)
	mvc	0(1,%r1), 0(2,%r1)
	mvc	0(0,%r1), 0(%r1)
	mvc	0(257,%r1), 0(%r1)
	mvc	-1(1,%r1), 0(%r1)
	mvc	4096(1,%r1), 0(%r1)
	mvc	0(1,%r1), -1(%r1)
	mvc	0(1,%r1), 4096(%r1)
	mvc	0(1,%r0), 0(%r1)
	mvc	0(1,%r1), 0(%r0)
	mvc	0(%r1,%r2), 0(%r1)
	mvc	0(1,%r2), 0(%r1,%r2)
	mvc	0(-), 0

#CHECK: error: invalid operand
#CHECK: mvghi	-1, 0
#CHECK: error: invalid operand
#CHECK: mvghi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: mvghi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: mvghi	0, -32769
#CHECK: error: invalid operand
#CHECK: mvghi	0, 32768

	mvghi	-1, 0
	mvghi	4096, 0
	mvghi	0(%r1,%r2), 0
	mvghi	0, -32769
	mvghi	0, 32768

#CHECK: error: invalid operand
#CHECK: mvhhi	-1, 0
#CHECK: error: invalid operand
#CHECK: mvhhi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: mvhhi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: mvhhi	0, -32769
#CHECK: error: invalid operand
#CHECK: mvhhi	0, 32768

	mvhhi	-1, 0
	mvhhi	4096, 0
	mvhhi	0(%r1,%r2), 0
	mvhhi	0, -32769
	mvhhi	0, 32768

#CHECK: error: invalid operand
#CHECK: mvhi	-1, 0
#CHECK: error: invalid operand
#CHECK: mvhi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: mvhi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: mvhi	0, -32769
#CHECK: error: invalid operand
#CHECK: mvhi	0, 32768

	mvhi	-1, 0
	mvhi	4096, 0
	mvhi	0(%r1,%r2), 0
	mvhi	0, -32769
	mvhi	0, 32768

#CHECK: error: invalid operand
#CHECK: mvi	-1, 0
#CHECK: error: invalid operand
#CHECK: mvi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: mvi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: mvi	0, -1
#CHECK: error: invalid operand
#CHECK: mvi	0, 256

	mvi	-1, 0
	mvi	4096, 0
	mvi	0(%r1,%r2), 0
	mvi	0, -1
	mvi	0, 256

#CHECK: error: invalid operand
#CHECK: mviy	-524289, 0
#CHECK: error: invalid operand
#CHECK: mviy	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: mviy	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: mviy	0, -1
#CHECK: error: invalid operand
#CHECK: mviy	0, 256

	mviy	-524289, 0
	mviy	524288, 0
	mviy	0(%r1,%r2), 0
	mviy	0, -1
	mviy	0, 256

#CHECK: error: invalid register pair
#CHECK: mxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: mxbr	%f2, %f0

	mxbr	%f0, %f2
	mxbr	%f2, %f0

#CHECK: error: invalid register pair
#CHECK: mxdb	%f2, 0
#CHECK: error: invalid operand
#CHECK: mxdb	%f0, -1
#CHECK: error: invalid operand
#CHECK: mxdb	%f0, 4096

	mxdb	%f2, 0
	mxdb	%f0, -1
	mxdb	%f0, 4096

#CHECK: error: invalid register pair
#CHECK: mxdbr	%f2, %f0

	mxdbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: n	%r0, -1
#CHECK: error: invalid operand
#CHECK: n	%r0, 4096

	n	%r0, -1
	n	%r0, 4096

#CHECK: error: invalid operand
#CHECK: ng	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ng	%r0, 524288

	ng	%r0, -524289
	ng	%r0, 524288

#CHECK: error: invalid operand
#CHECK: ni	-1, 0
#CHECK: error: invalid operand
#CHECK: ni	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: ni	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: ni	0, -1
#CHECK: error: invalid operand
#CHECK: ni	0, 256

	ni	-1, 0
	ni	4096, 0
	ni	0(%r1,%r2), 0
	ni	0, -1
	ni	0, 256

#CHECK: error: invalid operand
#CHECK: nihf	%r0, -1
#CHECK: error: invalid operand
#CHECK: nihf	%r0, 1 << 32

	nihf	%r0, -1
	nihf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: nihh	%r0, -1
#CHECK: error: invalid operand
#CHECK: nihh	%r0, 0x10000

	nihh	%r0, -1
	nihh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: nihl	%r0, -1
#CHECK: error: invalid operand
#CHECK: nihl	%r0, 0x10000

	nihl	%r0, -1
	nihl	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: nilf	%r0, -1
#CHECK: error: invalid operand
#CHECK: nilf	%r0, 1 << 32

	nilf	%r0, -1
	nilf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: nilh	%r0, -1
#CHECK: error: invalid operand
#CHECK: nilh	%r0, 0x10000

	nilh	%r0, -1
	nilh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: nill	%r0, -1
#CHECK: error: invalid operand
#CHECK: nill	%r0, 0x10000

	nill	%r0, -1
	nill	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: niy	-524289, 0
#CHECK: error: invalid operand
#CHECK: niy	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: niy	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: niy	0, -1
#CHECK: error: invalid operand
#CHECK: niy	0, 256

	niy	-524289, 0
	niy	524288, 0
	niy	0(%r1,%r2), 0
	niy	0, -1
	niy	0, 256

#CHECK: error: invalid operand
#CHECK: ny	%r0, -524289
#CHECK: error: invalid operand
#CHECK: ny	%r0, 524288

	ny	%r0, -524289
	ny	%r0, 524288

#CHECK: error: invalid operand
#CHECK: o	%r0, -1
#CHECK: error: invalid operand
#CHECK: o	%r0, 4096

	o	%r0, -1
	o	%r0, 4096

#CHECK: error: invalid operand
#CHECK: og	%r0, -524289
#CHECK: error: invalid operand
#CHECK: og	%r0, 524288

	og	%r0, -524289
	og	%r0, 524288

#CHECK: error: invalid operand
#CHECK: oi	-1, 0
#CHECK: error: invalid operand
#CHECK: oi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: oi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: oi	0, -1
#CHECK: error: invalid operand
#CHECK: oi	0, 256

	oi	-1, 0
	oi	4096, 0
	oi	0(%r1,%r2), 0
	oi	0, -1
	oi	0, 256

#CHECK: error: invalid operand
#CHECK: oihf	%r0, -1
#CHECK: error: invalid operand
#CHECK: oihf	%r0, 1 << 32

	oihf	%r0, -1
	oihf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: oihh	%r0, -1
#CHECK: error: invalid operand
#CHECK: oihh	%r0, 0x10000

	oihh	%r0, -1
	oihh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: oihl	%r0, -1
#CHECK: error: invalid operand
#CHECK: oihl	%r0, 0x10000

	oihl	%r0, -1
	oihl	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: oilf	%r0, -1
#CHECK: error: invalid operand
#CHECK: oilf	%r0, 1 << 32

	oilf	%r0, -1
	oilf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: oilh	%r0, -1
#CHECK: error: invalid operand
#CHECK: oilh	%r0, 0x10000

	oilh	%r0, -1
	oilh	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: oill	%r0, -1
#CHECK: error: invalid operand
#CHECK: oill	%r0, 0x10000

	oill	%r0, -1
	oill	%r0, 0x10000

#CHECK: error: invalid operand
#CHECK: oiy	-524289, 0
#CHECK: error: invalid operand
#CHECK: oiy	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: oiy	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: oiy	0, -1
#CHECK: error: invalid operand
#CHECK: oiy	0, 256

	oiy	-524289, 0
	oiy	524288, 0
	oiy	0(%r1,%r2), 0
	oiy	0, -1
	oiy	0, 256

#CHECK: error: invalid operand
#CHECK: oy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: oy	%r0, 524288

	oy	%r0, -524289
	oy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,0,0,-1
#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,0,0,64
#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,0,-1,0
#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,0,256,0
#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,-1,0,0
#CHECK: error: invalid operand
#CHECK: risbg	%r0,%r0,256,0,0

	risbg	%r0,%r0,0,0,-1
	risbg	%r0,%r0,0,0,64
	risbg	%r0,%r0,0,-1,0
	risbg	%r0,%r0,0,256,0
	risbg	%r0,%r0,-1,0,0
	risbg	%r0,%r0,256,0,0

#CHECK: error: invalid operand
#CHECK: rll	%r0,%r0,-524289
#CHECK: error: invalid operand
#CHECK: rll	%r0,%r0,524288
#CHECK: error: %r0 used in an address
#CHECK: rll	%r0,%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: rll	%r0,%r0,0(%r1,%r2)

	rll	%r0,%r0,-524289
	rll	%r0,%r0,524288
	rll	%r0,%r0,0(%r0)
	rll	%r0,%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: rllg	%r0,%r0,-524289
#CHECK: error: invalid operand
#CHECK: rllg	%r0,%r0,524288
#CHECK: error: %r0 used in an address
#CHECK: rllg	%r0,%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: rllg	%r0,%r0,0(%r1,%r2)

	rllg	%r0,%r0,-524289
	rllg	%r0,%r0,524288
	rllg	%r0,%r0,0(%r0)
	rllg	%r0,%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: s	%r0, -1
#CHECK: error: invalid operand
#CHECK: s	%r0, 4096

	s	%r0, -1
	s	%r0, 4096

#CHECK: error: invalid operand
#CHECK: sdb	%f0, -1
#CHECK: error: invalid operand
#CHECK: sdb	%f0, 4096

	sdb	%f0, -1
	sdb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: seb	%f0, -1
#CHECK: error: invalid operand
#CHECK: seb	%f0, 4096

	seb	%f0, -1
	seb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: sg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sg	%r0, 524288

	sg	%r0, -524289
	sg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: sgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sgf	%r0, 524288

	sgf	%r0, -524289
	sgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: sh	%r0, -1
#CHECK: error: invalid operand
#CHECK: sh	%r0, 4096

	sh	%r0, -1
	sh	%r0, 4096

#CHECK: error: invalid operand
#CHECK: shy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: shy	%r0, 524288

	shy	%r0, -524289
	shy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: sl	%r0, -1
#CHECK: error: invalid operand
#CHECK: sl	%r0, 4096

	sl	%r0, -1
	sl	%r0, 4096

#CHECK: error: invalid operand
#CHECK: slb	%r0, -524289
#CHECK: error: invalid operand
#CHECK: slb	%r0, 524288

	slb	%r0, -524289
	slb	%r0, 524288

#CHECK: error: invalid operand
#CHECK: slbg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: slbg	%r0, 524288

	slbg	%r0, -524289
	slbg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: slfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: slfi	%r0, (1 << 32)

	slfi	%r0, -1
	slfi	%r0, (1 << 32)

#CHECK: error: invalid operand
#CHECK: slg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: slg	%r0, 524288

	slg	%r0, -524289
	slg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: slgf	%r0, -524289
#CHECK: error: invalid operand
#CHECK: slgf	%r0, 524288

	slgf	%r0, -524289
	slgf	%r0, 524288

#CHECK: error: invalid operand
#CHECK: slgfi	%r0, -1
#CHECK: error: invalid operand
#CHECK: slgfi	%r0, (1 << 32)

	slgfi	%r0, -1
	slgfi	%r0, (1 << 32)

#CHECK: error: invalid operand
#CHECK: sll	%r0,-1
#CHECK: error: invalid operand
#CHECK: sll	%r0,4096
#CHECK: error: %r0 used in an address
#CHECK: sll	%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: sll	%r0,0(%r1,%r2)

	sll	%r0,-1
	sll	%r0,4096
	sll	%r0,0(%r0)
	sll	%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: sllg	%r0,%r0,-524289
#CHECK: error: invalid operand
#CHECK: sllg	%r0,%r0,524288
#CHECK: error: %r0 used in an address
#CHECK: sllg	%r0,%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: sllg	%r0,%r0,0(%r1,%r2)

	sllg	%r0,%r0,-524289
	sllg	%r0,%r0,524288
	sllg	%r0,%r0,0(%r0)
	sllg	%r0,%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: sly	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sly	%r0, 524288

	sly	%r0, -524289
	sly	%r0, 524288

#CHECK: error: invalid operand
#CHECK: sqdb	%f0, -1
#CHECK: error: invalid operand
#CHECK: sqdb	%f0, 4096

	sqdb	%f0, -1
	sqdb	%f0, 4096

#CHECK: error: invalid operand
#CHECK: sqeb	%f0, -1
#CHECK: error: invalid operand
#CHECK: sqeb	%f0, 4096

	sqeb	%f0, -1
	sqeb	%f0, 4096

#CHECK: error: invalid register pair
#CHECK: sqxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: sqxbr	%f2, %f0

	sqxbr	%f0, %f2
	sqxbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: sra	%r0,-1
#CHECK: error: invalid operand
#CHECK: sra	%r0,4096
#CHECK: error: %r0 used in an address
#CHECK: sra	%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: sra	%r0,0(%r1,%r2)

	sra	%r0,-1
	sra	%r0,4096
	sra	%r0,0(%r0)
	sra	%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: srag	%r0,%r0,-524289
#CHECK: error: invalid operand
#CHECK: srag	%r0,%r0,524288
#CHECK: error: %r0 used in an address
#CHECK: srag	%r0,%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: srag	%r0,%r0,0(%r1,%r2)

	srag	%r0,%r0,-524289
	srag	%r0,%r0,524288
	srag	%r0,%r0,0(%r0)
	srag	%r0,%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: srl	%r0,-1
#CHECK: error: invalid operand
#CHECK: srl	%r0,4096
#CHECK: error: %r0 used in an address
#CHECK: srl	%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: srl	%r0,0(%r1,%r2)

	srl	%r0,-1
	srl	%r0,4096
	srl	%r0,0(%r0)
	srl	%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: srlg	%r0,%r0,-524289
#CHECK: error: invalid operand
#CHECK: srlg	%r0,%r0,524288
#CHECK: error: %r0 used in an address
#CHECK: srlg	%r0,%r0,0(%r0)
#CHECK: error: invalid use of indexed addressing
#CHECK: srlg	%r0,%r0,0(%r1,%r2)

	srlg	%r0,%r0,-524289
	srlg	%r0,%r0,524288
	srlg	%r0,%r0,0(%r0)
	srlg	%r0,%r0,0(%r1,%r2)

#CHECK: error: invalid operand
#CHECK: st	%r0, -1
#CHECK: error: invalid operand
#CHECK: st	%r0, 4096

	st	%r0, -1
	st	%r0, 4096

#CHECK: error: invalid operand
#CHECK: stc	%r0, -1
#CHECK: error: invalid operand
#CHECK: stc	%r0, 4096

	stc	%r0, -1
	stc	%r0, 4096

#CHECK: error: invalid operand
#CHECK: stcy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: stcy	%r0, 524288

	stcy	%r0, -524289
	stcy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: std	%f0, -1
#CHECK: error: invalid operand
#CHECK: std	%f0, 4096

	std	%f0, -1
	std	%f0, 4096

#CHECK: error: invalid operand
#CHECK: stdy	%f0, -524289
#CHECK: error: invalid operand
#CHECK: stdy	%f0, 524288

	stdy	%f0, -524289
	stdy	%f0, 524288

#CHECK: error: invalid operand
#CHECK: ste	%f0, -1
#CHECK: error: invalid operand
#CHECK: ste	%f0, 4096

	ste	%f0, -1
	ste	%f0, 4096

#CHECK: error: invalid operand
#CHECK: stey	%f0, -524289
#CHECK: error: invalid operand
#CHECK: stey	%f0, 524288

	stey	%f0, -524289
	stey	%f0, 524288

#CHECK: error: invalid operand
#CHECK: stg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: stg	%r0, 524288

	stg	%r0, -524289
	stg	%r0, 524288

#CHECK: error: offset out of range
#CHECK: stgrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: stgrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: stgrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: stgrl	%r0, 0x100000000

	stgrl	%r0, -0x1000000002
	stgrl	%r0, -1
	stgrl	%r0, 1
	stgrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: sth	%r0, -1
#CHECK: error: invalid operand
#CHECK: sth	%r0, 4096

	sth	%r0, -1
	sth	%r0, 4096

#CHECK: error: offset out of range
#CHECK: sthrl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: sthrl	%r0, -1
#CHECK: error: offset out of range
#CHECK: sthrl	%r0, 1
#CHECK: error: offset out of range
#CHECK: sthrl	%r0, 0x100000000

	sthrl	%r0, -0x1000000002
	sthrl	%r0, -1
	sthrl	%r0, 1
	sthrl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: sthy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sthy	%r0, 524288

	sthy	%r0, -524289
	sthy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: stmg	%r0, %r0, -524289
#CHECK: error: invalid operand
#CHECK: stmg	%r0, %r0, 524288
#CHECK: error: invalid use of indexed addressing
#CHECK: stmg	%r0, %r0, 0(%r1,%r2)

	stmg	%r0, %r0, -524289
	stmg	%r0, %r0, 524288
	stmg	%r0, %r0, 0(%r1,%r2)

#CHECK: error: offset out of range
#CHECK: strl	%r0, -0x1000000002
#CHECK: error: offset out of range
#CHECK: strl	%r0, -1
#CHECK: error: offset out of range
#CHECK: strl	%r0, 1
#CHECK: error: offset out of range
#CHECK: strl	%r0, 0x100000000

	strl	%r0, -0x1000000002
	strl	%r0, -1
	strl	%r0, 1
	strl	%r0, 0x100000000

#CHECK: error: invalid operand
#CHECK: strv	%r0, -524289
#CHECK: error: invalid operand
#CHECK: strv	%r0, 524288

	strv	%r0, -524289
	strv	%r0, 524288

#CHECK: error: invalid operand
#CHECK: strvg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: strvg	%r0, 524288

	strvg	%r0, -524289
	strvg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: sty	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sty	%r0, 524288

	sty	%r0, -524289
	sty	%r0, 524288

#CHECK: error: invalid register pair
#CHECK: sxbr	%f0, %f2
#CHECK: error: invalid register pair
#CHECK: sxbr	%f2, %f0

	sxbr	%f0, %f2
	sxbr	%f2, %f0

#CHECK: error: invalid operand
#CHECK: sy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: sy	%r0, 524288

	sy	%r0, -524289
	sy	%r0, 524288

#CHECK: error: invalid operand
#CHECK: x	%r0, -1
#CHECK: error: invalid operand
#CHECK: x	%r0, 4096

	x	%r0, -1
	x	%r0, 4096

#CHECK: error: invalid operand
#CHECK: xg	%r0, -524289
#CHECK: error: invalid operand
#CHECK: xg	%r0, 524288

	xg	%r0, -524289
	xg	%r0, 524288

#CHECK: error: invalid operand
#CHECK: xi	-1, 0
#CHECK: error: invalid operand
#CHECK: xi	4096, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: xi	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: xi	0, -1
#CHECK: error: invalid operand
#CHECK: xi	0, 256

	xi	-1, 0
	xi	4096, 0
	xi	0(%r1,%r2), 0
	xi	0, -1
	xi	0, 256

#CHECK: error: invalid operand
#CHECK: xihf	%r0, -1
#CHECK: error: invalid operand
#CHECK: xihf	%r0, 1 << 32

	xihf	%r0, -1
	xihf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: xilf	%r0, -1
#CHECK: error: invalid operand
#CHECK: xilf	%r0, 1 << 32

	xilf	%r0, -1
	xilf	%r0, 1 << 32

#CHECK: error: invalid operand
#CHECK: xiy	-524289, 0
#CHECK: error: invalid operand
#CHECK: xiy	524288, 0
#CHECK: error: invalid use of indexed addressing
#CHECK: xiy	0(%r1,%r2), 0
#CHECK: error: invalid operand
#CHECK: xiy	0, -1
#CHECK: error: invalid operand
#CHECK: xiy	0, 256

	xiy	-524289, 0
	xiy	524288, 0
	xiy	0(%r1,%r2), 0
	xiy	0, -1
	xiy	0, 256

#CHECK: error: invalid operand
#CHECK: xy	%r0, -524289
#CHECK: error: invalid operand
#CHECK: xy	%r0, 524288

	xy	%r0, -524289
	xy	%r0, 524288
