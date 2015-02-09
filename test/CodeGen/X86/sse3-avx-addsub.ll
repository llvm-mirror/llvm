; RUN: llc < %s -march=x86-64 -mcpu=core2 | FileCheck %s -check-prefix=SSE -check-prefix=CHECK
; RUN: llc < %s -march=x86-64 -mcpu=corei7-avx | FileCheck %s -check-prefix=AVX -check-prefix=CHECK

; Test ADDSUB ISel patterns.

; Functions below are obtained from the following source:
;
; typedef double double2 __attribute__((ext_vector_type(2)));
; typedef double double4 __attribute__((ext_vector_type(4)));
; typedef float float4 __attribute__((ext_vector_type(4)));
; typedef float float8 __attribute__((ext_vector_type(8)));
;
; float4 test1(float4 A, float4 B) {
;   float4 X = A - B;
;   float4 Y = A + B;
;   return (float4){X[0], Y[1], X[2], Y[3]};
; }
;
; float8 test2(float8 A, float8 B) {
;   float8 X = A - B;
;   float8 Y = A + B;
;   return (float8){X[0], Y[1], X[2], Y[3], X[4], Y[5], X[6], Y[7]};
; }
;
; double4 test3(double4 A, double4 B) {
;   double4 X = A - B;
;   double4 Y = A + B;
;   return (double4){X[0], Y[1], X[2], Y[3]};
; }
;
; double2 test4(double2 A, double2 B) {
;   double2 X = A - B;
;   double2 Y = A + B;
;   return (double2){X[0], Y[1]};
; }

define <4 x float> @test1(<4 x float> %A, <4 x float> %B) {
  %sub = fsub <4 x float> %A, %B
  %add = fadd <4 x float> %A, %B
  %vecinit6 = shufflevector <4 x float> %sub, <4 x float> %add, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x float> %vecinit6
}
; CHECK-LABEL: test1
; SSE: addsubps
; AVX: vaddsubps
; CHECK-NEXT: ret


define <8 x float> @test2(<8 x float> %A, <8 x float> %B) {
  %sub = fsub <8 x float> %A, %B
  %add = fadd <8 x float> %A, %B
  %vecinit14 = shufflevector <8 x float> %sub, <8 x float> %add, <8 x i32> <i32 0, i32 9, i32 2, i32 11, i32 4, i32 13, i32 6, i32 15>
  ret <8 x float> %vecinit14
}
; CHECK-LABEL: test2
; SSE: addsubps
; SSE-NEXT: addsubps
; AVX: vaddsubps
; AVX-NOT: vaddsubps
; CHECK: ret


define <4 x double> @test3(<4 x double> %A, <4 x double> %B) {
  %sub = fsub <4 x double> %A, %B
  %add = fadd <4 x double> %A, %B
  %vecinit6 = shufflevector <4 x double> %sub, <4 x double> %add, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x double> %vecinit6
}
; CHECK-LABEL: test3
; SSE: addsubpd
; SSE: addsubpd
; AVX: vaddsubpd
; AVX-NOT: vaddsubpd
; CHECK: ret


define <2 x double> @test4(<2 x double> %A, <2 x double> %B) #0 {
  %add = fadd <2 x double> %A, %B
  %sub = fsub <2 x double> %A, %B
  %vecinit2 = shufflevector <2 x double> %sub, <2 x double> %add, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %vecinit2
}
; CHECK-LABEL: test4
; SSE: addsubpd
; AVX: vaddsubpd
; CHECK-NEXT: ret


define <4 x float> @test1b(<4 x float> %A, <4 x float>* %B) {
  %1 = load <4 x float>* %B
  %add = fadd <4 x float> %A, %1
  %sub = fsub <4 x float> %A, %1
  %vecinit6 = shufflevector <4 x float> %sub, <4 x float> %add, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x float> %vecinit6
}
; CHECK-LABEL: test1b
; SSE: addsubps
; AVX: vaddsubps
; CHECK-NEXT: ret


define <8 x float> @test2b(<8 x float> %A, <8 x float>* %B) {
  %1 = load <8 x float>* %B
  %add = fadd <8 x float> %A, %1
  %sub = fsub <8 x float> %A, %1
  %vecinit14 = shufflevector <8 x float> %sub, <8 x float> %add, <8 x i32> <i32 0, i32 9, i32 2, i32 11, i32 4, i32 13, i32 6, i32 15>
  ret <8 x float> %vecinit14
}
; CHECK-LABEL: test2b
; SSE: addsubps
; SSE-NEXT: addsubps
; AVX: vaddsubps
; AVX-NOT: vaddsubps
; CHECK: ret


define <4 x double> @test3b(<4 x double> %A, <4 x double>* %B) {
  %1 = load <4 x double>* %B
  %add = fadd <4 x double> %A, %1
  %sub = fsub <4 x double> %A, %1
  %vecinit6 = shufflevector <4 x double> %sub, <4 x double> %add, <4 x i32> <i32 0, i32 5, i32 2, i32 7>
  ret <4 x double> %vecinit6
}
; CHECK-LABEL: test3b
; SSE: addsubpd
; SSE: addsubpd
; AVX: vaddsubpd
; AVX-NOT: vaddsubpd
; CHECK: ret


define <2 x double> @test4b(<2 x double> %A, <2 x double>* %B) {
  %1 = load <2 x double>* %B
  %sub = fsub <2 x double> %A, %1
  %add = fadd <2 x double> %A, %1
  %vecinit2 = shufflevector <2 x double> %sub, <2 x double> %add, <2 x i32> <i32 0, i32 3>
  ret <2 x double> %vecinit2
}
; CHECK-LABEL: test4b
; SSE: addsubpd
; AVX: vaddsubpd
; CHECK-NEXT: ret

