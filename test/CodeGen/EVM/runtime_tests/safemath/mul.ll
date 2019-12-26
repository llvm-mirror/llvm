declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @mul(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @mul(i256 %a, i256 %b) {
entry:
  %a.addr = alloca i256
  store i256 %a, i256* %a.addr
  %b.addr = alloca i256
  store i256 %b, i256* %b.addr
  %retval = alloca i256
  %0 = load i256, i256* %a.addr
  %BO_EQ = icmp eq i256 %0, 0
  %cond = icmp ne i1 %BO_EQ, false
  br i1 %cond, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  store i256 0, i256* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %if.end

if.end:                                           ; preds = %return.end, %entry
  %c_addr = alloca i256
  %1 = load i256, i256* %a.addr
  %2 = load i256, i256* %b.addr
  %BO_MUL4 = mul nsw i256 %1,  %2
  store i256 %BO_MUL4, i256* %c_addr
  %3 = load i256, i256* %c_addr
  %4 = load i256, i256* %a.addr
  %BO_DIV3 = udiv i256 %3, %4
  %5 = load i256, i256* %b.addr
  %BO_EQ1 = icmp eq i256 %BO_DIV3, %5
  br i1 %BO_EQ1, label %continue, label %revert

revert:                                           ; preds = %if.end
  unreachable

continue:                                         ; preds = %if.end
  %6 = load i256, i256* %c_addr
  store i256 %6, i256* %retval
  br label %return

return.end2:                                      ; No predecessors!
  br label %return

return:                                           ; preds = %return.end2, %continue, %if.then
  %7 = load i256, i256* %retval
  ret i256 %7
}

