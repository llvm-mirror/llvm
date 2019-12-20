define i256 @bits(i256) #0 {
  %2 = alloca i256, align 8
  %3 = alloca i256, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  store i256 %0, i256* %3, align 8
  store i256 0, i256* %4, align 8
  store i256 0, i256* %5, align 8
  store i256 0, i256* %6, align 8
  %7 = load i256, i256* %3, align 8
  store i256 %7, i256* %5, align 8
  %8 = load i256, i256* %3, align 8
  %9 = icmp eq i256 %8, 0
  br i1 %9, label %10, label %11

; <label>:10:                                     ; preds = %1
  store i256 0, i256* %2, align 8
  br label %37

; <label>:11:                                     ; preds = %1
  br label %12

; <label>:12:                                     ; preds = %15, %11
  %13 = load i256, i256* %5, align 8
  %14 = icmp ne i256 %13, 0
  br i1 %14, label %15, label %20

; <label>:15:                                     ; preds = %12
  %16 = load i256, i256* %4, align 8
  %17 = add nsw i256 %16, 1
  store i256 %17, i256* %4, align 8
  %18 = load i256, i256* %5, align 8
  %19 = ashr i256 %18, 1
  store i256 %19, i256* %5, align 8
  br label %12

; <label>:20:                                     ; preds = %12
  store i256 0, i256* %6, align 8
  br label %21

; <label>:21:                                     ; preds = %33, %20
  %22 = load i256, i256* %6, align 8
  %23 = load i256, i256* %4, align 8
  %24 = icmp slt i256 %22, %23
  br i1 %24, label %25, label %36

; <label>:25:                                     ; preds = %21
  %26 = load i256, i256* %3, align 8
  %27 = load i256, i256* %6, align 8
  %28 = ashr i256 %26, %27
  %29 = and i256 %28, 1
  %30 = icmp eq i256 %29, 1
  br i1 %30, label %31, label %32

; <label>:31:                                     ; preds = %25
  br label %33

; <label>:32:                                     ; preds = %25
  store i256 0, i256* %2, align 8
  br label %37

; <label>:33:                                     ; preds = %31
  %34 = load i256, i256* %6, align 8
  %35 = add nsw i256 %34, 1
  store i256 %35, i256* %6, align 8
  br label %21

; <label>:36:                                     ; preds = %21
  store i256 1, i256* %2, align 8
  br label %37

; <label>:37:                                     ; preds = %36, %32, %10
  %38 = load i256, i256* %2, align 8
  ret i256 %38
}

