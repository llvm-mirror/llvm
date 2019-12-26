declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @zeros(i256 %0)
  call void @llvm.evm.mstore(i256 0, i256 %1)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @zeros(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  store i256 %0, i256* %2, align 4
  store i256 0, i256* %3, align 4
  br label %5

; <label>:5:                                      ; preds = %17, %1
  %6 = load i256, i256* %2, align 4
  %7 = icmp ugt i256 %6, 0
  br i1 %7, label %8, label %20

; <label>:8:                                      ; preds = %5
  %9 = load i256, i256* %2, align 4
  %10 = urem i256 %9, 10
  store i256 %10, i256* %4, align 4
  %11 = load i256, i256* %4, align 4
  %12 = icmp eq i256 %11, 0
  br i1 %12, label %13, label %16

; <label>:13:                                     ; preds = %8
  %14 = load i256, i256* %3, align 4
  %15 = add i256 %14, 1
  store i256 %15, i256* %3, align 4
  br label %17

; <label>:16:                                     ; preds = %8
  br label %20

; <label>:17:                                     ; preds = %13
  %18 = load i256, i256* %2, align 4
  %19 = udiv i256 %18, 10
  store i256 %19, i256* %2, align 4
  br label %5

; <label>:20:                                     ; preds = %16, %5
  %21 = load i256, i256* %3, align 4
  ret i256 %21
}
