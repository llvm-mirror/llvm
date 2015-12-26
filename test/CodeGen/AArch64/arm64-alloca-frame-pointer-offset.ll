; RUN: llc -march=arm64 -mcpu=cyclone < %s | FileCheck %s

; CHECK: foo
; CHECK: str w[[REG0:[0-9]+]], [x19, #264]
; CHECK: mov w[[REG1:[0-9]+]], w[[REG0]]
; CHECK: str w[[REG1]], [x19, #132]

define i32 @foo(i32 %a) nounwind {
  %retval = alloca i32, align 4
  %a.addr = alloca i32, align 4
  %arr = alloca [32 x i32], align 4
  %i = alloca i32, align 4
  %arr2 = alloca [32 x i32], align 4
  %j = alloca i32, align 4
  store i32 %a, i32* %a.addr, align 4
  %tmp = load i32, i32* %a.addr, align 4
  %tmp1 = zext i32 %tmp to i64
  %v = mul i64 4, %tmp1
  %vla = alloca i8, i64 %v, align 4
  %tmp2 = bitcast i8* %vla to i32*
  %tmp3 = load i32, i32* %a.addr, align 4
  store i32 %tmp3, i32* %i, align 4
  %tmp4 = load i32, i32* %a.addr, align 4
  store i32 %tmp4, i32* %j, align 4
  %tmp5 = load i32, i32* %j, align 4
  store i32 %tmp5, i32* %retval
  %x = load i32, i32* %retval
  ret i32 %x
}
