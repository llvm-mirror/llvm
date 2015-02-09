; RUN: llc < %s -mtriple=armv8-linux-gnueabihf -mattr=+fp-armv8 -float-abi=hard | FileCheck %s
@varfloat = global float 0.0
@vardouble = global double 0.0
define void @test_vsel32sgt(i32 %lhs32, i32 %rhs32, float %a, float %b) {
; CHECK: test_vsel32sgt
  %tst1 = icmp sgt i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: cmp r0, r1
; CHECK: vselgt.f32 s0, s0, s1
  ret void
}
define void @test_vsel64sgt(i32 %lhs32, i32 %rhs32, double %a, double %b) {
; CHECK: test_vsel64sgt
  %tst1 = icmp sgt i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: cmp r0, r1
; CHECK: vselgt.f64 d16, d0, d1
  ret void
}
define void @test_vsel32sge(i32 %lhs32, i32 %rhs32, float %a, float %b) {
; CHECK: test_vsel32sge
  %tst1 = icmp sge i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: cmp r0, r1
; CHECK: vselge.f32 s0, s0, s1
  ret void
}
define void @test_vsel64sge(i32 %lhs32, i32 %rhs32, double %a, double %b) {
; CHECK: test_vsel64sge
  %tst1 = icmp sge i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: cmp r0, r1
; CHECK: vselge.f64 d16, d0, d1
  ret void
}
define void @test_vsel32eq(i32 %lhs32, i32 %rhs32, float %a, float %b) {
; CHECK: test_vsel32eq
  %tst1 = icmp eq i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: cmp r0, r1
; CHECK: vseleq.f32 s0, s0, s1
  ret void
}
define void @test_vsel64eq(i32 %lhs32, i32 %rhs32, double %a, double %b) {
; CHECK: test_vsel64eq
  %tst1 = icmp eq i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: cmp r0, r1
; CHECK: vseleq.f64 d16, d0, d1
  ret void
}
define void @test_vsel32slt(i32 %lhs32, i32 %rhs32, float %a, float %b) {
; CHECK: test_vsel32slt
  %tst1 = icmp slt i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: cmp r0, r1
; CHECK: vselge.f32 s0, s1, s0
  ret void
}
define void @test_vsel64slt(i32 %lhs32, i32 %rhs32, double %a, double %b) {
; CHECK: test_vsel64slt
  %tst1 = icmp slt i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: cmp r0, r1
; CHECK: vselge.f64 d16, d1, d0
  ret void
}
define void @test_vsel32sle(i32 %lhs32, i32 %rhs32, float %a, float %b) {
; CHECK: test_vsel32sle
  %tst1 = icmp sle i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: cmp r0, r1
; CHECK: vselgt.f32 s0, s1, s0
  ret void
}
define void @test_vsel64sle(i32 %lhs32, i32 %rhs32, double %a, double %b) {
; CHECK: test_vsel64sle
  %tst1 = icmp sle i32 %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: cmp r0, r1
; CHECK: vselgt.f64 d16, d1, d0
  ret void
}
define void @test_vsel32ogt(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ogt
  %tst1 = fcmp ogt float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselgt.f32 s0, s2, s3
  ret void
}
define void @test_vsel64ogt(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ogt
  %tst1 = fcmp ogt float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselgt.f64 d16, d1, d2
  ret void
}
define void @test_vsel32oge(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32oge
  %tst1 = fcmp oge float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselge.f32 s0, s2, s3
  ret void
}
define void @test_vsel64oge(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64oge
  %tst1 = fcmp oge float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselge.f64 d16, d1, d2
  ret void
}
define void @test_vsel32oeq(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32oeq
  %tst1 = fcmp oeq float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vseleq.f32 s0, s2, s3
  ret void
}
define void @test_vsel64oeq(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64oeq
  %tst1 = fcmp oeq float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vseleq.f64 d16, d1, d2
  ret void
}
define void @test_vsel32ugt(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ugt
  %tst1 = fcmp ugt float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselge.f32 s0, s3, s2
  ret void
}
define void @test_vsel64ugt(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ugt
  %tst1 = fcmp ugt float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselge.f64 d16, d2, d1
  ret void
}
define void @test_vsel32uge(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32uge
  %tst1 = fcmp uge float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselgt.f32 s0, s3, s2
  ret void
}
define void @test_vsel64uge(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64uge
  %tst1 = fcmp uge float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselgt.f64 d16, d2, d1
  ret void
}
define void @test_vsel32olt(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32olt
  %tst1 = fcmp olt float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselgt.f32 s0, s2, s3
  ret void
}
define void @test_vsel64olt(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64olt
  %tst1 = fcmp olt float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselgt.f64 d16, d1, d2
  ret void
}
define void @test_vsel32ult(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ult
  %tst1 = fcmp ult float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselge.f32 s0, s3, s2
  ret void
}
define void @test_vsel64ult(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ult
  %tst1 = fcmp ult float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselge.f64 d16, d2, d1
  ret void
}
define void @test_vsel32ole(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ole
  %tst1 = fcmp ole float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselge.f32 s0, s2, s3
  ret void
}
define void @test_vsel64ole(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ole
  %tst1 = fcmp ole float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s1, s0
; CHECK: vselge.f64 d16, d1, d2
  ret void
}
define void @test_vsel32ule(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ule
  %tst1 = fcmp ule float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselgt.f32 s0, s3, s2
  ret void
}
define void @test_vsel64ule(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ule
  %tst1 = fcmp ule float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselgt.f64 d16, d2, d1
  ret void
}
define void @test_vsel32ord(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32ord
  %tst1 = fcmp ord float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselvs.f32 s0, s3, s2
  ret void
}
define void @test_vsel64ord(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64ord
  %tst1 = fcmp ord float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselvs.f64 d16, d2, d1
  ret void
}
define void @test_vsel32une(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32une
  %tst1 = fcmp une float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vseleq.f32 s0, s3, s2
  ret void
}
define void @test_vsel64une(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64une
  %tst1 = fcmp une float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vseleq.f64 d16, d2, d1
  ret void
}
define void @test_vsel32uno(float %lhs32, float %rhs32, float %a, float %b) {
; CHECK: test_vsel32uno
  %tst1 = fcmp uno float %lhs32, %rhs32
  %val1 = select i1 %tst1, float %a, float %b
  store float %val1, float* @varfloat
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselvs.f32 s0, s2, s3
  ret void
}
define void @test_vsel64uno(float %lhs32, float %rhs32, double %a, double %b) {
; CHECK: test_vsel64uno
  %tst1 = fcmp uno float %lhs32, %rhs32
  %val1 = select i1 %tst1, double %a, double %b
  store double %val1, double* @vardouble
; CHECK: vcmpe.f32 s0, s1
; CHECK: vselvs.f64 d16, d1, d2
  ret void
}
