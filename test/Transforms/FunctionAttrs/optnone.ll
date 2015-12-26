; RUN: opt < %s -functionattrs -S | FileCheck %s

@x = global i32 0

define void @test_opt(i8* %p) {
; CHECK-LABEL: @test_opt
; CHECK: (i8* nocapture readnone %p) #0 {
  ret void
}

define void @test_optnone(i8* %p) noinline optnone {
; CHECK-LABEL: @test_optnone
; CHECK: (i8* %p) #1 {
  ret void
}

declare i8 @strlen(i8*) noinline optnone
; CHECK-LABEL: @strlen
; CHECK: (i8*) #2

; CHECK-LABEL: attributes #0
; CHECK: = { norecurse readnone }
; CHECK-LABEL: attributes #1
; CHECK: = { noinline norecurse optnone }
; CHECK-LABEL: attributes #2
; CHECK: = { noinline optnone }
