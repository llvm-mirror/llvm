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
  store i256 %0, i256* %2, align 4
  store i256 0, i256* %3, align 4
  br label %4

; <label>:4:                                      ; preds = %7, %1
  %5 = load i256, i256* %2, align 4
  %6 = icmp ugt i256 %5, 0
  br i1 %6, label %7, label %12

; <label>:7:                                      ; preds = %4
  %8 = load i256, i256* %3, align 4
  %9 = add i256 %8, 1
  store i256 %9, i256* %3, align 4
  %10 = load i256, i256* %2, align 4
  %11 = udiv i256 %10, 10
  store i256 %11, i256* %2, align 4
  br label %4

; <label>:12:                                     ; preds = %4
  %13 = load i256, i256* %3, align 4
  ret i256 %13
}
