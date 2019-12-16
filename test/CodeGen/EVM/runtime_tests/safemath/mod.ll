define i256 @mod(i256 %a, i256 %b) {
entry:
  %a.addr = alloca i256
  store i256 %a, i256* %a.addr
  %b.addr = alloca i256
  store i256 %b, i256* %b.addr
  %retval = alloca i256
  %0 = load i256, i256* %b.addr
  %BO_NE = icmp ne i256 %0, 0
  br i1 %BO_NE, label %continue, label %revert

revert:                                           ; preds = %entry
  unreachable

continue:                                         ; preds = %entry
  %1 = load i256, i256* %a.addr
  %2 = load i256, i256* %b.addr
  %BO_Rem1 = urem i256 %1, %2
  store i256 %BO_Rem1, i256* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %return

return:                                           ; preds = %return.end, %continue
  %3 = load i256, i256* %retval
  ret i256 %3
}

