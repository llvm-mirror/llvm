declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @a()
  call void @llvm.evm.mstore(i256 0, i256 %0)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @a() #0 {
  %1 = call i256 @b()
  ret i256 %1
}

define i256 @b() #0 {
  ret i256 1
}
