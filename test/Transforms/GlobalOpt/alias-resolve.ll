; We use a temporary file so that the test fails when opt crashes.

; RUN: opt < %s -globalopt -S > %t
; RUN: FileCheck %s < %t

@foo1 = alias void ()* @foo2
; CHECK: @foo1 = alias void ()* @foo2

@foo2 = alias weak void()* @bar1
; CHECK: @foo2 = alias weak void ()* @bar2

@bar1  = alias void ()* @bar2
; CHECK: @bar1 = alias void ()* @bar2

declare void @bar2()
; CHECK: declare void @bar2()

define void @baz() {
entry:
         call void @foo1()
; CHECK: call void @foo2()

         call void @foo2()
; CHECK: call void @foo2()

         call void @bar1()
; CHECK: call void @bar2()

         ret void
}

@foo3 = alias void ()* @bar3
; CHECK-NOT: bar3

define internal void @bar3() {
  ret void
}
;CHECK: define void @foo3
