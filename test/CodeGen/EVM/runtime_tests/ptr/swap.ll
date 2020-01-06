declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @test(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @test(i256, i256) #0 {
  %3 = alloca i256, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  %6 = alloca i256, align 8
  %7 = alloca i256, align 8
  store i256 %0, i256* %4, align 8
  store i256 %1, i256* %5, align 8
  %8 = load i256, i256* %4, align 8
  store i256 %8, i256* %6, align 8
  %9 = load i256, i256* %5, align 8
  store i256 %9, i256* %7, align 8
  call void @swap(i256* %4, i256* %5)
  %10 = load i256, i256* %6, align 8
  %11 = load i256, i256* %5, align 8
  %12 = icmp eq i256 %10, %11
  br i1 %12, label %13, label %18

; <label>:13:                                     ; preds = %2
  %14 = load i256, i256* %7, align 8
  %15 = load i256, i256* %4, align 8
  %16 = icmp eq i256 %14, %15
  br i1 %16, label %17, label %18

; <label>:17:                                     ; preds = %13
  store i256 1, i256* %3, align 8
  br label %19

; <label>:18:                                     ; preds = %13, %2
  store i256 0, i256* %3, align 8
  br label %19

; <label>:19:                                     ; preds = %18, %17
  %20 = load i256, i256* %3, align 8
  ret i256 %20
}

define void @swap(i256*, i256*) #0 {
  %3 = alloca i256*, align 8
  %4 = alloca i256*, align 8
  %5 = alloca i256, align 8
  store i256* %0, i256** %3, align 8
  store i256* %1, i256** %4, align 8
  %6 = load i256*, i256** %3, align 8
  %7 = load i256, i256* %6, align 8
  store i256 %7, i256* %5, align 8
  %8 = load i256*, i256** %4, align 8
  %9 = load i256, i256* %8, align 8
  %10 = load i256*, i256** %3, align 8
  store i256 %9, i256* %10, align 8
  %11 = load i256, i256* %5, align 8
  %12 = load i256*, i256** %4, align 8
  store i256 %11, i256* %12, align 8
  ret void
}
