define i256 @a() #0 {
  %1 = call i256 @b()
  ret i256 %1
}

define i256 @b() #0 {
  ret i256 1
}
