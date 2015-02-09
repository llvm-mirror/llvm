; RUN: llc < %s -tailcallopt -code-model=medium -stack-alignment=4 -mtriple=i686-linux-gnu -mcpu=pentium | FileCheck %s

; Check the HiPE calling convention works (x86-32)

define void @zap(i32 %a, i32 %b) nounwind {
entry:
  ; CHECK:      movl 40(%esp), %eax
  ; CHECK-NEXT: movl 44(%esp), %edx
  ; CHECK-NEXT: movl       $8, %ecx
  ; CHECK-NEXT: calll addfour
  %0 = call cc 11 {i32, i32, i32} @addfour(i32 undef, i32 undef, i32 %a, i32 %b, i32 8)
  %res = extractvalue {i32, i32, i32} %0, 2

  ; CHECK:      movl %eax, 16(%esp)
  ; CHECK-NEXT: movl   $2, 12(%esp)
  ; CHECK-NEXT: movl   $1,  8(%esp)
  ; CHECK:      calll foo
  tail call void @foo(i32 undef, i32 undef, i32 1, i32 2, i32 %res) nounwind
  ret void
}

define cc 11 {i32, i32, i32} @addfour(i32 %hp, i32 %p, i32 %x, i32 %y, i32 %z) nounwind {
entry:
  ; CHECK:      addl %edx, %eax
  ; CHECK-NEXT: addl %ecx, %eax
  %0 = add i32 %x, %y
  %1 = add i32 %0, %z

  ; CHECK:      ret
  %res = insertvalue {i32, i32, i32} undef, i32 %1, 2
  ret {i32, i32, i32} %res
}

define cc 11 void @foo(i32 %hp, i32 %p, i32 %arg0, i32 %arg1, i32 %arg2) nounwind {
entry:
  ; CHECK:      movl  %esi, 16(%esp)
  ; CHECK-NEXT: movl  %ebp, 12(%esp)
  ; CHECK-NEXT: movl  %eax,  8(%esp)
  ; CHECK-NEXT: movl  %edx,  4(%esp)
  ; CHECK-NEXT: movl  %ecx,   (%esp)
  %hp_var   = alloca i32
  %p_var    = alloca i32
  %arg0_var = alloca i32
  %arg1_var = alloca i32
  %arg2_var = alloca i32
  store i32 %hp, i32* %hp_var
  store i32 %p, i32* %p_var
  store i32 %arg0, i32* %arg0_var
  store i32 %arg1, i32* %arg1_var
  store i32 %arg2, i32* %arg2_var

  ; CHECK:      movl  16(%esp), %esi
  ; CHECK-NEXT: movl  12(%esp), %ebp
  ; CHECK-NEXT: movl   8(%esp), %eax
  ; CHECK-NEXT: movl   4(%esp), %edx
  %0 = load i32* %hp_var
  %1 = load i32* %p_var
  %2 = load i32* %arg0_var
  %3 = load i32* %arg1_var
  %4 = load i32* %arg2_var
  ; CHECK:      jmp bar
  tail call cc 11 void @bar(i32 %0, i32 %1, i32 %2, i32 %3, i32 %4) nounwind
  ret void
}

define cc 11 void @baz() nounwind {
  %tmp_clos = load i32* @clos
  %tmp_clos2 = inttoptr i32 %tmp_clos to i32*
  %indirect_call = bitcast i32* %tmp_clos2 to void (i32, i32, i32)*
  ; CHECK:      movl $42, %eax
  ; CHECK-NEXT: jmpl *clos
  tail call cc 11 void %indirect_call(i32 undef, i32 undef, i32 42) nounwind
  ret void
}

@clos = external constant i32
declare cc 11 void @bar(i32, i32, i32, i32, i32)
