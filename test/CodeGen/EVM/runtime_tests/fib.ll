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
