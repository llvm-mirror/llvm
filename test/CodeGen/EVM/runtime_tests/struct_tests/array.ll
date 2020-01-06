declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @dde(i256 %0)
  call void @llvm.evm.mstore(i256 0, i256 %1)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @dde(i256) #0 {
  %2 = alloca i256, align 4
  %3 = alloca [2 x i256], align 4
  %4 = alloca i256, align 4
  store i256 %0, i256* %2, align 4
  %5 = load i256, i256* %2, align 4
  %6 = getelementptr inbounds [2 x i256], [2 x i256]* %3, i64 0, i64 1
  store volatile i256 %5, i256* %6, align 4
  %7 = getelementptr inbounds [2 x i256], [2 x i256]* %3, i64 0, i64 1
  %8 = load volatile i256, i256* %7, align 4
  store i256 %8, i256* %4, align 4
  %9 = load i256, i256* %4, align 4
  ret i256 %9
}
