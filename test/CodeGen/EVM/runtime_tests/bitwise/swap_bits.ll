define i256 @swap(i256, i256, i256) #0 {
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  store i256 %0, i256* %4, align 8
  store i256 %1, i256* %5, align 8
  store i256 %2, i256* %6, align 8
  %7 = load i256, i256* %4, align 8
  %8 = load i256, i256* %5, align 8
  %9 = trunc i256 %8 to i256
  %10 = shl i256 1, %9
  %11 = sext i256 %10 to i256
  %12 = and i256 %7, %11
  %13 = load i256, i256* %5, align 8
  %14 = ashr i256 %12, %13
  %15 = load i256, i256* %4, align 8
  %16 = load i256, i256* %6, align 8
  %17 = trunc i256 %16 to i256
  %18 = shl i256 1, %17
  %19 = sext i256 %18 to i256
  %20 = and i256 %15, %19
  %21 = load i256, i256* %6, align 8
  %22 = ashr i256 %20, %21
  %23 = xor i256 %14, %22
  %24 = icmp ne i256 %23, 0
  br i1 %24, label %25, label %38

; <label>:25:                                     ; preds = %3
  %26 = load i256, i256* %5, align 8
  %27 = trunc i256 %26 to i256
  %28 = shl i256 1, %27
  %29 = sext i256 %28 to i256
  %30 = load i256, i256* %4, align 8
  %31 = xor i256 %30, %29
  store i256 %31, i256* %4, align 8
  %32 = load i256, i256* %6, align 8
  %33 = trunc i256 %32 to i256
  %34 = shl i256 1, %33
  %35 = sext i256 %34 to i256
  %36 = load i256, i256* %4, align 8
  %37 = xor i256 %36, %35
  store i256 %37, i256* %4, align 8
  br label %38

; <label>:38:                                     ; preds = %25, %3
  %39 = load i256, i256* %4, align 8
  ret i256 %39
}

