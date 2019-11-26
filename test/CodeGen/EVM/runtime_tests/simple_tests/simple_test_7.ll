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

