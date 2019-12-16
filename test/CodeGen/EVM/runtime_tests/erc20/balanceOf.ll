define i256 @balanceOf(i160 %account) {
entry:
  %account.addr = alloca i160
  store i160 %account, i160* %account.addr
  %retval = alloca i256
  %0 = load i160, i160* %account.addr
  %emitConcateArr = alloca [52 x i8]
  %1 = getelementptr inbounds [52 x i8], [52 x i8]* %emitConcateArr, i32 0, i32 0
  %PtrCast = bitcast i8* %1 to i256*
  store i256 1, i256* %PtrCast
  %2 = getelementptr inbounds [52 x i8], [52 x i8]* %emitConcateArr, i32 0, i32 32
  %PtrCast1 = bitcast i8* %2 to i160*
  store i160 %0, i160* %PtrCast1
  %3 = insertvalue %bytes zeroinitializer, i32 52, 0
  %4 = getelementptr inbounds [52 x i8], [52 x i8]* %emitConcateArr, i32 0, i32 0
  %5 = insertvalue %bytes %3, i8* %4, 1
  %Mapping = call i256 @keccak256(%bytes %5)
  %6 = alloca i256
  %7 = alloca i256
  store i256 %Mapping, i256* %6
  call void @storageLoad(i256* %6, i256* %7)
  %8 = load i256, i256* %7
  store i256 %8, i256* %retval
  br label %return

return.end:                                       ; No predecessors!
  br label %return

return:                                           ; preds = %return.end, %entry
  %9 = load i256, i256* %retval
  ret i256 %9
}

