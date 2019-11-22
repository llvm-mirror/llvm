define void @store(i256* %x, i256 %y) {
  store i256 %y, i256* %x
  ret void
}

