declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @abc(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @abc(i256, i256) #0 {
  %3 = alloca i256, align 4
  %4 = alloca i256, align 4
  %5 = alloca i256, align 4
  store i256 %0, i256* %3, align 4
  store i256 %1, i256* %4, align 4
  store i256 0, i256* %5, align 4
  br label %6

; <label>:6:                                      ; preds = %13, %2
  %7 = load i256, i256* %5, align 4
  %8 = icmp ult i256 %7, 10
  br i1 %8, label %9, label %16

; <label>:9:                                      ; preds = %6
  %10 = load i256, i256* %4, align 4
  %11 = load i256, i256* %3, align 4
  %12 = add i256 %11, %10
  store i256 %12, i256* %3, align 4
  br label %13

; <label>:13:                                     ; preds = %9
  %14 = load i256, i256* %5, align 4
  %15 = add i256 %14, 1
  store i256 %15, i256* %5, align 4
  br label %6

; <label>:16:                                     ; preds = %6
  %17 = load i256, i256* %3, align 4
  ret i256 %17
}

