declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
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

; <label>:6:                                      ; preds = %14, %2
  %7 = load i256, i256* %5, align 4
  %8 = load i256, i256* %4, align 4
  %9 = icmp ult i256 %7, %8
  br i1 %9, label %10, label %17

; <label>:10:                                     ; preds = %6
  %11 = load i256, i256* %3, align 4
  %12 = load i256, i256* %3, align 4
  %13 = add i256 %12, 1
  store i256 %13, i256* %3, align 4
  br label %14

; <label>:14:                                     ; preds = %10
  %15 = load i256, i256* %5, align 4
  %16 = add i256 %15, 1
  store i256 %16, i256* %5, align 4
  br label %6

; <label>:17:                                     ; preds = %6
  %18 = load i256, i256* %3, align 4
  ret i256 %18
}

