declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @n_bit_position(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @n_bit_position(i256, i256) #0 {
  %3 = alloca i256, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  store i256 %0, i256* %3, align 8
  store i256 %1, i256* %4, align 8
  %6 = load i256, i256* %3, align 8
  %7 = load i256, i256* %4, align 8
  %8 = ashr i256 %6, %7
  store i256 %8, i256* %5, align 8
  %9 = load i256, i256* %5, align 8
  %10 = and i256 %9, 1
  ret i256 %10
}

