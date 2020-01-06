declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  call void @llvm.evm.mstore(i256 64, i256 128)
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @div(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @div(i256 %a, i256 %b) {
entry:
  %a.addr = alloca i256
  store i256 %a, i256* %a.addr
  %b.addr = alloca i256
  store i256 %b, i256* %b.addr
  %retval = alloca i256
  %0 = load i256, i256* %b.addr
  %BO_GT = icmp ugt i256 %0, 0
  br i1 %BO_GT, label %continue, label %revert

revert:                                           ; preds = %entry
  unreachable

continue:                                         ; preds = %entry
  %c_addr = alloca i256
  %1 = load i256, i256* %a.addr
  %2 = load i256, i256* %b.addr
  %BO_DIV1 = udiv i256 %1, %2
  store i256 %BO_DIV1, i256* %c_addr
  %3 = load i256, i256* %c_addr
  store i256 %3, i256* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %return

return:                                           ; preds = %return.end, %continue
  %4 = load i256, i256* %retval
  ret i256 %4
}

