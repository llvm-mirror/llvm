declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @hcf(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @hcf(i256, i256) #0 {
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  %5 = alloca i256, align 4
  store i256 %0, i256* %4, align 4
  store i256 %1, i256* %5, align 4
  br label %6

; <label>:6:                                      ; preds = %2
  %7 = load i256, i256* %4, align 4
  %8 = load i256, i256* %5, align 4
  %9 = icmp ne i256 %7, %8
  br i1 %9, label %10, label %26

; <label>:10:                                     ; preds = %6
  %11 = load i256, i256* %4, align 4
  %12 = load i256, i256* %5, align 4
  %13 = icmp sgt i256 %11, %12
  br i1 %13, label %14, label %20

; <label>:14:                                     ; preds = %10
  %15 = load i256, i256* %4, align 4
  %16 = load i256, i256* %5, align 4
  %17 = sub nsw i256 %15, %16
  %18 = load i256, i256* %5, align 4
  %19 = call i256 @hcf(i256 %17, i256 %18)
  store i256 %19, i256* %3, align 4
  br label %28

; <label>:20:                                     ; preds = %10
  %21 = load i256, i256* %4, align 4
  %22 = load i256, i256* %5, align 4
  %23 = load i256, i256* %4, align 4
  %24 = sub nsw i256 %22, %23
  %25 = call i256 @hcf(i256 %21, i256 %24)
  store i256 %25, i256* %3, align 4
  br label %28

; <label>:26:                                     ; preds = %6
  %27 = load i256, i256* %4, align 4
  store i256 %27, i256* %3, align 4
  br label %28

; <label>:28:                                     ; preds = %26, %20, %14
  %29 = load i256, i256* %3, align 4
  ret i256 %29
}

