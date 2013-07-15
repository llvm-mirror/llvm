; RUN: llc < %s -mtriple=powerpc64-unknown-linux-gnu -mcpu=pwr7 | FileCheck %s
; RUN: llc < %s -mtriple=powerpc64-unknown-linux-gnu -mcpu=a2 | FileCheck -check-prefix=CHECK-NOAV %s
target datalayout = "E-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-f128:128:128-v128:128:128-n32:64"
target triple = "powerpc64-unknown-linux-gnu"

%struct.__jmp_buf_tag = type { [64 x i64], i32, %struct.__sigset_t, [8 x i8] }
%struct.__sigset_t = type { [16 x i64] }

@env_sigill = internal global [1 x %struct.__jmp_buf_tag] zeroinitializer, align 16

define void @foo() #0 {
entry:
  call void @llvm.eh.sjlj.longjmp(i8* bitcast ([1 x %struct.__jmp_buf_tag]* @env_sigill to i8*))
  unreachable

; CHECK: @foo
; CHECK: addis [[REG:[0-9]+]], 2, env_sigill@toc@ha
; CHECK: addi [[REG]], [[REG]], env_sigill@toc@l
; CHECK: ld 31, 0([[REG]])
; CHECK: ld [[REG2:[0-9]+]], 8([[REG]])
; CHECK: ld 1, 16([[REG]])
; CHECK: mtctr [[REG2]]
; CHECK: ld 2, 24([[REG]])
; CHECK: bctr

return:                                           ; No predecessors!
  ret void
}

declare void @llvm.eh.sjlj.longjmp(i8*) #1

define signext i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  store i32 0, i32* %retval
  %0 = call i8* @llvm.frameaddress(i32 0)
  store i8* %0, i8** bitcast ([1 x %struct.__jmp_buf_tag]* @env_sigill to i8**)
  %1 = call i8* @llvm.stacksave()
  store i8* %1, i8** getelementptr (i8** bitcast ([1 x %struct.__jmp_buf_tag]* @env_sigill to i8**), i32 2)
  %2 = call i32 @llvm.eh.sjlj.setjmp(i8* bitcast ([1 x %struct.__jmp_buf_tag]* @env_sigill to i8*))
  %tobool = icmp ne i32 %2, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  store i32 1, i32* %retval
  br label %return

if.else:                                          ; preds = %entry
  call void @foo()
  br label %if.end

if.end:                                           ; preds = %if.else
  store i32 0, i32* %retval
  br label %return

return:                                           ; preds = %if.end, %if.then
  %3 = load i32* %retval
  ret i32 %3

; FIXME: We should be saving VRSAVE on Darwin, but we're not!

; CHECK: @main
; CHECK: std
; Make sure that we're not saving VRSAVE on non-Darwin:
; CHECK-NOT: mfspr
; CHECK: stfd
; CHECK: stvx

; CHECK: addis [[REG:[0-9]+]], 2, env_sigill@toc@ha
; CHECK: std 31, env_sigill@toc@l([[REG]])
; CHECK: addi [[REG]], [[REG]], env_sigill@toc@l
; CHECK: std [[REG]], [[OFF:[0-9]+]](31)                  # 8-byte Folded Spill
; CHECK: std 1, 16([[REG]])
; CHECK: std 2, 24([[REG]])
; CHECK: bcl 20, 31, .LBB1_1
; CHECK: li 3, 1
; CHECK: #EH_SjLj_Setup	.LBB1_1
; CHECK: b .LBB1_2

; CHECK: .LBB1_1:
; CHECK: mflr [[REGL:[0-9]+]]
; CHECK: ld [[REG2:[0-9]+]], [[OFF]](31)                   # 8-byte Folded Reload
; CHECK: std [[REGL]], 8([[REG2]])
; CHECK: li 3, 0

; CHECK: .LBB1_2:

; CHECK: lfd
; CHECK: lvx
; CHECK: ld
; CHECK: blr

; CHECK-NOAV: @main
; CHECK-NOAV-NOT: stvx
; CHECK-NOAV: bcl
; CHECK-NOAV: mflr
; CHECK-NOAV: bl foo
; CHECK-NOAV-NOT: lvx
; CHECK-NOAV: blr
}

declare i8* @llvm.frameaddress(i32) #2

declare i8* @llvm.stacksave() #3

declare i32 @llvm.eh.sjlj.setjmp(i8*) #3

attributes #0 = { nounwind "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { noreturn nounwind }
attributes #2 = { nounwind readnone }
attributes #3 = { nounwind }

