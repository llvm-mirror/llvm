define i256 @sccweqand(i256 %a, i256 %b) {
  %t1 = and i256 %a, %b
  %t2 = icmp eq i256 %t1, 0
  %t3 = zext i1 %t2 to i256
  ret i256 %t3
}

