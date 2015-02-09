; RUN: llc < %s -mtriple=i686-windows | FileCheck %s -check-prefix=NORMAL
; RUN: llc < %s -mtriple=x86_64-windows | FileCheck %s -check-prefix=X64
; RUN: llc < %s -mtriple=i686-windows -force-align-stack -stack-alignment=32 | FileCheck %s -check-prefix=ALIGNED 

declare void @good(i32 %a, i32 %b, i32 %c, i32 %d)
declare void @inreg(i32 %a, i32 inreg %b, i32 %c, i32 %d)

; Here, we should have a reserved frame, so we don't expect pushes
; NORMAL-LABEL: test1:
; NORMAL: subl    $16, %esp
; NORMAL-NEXT: movl    $4, 12(%esp)
; NORMAL-NEXT: movl    $3, 8(%esp)
; NORMAL-NEXT: movl    $2, 4(%esp)
; NORMAL-NEXT: movl    $1, (%esp)
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test1() {
entry:
  call void @good(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; We're optimizing for code size, so we should get pushes for x86,
; even though there is a reserved call frame.
; Make sure we don't touch x86-64
; NORMAL-LABEL: test1b:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: pushl   $4
; NORMAL-NEXT: pushl   $3
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   $1
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
; X64-LABEL: test1b:
; X64: movl    $1, %ecx
; X64-NEXT: movl    $2, %edx
; X64-NEXT: movl    $3, %r8d
; X64-NEXT: movl    $4, %r9d
; X64-NEXT: callq   good
define void @test1b() optsize {
entry:
  call void @good(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; Same as above, but for minsize
; NORMAL-LABEL: test1c:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: pushl   $4
; NORMAL-NEXT: pushl   $3
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   $1
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test1c() minsize {
entry:
  call void @good(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; If we have a reserved frame, we should have pushes
; NORMAL-LABEL: test2:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: pushl   $4
; NORMAL-NEXT: pushl   $3
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   $1
; NORMAL-NEXT: call
define void @test2(i32 %k) {
entry:
  %a = alloca i32, i32 %k
  call void @good(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; Again, we expect a sequence of 4 immediate pushes
; Checks that we generate the right pushes for >8bit immediates
; NORMAL-LABEL: test2b:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: pushl   $4096
; NORMAL-NEXT: pushl   $3072
; NORMAL-NEXT: pushl   $2048
; NORMAL-NEXT: pushl   $1024
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test2b() optsize {
entry:
  call void @good(i32 1024, i32 2048, i32 3072, i32 4096)
  ret void
}

; The first push should push a register
; NORMAL-LABEL: test3:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: pushl   $4
; NORMAL-NEXT: pushl   $3
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   %e{{..}}
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test3(i32 %k) optsize {
entry:
  call void @good(i32 %k, i32 2, i32 3, i32 4)
  ret void
}

; We don't support weird calling conventions
; NORMAL-LABEL: test4:
; NORMAL: subl    $12, %esp
; NORMAL-NEXT: movl    $4, 8(%esp)
; NORMAL-NEXT: movl    $3, 4(%esp)
; NORMAL-NEXT: movl    $1, (%esp)
; NORMAL-NEXT: movl    $2, %eax
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $12, %esp
define void @test4() optsize {
entry:
  call void @inreg(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; When there is no reserved call frame, check that additional alignment
; is added when the pushes don't add up to the required alignment.
; ALIGNED-LABEL: test5:
; ALIGNED: subl    $16, %esp
; ALIGNED-NEXT: pushl   $4
; ALIGNED-NEXT: pushl   $3
; ALIGNED-NEXT: pushl   $2
; ALIGNED-NEXT: pushl   $1
; ALIGNED-NEXT: call
define void @test5(i32 %k) {
entry:
  %a = alloca i32, i32 %k
  call void @good(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; Check that pushing the addresses of globals (Or generally, things that 
; aren't exactly immediates) isn't broken.
; Fixes PR21878.
; NORMAL-LABEL: test6:
; NORMAL: pushl    $_ext
; NORMAL-NEXT: call
declare void @f(i8*)
@ext = external constant i8

define void @test6() {
  call void @f(i8* @ext)
  br label %bb
bb:
  alloca i32
  ret void
}

; Check that we fold simple cases into the push
; NORMAL-LABEL: test7:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: movl 4(%esp), [[EAX:%e..]]
; NORMAL-NEXT: pushl   $4
; NORMAL-NEXT: pushl   ([[EAX]])
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   $1
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test7(i32* %ptr) optsize {
entry:
  %val = load i32* %ptr
  call void @good(i32 1, i32 2, i32 %val, i32 4)
  ret void
}

; But we don't want to fold stack-relative loads into the push,
; because the offset will be wrong
; NORMAL-LABEL: test8:
; NORMAL-NOT: subl {{.*}} %esp
; NORMAL: movl 4(%esp), [[EAX:%e..]]
; NORMAL-NEXT: pushl   $4
; NORMAL-NEXT: pushl   [[EAX]]
; NORMAL-NEXT: pushl   $2
; NORMAL-NEXT: pushl   $1
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test8(i32* %ptr) optsize {
entry:
  %val = ptrtoint i32* %ptr to i32
  call void @good(i32 1, i32 2, i32 %val, i32 4)
  ret void
}

; If one function is using push instructions, and the other isn't
; (because it has frame-index references), then we must resolve
; these references correctly.
; NORMAL-LABEL: test9:
; NORMAL-NOT: leal (%esp), 
; NORMAL: pushl $4
; NORMAL-NEXT: pushl $3
; NORMAL-NEXT: pushl $2
; NORMAL-NEXT: pushl $1
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
; NORMAL-NEXT: subl $16, %esp
; NORMAL-NEXT: leal 16(%esp), [[EAX:%e..]]
; NORMAL-NEXT: movl    [[EAX]], 12(%esp)
; NORMAL-NEXT: movl    $7, 8(%esp)
; NORMAL-NEXT: movl    $6, 4(%esp)
; NORMAL-NEXT: movl    $5, (%esp)
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
define void @test9() optsize {
entry:
  %p = alloca i32, align 4
  call void @good(i32 1, i32 2, i32 3, i32 4)
  %0 = ptrtoint i32* %p to i32
  call void @good(i32 5, i32 6, i32 7, i32 %0)
  ret void
}

; We can end up with an indirect call which gets reloaded on the spot.
; Make sure we reference the correct stack slot - we spill into (%esp)
; and reload from 16(%esp) due to the pushes.
; NORMAL-LABEL: test10:
; NORMAL: movl $_good, [[ALLOC:.*]]
; NORMAL-NEXT: movl [[ALLOC]], [[EAX:%e..]]
; NORMAL-NEXT: movl [[EAX]], (%esp) # 4-byte Spill
; NORMAL: nop
; NORMAL: pushl $4
; NORMAL-NEXT: pushl $3
; NORMAL-NEXT: pushl $2
; NORMAL-NEXT: pushl $1
; NORMAL-NEXT: calll *16(%esp)
; NORMAL-NEXT: addl $16, %esp
define void @test10() optsize {
  %stack_fptr = alloca void (i32, i32, i32, i32)*
  store void (i32, i32, i32, i32)* @good, void (i32, i32, i32, i32)** %stack_fptr
  %good_ptr = load volatile void (i32, i32, i32, i32)** %stack_fptr
  call void asm sideeffect "nop", "~{ax},~{bx},~{cx},~{dx},~{bp},~{si},~{di}"()
  call void (i32, i32, i32, i32)* %good_ptr(i32 1, i32 2, i32 3, i32 4)
  ret void
}

; We can't fold the load from the global into the push because of 
; interference from the store
; NORMAL-LABEL: test11:
; NORMAL: movl    _the_global, [[EAX:%e..]]
; NORMAL-NEXT: movl    $42, _the_global
; NORMAL-NEXT: pushl $4
; NORMAL-NEXT: pushl $3
; NORMAL-NEXT: pushl $2
; NORMAL-NEXT: pushl [[EAX]]
; NORMAL-NEXT: call
; NORMAL-NEXT: addl $16, %esp
@the_global = external global i32
define void @test11() optsize {
  %myload = load i32* @the_global
  store i32 42, i32* @the_global
  call void @good(i32 %myload, i32 2, i32 3, i32 4)
  ret void
}
