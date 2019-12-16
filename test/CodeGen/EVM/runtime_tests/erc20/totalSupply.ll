declare i256 @llvm.evm.sload(i256)

define i256 @totalSupply() {
entry:
  %retval = alloca i256
  %0 = call i256 @llvm.evm.sload(i256 0)
  store i256 %0, i256* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %return

return:                                           ; preds = %return.end, %entry
  %1 = load i256, i256* %retval
  ret i256 %1
}

