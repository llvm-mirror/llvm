declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @max(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

;; simple comparison
define i256 @max(i256 %a, i256 %b) {
entry:
  %0 = alloca i256
  %1 = icmp ugt i256 %a, %b
  br i1 %1, label %true, label %false

true:
  store i256 %a, i256* %0
  br label %finally

false:
  store i256 %b, i256* %0
  br label %finally

finally:
  %2 = load i256, i256* %0
  ret i256 %2
}

