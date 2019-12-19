define i256 @test() #0 {
  %1 = alloca i256, align 8
  %2 = alloca [2 x i256], align 16
  %3 = getelementptr inbounds [2 x i256], [2 x i256]* %2, i256 0, i256 0
  store i256 1, i256* %3, align 16
  %4 = getelementptr inbounds [2 x i256], [2 x i256]* %2, i256 0, i256 1
  store i256 0, i256* %4, align 8
  %5 = getelementptr inbounds [2 x i256], [2 x i256]* %2, i256 0, i256 0
  call void @insertionSort(i256* %5, i256 2)
  %6 = getelementptr inbounds [2 x i256], [2 x i256]* %2, i256 0, i256 0
  %7 = load i256, i256* %6, align 16
  %8 = icmp eq i256 %7, 0
  br i1 %8, label %9, label %14

; <label>:9:                                      ; preds = %0
  %10 = getelementptr inbounds [2 x i256], [2 x i256]* %2, i256 0, i256 1
  %11 = load i256, i256* %10, align 8
  %12 = icmp eq i256 %11, 1
  br i1 %12, label %13, label %14

; <label>:13:                                     ; preds = %9
  store i256 1, i256* %1, align 8
  br label %15

; <label>:14:                                     ; preds = %9, %0
  store i256 0, i256* %1, align 8
  br label %15

; <label>:15:                                     ; preds = %14, %13
  %16 = load i256, i256* %1, align 8
  ret i256 %16
}

define void @insertionSort(i256*, i256) #0 {
  %3 = alloca i256*, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  %7 = alloca i256, align 8
  store i256* %0, i256** %3, align 8
  store i256 %1, i256* %4, align 8
  store i256 1, i256* %5, align 8
  br label %8

; <label>:8:                                      ; preds = %48, %2
  %9 = load i256, i256* %5, align 8
  %10 = load i256, i256* %4, align 8
  %11 = icmp slt i256 %9, %10
  br i1 %11, label %12, label %51

; <label>:12:                                     ; preds = %8
  %13 = load i256*, i256** %3, align 8
  %14 = load i256, i256* %5, align 8
  %15 = getelementptr inbounds i256, i256* %13, i256 %14
  %16 = load i256, i256* %15, align 8
  store i256 %16, i256* %6, align 8
  %17 = load i256, i256* %5, align 8
  %18 = sub nsw i256 %17, 1
  store i256 %18, i256* %7, align 8
  br label %19

; <label>:19:                                     ; preds = %31, %12
  %20 = load i256, i256* %7, align 8
  %21 = icmp sge i256 %20, 0
  br i1 %21, label %22, label %29

; <label>:22:                                     ; preds = %19
  %23 = load i256*, i256** %3, align 8
  %24 = load i256, i256* %7, align 8
  %25 = getelementptr inbounds i256, i256* %23, i256 %24
  %26 = load i256, i256* %25, align 8
  %27 = load i256, i256* %6, align 8
  %28 = icmp sgt i256 %26, %27
  br label %29

; <label>:29:                                     ; preds = %22, %19
  %30 = phi i1 [ false, %19 ], [ %28, %22 ]
  br i1 %30, label %31, label %42

; <label>:31:                                     ; preds = %29
  %32 = load i256*, i256** %3, align 8
  %33 = load i256, i256* %7, align 8
  %34 = getelementptr inbounds i256, i256* %32, i256 %33
  %35 = load i256, i256* %34, align 8
  %36 = load i256*, i256** %3, align 8
  %37 = load i256, i256* %7, align 8
  %38 = add nsw i256 %37, 1
  %39 = getelementptr inbounds i256, i256* %36, i256 %38
  store i256 %35, i256* %39, align 8
  %40 = load i256, i256* %7, align 8
  %41 = sub nsw i256 %40, 1
  store i256 %41, i256* %7, align 8
  br label %19

; <label>:42:                                     ; preds = %29
  %43 = load i256, i256* %6, align 8
  %44 = load i256*, i256** %3, align 8
  %45 = load i256, i256* %7, align 8
  %46 = add nsw i256 %45, 1
  %47 = getelementptr inbounds i256, i256* %44, i256 %46
  store i256 %43, i256* %47, align 8
  br label %48

; <label>:48:                                     ; preds = %42
  %49 = load i256, i256* %5, align 8
  %50 = add nsw i256 %49, 1
  store i256 %50, i256* %5, align 8
  br label %8

; <label>:51:                                     ; preds = %8
  ret void
}

