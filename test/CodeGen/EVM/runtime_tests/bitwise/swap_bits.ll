define i256 @swap(i256, i256, i256) #0 {
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  store i256 %0, i256* %4, align 8
  store i256 %1, i256* %5, align 8
  store i256 %2, i256* %6, align 8
  %7 = load i256, i256* %4, align 8
  %8 = load i256, i256* %5, align 8
  %9 = shl i256 1, %8
  %10 = and i256 %7, %9
  %11 = load i256, i256* %5, align 8
  %12 = ashr i256 %10, %11
  %13 = load i256, i256* %4, align 8
  %14 = load i256, i256* %6, align 8
  %15 = shl i256 1, %14
  %16 = and i256 %13, %15
  %17 = load i256, i256* %6, align 8
  %18 = ashr i256 %16, %17
  %19 = xor i256 %12, %18
  %20 = icmp ne i256 %19, 0
  br i1 %20, label %21, label %30

; <label>:21:                                     ; preds = %3
  %22 = load i256, i256* %5, align 8
  %23 = shl i256 1, %22
  %24 = load i256, i256* %4, align 8
  %25 = xor i256 %24, %23
  store i256 %25, i256* %4, align 8
  %26 = load i256, i256* %6, align 8
  %27 = shl i256 1, %26
  %28 = load i256, i256* %4, align 8
  %29 = xor i256 %28, %27
  store i256 %29, i256* %4, align 8
  br label %30

; <label>:30:                                     ; preds = %21, %3
  %31 = load i256, i256* %4, align 8
  ret i256 %31
}

