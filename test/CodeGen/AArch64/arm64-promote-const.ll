; Disable machine cse to stress the different path of the algorithm.
; Otherwise, we always fall in the simple case, i.e., only one definition.
; RUN: llc < %s -mtriple=arm64-apple-ios7.0 -disable-machine-cse -aarch64-stress-promote-const -mcpu=cyclone | FileCheck -check-prefix=PROMOTED %s
; The REGULAR run just checks that the inputs passed to promote const expose
; the appropriate patterns.
; RUN: llc < %s -mtriple=arm64-apple-ios7.0 -disable-machine-cse -aarch64-promote-const=false -mcpu=cyclone | FileCheck -check-prefix=REGULAR %s

%struct.uint8x16x4_t = type { [4 x <16 x i8>] }

; Constant is a structure
define %struct.uint8x16x4_t @test1() {
; PROMOTED-LABEL: test1:
; Promote constant has created a big constant for the whole structure
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], __PromotedConst@PAGE
; PROMOTED: add [[BASEADDR:x[0-9]+]], [[PAGEADDR]], __PromotedConst@PAGEOFF
; Destination registers are defined by the ABI
; PROMOTED-NEXT: ldp q0, q1, {{\[}}[[BASEADDR]]]
; PROMOTED-NEXT: ldp q2, q3, {{\[}}[[BASEADDR]], #32]
; PROMOTED-NEXT: ret

; REGULAR-LABEL: test1:
; Regular access is quite bad, it performs 4 loads, one for each chunk of
; the structure
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL:lCP.*]]@PAGE
; Destination registers are defined by the ABI
; REGULAR: ldr q0, {{\[}}[[PAGEADDR]], [[CSTLABEL]]@PAGEOFF]
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL:lCP.*]]@PAGE
; REGULAR: ldr q1, {{\[}}[[PAGEADDR]], [[CSTLABEL]]@PAGEOFF]
; REGULAR: adrp [[PAGEADDR2:x[0-9]+]], [[CSTLABEL2:lCP.*]]@PAGE
; REGULAR: ldr q2, {{\[}}[[PAGEADDR2]], [[CSTLABEL2]]@PAGEOFF]
; REGULAR: adrp [[PAGEADDR3:x[0-9]+]], [[CSTLABEL3:lCP.*]]@PAGE
; REGULAR: ldr q3, {{\[}}[[PAGEADDR3]], [[CSTLABEL3]]@PAGEOFF]
; REGULAR-NEXT: ret
entry:
  ret %struct.uint8x16x4_t { [4 x <16 x i8>] [<16 x i8> <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>, <16 x i8> <i8 32, i8 124, i8 121, i8 120, i8 8, i8 117, i8 -56, i8 113, i8 -76, i8 110, i8 -53, i8 107, i8 7, i8 105, i8 103, i8 102>, <16 x i8> <i8 -24, i8 99, i8 -121, i8 97, i8 66, i8 95, i8 24, i8 93, i8 6, i8 91, i8 12, i8 89, i8 39, i8 87, i8 86, i8 85>, <16 x i8> <i8 -104, i8 83, i8 -20, i8 81, i8 81, i8 80, i8 -59, i8 78, i8 73, i8 77, i8 -37, i8 75, i8 122, i8 74, i8 37, i8 73>] }
}

; Two different uses of the same constant in the same basic block
define <16 x i8> @test2(<16 x i8> %arg) {
entry:
; PROMOTED-LABEL: test2:
; In stress mode, constant vector are promoted
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], [[CSTV1:__PromotedConst[0-9]+]]@PAGE
; PROMOTED: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTV1]]@PAGEOFF]
; Destination register is defined by ABI
; PROMOTED-NEXT: add.16b v0, v0, v[[REGNUM]]
; PROMOTED-NEXT: mla.16b v0, v0, v[[REGNUM]]
; PROMOTED-NEXT: ret

; REGULAR-LABEL: test2:
; Regular access is strickly the same as promoted access.
; The difference is that the address (and thus the space in memory) is not
; shared between constants
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL:lCP.*]]@PAGE
; REGULAR: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTLABEL]]@PAGEOFF]
; Destination register is defined by ABI
; REGULAR-NEXT: add.16b v0, v0, v[[REGNUM]]
; REGULAR-NEXT: mla.16b v0, v0, v[[REGNUM]]
; REGULAR-NEXT: ret
  %add.i = add <16 x i8> %arg, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  %mul.i = mul <16 x i8> %add.i, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  %add.i9 = add <16 x i8> %add.i, %mul.i
  ret <16 x i8> %add.i9
}

; Two different uses of the sane constant in two different basic blocks,
; one dominates the other
define <16 x i8> @test3(<16 x i8> %arg, i32 %path) {
; PROMOTED-LABEL: test3:
; In stress mode, constant vector are promoted
; Since, the constant is the same as the previous function,
; the same address must be used
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], [[CSTV1]]@PAGE
; PROMOTED-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTV1]]@PAGEOFF]
; Destination register is defined by ABI
; PROMOTED-NEXT: add.16b v0, v0, v[[REGNUM]]
; PROMOTED-NEXT: cbnz w0, [[LABEL:LBB.*]]
; Next BB
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], [[CSTV2:__PromotedConst[0-9]+]]@PAGE
; PROMOTED-NEXT: ldr q[[REGNUM]], {{\[}}[[PAGEADDR]], [[CSTV2]]@PAGEOFF]
; Next BB
; PROMOTED-NEXT: [[LABEL]]:
; PROMOTED-NEXT: mul.16b [[DESTV:v[0-9]+]], v0, v[[REGNUM]]
; PROMOTED-NEXT: add.16b v0, v0, [[DESTV]]
; PROMOTED-NEXT: ret

; REGULAR-LABEL: test3:
; Regular mode does not elimitate common sub expression by its own.
; In other words, the same loads appears several times.
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL1:lCP.*]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTLABEL1]]@PAGEOFF]
; Destination register is defined by ABI
; REGULAR-NEXT: add.16b v0, v0, v[[REGNUM]]
; REGULAR-NEXT: cbz w0, [[LABELelse:LBB.*]]
; Next BB
; Redundant load
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL1]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM]], {{\[}}[[PAGEADDR]], [[CSTLABEL1]]@PAGEOFF]
; REGULAR-NEXT: b [[LABELend:LBB.*]]
; Next BB
; REGULAR-NEXT: [[LABELelse]]
; REGULAR-NEXT: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL2:lCP.*]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM]], {{\[}}[[PAGEADDR]], [[CSTLABEL2]]@PAGEOFF]
; Next BB
; REGULAR-NEXT: [[LABELend]]:
; REGULAR-NEXT: mul.16b [[DESTV:v[0-9]+]], v0, v[[REGNUM]]
; REGULAR-NEXT: add.16b v0, v0, [[DESTV]]
; REGULAR-NEXT: ret
entry:
  %add.i = add <16 x i8> %arg, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  %tobool = icmp eq i32 %path, 0
  br i1 %tobool, label %if.else, label %if.then

if.then:                                          ; preds = %entry
  %mul.i13 = mul <16 x i8> %add.i, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  br label %if.end

if.else:                                          ; preds = %entry
  %mul.i = mul <16 x i8> %add.i, <i8 -24, i8 99, i8 -121, i8 97, i8 66, i8 95, i8 24, i8 93, i8 6, i8 91, i8 12, i8 89, i8 39, i8 87, i8 86, i8 85>
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  %ret2.0 = phi <16 x i8> [ %mul.i13, %if.then ], [ %mul.i, %if.else ]
  %add.i12 = add <16 x i8> %add.i, %ret2.0
  ret <16 x i8> %add.i12
}

; Two different uses of the sane constant in two different basic blocks,
; none dominates the other
define <16 x i8> @test4(<16 x i8> %arg, i32 %path) {
; PROMOTED-LABEL: test4:
; In stress mode, constant vector are promoted
; Since, the constant is the same as the previous function,
; the same address must be used
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], [[CSTV1]]@PAGE
; PROMOTED-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTV1]]@PAGEOFF]
; Destination register is defined by ABI
; PROMOTED-NEXT: add.16b v0, v0, v[[REGNUM]]
; PROMOTED-NEXT: cbz w0, [[LABEL:LBB.*]]
; Next BB
; PROMOTED: mul.16b v0, v0, v[[REGNUM]]
; Next BB
; PROMOTED-NEXT: [[LABEL]]:
; PROMOTED-NEXT: ret


; REGULAR-LABEL: test4:
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL3:lCP.*]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTLABEL3]]@PAGEOFF]
; Destination register is defined by ABI
; REGULAR-NEXT: add.16b v0, v0, v[[REGNUM]]
; REGULAR-NEXT: cbz w0, [[LABEL:LBB.*]]
; Next BB
; Redundant expression
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL3]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTLABEL3]]@PAGEOFF]
; Destination register is defined by ABI
; REGULAR-NEXT: mul.16b v0, v0, v[[REGNUM]]
; Next BB
; REGULAR-NEXT: [[LABEL]]:
; REGULAR-NEXT: ret
entry:
  %add.i = add <16 x i8> %arg, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  %tobool = icmp eq i32 %path, 0
  br i1 %tobool, label %if.end, label %if.then

if.then:                                          ; preds = %entry
  %mul.i = mul <16 x i8> %add.i, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  br label %if.end

if.end:                                           ; preds = %entry, %if.then
  %ret.0 = phi <16 x i8> [ %mul.i, %if.then ], [ %add.i, %entry ]
  ret <16 x i8> %ret.0
}

; Two different uses of the sane constant in two different basic blocks,
; one is in a phi.
define <16 x i8> @test5(<16 x i8> %arg, i32 %path) {
; PROMOTED-LABEL: test5:
; In stress mode, constant vector are promoted
; Since, the constant is the same as the previous function,
; the same address must be used
; PROMOTED: adrp [[PAGEADDR:x[0-9]+]], [[CSTV1]]@PAGE
; PROMOTED-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTV1]]@PAGEOFF]
; PROMOTED-NEXT: cbz w0, [[LABEL:LBB.*]]
; Next BB
; PROMOTED: add.16b [[DESTV:v[0-9]+]], v0, v[[REGNUM]]
; PROMOTED-NEXT: mul.16b v[[REGNUM]], [[DESTV]], v[[REGNUM]]
; Next BB
; PROMOTED-NEXT: [[LABEL]]:
; PROMOTED-NEXT: mul.16b [[TMP1:v[0-9]+]], v[[REGNUM]], v[[REGNUM]]
; PROMOTED-NEXT: mul.16b [[TMP2:v[0-9]+]], [[TMP1]], [[TMP1]]
; PROMOTED-NEXT: mul.16b [[TMP3:v[0-9]+]], [[TMP2]], [[TMP2]]
; PROMOTED-NEXT: mul.16b v0, [[TMP3]], [[TMP3]]
; PROMOTED-NEXT: ret

; REGULAR-LABEL: test5:
; REGULAR: cbz w0, [[LABELelse:LBB.*]]
; Next BB
; REGULAR: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL:lCP.*]]@PAGE
; REGULAR-NEXT: ldr q[[REGNUM:[0-9]+]], {{\[}}[[PAGEADDR]], [[CSTLABEL]]@PAGEOFF]
; REGULAR-NEXT: add.16b [[DESTV:v[0-9]+]], v0, v[[REGNUM]]
; REGULAR-NEXT: mul.16b v[[DESTREGNUM:[0-9]+]], [[DESTV]], v[[REGNUM]]
; REGULAR-NEXT: b [[LABELend:LBB.*]]
; Next BB
; REGULAR-NEXT: [[LABELelse]]
; REGULAR-NEXT: adrp [[PAGEADDR:x[0-9]+]], [[CSTLABEL:lCP.*]]@PAGE
; REGULAR-NEXT: ldr q[[DESTREGNUM]], {{\[}}[[PAGEADDR]], [[CSTLABEL]]@PAGEOFF]
; Next BB
; REGULAR-NEXT: [[LABELend]]:
; REGULAR-NEXT: mul.16b [[TMP1:v[0-9]+]], v[[DESTREGNUM]], v[[DESTREGNUM]]
; REGULAR-NEXT: mul.16b [[TMP2:v[0-9]+]], [[TMP1]], [[TMP1]]
; REGULAR-NEXT: mul.16b [[TMP3:v[0-9]+]], [[TMP2]], [[TMP2]]
; REGULAR-NEXT: mul.16b v0, [[TMP3]], [[TMP3]]
; REGULAR-NEXT: ret
entry:
  %tobool = icmp eq i32 %path, 0
  br i1 %tobool, label %if.end, label %if.then

if.then:                                          ; preds = %entry
  %add.i = add <16 x i8> %arg, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  %mul.i26 = mul <16 x i8> %add.i, <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>
  br label %if.end

if.end:                                           ; preds = %entry, %if.then
  %ret.0 = phi <16 x i8> [ %mul.i26, %if.then ], [ <i8 -40, i8 -93, i8 -118, i8 -99, i8 -75, i8 -105, i8 74, i8 -110, i8 62, i8 -115, i8 -119, i8 -120, i8 34, i8 -124, i8 0, i8 -128>, %entry ]
  %mul.i25 = mul <16 x i8> %ret.0, %ret.0
  %mul.i24 = mul <16 x i8> %mul.i25, %mul.i25
  %mul.i23 = mul <16 x i8> %mul.i24, %mul.i24
  %mul.i = mul <16 x i8> %mul.i23, %mul.i23
  ret <16 x i8> %mul.i
}

define void @accessBig(i64* %storage) {
; PROMOTED-LABEL: accessBig:
; PROMOTED: adrp
; PROMOTED: ret
  %addr = bitcast i64* %storage to <1 x i80>*
  store <1 x i80> <i80 483673642326615442599424>, <1 x i80>* %addr
  ret void
}

define void @asmStatement() {
; PROMOTED-LABEL: asmStatement:
; PROMOTED-NOT: adrp
; PROMOTED: ret
  call void asm sideeffect "bfxil w0, w0, $0, $1", "i,i"(i32 28, i32 4)
  ret void
}

