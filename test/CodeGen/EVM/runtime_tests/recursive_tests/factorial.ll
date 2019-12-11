
define i256 @factorial(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca i256, align 4
  store i256 %0, i256* %3, align 4
  %4 = load i256, i256* %3, align 4
  %5 = icmp eq i256 %4, 0
  br i1 %5, label %6, label %7

; <label>:6:                                      ; preds = %1
  store i256 1, i256* %2, align 4
  br label %13

; <label>:7:                                      ; preds = %1
  %8 = load i256, i256* %3, align 4
  %9 = load i256, i256* %3, align 4
  %10 = sub nsw i256 %9, 1
  %11 = call i256 @factorial(i256 %10)
  %12 = mul nsw i256 %8, %11
  store i256 %12, i256* %2, align 4
  br label %13

; <label>:13:                                     ; preds = %7, %6
  %14 = load i256, i256* %2, align 4
  ret i256 %14
}

