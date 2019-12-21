define i256 @n_bit_position(i256, i256) #0 {
  %3 = alloca i256, align 8
  %4 = alloca i256, align 8
  %5 = alloca i256, align 8
  store i256 %0, i256* %3, align 8
  store i256 %1, i256* %4, align 8
  %6 = load i256, i256* %3, align 8
  %7 = load i256, i256* %4, align 8
  %8 = ashr i256 %6, %7
  store i256 %8, i256* %5, align 8
  %9 = load i256, i256* %5, align 8
  %10 = and i256 %9, 1
  ret i256 %10
}

