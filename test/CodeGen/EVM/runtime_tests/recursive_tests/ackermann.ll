declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @ackermann(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}


define i256 @ackermann(i256, i256) #0 {
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  %5 = alloca i256, align 4
  store i256 %0, i256* %4, align 4
  store i256 %1, i256* %5, align 4
  %6 = load i256, i256* %4, align 4
  %7 = icmp ne i256 %6, 0
  br i1 %7, label %11, label %8

; <label>:8:                                      ; preds = %2
  %9 = load i256, i256* %5, align 4
  %10 = add nsw i256 %9, 1
  store i256 %10, i256* %3, align 4
  br label %26

; <label>:11:                                     ; preds = %2
  %12 = load i256, i256* %5, align 4
  %13 = icmp ne i256 %12, 0
  br i1 %13, label %18, label %14

; <label>:14:                                     ; preds = %11
  %15 = load i256, i256* %4, align 4
  %16 = sub nsw i256 %15, 1
  %17 = call i256 @ackermann(i256 %16, i256 1)
  store i256 %17, i256* %3, align 4
  br label %26

; <label>:18:                                     ; preds = %11
  %19 = load i256, i256* %4, align 4
  %20 = sub nsw i256 %19, 1
  %21 = load i256, i256* %4, align 4
  %22 = load i256, i256* %5, align 4
  %23 = sub nsw i256 %22, 1
  %24 = call i256 @ackermann(i256 %21, i256 %23)
  %25 = call i256 @ackermann(i256 %20, i256 %24)
  store i256 %25, i256* %3, align 4
  br label %26

; <label>:26:                                     ; preds = %18, %14, %8
  %27 = load i256, i256* %3, align 4
  ret i256 %27
}
