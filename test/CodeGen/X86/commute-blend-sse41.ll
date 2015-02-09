; RUN: llc -O3 -mtriple=x86_64-unknown -mcpu=corei7 < %s | FileCheck %s

define <8 x i16> @commute_fold_pblendw(<8 x i16> %a, <8 x i16>* %b) #0 {
  %1 = load <8 x i16>* %b
  %2 = call <8 x i16> @llvm.x86.sse41.pblendw(<8 x i16> %1, <8 x i16> %a, i8 17)
  ret <8 x i16> %2

  ;LABEL:      commute_fold_pblendw
  ;CHECK:      pblendw {{.*#+}} xmm0 = xmm0[0],mem[1,2,3],xmm0[4],mem[5,6,7]
  ;CHECK-NEXT: retq
}
declare <8 x i16> @llvm.x86.sse41.pblendw(<8 x i16>, <8 x i16>, i8) nounwind readnone

define <4 x float> @commute_fold_blendps(<4 x float> %a, <4 x float>* %b) #0 {
  %1 = load <4 x float>* %b
  %2 = call <4 x float> @llvm.x86.sse41.blendps(<4 x float> %1, <4 x float> %a, i8 3)
  ret <4 x float> %2

  ;LABEL:      commute_fold_blendps
  ;CHECK:      blendps {{.*#+}} xmm0 = xmm0[0,1],mem[2,3]
  ;CHECK-NEXT: retq
}
declare <4 x float> @llvm.x86.sse41.blendps(<4 x float>, <4 x float>, i8) nounwind readnone

define <2 x double> @commute_fold_blendpd(<2 x double> %a, <2 x double>* %b) #0 {
  %1 = load <2 x double>* %b
  %2 = call <2 x double> @llvm.x86.sse41.blendpd(<2 x double> %1, <2 x double> %a, i8 1)
  ret <2 x double> %2

  ;LABEL:      commute_fold_vblendpd
  ;CHECK:      blendpd {{.*#+}} xmm0 = xmm0[0],mem[1]
  ;CHECK-NEXT: retq
}
declare <2 x double> @llvm.x86.sse41.blendpd(<2 x double>, <2 x double>, i8) nounwind readnone
