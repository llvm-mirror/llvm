define i256 @sccweqand(i256 %a, i256 %b) {
  %t2 = icmp ne i256 %a, %b
  %t3 = zext i1 %t2 to i256
  ret i256 %t3
}

