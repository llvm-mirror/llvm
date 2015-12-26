; RUN: llc -mcpu=pwr8 -mtriple=powerpc64le-unknown-linux-gnu -O3 < %s | FileCheck %s

; These tests verify that VSX swap optimization works when loading a scalar
; into a vector register.


@x = global <2 x double> <double 9.970000e+01, double -1.032220e+02>, align 16
@z = global <2 x double> <double 2.332000e+01, double 3.111111e+01>, align 16
@y = global double 1.780000e+00, align 8

define void @bar0() {
entry:
  %0 = load <2 x double>, <2 x double>* @x, align 16
  %1 = load double, double* @y, align 8
  %vecins = insertelement <2 x double> %0, double %1, i32 0
  store <2 x double> %vecins, <2 x double>* @z, align 16
  ret void
}

; CHECK-LABEL: @bar0
; CHECK-DAG: lxvd2x [[REG1:[0-9]+]]
; CHECK-DAG: lxsdx [[REG2:[0-9]+]]
; CHECK: xxspltd [[REG4:[0-9]+]], [[REG2]], 0
; CHECK: xxpermdi [[REG5:[0-9]+]], [[REG4]], [[REG1]], 1
; CHECK: stxvd2x [[REG5]]

define void @bar1() {
entry:
  %0 = load <2 x double>, <2 x double>* @x, align 16
  %1 = load double, double* @y, align 8
  %vecins = insertelement <2 x double> %0, double %1, i32 1
  store <2 x double> %vecins, <2 x double>* @z, align 16
  ret void
}

; CHECK-LABEL: @bar1
; CHECK-DAG: lxvd2x [[REG1:[0-9]+]]
; CHECK-DAG: lxsdx [[REG2:[0-9]+]]
; CHECK: xxspltd [[REG4:[0-9]+]], [[REG2]], 0
; CHECK: xxmrghd [[REG5:[0-9]+]], [[REG1]], [[REG4]]
; CHECK: stxvd2x [[REG5]]

