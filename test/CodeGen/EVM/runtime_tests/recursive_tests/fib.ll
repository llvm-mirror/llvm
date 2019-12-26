declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @"fn_fib"(i256 %0)
  call void @llvm.evm.mstore(i256 0, i256 %1)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define i256 @"fn_fib"(i256 %".1")
{
fn_fib_entry:
  %".3" = icmp sle i256 %".1", 1
  br i1 %".3", label %"fn_fib_entry.if", label %"fn_fib_entry.endif"
fn_fib_entry.if:
  ret i256 1
fn_fib_entry.endif:
  %".6" = sub i256 %".1", 1
  %".7" = sub i256 %".1", 2
  %".8" = call i256 (i256) @"fn_fib"(i256 %".6")
  %".9" = call i256 (i256) @"fn_fib"(i256 %".7")
  %".10" = add i256 %".8", %".9"
  ret i256 %".10"
}
