; RUN: llc < %s -march=x86 -mtriple=i386-linux-gnu | FileCheck -check-prefix=X32_LINUX %s
; RUN: llc < %s -march=x86-64 -mtriple=x86_64-linux-gnu | FileCheck -check-prefix=X64_LINUX %s
; RUN: llc < %s -march=x86 -mtriple=x86-pc-win32 | FileCheck -check-prefix=X32_WIN %s
; RUN: llc < %s -march=x86-64 -mtriple=x86_64-pc-win32 | FileCheck -check-prefix=X64_WIN %s

@i1 = thread_local global i32 15
@i2 = external thread_local global i32
@i3 = internal thread_local global i32 15
@i4 = hidden thread_local global i32 15
@i5 = external hidden thread_local global i32
@s1 = thread_local global i16 15
@b1 = thread_local global i8 0

define i32 @f1() {
; X32_LINUX-LABEL: f1:
; X32_LINUX:      movl %gs:i1@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f1:
; X64_LINUX:      movl %fs:i1@TPOFF, %eax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f1:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movl _i1@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f1:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movl i1@SECREL32(%rax), %eax
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i32* @i1
	ret i32 %tmp1
}

define i32* @f2() {
; X32_LINUX-LABEL: f2:
; X32_LINUX:      movl %gs:0, %eax
; X32_LINUX-NEXT: leal i1@NTPOFF(%eax), %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f2:
; X64_LINUX:      movq %fs:0, %rax
; X64_LINUX-NEXT: leaq i1@TPOFF(%rax), %rax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f2:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: leal _i1@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f2:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: leaq i1@SECREL32(%rax), %rax
; X64_WIN-NEXT: ret

entry:
	ret i32* @i1
}

define i32 @f3() nounwind {
; X32_LINUX-LABEL: f3:
; X32_LINUX:      movl i2@INDNTPOFF, %eax
; X32_LINUX-NEXT: movl %gs:(%eax), %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f3:
; X64_LINUX:      movq i2@GOTTPOFF(%rip), %rax
; X64_LINUX-NEXT: movl %fs:(%rax), %eax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f3:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movl _i2@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f3:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movl i2@SECREL32(%rax), %eax
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i32* @i2
	ret i32 %tmp1
}

define i32* @f4() {
; X32_LINUX-LABEL: f4:
; X32_LINUX:      movl %gs:0, %eax
; X32_LINUX-NEXT: addl i2@INDNTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f4:
; X64_LINUX:      movq %fs:0, %rax
; X64_LINUX-NEXT: addq i2@GOTTPOFF(%rip), %rax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f4:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: leal _i2@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f4:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: leaq i2@SECREL32(%rax), %rax
; X64_WIN-NEXT: ret

entry:
	ret i32* @i2
}

define i32 @f5() nounwind {
; X32_LINUX-LABEL: f5:
; X32_LINUX:      movl %gs:i3@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f5:
; X64_LINUX:      movl %fs:i3@TPOFF, %eax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f5:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movl _i3@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f5:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movl i3@SECREL32(%rax), %eax
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i32* @i3
	ret i32 %tmp1
}

define i32* @f6() {
; X32_LINUX-LABEL: f6:
; X32_LINUX:      movl %gs:0, %eax
; X32_LINUX-NEXT: leal i3@NTPOFF(%eax), %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f6:
; X64_LINUX:      movq %fs:0, %rax
; X64_LINUX-NEXT: leaq i3@TPOFF(%rax), %rax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f6:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: leal _i3@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f6:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: leaq i3@SECREL32(%rax), %rax
; X64_WIN-NEXT: ret

entry:
	ret i32* @i3
}

define i32 @f7() {
; X32_LINUX-LABEL: f7:
; X32_LINUX:      movl %gs:i4@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f7:
; X64_LINUX:      movl %fs:i4@TPOFF, %eax
; X64_LINUX-NEXT: ret

entry:
	%tmp1 = load i32* @i4
	ret i32 %tmp1
}

define i32* @f8() {
; X32_LINUX-LABEL: f8:
; X32_LINUX:      movl %gs:0, %eax
; X32_LINUX-NEXT: leal i4@NTPOFF(%eax), %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f8:
; X64_LINUX:      movq %fs:0, %rax
; X64_LINUX-NEXT: leaq i4@TPOFF(%rax), %rax
; X64_LINUX-NEXT: ret

entry:
	ret i32* @i4
}

define i32 @f9() {
; X32_LINUX-LABEL: f9:
; X32_LINUX:      movl %gs:i5@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f9:
; X64_LINUX:      movl %fs:i5@TPOFF, %eax
; X64_LINUX-NEXT: ret

entry:
	%tmp1 = load i32* @i5
	ret i32 %tmp1
}

define i32* @f10() {
; X32_LINUX-LABEL: f10:
; X32_LINUX:      movl %gs:0, %eax
; X32_LINUX-NEXT: leal i5@NTPOFF(%eax), %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f10:
; X64_LINUX:      movq %fs:0, %rax
; X64_LINUX-NEXT: leaq i5@TPOFF(%rax), %rax
; X64_LINUX-NEXT: ret

entry:
	ret i32* @i5
}

define i16 @f11() {
; X32_LINUX-LABEL: f11:
; X32_LINUX:      movzwl %gs:s1@NTPOFF, %eax
; Why is this kill line here, but no where else?
; X32_LINUX-NEXT: # kill
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f11:
; X64_LINUX:      movzwl %fs:s1@TPOFF, %eax
; X64_LINUX-NEXT: # kill
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f11:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movzwl _s1@SECREL32(%eax), %eax
; X32_WIN-NEXT: # kill
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f11:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movzwl s1@SECREL32(%rax), %eax
; X64_WIN-NEXT: # kill
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i16* @s1
	ret i16 %tmp1
}

define i32 @f12() {
; X32_LINUX-LABEL: f12:
; X32_LINUX:      movswl %gs:s1@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f12:
; X64_LINUX:      movswl %fs:s1@TPOFF, %eax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f12:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movswl _s1@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f12:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movswl s1@SECREL32(%rax), %eax
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i16* @s1
  %tmp2 = sext i16 %tmp1 to i32
	ret i32 %tmp2
}

define i8 @f13() {
; X32_LINUX-LABEL: f13:
; X32_LINUX:      movb %gs:b1@NTPOFF, %al
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f13:
; X64_LINUX:      movb %fs:b1@TPOFF, %al
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f13:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movb _b1@SECREL32(%eax), %al
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f13:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movb b1@SECREL32(%rax), %al
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i8* @b1
	ret i8 %tmp1
}

define i32 @f14() {
; X32_LINUX-LABEL: f14:
; X32_LINUX:      movsbl %gs:b1@NTPOFF, %eax
; X32_LINUX-NEXT: ret
; X64_LINUX-LABEL: f14:
; X64_LINUX:      movsbl %fs:b1@TPOFF, %eax
; X64_LINUX-NEXT: ret
; X32_WIN-LABEL: f14:
; X32_WIN:      movl __tls_index, %eax
; X32_WIN-NEXT: movl %fs:__tls_array, %ecx
; X32_WIN-NEXT: movl (%ecx,%eax,4), %eax
; X32_WIN-NEXT: movsbl _b1@SECREL32(%eax), %eax
; X32_WIN-NEXT: ret
; X64_WIN-LABEL: f14:
; X64_WIN:      movl _tls_index(%rip), %eax
; X64_WIN-NEXT: movq %gs:88, %rcx
; X64_WIN-NEXT: movq (%rcx,%rax,8), %rax
; X64_WIN-NEXT: movsbl b1@SECREL32(%rax), %eax
; X64_WIN-NEXT: ret

entry:
	%tmp1 = load i8* @b1
  %tmp2 = sext i8 %tmp1 to i32
	ret i32 %tmp2
}

