target datalayout = "E-m:e-p:32:32-i64:64-n32"
target triple = "powerpc"
; RUN: llc -mcpu=ppc < %s | FileCheck %s
; RUN: llc -mcpu=pwr7 < %s | FileCheck %s

define i64 @test1() nounwind {
entry:
  %r = call i64 @llvm.readcyclecounter()
  ret i64 %r
}

; CHECK: @test1
; CHECK: mfspr 3, 269
; CHECK: mfspr 4, 268
; CHECK: mfspr [[REG:[0-9]+]], 269
; CHECK: cmpw [[CR:[0-9]+]], 3, [[REG]]
; CHECK: bne [[CR]], .LBB

declare i64 @llvm.readcyclecounter()

