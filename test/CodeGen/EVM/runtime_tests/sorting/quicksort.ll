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
  call void @quicksort(i256* %8, i256 0, i256 4)
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

define void @quicksort(i256*, i256, i256) #0 {
  %4 = alloca i256*, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  %7 = alloca i256, align 8
  %8 = alloca i256, align 8
  %9 = alloca i256, align 8
  %10 = alloca i256, align 8
  store i256* %0, i256** %4, align 8
  store i256 %1, i256* %5, align 8
  store i256 %2, i256* %6, align 8
  %11 = load i256, i256* %5, align 8
  %12 = load i256, i256* %6, align 8
  %13 = icmp slt i256 %11, %12
  br i1 %13, label %14, label %101

; <label>:14:                                     ; preds = %3
  %15 = load i256, i256* %5, align 8
  store i256 %15, i256* %9, align 8
  %16 = load i256, i256* %5, align 8
  store i256 %16, i256* %7, align 8
  %17 = load i256, i256* %6, align 8
  store i256 %17, i256* %8, align 8
  br label %18

; <label>:18:                                     ; preds = %76, %14
  %19 = load i256, i256* %7, align 8
  %20 = load i256, i256* %8, align 8
  %21 = icmp slt i256 %19, %20
  br i1 %21, label %22, label %77

; <label>:22:                                     ; preds = %18
  br label %23

; <label>:23:                                     ; preds = %39, %22
  %24 = load i256*, i256** %4, align 8
  %25 = load i256, i256* %7, align 8
  %26 = getelementptr inbounds i256, i256* %24, i256 %25
  %27 = load i256, i256* %26, align 8
  %28 = load i256*, i256** %4, align 8
  %29 = load i256, i256* %9, align 8
  %30 = getelementptr inbounds i256, i256* %28, i256 %29
  %31 = load i256, i256* %30, align 8
  %32 = icmp sle i256 %27, %31
  br i1 %32, label %33, label %37

; <label>:33:                                     ; preds = %23
  %34 = load i256, i256* %7, align 8
  %35 = load i256, i256* %6, align 8
  %36 = icmp slt i256 %34, %35
  br label %37

; <label>:37:                                     ; preds = %33, %23
  %38 = phi i1 [ false, %23 ], [ %36, %33 ]
  br i1 %38, label %39, label %42

; <label>:39:                                     ; preds = %37
  %40 = load i256, i256* %7, align 8
  %41 = add nsw i256 %40, 1
  store i256 %41, i256* %7, align 8
  br label %23

; <label>:42:                                     ; preds = %37
  br label %43

; <label>:43:                                     ; preds = %53, %42
  %44 = load i256*, i256** %4, align 8
  %45 = load i256, i256* %8, align 8
  %46 = getelementptr inbounds i256, i256* %44, i256 %45
  %47 = load i256, i256* %46, align 8
  %48 = load i256*, i256** %4, align 8
  %49 = load i256, i256* %9, align 8
  %50 = getelementptr inbounds i256, i256* %48, i256 %49
  %51 = load i256, i256* %50, align 8
  %52 = icmp sgt i256 %47, %51
  br i1 %52, label %53, label %56

; <label>:53:                                     ; preds = %43
  %54 = load i256, i256* %8, align 8
  %55 = add nsw i256 %54, -1
  store i256 %55, i256* %8, align 8
  br label %43

; <label>:56:                                     ; preds = %43
  %57 = load i256, i256* %7, align 8
  %58 = load i256, i256* %8, align 8
  %59 = icmp slt i256 %57, %58
  br i1 %59, label %60, label %76

; <label>:60:                                     ; preds = %56
  %61 = load i256*, i256** %4, align 8
  %62 = load i256, i256* %7, align 8
  %63 = getelementptr inbounds i256, i256* %61, i256 %62
  %64 = load i256, i256* %63, align 8
  store i256 %64, i256* %10, align 8
  %65 = load i256*, i256** %4, align 8
  %66 = load i256, i256* %8, align 8
  %67 = getelementptr inbounds i256, i256* %65, i256 %66
  %68 = load i256, i256* %67, align 8
  %69 = load i256*, i256** %4, align 8
  %70 = load i256, i256* %7, align 8
  %71 = getelementptr inbounds i256, i256* %69, i256 %70
  store i256 %68, i256* %71, align 8
  %72 = load i256, i256* %10, align 8
  %73 = load i256*, i256** %4, align 8
  %74 = load i256, i256* %8, align 8
  %75 = getelementptr inbounds i256, i256* %73, i256 %74
  store i256 %72, i256* %75, align 8
  br label %76

; <label>:76:                                     ; preds = %60, %56
  br label %18

; <label>:77:                                     ; preds = %18
  %78 = load i256*, i256** %4, align 8
  %79 = load i256, i256* %9, align 8
  %80 = getelementptr inbounds i256, i256* %78, i256 %79
  %81 = load i256, i256* %80, align 8
  store i256 %81, i256* %10, align 8
  %82 = load i256*, i256** %4, align 8
  %83 = load i256, i256* %8, align 8
  %84 = getelementptr inbounds i256, i256* %82, i256 %83
  %85 = load i256, i256* %84, align 8
  %86 = load i256*, i256** %4, align 8
  %87 = load i256, i256* %9, align 8
  %88 = getelementptr inbounds i256, i256* %86, i256 %87
  store i256 %85, i256* %88, align 8
  %89 = load i256, i256* %10, align 8
  %90 = load i256*, i256** %4, align 8
  %91 = load i256, i256* %8, align 8
  %92 = getelementptr inbounds i256, i256* %90, i256 %91
  store i256 %89, i256* %92, align 8
  %93 = load i256*, i256** %4, align 8
  %94 = load i256, i256* %5, align 8
  %95 = load i256, i256* %8, align 8
  %96 = sub nsw i256 %95, 1
  call void @quicksort(i256* %93, i256 %94, i256 %96)
  %97 = load i256*, i256** %4, align 8
  %98 = load i256, i256* %8, align 8
  %99 = add nsw i256 %98, 1
  %100 = load i256, i256* %6, align 8
  call void @quicksort(i256* %97, i256 %99, i256 %100)
  br label %101

; <label>:101:                                    ; preds = %77, %3
  ret void
}
