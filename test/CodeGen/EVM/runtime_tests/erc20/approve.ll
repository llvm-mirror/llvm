declare void @_approve(i256, i256, i256)


define i1 @approve(i256 %spender, i256 %value) {
entry:
  %spender.addr = alloca i256
  store i256 %spender, i256* %spender.addr
  %value.addr = alloca i256
  store i256 %value, i256* %value.addr
  %retval = alloca i1
  %0 = load i256, i256* %spender.addr
  %1 = load i256, i256* %value.addr
  call void @_approve(i256 0, i256 %0, i256 %1)
  store i1 true, i1* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %return

return:                                           ; preds = %return.end, %entry
  %2 = load i1, i1* %retval
  ret i1 %2
}

