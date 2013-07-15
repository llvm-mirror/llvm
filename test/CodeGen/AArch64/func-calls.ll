; RUN: llc -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu | FileCheck %s

%myStruct = type { i64 , i8, i32 }

@var8 = global i8 0
@var8_2 = global i8 0
@var32 = global i32 0
@var64 = global i64 0
@var128 = global i128 0
@varfloat = global float 0.0
@varfloat_2 = global float 0.0
@vardouble = global double 0.0
@varstruct = global %myStruct zeroinitializer
@varsmallstruct = global [2 x i64] zeroinitializer

declare void @take_i8s(i8 %val1, i8 %val2)
declare void @take_floats(float %val1, float %val2)

define void @simple_args() {
; CHECK-LABEL: simple_args:
  %char1 = load i8* @var8
  %char2 = load i8* @var8_2
  call void @take_i8s(i8 %char1, i8 %char2)
; CHECK: ldrb w0, [{{x[0-9]+}}, #:lo12:var8]
; CHECK: ldrb w1, [{{x[0-9]+}}, #:lo12:var8_2]
; CHECK: bl take_i8s

  %float1 = load float* @varfloat
  %float2 = load float* @varfloat_2
  call void @take_floats(float %float1, float %float2)
; CHECK: ldr s1, [{{x[0-9]+}}, #:lo12:varfloat_2]
; CHECK: ldr s0, [{{x[0-9]+}}, #:lo12:varfloat]
; CHECK: bl take_floats

  ret void
}

declare i32 @return_int()
declare double @return_double()
declare [2 x i64] @return_smallstruct()
declare void @return_large_struct(%myStruct* sret %retval)

define void @simple_rets() {
; CHECK-LABEL: simple_rets:

  %int = call i32 @return_int()
  store i32 %int, i32* @var32
; CHECK: bl return_int
; CHECK: str w0, [{{x[0-9]+}}, #:lo12:var32]

  %dbl = call double @return_double()
  store double %dbl, double* @vardouble
; CHECK: bl return_double
; CHECK: str d0, [{{x[0-9]+}}, #:lo12:vardouble]

  %arr = call [2 x i64] @return_smallstruct()
  store [2 x i64] %arr, [2 x i64]* @varsmallstruct
; CHECK: bl return_smallstruct
; CHECK: str x1, [{{x[0-9]+}}, #8]
; CHECK: str x0, [{{x[0-9]+}}, #:lo12:varsmallstruct]

  call void @return_large_struct(%myStruct* sret @varstruct)
; CHECK: add x8, {{x[0-9]+}}, #:lo12:varstruct
; CHECK: bl return_large_struct

  ret void
}


declare i32 @struct_on_stack(i8 %var0, i16 %var1, i32 %var2, i64 %var3, i128 %var45,
                             i32* %var6, %myStruct* byval %struct, i32 %stacked,
                             double %notstacked)
declare void @stacked_fpu(float %var0, double %var1, float %var2, float %var3,
                          float %var4, float %var5, float %var6, float %var7,
                          float %var8)

define void @check_stack_args() {
  call i32 @struct_on_stack(i8 0, i16 12, i32 42, i64 99, i128 1,
                            i32* @var32, %myStruct* byval @varstruct,
                            i32 999, double 1.0)
  ; Want to check that the final double is passed in registers and
  ; that varstruct is passed on the stack. Rather dependent on how a
  ; memcpy gets created, but the following works for now.
; CHECK: mov x0, sp
; CHECK: str {{w[0-9]+}}, [x0]
; CHECK: str {{w[0-9]+}}, [x0, #12]
; CHECK: fmov d0,
; CHECK: bl struct_on_stack

  call void @stacked_fpu(float -1.0, double 1.0, float 4.0, float 2.0,
                         float -2.0, float -8.0, float 16.0, float 1.0,
                         float 64.0)
; CHECK: ldr s[[STACKEDREG:[0-9]+]], [{{x[0-9]+}}, #:lo12:.LCPI
; CHECK: mov x0, sp
; CHECK: str d[[STACKEDREG]], [x0]
; CHECK: bl stacked_fpu
  ret void
}


declare void @check_i128_stackalign(i32 %val0, i32 %val1, i32 %val2, i32 %val3,
                                    i32 %val4, i32 %val5, i32 %val6, i32 %val7,
                                    i32 %stack1, i128 %stack2)

declare void @check_i128_regalign(i32 %val0, i128 %val1)


define void @check_i128_align() {
; CHECK-LABEL: check_i128_align:
  %val = load i128* @var128
  call void @check_i128_stackalign(i32 0, i32 1, i32 2, i32 3,
                                   i32 4, i32 5, i32 6, i32 7,
                                   i32 42, i128 %val)
; CHECK: ldr [[I128LO:x[0-9]+]], [{{x[0-9]+}}, #:lo12:var128]
; CHECK: ldr [[I128HI:x[0-9]+]], [{{x[0-9]+}}, #8]
; CHECK: mov x[[SPREG:[0-9]+]], sp
; CHECK: str [[I128HI]], [x[[SPREG]], #24]
; CHECK: str [[I128LO]], [x[[SPREG]], #16]
; CHECK: bl check_i128_stackalign

  call void @check_i128_regalign(i32 0, i128 42)
; CHECK-NOT: mov x1
; CHECK: movz x2, #42
; CHECK: mov x3, xzr
; CHECK: bl check_i128_regalign

  ret void
}

@fptr = global void()* null

define void @check_indirect_call() {
; CHECK-LABEL: check_indirect_call:
  %func = load void()** @fptr
  call void %func()
; CHECK: ldr [[FPTR:x[0-9]+]], [{{x[0-9]+}}, #:lo12:fptr]
; CHECK: blr [[FPTR]]

  ret void
}
