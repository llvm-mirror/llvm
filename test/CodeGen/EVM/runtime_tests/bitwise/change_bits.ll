define i256 @changebits(i256, i256, i256, i256) #0 {
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  %7 = alloca i256, align 8
  %8 = alloca i256, align 8
  %9 = alloca i256, align 8
  %10 = alloca i256, align 8
  %11 = alloca i256, align 8
  %12 = alloca i256, align 8
  %13 = alloca i256, align 8
  %14 = alloca i256, align 8
  %15 = alloca i256, align 8
  store i256 %0, i256* %5, align 8
  store i256 %1, i256* %6, align 8
  store i256 %2, i256* %7, align 8
  store i256 %3, i256* %8, align 8
  store i256 0, i256* %12, align 8
  store i256 0, i256* %13, align 8
  store i256 0, i256* %14, align 8
  store i256 1, i256* %15, align 8
  %16 = load i256, i256* %5, align 8
  store i256 %16, i256* %9, align 8
  %17 = load i256, i256* %5, align 8
  store i256 %17, i256* %10, align 8
  %18 = load i256, i256* %6, align 8
  store i256 %18, i256* %11, align 8
  br label %19

; <label>:19:                                     ; preds = %78, %4
  %20 = load i256, i256* %7, align 8
  %21 = load i256, i256* %8, align 8
  %22 = icmp sle i256 %20, %21
  br i1 %22, label %23, label %81

; <label>:23:                                     ; preds = %19
  store i256 1, i256* %15, align 8
  %24 = load i256, i256* %10, align 8
  store i256 %24, i256* %5, align 8
  %25 = load i256, i256* %11, align 8
  store i256 %25, i256* %6, align 8
  br label %26

; <label>:26:                                     ; preds = %37, %23
  %27 = load i256, i256* %14, align 8
  %28 = load i256, i256* %7, align 8
  %29 = icmp sle i256 %27, %28
  br i1 %29, label %30, label %42

; <label>:30:                                     ; preds = %26
  %31 = load i256, i256* %14, align 8
  %32 = load i256, i256* %7, align 8
  %33 = icmp eq i256 %31, %32
  br i1 %33, label %34, label %37

; <label>:34:                                     ; preds = %30
  %35 = load i256, i256* %5, align 8
  %36 = and i256 %35, 1
  store i256 %36, i256* %12, align 8
  br label %37

; <label>:37:                                     ; preds = %34, %30
  %38 = load i256, i256* %14, align 8
  %39 = add nsw i256 %38, 1
  store i256 %39, i256* %14, align 8
  %40 = load i256, i256* %5, align 8
  %41 = ashr i256 %40, 1
  store i256 %41, i256* %5, align 8
  br label %26

; <label>:42:                                     ; preds = %26
  store i256 0, i256* %14, align 8
  br label %43

; <label>:43:                                     ; preds = %54, %42
  %44 = load i256, i256* %14, align 8
  %45 = load i256, i256* %7, align 8
  %46 = icmp sle i256 %44, %45
  br i1 %46, label %47, label %59

; <label>:47:                                     ; preds = %43
  %48 = load i256, i256* %14, align 8
  %49 = load i256, i256* %7, align 8
  %50 = icmp eq i256 %48, %49
  br i1 %50, label %51, label %54

; <label>:51:                                     ; preds = %47
  %52 = load i256, i256* %6, align 8
  %53 = and i256 %52, 1
  store i256 %53, i256* %13, align 8
  br label %54

; <label>:54:                                     ; preds = %51, %47
  %55 = load i256, i256* %14, align 8
  %56 = add nsw i256 %55, 1
  store i256 %56, i256* %14, align 8
  %57 = load i256, i256* %6, align 8
  %58 = ashr i256 %57, 1
  store i256 %58, i256* %6, align 8
  br label %43

; <label>:59:                                     ; preds = %43
  store i256 0, i256* %14, align 8
  %60 = load i256, i256* %12, align 8
  %61 = load i256, i256* %13, align 8
  %62 = icmp eq i256 %60, %61
  br i1 %62, label %63, label %64

; <label>:63:                                     ; preds = %59
  br label %77

; <label>:64:                                     ; preds = %59
  br label %65

; <label>:65:                                     ; preds = %70, %64
  %66 = load i256, i256* %14, align 8
  %67 = add nsw i256 %66, 1
  store i256 %67, i256* %14, align 8
  %68 = load i256, i256* %7, align 8
  %69 = icmp slt i256 %66, %68
  br i1 %69, label %70, label %73

; <label>:70:                                     ; preds = %65
  %71 = load i256, i256* %15, align 8
  %72 = shl i256 %71, 1
  store i256 %72, i256* %15, align 8
  br label %65

; <label>:73:                                     ; preds = %65
  %74 = load i256, i256* %15, align 8
  %75 = load i256, i256* %9, align 8
  %76 = xor i256 %75, %74
  store i256 %76, i256* %9, align 8
  br label %77

; <label>:77:                                     ; preds = %73, %63
  store i256 0, i256* %14, align 8
  br label %78

; <label>:78:                                     ; preds = %77
  %79 = load i256, i256* %7, align 8
  %80 = add nsw i256 %79, 1
  store i256 %80, i256* %7, align 8
  br label %19

; <label>:81:                                     ; preds = %19
  %82 = load i256, i256* %9, align 8
  ret i256 %82
}

