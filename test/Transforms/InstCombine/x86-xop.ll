; RUN: opt < %s -instcombine -S | FileCheck %s

define <2 x i64> @cmp_slt_v2i64(<2 x i64> %a, <2 x i64> %b) {
; CHECK-LABEL: @cmp_slt_v2i64
; CHECK-NEXT: %1 = icmp slt <2 x i64> %a, %b
; CHECK-NEXT: %2 = sext <2 x i1> %1 to <2 x i64>
; CHECK-NEXT: ret <2 x i64> %2
  %1 = tail call <2 x i64> @llvm.x86.xop.vpcomltq(<2 x i64> %a, <2 x i64> %b)
  ret <2 x i64> %1
}

define <2 x i64> @cmp_ult_v2i64(<2 x i64> %a, <2 x i64> %b) {
; CHECK-LABEL: @cmp_ult_v2i64
; CHECK-NEXT: %1 = icmp ult <2 x i64> %a, %b
; CHECK-NEXT: %2 = sext <2 x i1> %1 to <2 x i64>
; CHECK-NEXT: ret <2 x i64> %2
  %1 = tail call <2 x i64> @llvm.x86.xop.vpcomltuq(<2 x i64> %a, <2 x i64> %b)
  ret <2 x i64> %1
}

define <2 x i64> @cmp_sle_v2i64(<2 x i64> %a, <2 x i64> %b) {
; CHECK-LABEL: @cmp_sle_v2i64
; CHECK-NEXT: %1 = icmp sle <2 x i64> %a, %b
; CHECK-NEXT: %2 = sext <2 x i1> %1 to <2 x i64>
; CHECK-NEXT: ret <2 x i64> %2
  %1 = tail call <2 x i64> @llvm.x86.xop.vpcomleq(<2 x i64> %a, <2 x i64> %b)
  ret <2 x i64> %1
}

define <2 x i64> @cmp_ule_v2i64(<2 x i64> %a, <2 x i64> %b) {
; CHECK-LABEL: @cmp_ule_v2i64
; CHECK-NEXT: %1 = icmp ule <2 x i64> %a, %b
; CHECK-NEXT: %2 = sext <2 x i1> %1 to <2 x i64>
; CHECK-NEXT: ret <2 x i64> %2
  %1 = tail call <2 x i64> @llvm.x86.xop.vpcomleuq(<2 x i64> %a, <2 x i64> %b)
  ret <2 x i64> %1
}

define <4 x i32> @cmp_sgt_v4i32(<4 x i32> %a, <4 x i32> %b) {
; CHECK-LABEL: @cmp_sgt_v4i32
; CHECK-NEXT: %1 = icmp sgt <4 x i32> %a, %b
; CHECK-NEXT: %2 = sext <4 x i1> %1 to <4 x i32>
; CHECK-NEXT: ret <4 x i32> %2
  %1 = tail call <4 x i32> @llvm.x86.xop.vpcomgtd(<4 x i32> %a, <4 x i32> %b)
  ret <4 x i32> %1
}

define <4 x i32> @cmp_ugt_v4i32(<4 x i32> %a, <4 x i32> %b) {
; CHECK-LABEL: @cmp_ugt_v4i32
; CHECK-NEXT: %1 = icmp ugt <4 x i32> %a, %b
; CHECK-NEXT: %2 = sext <4 x i1> %1 to <4 x i32>
; CHECK-NEXT: ret <4 x i32> %2
  %1 = tail call <4 x i32> @llvm.x86.xop.vpcomgtud(<4 x i32> %a, <4 x i32> %b)
  ret <4 x i32> %1
}

define <4 x i32> @cmp_sge_v4i32(<4 x i32> %a, <4 x i32> %b) {
; CHECK-LABEL: @cmp_sge_v4i32
; CHECK-NEXT: %1 = icmp sge <4 x i32> %a, %b
; CHECK-NEXT: %2 = sext <4 x i1> %1 to <4 x i32>
; CHECK-NEXT: ret <4 x i32> %2
  %1 = tail call <4 x i32> @llvm.x86.xop.vpcomged(<4 x i32> %a, <4 x i32> %b)
  ret <4 x i32> %1
}

define <4 x i32> @cmp_uge_v4i32(<4 x i32> %a, <4 x i32> %b) {
; CHECK-LABEL: @cmp_uge_v4i32
; CHECK-NEXT: %1 = icmp uge <4 x i32> %a, %b
; CHECK-NEXT: %2 = sext <4 x i1> %1 to <4 x i32>
; CHECK-NEXT: ret <4 x i32> %2
  %1 = tail call <4 x i32> @llvm.x86.xop.vpcomgeud(<4 x i32> %a, <4 x i32> %b)
  ret <4 x i32> %1
}

define <8 x i16> @cmp_seq_v8i16(<8 x i16> %a, <8 x i16> %b) {
; CHECK-LABEL: @cmp_seq_v8i16
; CHECK-NEXT: %1 = icmp eq <8 x i16> %a, %b
; CHECK-NEXT: %2 = sext <8 x i1> %1 to <8 x i16>
; CHECK-NEXT: ret <8 x i16> %2
  %1 = tail call <8 x i16> @llvm.x86.xop.vpcomeqw(<8 x i16> %a, <8 x i16> %b)
  ret <8 x i16> %1
}

define <8 x i16> @cmp_ueq_v8i16(<8 x i16> %a, <8 x i16> %b) {
; CHECK-LABEL: @cmp_ueq_v8i16
; CHECK-NEXT: %1 = icmp eq <8 x i16> %a, %b
; CHECK-NEXT: %2 = sext <8 x i1> %1 to <8 x i16>
; CHECK-NEXT: ret <8 x i16> %2
  %1 = tail call <8 x i16> @llvm.x86.xop.vpcomequw(<8 x i16> %a, <8 x i16> %b)
  ret <8 x i16> %1
}

define <8 x i16> @cmp_sne_v8i16(<8 x i16> %a, <8 x i16> %b) {
; CHECK-LABEL: @cmp_sne_v8i16
; CHECK-NEXT: %1 = icmp ne <8 x i16> %a, %b
; CHECK-NEXT: %2 = sext <8 x i1> %1 to <8 x i16>
; CHECK-NEXT: ret <8 x i16> %2
  %1 = tail call <8 x i16> @llvm.x86.xop.vpcomnew(<8 x i16> %a, <8 x i16> %b)
  ret <8 x i16> %1
}

define <8 x i16> @cmp_une_v8i16(<8 x i16> %a, <8 x i16> %b) {
; CHECK-LABEL: @cmp_une_v8i16
; CHECK-NEXT: %1 = icmp ne <8 x i16> %a, %b
; CHECK-NEXT: %2 = sext <8 x i1> %1 to <8 x i16>
; CHECK-NEXT: ret <8 x i16> %2
  %1 = tail call <8 x i16> @llvm.x86.xop.vpcomneuw(<8 x i16> %a, <8 x i16> %b)
  ret <8 x i16> %1
}

define <16 x i8> @cmp_strue_v16i8(<16 x i8> %a, <16 x i8> %b) {
; CHECK-LABEL: @cmp_strue_v16i8
; CHECK-NEXT: ret <16 x i8> <i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1>
  %1 = tail call <16 x i8> @llvm.x86.xop.vpcomtrueb(<16 x i8> %a, <16 x i8> %b)
  ret <16 x i8> %1
}

define <16 x i8> @cmp_utrue_v16i8(<16 x i8> %a, <16 x i8> %b) {
; CHECK-LABEL: @cmp_utrue_v16i8
; CHECK-NEXT: ret <16 x i8> <i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1>
  %1 = tail call <16 x i8> @llvm.x86.xop.vpcomtrueub(<16 x i8> %a, <16 x i8> %b)
  ret <16 x i8> %1
}

define <16 x i8> @cmp_sfalse_v16i8(<16 x i8> %a, <16 x i8> %b) {
; CHECK-LABEL: @cmp_sfalse_v16i8
; CHECK-NEXT: ret <16 x i8> zeroinitializer
  %1 = tail call <16 x i8> @llvm.x86.xop.vpcomfalseb(<16 x i8> %a, <16 x i8> %b)
  ret <16 x i8> %1
}

define <16 x i8> @cmp_ufalse_v16i8(<16 x i8> %a, <16 x i8> %b) {
; CHECK-LABEL: @cmp_ufalse_v16i8
; CHECK-NEXT: ret <16 x i8> zeroinitializer
  %1 = tail call <16 x i8> @llvm.x86.xop.vpcomfalseub(<16 x i8> %a, <16 x i8> %b)
  ret <16 x i8> %1
}

declare <16 x i8> @llvm.x86.xop.vpcomltb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomltw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomltd(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomltq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomltub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomltuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomltud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomltuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomleb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomlew(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomled(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomleq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomleub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomleuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomleud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomleuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomgtb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomgtw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomgtd(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomgtq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomgtub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomgtuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomgtud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomgtuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomgeb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomgew(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomged(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomgeq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomgeub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomgeuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomgeud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomgeuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomeqb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomeqw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomeqd(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomeqq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomequb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomequw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomequd(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomequq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomneb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomnew(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomned(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomneq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomneub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomneuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomneud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomneuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomfalseb(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomfalsew(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomfalsed(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomfalseq(<2 x i64>, <2 x i64>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomfalseub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomfalseuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomfalseud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomfalseuq(<2 x i64>, <2 x i64>) nounwind readnone

declare <16 x i8> @llvm.x86.xop.vpcomtrueb(<16 x i8>, <16 x i8>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomtrued(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomtrueq(<2 x i64>, <2 x i64>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomtruew(<8 x i16>, <8 x i16>) nounwind readnone
declare <16 x i8> @llvm.x86.xop.vpcomtrueub(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.x86.xop.vpcomtrueuw(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.x86.xop.vpcomtrueud(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i64> @llvm.x86.xop.vpcomtrueuq(<2 x i64>, <2 x i64>) nounwind readnone
