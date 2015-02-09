; RUN: llc -mtriple=i686-linux-gnu -o - %s | FileCheck %s

@var = global i128 0

; We were trying to convert the i128 operation into a libcall, but failing to
; perform sret demotion when we couldn't return the result in registers. Make
; sure we marshal the return properly:

define void @test_sret_libcall(i128 %l, i128 %r) {
; CHECK-LABEL: test_sret_libcall:

  ; Stack for call: 4(sret ptr), 16(i128 %l), 16(128 %r). So next logical
  ; (aligned) place for the actual sret data is %esp + 40.
; CHECK: leal 40(%esp), [[SRET_ADDR:%[a-z]+]]
; CHECK: movl [[SRET_ADDR]], (%esp)
; CHECK: calll __multi3
; CHECK-DAG: movl 40(%esp), [[RES0:%[a-z]+]]
; CHECK-DAG: movl 44(%esp), [[RES1:%[a-z]+]]
; CHECK-DAG: movl 48(%esp), [[RES2:%[a-z]+]]
; CHECK-DAG: movl 52(%esp), [[RES3:%[a-z]+]]
; CHECK-DAG: movl [[RES0]], var
; CHECK-DAG: movl [[RES1]], var+4
; CHECK-DAG: movl [[RES2]], var+8
; CHECK-DAG: movl [[RES3]], var+12
  %prod = mul i128 %l, %r
  store i128 %prod, i128* @var
  ret void
}
