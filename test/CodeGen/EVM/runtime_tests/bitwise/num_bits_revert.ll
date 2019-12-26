declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @num_bits(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @num_bits(i256, i256) #0 {
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  %5 = alloca i256, align 4
  %6 = alloca i256, align 4
  %7 = alloca i256, align 4
  %8 = alloca i256, align 4
  store i256 %0, i256* %3, align 4
  store i256 %1, i256* %4, align 4
  store i256 0, i256* %6, align 4
  store i256 31, i256* %5, align 4
  br label %9

; <label>:9:                                      ; preds = %28, %2
  %10 = load i256, i256* %5, align 4
  %11 = icmp sge i256 %10, 0
  br i1 %11, label %12, label %31

; <label>:12:                                     ; preds = %9
  %13 = load i256, i256* %3, align 4
  %14 = load i256, i256* %5, align 4
  %15 = ashr i256 %13, %14
  %16 = and i256 %15, 1
  store i256 %16, i256* %7, align 4
  %17 = load i256, i256* %4, align 4
  %18 = load i256, i256* %5, align 4
  %19 = ashr i256 %17, %18
  %20 = and i256 %19, 1
  store i256 %20, i256* %8, align 4
  %21 = load i256, i256* %7, align 4
  %22 = load i256, i256* %8, align 4
  %23 = icmp ne i256 %21, %22
  br i1 %23, label %24, label %27

; <label>:24:                                     ; preds = %12
  %25 = load i256, i256* %6, align 4
  %26 = add nsw i256 %25, 1
  store i256 %26, i256* %6, align 4
  br label %27

; <label>:27:                                     ; preds = %24, %12
  br label %28

; <label>:28:                                     ; preds = %27
  %29 = load i256, i256* %5, align 4
  %30 = add nsw i256 %29, -1
  store i256 %30, i256* %5, align 4
  br label %9

; <label>:31:                                     ; preds = %9
  %32 = load i256, i256* %6, align 4
  ret i256 %32
}
