declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @sort()
  call void @llvm.evm.mstore(i256 0, i256 %0)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

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
  call void @insertionSort(i256* %8, i256 5)
  %9 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 0
  %10 = load i256, i256* %9, align 16
  %11 = icmp eq i256 %10, 1
  br i1 %11, label %12, label %29

; <label>:12:                                     ; preds = %0
  %13 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 1
  %14 = load i256, i256* %13, align 8
  %15 = icmp eq i256 %14, 2
  br i1 %15, label %16, label %29

; <label>:16:                                     ; preds = %12
  %17 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 2
  %18 = load i256, i256* %17, align 16
  %19 = icmp eq i256 %18, 3
  br i1 %19, label %20, label %29

; <label>:20:                                     ; preds = %16
  %21 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 3
  %22 = load i256, i256* %21, align 8
  %23 = icmp eq i256 %22, 4
  br i1 %23, label %24, label %29

; <label>:24:                                     ; preds = %20
  %25 = getelementptr inbounds [5 x i256], [5 x i256]* %2, i256 0, i256 4
  %26 = load i256, i256* %25, align 16
  %27 = icmp eq i256 %26, 5
  br i1 %27, label %28, label %29

; <label>:28:                                     ; preds = %24
  store i256 1, i256* %1, align 8
  br label %30

; <label>:29:                                     ; preds = %24, %20, %16, %12, %0
  store i256 0, i256* %1, align 8
  br label %30

; <label>:30:                                     ; preds = %29, %28
  %31 = load i256, i256* %1, align 8
  ret i256 %31
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
