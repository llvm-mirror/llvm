; RUN: llc -march=hexagon -mcpu=hexagonv5 -hexagon-bit=0 < %s | FileCheck %s
; Optimized bitwise operations.

define i32 @my_clrbit(i32 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #31)
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  %0 = load i32, i32* %x.addr, align 4
  %and = and i32 %0, 2147483647
  ret i32 %and
}

define i64 @my_clrbit2(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #31)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %and = and i64 %0, -2147483649
  ret i64 %and
}

define i64 @my_clrbit3(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #31)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %and = and i64 %0, 9223372036854775807
  ret i64 %and
}

define i32 @my_clrbit4(i32 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #13)
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  %0 = load i32, i32* %x.addr, align 4
  %and = and i32 %0, -8193
  ret i32 %and
}

define i64 @my_clrbit5(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #13)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %and = and i64 %0, -8193
  ret i64 %and
}

define i64 @my_clrbit6(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = clrbit(r{{[0-9]+}}, #27)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %and = and i64 %0, -576460752303423489
  ret i64 %and
}

define zeroext i16 @my_setbit(i16 zeroext %crc) nounwind {
entry:
; CHECK: memh(r{{[0-9]+}}+#0){{ *}}={{ *}}setbit(#15)
  %crc.addr = alloca i16, align 2
  store i16 %crc, i16* %crc.addr, align 2
  %0 = load i16, i16* %crc.addr, align 2
  %conv = zext i16 %0 to i32
  %or = or i32 %conv, 32768
  %conv1 = trunc i32 %or to i16
  store i16 %conv1, i16* %crc.addr, align 2
  %1 = load i16, i16* %crc.addr, align 2
  ret i16 %1
}

define i32 @my_setbit2(i32 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}}{{ *}}={{ *}}setbit(r{{[0-9]+}}, #15)
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  %0 = load i32, i32* %x.addr, align 4
  %or = or i32 %0, 32768
  ret i32 %or
}

define i64 @my_setbit3(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}}{{ *}}={{ *}}setbit(r{{[0-9]+}}, #15)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %or = or i64 %0, 32768
  ret i64 %or
}

define i32 @my_setbit4(i32 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}}{{ *}}={{ *}}setbit(r{{[0-9]+}}, #31)
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  %0 = load i32, i32* %x.addr, align 4
  %or = or i32 %0, -2147483648
  ret i32 %or
}

define i64 @my_setbit5(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}}{{ *}}={{ *}}setbit(r{{[0-9]+}}, #13)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %or = or i64 %0, 35184372088832
  ret i64 %or
}

define zeroext i16 @my_togglebit(i16 zeroext %crc) nounwind {
entry:
; CHECK: r{{[0-9]+}} = togglebit(r{{[0-9]+}}, #15)
  %crc.addr = alloca i16, align 2
  store i16 %crc, i16* %crc.addr, align 2
  %0 = load i16, i16* %crc.addr, align 2
  %conv = zext i16 %0 to i32
  %xor = xor i32 %conv, 32768
  %conv1 = trunc i32 %xor to i16
  store i16 %conv1, i16* %crc.addr, align 2
  %1 = load i16, i16* %crc.addr, align 2
  ret i16 %1
}

define i32 @my_togglebit2(i32 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = togglebit(r{{[0-9]+}}, #15)
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  %0 = load i32, i32* %x.addr, align 4
  %xor = xor i32 %0, 32768
  ret i32 %xor
}

define i64 @my_togglebit3(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = togglebit(r{{[0-9]+}}, #15)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %xor = xor i64 %0, 32768
  ret i64 %xor
}

define i64 @my_togglebit4(i64 %x) nounwind {
entry:
; CHECK: r{{[0-9]+}} = togglebit(r{{[0-9]+}}, #20)
  %x.addr = alloca i64, align 8
  store i64 %x, i64* %x.addr, align 8
  %0 = load i64, i64* %x.addr, align 8
  %xor = xor i64 %0, 4503599627370496
  ret i64 %xor
}
