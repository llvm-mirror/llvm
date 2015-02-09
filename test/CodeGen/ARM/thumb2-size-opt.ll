; RUN: llc -mtriple=thumbv7-linux-gnueabihf -o - -show-mc-encoding -t2-reduce-limit=0 -t2-reduce-limit2=0 %s | FileCheck %s
; RUN: llc -mtriple=thumbv7-linux-gnueabihf -o - -show-mc-encoding %s | FileCheck %s --check-prefix=CHECK-OPT

define i32 @and(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: and:
; CHECK: and.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: ands r{{[0-7]}}, r{{[0-7]}} @ encoding: [{{0x..,0x..}}]
entry:
  %and = and i32 %b, %a
  ret i32 %and
}

define i32 @asr-imm(i32 %a) nounwind readnone {
; CHECK-LABEL: "asr-imm":
; CHECK: asr.w r{{[0-9]+}}, r{{[0-9]+}}, #13 @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: asrs r{{[0-7]}}, r{{[0-7]}}, #13 @ encoding: [{{0x..,0x..}}]
entry:
  %shr = ashr i32 %a, 13
  ret i32 %shr
}

define i32 @asr-reg(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: "asr-reg":
; CHECK: asr.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: asrs r{{[0-7]}}, r{{[0-7]}} @ encoding: [{{0x..,0x..}}]
entry:
  %shr = ashr i32 %a, %b
  ret i32 %shr
}

define i32 @bic(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: bic:
; CHECK: bic.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: bics r{{[0-7]}}, r{{[0-7]}} @ encoding: [{{0x..,0x..}}]
entry:
  %neg = xor i32 %b, -1
  %and = and i32 %neg, %a
  ret i32 %and
}

define i32 @eor(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: eor:
; CHECK: eor.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: eors r{{[0-7]}}, r{{[0-7]}} @ encoding: [{{0x..,0x..}}]
entry:
  %eor = xor i32 %a, %b
  ret i32 %eor
}

define i32 @lsl-imm(i32 %a) nounwind readnone {
; CHECK-LABEL: "lsl-imm":
; CHECK: lsl.w r{{[0-9]+}}, r{{[0-9]+}}, #13 @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: lsls r{{[0-7]}}, r{{[0-7]}}, #13  @ encoding: [{{0x..,0x..}}]
entry:
  %shl = shl i32 %a, 13
  ret i32 %shl
}

define i32 @lsl-reg(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: "lsl-reg":
; CHECK: lsl.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: lsls r{{[0-7]}}, r{{[0-7]}}  @ encoding: [{{0x..,0x..}}]
entry:
  %shl = shl i32 %a, %b
  ret i32 %shl
}

define i32 @lsr-imm(i32 %a) nounwind readnone {
; CHECK-LABEL: "lsr-imm":
; CHECK: lsr.w r{{[0-9]+}}, r{{[0-9]+}}, #13 @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: lsrs r{{[0-7]}}, r{{[0-7]}}, #13  @ encoding: [{{0x..,0x..}}]
entry:
  %shr = lshr i32 %a, 13
  ret i32 %shr
}

define i32 @lsr-reg(i32 %a, i32 %b) nounwind readnone {
; CHECK-LABEL: "lsr-reg":
; CHECK: lsr.w r{{[0-9]+}}, r{{[0-9]+}}, r{{[0-9]+}} @ encoding: [{{0x..,0x..,0x..,0x..}}]
; CHECK-OPT: lsrs r{{[0-7]}}, r{{[0-7]}}  @ encoding: [{{0x..,0x..}}]
entry:
  %shr = lshr i32 %a, %b
  ret i32 %shr
}
