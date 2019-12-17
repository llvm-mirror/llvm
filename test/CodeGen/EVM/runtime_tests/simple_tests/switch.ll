define i256 @abc(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca i256, align 4
  store i256 %0, i256* %3, align 4
  %4 = load i256, i256* %3, align 4
  switch i256 %4, label %7 [
    i256 1, label %5
    i256 2, label %6
  ]

; <label>:5:                                      ; preds = %1
  store i256 100, i256* %2, align 4
  br label %8

; <label>:6:                                      ; preds = %1
  store i256 200, i256* %2, align 4
  br label %8

; <label>:7:                                      ; preds = %1
  store i256 300, i256* %2, align 4
  br label %8

; <label>:8:                                      ; preds = %7, %6, %5
  %9 = load i256, i256* %2, align 4
  ret i256 %9
}

