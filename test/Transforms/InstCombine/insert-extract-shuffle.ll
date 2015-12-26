; RUN: opt -S -instcombine %s | FileCheck %s

define <1 x i8> @test1(<8 x i8> %in) {
; CHECK-LABEL: @test1
; CHECK: shufflevector <8 x i8> %in, <8 x i8> undef, <1 x i32> <i32 5>
  %val = extractelement <8 x i8> %in, i32 5
  %vec = insertelement <1 x i8> undef, i8 %val, i32 0
  ret <1 x i8> %vec
}

define <4 x i16> @test2(<8 x i16> %in, <8 x i16> %in2) {
; CHECK-LABEL: @test2
; CHECK: shufflevector <8 x i16> %in2, <8 x i16> %in, <4 x i32> <i32 11, i32 9, i32 0, i32 10>
  %elt0 = extractelement <8 x i16> %in, i32 3
  %elt1 = extractelement <8 x i16> %in, i32 1
  %elt2 = extractelement <8 x i16> %in2, i32 0
  %elt3 = extractelement <8 x i16> %in, i32 2

  %vec.0 = insertelement <4 x i16> undef, i16 %elt0, i32 0
  %vec.1 = insertelement <4 x i16> %vec.0, i16 %elt1, i32 1
  %vec.2 = insertelement <4 x i16> %vec.1, i16 %elt2, i32 2
  %vec.3 = insertelement <4 x i16> %vec.2, i16 %elt3, i32 3

  ret <4 x i16> %vec.3
}

define <2 x i64> @test_vcopyq_lane_p64(<2 x i64> %a, <1 x i64> %b) {
; CHECK-LABEL: @test_vcopyq_lane_p64
; CHECK-NEXT: %[[WIDEVEC:.*]] = shufflevector <1 x i64> %b, <1 x i64> undef, <2 x i32> <i32 0, i32 undef>
; CHECK-NEXT: shufflevector <2 x i64> %a, <2 x i64> %[[WIDEVEC]], <2 x i32> <i32 0, i32 2>
; CHECK-NEXT: ret <2 x i64> %res
  %elt = extractelement <1 x i64> %b, i32 0
  %res = insertelement <2 x i64> %a, i64 %elt, i32 1
  ret <2 x i64> %res
}

; PR2109: https://llvm.org/bugs/show_bug.cgi?id=2109

define <4 x float> @widen_extract2(<4 x float> %ins, <2 x float> %ext) {
; CHECK-LABEL: @widen_extract2(
; CHECK-NEXT: %[[WIDEVEC:.*]] = shufflevector <2 x float> %ext, <2 x float> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
; CHECK-NEXT: shufflevector <4 x float> %ins, <4 x float> %[[WIDEVEC]], <4 x i32> <i32 0, i32 4, i32 2, i32 5>
; CHECK-NEXT: ret <4 x float> %i2
  %e1 = extractelement <2 x float> %ext, i32 0
  %e2 = extractelement <2 x float> %ext, i32 1
  %i1 = insertelement <4 x float> %ins, float %e1, i32 1
  %i2 = insertelement <4 x float> %i1, float %e2, i32 3
  ret <4 x float> %i2
}

define <4 x float> @widen_extract3(<4 x float> %ins, <3 x float> %ext) {
; CHECK-LABEL: @widen_extract3(
; CHECK-NEXT: %[[WIDEVEC:.*]] = shufflevector <3 x float> %ext, <3 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 undef>
; CHECK-NEXT: shufflevector <4 x float> %ins, <4 x float> %[[WIDEVEC]], <4 x i32> <i32 6, i32 5, i32 4, i32 3>
; CHECK-NEXT: ret <4 x float> %i3
  %e1 = extractelement <3 x float> %ext, i32 0
  %e2 = extractelement <3 x float> %ext, i32 1
  %e3 = extractelement <3 x float> %ext, i32 2
  %i1 = insertelement <4 x float> %ins, float %e1, i32 2
  %i2 = insertelement <4 x float> %i1, float %e2, i32 1
  %i3 = insertelement <4 x float> %i2, float %e3, i32 0
  ret <4 x float> %i3
}

define <8 x float> @widen_extract4(<8 x float> %ins, <2 x float> %ext) {
; CHECK-LABEL: @widen_extract4(
; CHECK-NEXT: %[[WIDEVEC:.*]] = shufflevector <2 x float> %ext, <2 x float> undef, <8 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
; CHECK-NEXT: shufflevector <8 x float> %ins, <8 x float> %[[WIDEVEC]], <8 x i32> <i32 0, i32 1, i32 8, i32 3, i32 4, i32 5, i32 6, i32 7>
; CHECK-NEXT: ret <8 x float> %i1
  %e1 = extractelement <2 x float> %ext, i32 0
  %i1 = insertelement <8 x float> %ins, float %e1, i32 2
  ret <8 x float> %i1
}

