
; Function Attrs: noinline nounwind optnone ssp uwtable
define i256 @dde(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca [2 x i256], align 4
  %4 = alloca i256, align 4
  store i256 %0, i256* %2, align 4
  %5 = load i256, i256* %2, align 4
  %6 = getelementptr inbounds [2 x i256], [2 x i256]* %3, i64 0, i64 1
  store volatile i256 %5, i256* %6, align 4
  %7 = getelementptr inbounds [2 x i256], [2 x i256]* %3, i64 0, i64 1
  %8 = load volatile i256, i256* %7, align 4
  store i256 %8, i256* %4, align 4
  %9 = load i256, i256* %4, align 4
  ret i256 %9
}
