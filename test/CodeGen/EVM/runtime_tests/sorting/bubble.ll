define i256 @sort() #0 {
  %1 = alloca i256, align 8
  %2 = alloca [5 x i256], align 16
  %3 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 0
  store i256 5, i256* %3, align 16
  %4 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 1
  store i256 4, i256* %4, align 8
  %5 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 2
  store i256 3, i256* %5, align 16
  %6 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 3
  store i256 1, i256* %6, align 8
  %7 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 4
  store i256 2, i256* %7, align 16
  %8 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 0
  call void @bubbleSort(i256* %8, i256 5)
  %9 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 0
  %10 = load i256, i256* %9, align 16
  %11 = icmp eq i256 %10, 1
  br i1 %11, label %12, label %13

; <label>:12:                                     ; preds = %0
  store i256 1, i256* %1, align 8
  br label %14

; <label>:13:                                     ; preds = %0
  store i256 0, i256* %1, align 8
  br label %14

; <label>:14:                                     ; preds = %13, %12
  %15 = load i256, i256* %1, align 8
  ret i256 %15
}

define void @bubbleSort(i256*, i256) #0 {
  %3 = alloca i256*, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  store i256* %0, i256** %3, align 8
  store i256 %1, i256* %4, align 8
  store i256 0, i256* %5, align 8
  br label %7

; <label>:7:                                      ; preds = %44, %2
  %8 = load i256, i256* %5, align 8
  %9 = load i256, i256* %4, align 8
  %10 = sub nsw i256 %9, 1
  %11 = icmp slt i256 %8, %10
  br i1 %11, label %12, label %47

; <label>:12:                                     ; preds = %7
  store i256 0, i256* %6, align 8
  br label %13

; <label>:13:                                     ; preds = %40, %12
  %14 = load i256, i256* %6, align 8
  %15 = load i256, i256* %4, align 8
  %16 = load i256, i256* %5, align 8
  %17 = sub nsw i256 %15, %16
  %18 = sub nsw i256 %17, 1
  %19 = icmp slt i256 %14, %18
  br i1 %19, label %20, label %43

; <label>:20:                                     ; preds = %13
  %21 = load i256*, i256** %3, align 8
  %22 = load i256, i256* %6, align 8
  %23 = getelementptr inbounds i256, i256* %21, i256 %22
  %24 = load i256, i256* %23, align 8
  %25 = load i256*, i256** %3, align 8
  %26 = load i256, i256* %6, align 8
  %27 = add nsw i256 %26, 1
  %28 = getelementptr inbounds i256, i256* %25, i256 %27
  %29 = load i256, i256* %28, align 8
  %30 = icmp sgt i256 %24, %29
  br i1 %30, label %31, label %39

; <label>:31:                                     ; preds = %20
  %32 = load i256*, i256** %3, align 8
  %33 = load i256, i256* %6, align 8
  %34 = getelementptr inbounds i256, i256* %32, i256 %33
  %35 = load i256*, i256** %3, align 8
  %36 = load i256, i256* %6, align 8
  %37 = add nsw i256 %36, 1
  %38 = getelementptr inbounds i256, i256* %35, i256 %37
  call void @swap(i256* %34, i256* %38)
  br label %39

; <label>:39:                                     ; preds = %31, %20
  br label %40

; <label>:40:                                     ; preds = %39
  %41 = load i256, i256* %6, align 8
  %42 = add nsw i256 %41, 1
  store i256 %42, i256* %6, align 8
  br label %13

; <label>:43:                                     ; preds = %13
  br label %44

; <label>:44:                                     ; preds = %43
  %45 = load i256, i256* %5, align 8
  %46 = add nsw i256 %45, 1
  store i256 %46, i256* %5, align 8
  br label %7

; <label>:47:                                     ; preds = %7
  ret void
}

; Function Attrs: noinline nounwind optnone ssp uwtable
define void @swap(i256*, i256*) #0 {
  %3 = alloca i256*, align 8
  %4 = alloca i256*, align 8
  %5 = alloca i256, align 8
  store i256* %0, i256** %3, align 8
  store i256* %1, i256** %4, align 8
  %6 = load i256*, i256** %3, align 8
  %7 = load i256, i256* %6, align 8
  store i256 %7, i256* %5, align 8
  %8 = load i256*, i256** %4, align 8
  %9 = load i256, i256* %8, align 8
  %10 = load i256*, i256** %3, align 8
  store i256 %9, i256* %10, align 8
  %11 = load i256, i256* %5, align 8
  %12 = load i256*, i256** %4, align 8
  store i256 %11, i256* %12, align 8
  ret void
}

