define i256 @abcd(i256 %a, i256 %b) {
entry:
  %0 = add nsw i256 %a, %b
  ret i256 %0
}

