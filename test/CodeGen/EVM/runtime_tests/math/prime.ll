declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @checkPrimeNumber(i256 %0)
  call void @llvm.evm.mstore(i256 0, i256 %1)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @checkPrimeNumber(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  store i256 %0, i256* %2, align 4
  store i256 1, i256* %4, align 4
  store i256 2, i256* %3, align 4
  br label %5

; <label>:5:                                      ; preds = %17, %1
  %6 = load i256, i256* %3, align 4
  %7 = load i256, i256* %2, align 4
  %8 = sdiv i256 %7, 2
  %9 = icmp sle i256 %6, %8
  br i1 %9, label %10, label %20

; <label>:10:                                     ; preds = %5
  %11 = load i256, i256* %2, align 4
  %12 = load i256, i256* %3, align 4
  %13 = srem i256 %11, %12
  %14 = icmp eq i256 %13, 0
  br i1 %14, label %15, label %16

; <label>:15:                                     ; preds = %10
  store i256 0, i256* %4, align 4
  br label %20

; <label>:16:                                     ; preds = %10
  br label %17

; <label>:17:                                     ; preds = %16
  %18 = load i256, i256* %3, align 4
  %19 = add nsw i256 %18, 1
  store i256 %19, i256* %3, align 4
  br label %5

; <label>:20:                                     ; preds = %15, %5
  %21 = load i256, i256* %4, align 4
  ret i256 %21
}
