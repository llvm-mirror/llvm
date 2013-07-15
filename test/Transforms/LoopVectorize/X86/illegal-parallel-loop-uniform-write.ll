; RUN: opt < %s  -loop-vectorize -force-vector-unroll=1 -force-vector-width=4 -dce -instcombine -S | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

;CHECK-LABEL: @foo(
;CHECK-NOT: <4 x i32>
;CHECK: ret void

; Function Attrs: nounwind uwtable 
define void @foo(i32* nocapture %a, i32* nocapture %b, i32 %k, i32 %m) #0 {
entry:
  %cmp27 = icmp sgt i32 %m, 0
  br i1 %cmp27, label %for.body3.lr.ph.us, label %for.end15

for.end.us:                                       ; preds = %for.body3.us
  %arrayidx9.us = getelementptr inbounds i32* %b, i64 %indvars.iv33
  %0 = load i32* %arrayidx9.us, align 4, !llvm.mem.parallel_loop_access !3
  %add10.us = add nsw i32 %0, 3
  store i32 %add10.us, i32* %arrayidx9.us, align 4, !llvm.mem.parallel_loop_access !3
  %indvars.iv.next34 = add i64 %indvars.iv33, 1
  %lftr.wideiv35 = trunc i64 %indvars.iv.next34 to i32
  %exitcond36 = icmp eq i32 %lftr.wideiv35, %m
  br i1 %exitcond36, label %for.end15, label %for.body3.lr.ph.us, !llvm.loop !5

for.body3.us:                                     ; preds = %for.body3.us, %for.body3.lr.ph.us
  %indvars.iv29 = phi i64 [ 0, %for.body3.lr.ph.us ], [ %indvars.iv.next30, %for.body3.us ]
  %1 = trunc i64 %indvars.iv29 to i32
  %add4.us = add i32 %add.us, %1
  %idxprom.us = sext i32 %add4.us to i64
  %arrayidx.us = getelementptr inbounds i32* %a, i64 %idxprom.us
  %2 = load i32* %arrayidx.us, align 4, !llvm.mem.parallel_loop_access !3
  %add5.us = add nsw i32 %2, 1
  store i32 %add5.us, i32* %arrayidx7.us, align 4, !llvm.mem.parallel_loop_access !3
  %indvars.iv.next30 = add i64 %indvars.iv29, 1
  %lftr.wideiv31 = trunc i64 %indvars.iv.next30 to i32
  %exitcond32 = icmp eq i32 %lftr.wideiv31, %m
  br i1 %exitcond32, label %for.end.us, label %for.body3.us, !llvm.loop !4

for.body3.lr.ph.us:                               ; preds = %for.end.us, %entry
  %indvars.iv33 = phi i64 [ %indvars.iv.next34, %for.end.us ], [ 0, %entry ]
  %3 = trunc i64 %indvars.iv33 to i32
  %add.us = add i32 %3, %k
  %arrayidx7.us = getelementptr inbounds i32* %a, i64 %indvars.iv33
  br label %for.body3.us

for.end15:                                        ; preds = %for.end.us, %entry
  ret void
}

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }

!3 = metadata !{metadata !4, metadata !5}
!4 = metadata !{metadata !4}
!5 = metadata !{metadata !5}

